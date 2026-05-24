/* --- MoqRelayApp.cpp --- */

/* ------------------------------------------
author: undefined
date: 5/15/2026
------------------------------------------ */

#include "MoqRelayApp.h"
#include "utils/StringUtils.h"
#include <regex.h>
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "models/MoqSubscriber_m.h"

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
}

void MoqRelayApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqRelayApp" << std::endl;

    connectPort = par("connectPort");
    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");
    socket.bind(localAddress, localPort);
    
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

void MoqRelayApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
}

void MoqRelayApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    EV_DEBUG << "Data arrived" << std::endl;
}

void MoqRelayApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqRelayApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    sendingAllowed = false;
}

void MoqRelayApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    sendingAllowed = true;
}


void MoqRelayApp::onSubscribe(std::string sid, std::string trackAlias, long streamId){
    std::vector<std::string> trackinfo = StringUtils::splitString(trackAlias, "/");
    if(subscriberSockets.find(sid) != subscriberSockets.end()){

        inet::QuicSocket* subscriberSocket = subscriberSockets.find(sid)->second;
        if(trackinfo.size() != 2){
            // send an error message on wrong track alias format
            auto response = new inet::Packet("SUBSCRIBE_ERROR");
            response->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
            auto streamReq = response->addTagIfAbsent<inet::QuicStreamReq>();
            streamReq->setStreamID(streamId);
            subscriberSocket->send(response);
        }else{
            TrackKey tKey{trackinfo[0], trackinfo[1]};
            // register subsciber to the topic, and send subscribe ok to subscriber, then send subsribe ok with track to publisher
            auto it = publishedTracks.find(tKey);
            auto publisherSocketIt = publisherSocketsByTrackKey.find(tKey);
            if(it != publishedTracks.end() && publisherSocketIt != publisherSocketsByTrackKey.end()){
                TrackMeta track = it->second;
                inet::QuicSocket* publisherSocket = publisherSocketIt->second;
                if(subscriberByTrack.find(tKey) == subscriberByTrack.end()){
                    subscriberByTrack[tKey] = std::vector<std::string>();
                }else{
                    subscriberByTrack[tKey].push_back(sid);
                }
                
                auto responseSubscriber = new inet::Packet("SUBSCRIBE_OK");
                responseSubscriber->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
                auto streamReq = responseSubscriber->addTagIfAbsent<inet::QuicStreamReq>();
                streamReq->setStreamID(streamId); // TODO: change where to get the stream id
                subscriberSocket->send(responseSubscriber);
                
                auto responsePublisher = new inet::Packet("SUBSCRIBE_OK");
                inet::Ptr<MoqSubscriber> header = inet::makeShared<MoqSubscriber>();
                header->setTrackNamespace(track.trackNamespace.c_str());
                header->setTrackName(track.trackName.c_str());
                header->setSubscriberId(sid.c_str());
                header->setSubscriberPriority(track.priority);
                header->setStartObjectId(track.nextObjectId);
                responsePublisher->insertAtBack(header);
                auto streamReq = responsePublisher->addTagIfAbsent<inet::QuicStreamReq>();
                streamReq->setStreamID(streamId); // TODO: change where to get the stream id
                publisherSocket->send(responsePublisher);

            }else{
                auto response = new inet::Packet("SUBSCRIBE_ERROR");
                response->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));
                auto streamReq = response->addTagIfAbsent<inet::QuicStreamReq>();
                streamReq->setStreamID(streamId);
                subscriberSocket->send(response);
            }
        }
    }
}

void MoqRelayApp::onPublish(std::string pid, TrackMeta){
    
}

void MoqRelayApp::relayTrackData(std::string trackAlias, std::string sid){
    
}
}
