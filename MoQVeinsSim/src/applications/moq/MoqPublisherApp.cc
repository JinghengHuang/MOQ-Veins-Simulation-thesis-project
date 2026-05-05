#include "MoqPublisherApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "models/MoqTrackHeader_m.h"
#include <algorithm>

namespace moqveinssim {
static constexpr int MOQ_OBJECT_HEADER_BYTES = 32;

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
    if (msg->isSelfMessage()) {
        int id = msg->getKind();
        if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME){
            handleTimeout(msg);
        }
        if (sendingAllowed == true){
            auto it = tracks.find(id);
            if (it != tracks.end()) {
                TrackMeta& track = it->second;
                // Send track packet
                inet::Packet *packet = new inet::Packet("MoqTrackObjectData");
                inet::Ptr<MoqTrackHeader> header = inet::makeShared<MoqTrackHeader>();
                header->setChunkLength(inet::B(MOQ_OBJECT_HEADER_BYTES));
                header->setTrackId(track.trackId);
                header->setObjectId(track.nextObjectId);
                header->setCreationTime(inet::simTime());
                // packet->insertAtFront(header); No header for now, QUIC can't handle it, implement stats externally
                tracks[id].nextObjectId += 1;
                auto payload = inet::makeShared<inet::ByteCountChunk>(inet::B(track.packetSize));
                packet->insertAtBack(payload);

                // stream mapping
                // QUIC stream id rule: 62 bit unsigned int with least significant bit meaning sender of the packet(client/server) and 2nd lowest meaning direction(uni/bi)
                // start from 0 and +4 each time means send by client and bidirectional
                // for each track a unique stream
                auto iter = trackToStreamMap.find(track.trackId);
                int nextStartStreamId = 0;
                if (iter != trackToStreamMap.end()) {
                    trackToStreamMap[track.trackId] = track.trackId;
                }else{
                    trackToStreamMap[track.trackId] = nextStartStreamId;
                    nextStartStreamId += 4;
                }
                int streamId = trackToStreamMap[track.trackId];
                packet->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);

                socket.send(packet);
                EV_INFO << "sendData - track " << track.trackId << " - name " << track.trackName << " - size " << track.packetSize << " B" << " - streamId  " << streamId << std::endl;
                EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
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

    const auto* arr = omnetpp::check_and_cast<const omnetpp::cValueArray*>(par("tracks").objectValue());

    for (int i = 0; i < arr->size(); i++) {
        auto& elem = arr->get(i);
        const auto* map = omnetpp::check_and_cast<omnetpp::cValueMap*>(elem.objectValue());
        TrackMeta track;
        track.trackId = i;
        track.trackName = (*map)["trackName"].stringValue();
        track.packetSize = (*map)["packetSize"].intValueInUnit("B");;
        track.sendInterval = (*map)["sendInterval"].doubleValueInUnit("s");;
        track.priority = (*map)["priority"].intValue();
        track.nextObjectId = 0;
        track.timer = new omnetpp::cMessage(("trackTimer-" + std::to_string(track.trackId)).c_str());
        track.timer->setKind(track.trackId);
        tracks[i] = track;

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

void MoqPublisherApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
    sendTrackData();
}

void MoqPublisherApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    EV_DEBUG << "Data arrived" << std::endl;
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

// Based on track configurations, send track data
void MoqPublisherApp::sendTrackData(){
    
    for (auto & track : tracks){

        scheduleAt(inet::simTime() + track.second.sendInterval, track.second.timer);
    }
}

}
