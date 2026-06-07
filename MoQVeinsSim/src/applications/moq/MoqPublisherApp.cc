#include "MoqPublisherApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "models/MoqObjectChunk_m.h"
#include "models/MoqPublisherAnnounce_m.h"
#include "models/MoqSubscriber_m.h"
#include <algorithm>

#include <omnetpp.h>
#include <csignal>

namespace moqveinssim {
Define_Module(MoqPublisherApp);

MoqPublisherApp::MoqPublisherApp() {
    timerConnect = new inet::cMessage("MOQ Publisher Timer - Connect");
    timerConnect->setKind(TIMER_CONNECT);

    timerLimitRuntime = new inet::cMessage("MOQ Publisher Timer - Runtime limit");
    timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);
}

MoqPublisherApp::~MoqPublisherApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
    
    for (auto& track : tracks) {
        cancelAndDelete(track.second.timer);
        track.second.timer = nullptr;
    }
    errorEvent = nullptr;
}

void MoqPublisherApp::handleTimeout(omnetpp::cMessage *msg)
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

void MoqPublisherApp::handleMessageWhenUp(omnetpp::cMessage *msg)
{
    EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
    EV_DEBUG << "NAME: " << msg->getName() << std::endl;
    EV_DEBUG << "SELF_MESSAGE: " << msg->isSelfMessage() << std::endl;
    if (msg->isSelfMessage()) {
        int id = msg->getKind();
        if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME){
            handleTimeout(msg);
        }
        if (sendingAllowed == true){
            std::string name = msg->getName();
            EV_DEBUG << "NAME: " << name << std::endl;
            long tid = std::stoi(name);
            auto it = tracks.find(tid);
            inet::Packet *packet = nullptr;
            TrackMeta* track = nullptr;
            
            EV_DEBUG << "ID: " << id << std::endl;
            switch (id)
            {
            case PUB_ANNOUNCE:
                if (it != tracks.end()) {
                    track = &it->second;
                    // Send track packet, just announcement
                    packet = new inet::Packet("ANNOUNCE");
                    inet::Ptr<MoqPublisherAnnounce> header = inet::makeShared<MoqPublisherAnnounce>();
                    header->setTrackId(track->trackId);
                    header->setTrackNamespace(track->trackNamespace.c_str());
                    header->setTrackName(track->trackName.c_str());
                    header->setTrackAlias(track->trackAlias.c_str());
                    header->setPriority(track->priority);
                    header->setSendInterval(track->sendInterval);
                    header->setPayloadSize(track->packetSize);
                    header->setChunkLength(inet::B(64));
                    packet->insertAtBack(header);

                }
                break;
            case SUB_SUCCESS:
                DEBUG_TRAP;
                if (it != tracks.end()) {
                    track = &it->second;
                    // Send track packet with data stream
                    packet = new inet::Packet("TRACK_OBJ");
                    inet::Ptr<MoqObjectChunk> header = inet::makeShared<MoqObjectChunk>();
                    header->setTrackId(track->trackId);
                    header->setTrackAlias(track->trackAlias.c_str());
                    header->setPriority(track->priority);
                    header->setObjectId(track->nextObjectId);
                    header->setPayloadLength(track->packetSize);
                    header->setCreationTime(omnetpp::simTime());
                    header->setGroupId(0); // for now all group is 0, will see if its necessary to implement group or only track/object is enough
                    
                    track->nextObjectId++;
                    // Inflate chunkLength to cover both the header fields and the payload.
                    // This keeps the packet as a single chunk so QUIC fragments it as
                    // SliceChunks rather than a SequenceChunk — SequenceChunk insertion
                    // flattens sub-chunks individually, which confuses the receiver's QUIC
                    // frame parser into treating leftover payload bytes as a FrameHeader.
                    header->setChunkLength(inet::B(64 + track->packetSize));
                    packet->insertAtBack(header);
                    EV_INFO << "Send track object data: " << track->trackAlias.c_str() << std::endl;
                    // Arrange sending another packet of the same event
                    sendTrackData(tid);
                }
                break;
            case SUB_ERROR:
                // Do nothing on error packet
                EV_DEBUG << "Getting sub error: " << msg->getKind() << std::endl;
                break;
            }
            
            // stream mapping
            // QUIC stream id rule: 62 bit unsigned int with least significant bit meaning sender of the packet(client/server) and 2nd lowest meaning direction(uni/bi)
            // start from 0 and +4 each time means send by client and bidirectional
            // for each track a unique stream
            auto iter = trackToStreamMap.find(tid);
            int nextStartStreamId = 0;
            if (iter == trackToStreamMap.end()) {
                trackToStreamMap[tid] = nextStartStreamId;
                nextStartStreamId += 4;
            }
            iter = trackToStreamMap.find(tid);
            if (iter != trackToStreamMap.end() && track != nullptr && packet != nullptr) {
                int streamId = iter->second;
                packet->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);

                socket.send(packet);
                EV_INFO << "sendData - track " << track->trackId << " - name " << track->trackName << " - size " << track->packetSize << " B" << " - streamId  " << streamId << std::endl;
            }
        }
    } else if (msg->arrivedOn("socketIn")) { // from QUIC
        // TODO: Add and handle events: case QUIC_I_SENDQUEUE_DRAINING and QUIC_I_SENDQUEUE_FULL
        socket.processMessage(msg);
        //delete msg;
    } else { // something really strange...
        throw omnetpp::cRuntimeError("Invalid message: %d", (int) msg->getKind());
    }
}


void MoqPublisherApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqPublisherApp" << std::endl;

    connectPort = par("connectPort");
    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");
    socket.bind(localAddress, localPort);
    socket.listen();

    const auto* arr = dynamic_cast<const omnetpp::cValueArray*>(par("tracks").objectValue());
    omnetpp::cModule* host = getParentModule();
    // Get car name
    std::string vId = host->getFullName();
    
    EV_INFO << "Publisher in car " << vId << std::endl;
    if (arr != nullptr){
        for (int i = 0; i < arr->size(); i++) {
            auto& elem = arr->get(i);
            const auto* map = dynamic_cast<const omnetpp::cValueMap*>(elem.objectValue());
            if (map != nullptr){
                TrackMeta track;
                track.publisherId = i;
                track.trackId = i;
                track.trackNamespace = vId;
                track.trackName = (*map)["trackName"].stringValue();
                track.trackAlias = track.trackNamespace + "/" + track.trackName; // alias: namespace/name
                track.packetSize = (*map)["packetSize"].intValueInUnit("B");;
                track.sendInterval = (*map)["sendInterval"].doubleValueInUnit("s");;
                track.priority = (*map)["priority"].intValue();
                track.nextObjectId = 0;
                track.timer = new omnetpp::cMessage((std::to_string(track.trackId)).c_str(), PUB_ANNOUNCE);
                tracks[i] = track;
            }
        }
    }

    scheduleAt(par("connectTime"), timerConnect);
}

void MoqPublisherApp::handleStopOperation(inet::LifecycleOperation *operation)
{
    EV_INFO << "handleStopOperation" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqPublisherApp::handleCrashOperation(inet::LifecycleOperation *operation)
{
    EV_ERROR << "MOQ Publisher FAILED!" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqPublisherApp::socketDataAvailable(inet::QuicSocket* socket, inet::QuicDataInfo *dataInfo) {
    socket->recv(static_cast<int64_t>(dataInfo->getAvaliableDataSize()), dataInfo->getStreamID());
}

void MoqPublisherApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
    sendTrackAnnouncementData();
}

void MoqPublisherApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    EV_INFO << "Data arrived: " << packet->getName() << std::endl;
    auto frontChunk = packet->peekAtFront<inet::Chunk>();
    const auto *subscribeOkHeader = dynamic_cast<const MoqSubscriber *>(frontChunk.get());
    if (subscribeOkHeader != nullptr) {
        std::string trackAlias = std::string(subscribeOkHeader->getTrackNamespace()) + "/" + subscribeOkHeader->getTrackName();
        for (auto& entry : tracks) {
            TrackMeta& track = entry.second;
            if (track.trackAlias == trackAlias) {
                cancelEvent(track.timer);
                track.timer->setKind(SUB_SUCCESS);
                scheduleAt(omnetpp::simTime(), track.timer);
                EV_INFO << "SUBSCRIBE_OK for track: " << trackAlias << " - starting data transmission" << std::endl;
                break;
            }
        }
    }
    delete packet;
}

void MoqPublisherApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqPublisherApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    sendingAllowed = false;
}

void MoqPublisherApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    sendingAllowed = true;
}

// Based on track configurations, send track announcement data
void MoqPublisherApp::sendTrackAnnouncementData(){
    
    for (auto & track : tracks){

        scheduleAt(inet::simTime(), track.second.timer);
    }
}
// Send track announcement data
void MoqPublisherApp::sendTrackData(long tid){
    
    EV_INFO << "Sending track data of " << tid << std::endl;
    const auto track = tracks.find(tid);
    if(track != tracks.end()){
        TrackMeta tm = track->second;
        DEBUG_TRAP;
        scheduleAt(inet::simTime() + tm.sendInterval, tm.timer);
    }else{
        errorEvent = new omnetpp::cMessage("SUB_ERROR");
        errorEvent->setKind(SUB_ERROR);
        scheduleAt(inet::simTime(), errorEvent);
    }
}

}
