/* --- MoqRelayApp.cpp --- */

/* ------------------------------------------
author: Jingheng Huang
date: 5/15/2026
------------------------------------------ */

#include "MoqRelayApp.h"
#include "utils/StringUtils.h"
#include <regex.h>
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "models/MoqSubscriber_m.h"
#include "models/MoqPublisherAnnounce_m.h"

namespace moqveinssim {
Define_Module(MoqRelayApp);

MoqRelayApp::MoqRelayApp() {
    timerConnect = new inet::cMessage("MOQ Publisher Timer - Connect");
    timerConnect->setKind(TIMER_CONNECT);

    timerLimitRuntime = new inet::cMessage("MOQ Publisher Timer - Runtime limit");
    timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);
}

MoqRelayApp::~MoqRelayApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
    // Clear pointer maps before deleteSockets() frees the objects
    publisherSockets.clear();
    publisherSocketsByTrackKey.clear();
    subscriberSockets.clear();
    socketMap.deleteSockets();
}

void MoqRelayApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqRelayApp" << std::endl;

    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");
    socket.bind(localAddress, localPort);
    socket.listen();
}

void MoqRelayApp::handleStopOperation(inet::LifecycleOperation *operation)
{
    EV_INFO << "handleStopOperation" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqRelayApp::handleCrashOperation(inet::LifecycleOperation *operation)
{
    EV_ERROR << "MOQ Relay FAILED!" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

// Accept incoming QUIC connections and register them in the socket map.
void MoqRelayApp::socketConnectionAvailable(inet::QuicSocket *listenSocket)
{
    inet::QuicSocket *newSocket = listenSocket->accept();
    newSocket->setCallback(this);
    socketMap.addSocket(newSocket);
    EV_INFO << "Accepted connection, socketId=" << newSocket->getSocketId() << std::endl;
}

void MoqRelayApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
}

void MoqRelayApp::socketDataAvailable(inet::QuicSocket *peerSocket, inet::QuicDataInfo *dataInfo) {
    // QUIC signals that stream data is ready; request delivery as a QUIC_I_DATA packet.
    peerSocket->recv(static_cast<int64_t>(dataInfo->getAvaliableDataSize()), dataInfo->getStreamID());
}

void MoqRelayApp::socketDataArrived(inet::QuicSocket *peerSocket, inet::Packet *packet) {
    // QUIC recv() delivers data in a "data" wrapper regardless of the original name.
    // Identify message type by dynamic-casting the leading chunk.
    auto frontChunk = packet->peekAtFront<inet::Chunk>();
    const auto *announceHeader = dynamic_cast<const MoqPublisherAnnounce *>(frontChunk.get());
    const auto *subHeader      = dynamic_cast<const MoqSubscriber *>(frontChunk.get());

    if (announceHeader != nullptr) {
        EV_DEBUG << "ANNOUNCE packet received" << std::endl;
        std::string pid = std::to_string(announceHeader->getPublisherId());
        TrackKey tKey{announceHeader->getTrackNamespace(), announceHeader->getTrackName()};

        TrackMeta tm;
        tm.publisherId    = announceHeader->getPublisherId();
        tm.trackId        = announceHeader->getTrackId();
        tm.trackNamespace = announceHeader->getTrackNamespace();
        tm.trackName      = announceHeader->getTrackName();
        tm.trackAlias     = announceHeader->getTrackAlias();
        tm.packetSize     = announceHeader->getPayloadSize();
        tm.sendInterval   = announceHeader->getSendInterval();
        tm.priority       = announceHeader->getPriority();
        tm.nextObjectId   = 0;

        publishedTracks[tKey]            = tm;
        publisherSockets[pid]            = peerSocket;
        publisherSocketsByTrackKey[tKey] = peerSocket;

        EV_INFO << "Registered track " << tm.trackAlias << " from publisher " << pid << std::endl;

        auto streamTag = packet->findTag<inet::QuicStreamReq>();
        long streamId = streamTag ? streamTag->getStreamID() : 0;

        auto response = new inet::Packet("ANNOUNCE_OK");
        response->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
        response->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
        peerSocket->send(response);
    }
    else if (subHeader != nullptr) {
        EV_DEBUG << "SUBSCRIBE packet received" << std::endl;

        std::string sid = subHeader->getSubscriberId();
        std::string trackAlias = std::string(subHeader->getTrackNamespace()) + "/" + subHeader->getTrackName();

        auto streamTag = packet->findTag<inet::QuicStreamReq>();
        long streamId = streamTag ? streamTag->getStreamID() : 0;

        subscriberSockets[sid] = peerSocket;

        EV_INFO << "Subscriber " << sid << " subscribed to " << trackAlias << std::endl;

        onSubscribe(sid, trackAlias, streamId);
    }
    else {
        EV_DEBUG << "Unknown packet type in socketDataArrived" << std::endl;
    }
}

void MoqRelayApp::socketClosed(inet::QuicSocket *closedSocket) {
    EV_INFO << "socketClosed, socketId=" << closedSocket->getSocketId() << std::endl;
    socketMap.removeSocket(closedSocket);
    delete closedSocket;
}

void MoqRelayApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    sendingAllowed = false;
}

void MoqRelayApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    sendingAllowed = true;
}

void MoqRelayApp::handleMessageWhenUp(omnetpp::cMessage *msg)
{
    EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
    if (msg->isSelfMessage()) {
        int id = msg->getKind();
        if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME) {
            handleTimeout(msg);
        }
    } else if (msg->arrivedOn("socketIn")) {
        // Route to the accepted client socket that owns this message.
        // Falls back to the listening socket for connection-available events.
        inet::ISocket *sock = socketMap.findSocketFor(msg);
        if (sock) {
            sock->processMessage(msg);
        } else {
            socket.processMessage(msg);
        }
    } else {
        throw omnetpp::cRuntimeError("Invalid message: %d", (int) msg->getKind());
    }
}

TrackKey MoqRelayApp::getTrackKey(std::string trackAlias){

    std::vector<std::string> trackinfo = StringUtils::splitString(trackAlias, "/");
    if(trackinfo.size() != 2){
        throw "Invalid track alias";
    }
    TrackKey tKey{trackinfo[0], trackinfo[1]};
    return tKey;
}

void MoqRelayApp::onSubscribe(std::string sid, std::string trackAlias, long streamId){
    if(subscriberSockets.find(sid) != subscriberSockets.end()){

        inet::QuicSocket* subscriberSocket = subscriberSockets.find(sid)->second;
        try{
            TrackKey tKey = getTrackKey(trackAlias);
            auto it = publishedTracks.find(tKey);
            auto publisherSocketIt = publisherSocketsByTrackKey.find(tKey);
            if(it != publishedTracks.end() && publisherSocketIt != publisherSocketsByTrackKey.end()){
                TrackMeta track = it->second;
                inet::QuicSocket* publisherSocket = publisherSocketIt->second;

                if(subscriberByTrack.find(tKey) == subscriberByTrack.end()){
                    subscriberByTrack[tKey] = std::vector<std::string>();
                }
                subscriberByTrack[tKey].push_back(sid);

                auto responseSubscriber = new inet::Packet("SUBSCRIBE_OK");
                responseSubscriber->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
                responseSubscriber->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                subscriberSocket->send(responseSubscriber);

                auto responsePublisher = new inet::Packet("SUBSCRIBE_OK");
                inet::Ptr<MoqSubscriber> header = inet::makeShared<MoqSubscriber>();
                header->setTrackNamespace(track.trackNamespace.c_str());
                header->setTrackName(track.trackName.c_str());
                header->setSubscriberId(sid.c_str());
                header->setSubscriberPriority(track.priority);
                header->setStartObjectId(track.nextObjectId);
                header->setChunkLength(inet::B(64));
                responsePublisher->insertAtBack(header);
                responsePublisher->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                publisherSocket->send(responsePublisher);

            }else{
                auto response = new inet::Packet("SUBSCRIBE_ERROR");
                response->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
                response->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                subscriberSocket->send(response);
            }
        } catch (const std::string msg) {
            auto response = new inet::Packet("SUBSCRIBE_ERROR");
            response->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
            response->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
            subscriberSocket->send(response);
        }
    }
}

void MoqRelayApp::onPublish(std::string pid, TrackMeta tm){
}

void MoqRelayApp::relayTrackData(std::string trackAlias, std::string sid){
}

void MoqRelayApp::handleTimeout(omnetpp::cMessage *msg)
{
    EV_DETAIL << "handle timeout of kind " << msg->getKind() << std::endl;
    switch (msg->getKind()) {

    }
}

}
