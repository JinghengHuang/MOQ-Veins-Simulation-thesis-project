#include "MoqSubscriberApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/chunk/SliceChunk.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "models/MoqObjectChunk_m.h"
#include "models/MoqPublisherAnnounce_m.h"
#include "models/MoqSubscriber_m.h"
#include <algorithm>

#include <omnetpp.h>

namespace moqveinssim {
Define_Module(MoqSubscriberApp);

MoqSubscriberApp::MoqSubscriberApp() {
    timerConnect = new inet::cMessage("MOQ Subscriber Timer - Connect");
    timerConnect->setKind(TIMER_CONNECT);

    timerLimitRuntime = new inet::cMessage("MOQ Subscriber Timer - Runtime limit");
    timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);
}

MoqSubscriberApp::~MoqSubscriberApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
    
    for (auto& track : tracks) {
        cancelAndDelete(track.second.timer);
        track.second.timer = nullptr;
    }
    errorEvent = nullptr;
}

void MoqSubscriberApp::handleTimeout(omnetpp::cMessage *msg)
{
    EV_DETAIL << "handle timeout of kind " << msg->getKind() << std::endl;
    switch (msg->getKind()) {
        case TIMER_CONNECT:
            EV_INFO << "connect - address: " << connectAddress << std::endl;
            //socket.connect(connectAddress, connectPort, 0, 0, 0);
            socket.connect(connectAddress, connectPort);
            break;
        case TIMER_LIMIT_RUNTIME:
            socket.close();
            finish();
            break;
        default:
            throw omnetpp::cRuntimeError("Invalid timer: %d", (int) msg->getKind());
    }
}

void MoqSubscriberApp::handleMessageWhenUp(omnetpp::cMessage *msg)
{
    EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
    if (msg->isSelfMessage()) {
        int id = msg->getKind();
        if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME){
            handleTimeout(msg);
        }
        if (sendingAllowed == true){
            std::string name = msg->getName();
            TrackMeta* track = nullptr;
            long tid = std::stoi(name);
            auto it = tracks.find(tid);
            
            if (it != tracks.end()) {
                track = &it->second;
                auto subscribeRequest = new inet::Packet("SUBSCRIBE");

                std::string vId = getParentModule()->getFullName();
                inet::Ptr<MoqSubscriber> subHeader = inet::makeShared<MoqSubscriber>();
                subHeader->setSubscriberId(vId.c_str());
                subHeader->setTrackNamespace(track->trackNamespace.c_str());
                subHeader->setTrackName(track->trackName.c_str());
                subHeader->setSubscriberPriority(track->priority);
                subHeader->setStartObjectId(0);
                subHeader->setChunkLength(inet::B(64));
                subscribeRequest->insertAtBack(subHeader);

                auto iter = trackToStreamMap.find(tid);
                int nextStartStreamId = 0;
                if (iter == trackToStreamMap.end()) {
                    trackToStreamMap[tid] = nextStartStreamId;
                    nextStartStreamId += 4;
                }
                iter = trackToStreamMap.find(tid);
                if (iter != trackToStreamMap.end() && track != nullptr && subscribeRequest != nullptr) {
                    int streamId = iter->second;
                    subscribeRequest->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);

                    socket.send(subscribeRequest);
                }
            }

        }
    } else if (msg->arrivedOn("socketIn")) { // from QUIC
        // TODO: Add and handle events: case QUIC_I_SENDQUEUE_DRAINING and QUIC_I_SENDQUEUE_FULL
        socket.processMessage(msg);
    } else { // something really strange...
        throw omnetpp::cRuntimeError("Invalid message: %d", (int) msg->getKind());
    }
}


void MoqSubscriberApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqSubscriberApp" << std::endl;

    connectPort = par("connectPort");
    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");
    socket.bind(localAddress, localPort);

    std::string vId = getParentModule()->getFullName();
    const auto* arr = dynamic_cast<const omnetpp::cValueArray*>(par("tracks").objectValue());
    if (arr != nullptr) {
        for (int i = 0; i < arr->size(); i++) {
            const auto* map = dynamic_cast<const omnetpp::cValueMap*>(arr->get(i).objectValue());
            if (map != nullptr) {
                TrackMeta track;
                track.trackId        = i;
                track.trackNamespace = (*map)["trackNamespace"].stringValue();
                track.trackName      = (*map)["trackName"].stringValue();
                track.trackAlias     = track.trackNamespace + "/" + track.trackName;
                track.priority       = (*map)["subscriberPriority"].intValue();
                track.nextObjectId   = (*map)["startObjectId"].intValue();
                track.timer = new omnetpp::cMessage(std::to_string(i).c_str(), PUB_ANNOUNCE);
                tracks[i] = track;
            }
        }
    }

    scheduleAt(inet::simTime() + par("connectTime"), timerConnect);
}

void MoqSubscriberApp::handleStopOperation(inet::LifecycleOperation *operation)
{
    EV_INFO << "handleStopOperation" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqSubscriberApp::handleCrashOperation(inet::LifecycleOperation *operation)
{
    EV_ERROR << "MOQ Publisher FAILED!" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqSubscriberApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
    sendTrackSubscribeData();
}

void MoqSubscriberApp::socketDataAvailable(inet::QuicSocket* socket, inet::QuicDataInfo *dataInfo) {
    socket->recv(static_cast<int64_t>(dataInfo->getAvaliableDataSize()), dataInfo->getStreamID());
}

void MoqSubscriberApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    auto frontChunk = packet->peekAtFront<inet::Chunk>();
    EV_DEBUG << "Received packet: " << packet->getFullName() << " With header: " << frontChunk.get()->getClassName() << std::endl;

    // Large objects arrive as SliceChunk fragments; unwrap to access the original MoqObjectChunk.
    const MoqObjectChunk *chunk = dynamic_cast<const MoqObjectChunk *>(frontChunk.get());
    bool isFirstSlice = true;
    if (chunk == nullptr) {
        const auto *sliceChunk = dynamic_cast<const inet::SliceChunk *>(frontChunk.get());
        if (sliceChunk != nullptr) {
            chunk = dynamic_cast<const MoqObjectChunk *>(sliceChunk->getChunk().get());
            isFirstSlice = (sliceChunk->getOffset() == inet::b(0));
        }
    }
    if (chunk == nullptr) {
        delete packet;
        return;
    }
    if (isFirstSlice) {
        receive_count += 1;
        EV_INFO << "Track object received"
                << " | trackId=" << chunk->getTrackId()
                << " | trackAlias=" << chunk->getTrackAlias()
                << " | groupId=" << chunk->getGroupId()
                << " | objectId=" << chunk->getObjectId()
                << " | payloadLength=" << chunk->getPayloadLength()
                << " | creationTime=" << chunk->getCreationTime()
                << std::endl;
    }
    delete packet;
}

void MoqSubscriberApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqSubscriberApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    EV_WARN << "Send queue full" << std::endl;
}

void MoqSubscriberApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    EV_DEBUG << "Send queue drained" << std::endl;
}

// Based on track configurations, send track announcement data
void MoqSubscriberApp::sendTrackSubscribeData(){
    
    for (auto & track : tracks){

        scheduleAt(inet::simTime(), track.second.timer);
    }
}

}
