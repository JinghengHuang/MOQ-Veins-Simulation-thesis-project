#include "MoqPublisherApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "models/MoqFraming.h"
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
                    // Announce as a length-prefixed control byte frame on the control stream.
                    packet = new inet::Packet("ANNOUNCE");
                    MoqControlFrame c;
                    c.type = CTRL_ANNOUNCE;
                    c.trackId = track->trackId;
                    c.publisherId = track->publisherId;
                    c.priority = track->priority;
                    c.payloadSize = track->packetSize;
                    c.sendInterval = track->sendInterval;
                    c.trackNamespace = track->trackNamespace;
                    c.trackName = track->trackName;
                    c.trackAlias = track->trackAlias;
                    packet->insertAtBack(inet::makeShared<inet::BytesChunk>(MoqFraming::encodeControl(c)));
                }
                break;
            case SUB_SUCCESS:
                if (it != tracks.end()) {
                    track = &it->second;
                    // Send the object as a MoQ-style length-prefixed byte frame. Encoding
                    // the whole object (header + payload) as one BytesChunk lets consecutive
                    // objects coalesce on the stream instead of forming a SequenceChunk, and
                    // makes objects self-delimiting (the receiver frames by length).
                    packet = new inet::Packet("TRACK_OBJ");
                    MoqObjectFrame f;
                    f.trackId = track->trackId;
                    f.trackAlias = track->trackAlias;
                    f.groupId = 0; // single group for now
                    f.objectId = track->nextObjectId;
                    f.priority = track->priority;
                    f.payloadLength = track->packetSize;
                    f.creationTime = omnetpp::simTime();
                    track->nextObjectId++;
                    auto frameBytes = MoqFraming::encode(f);
                    packet->insertAtBack(inet::makeShared<inet::BytesChunk>(frameBytes));
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
            
            // Stream mapping: control (ANNOUNCE) goes on the control stream; object data
            // goes on a per-track DATA stream (client-bidi 4,8,...). Keeping data streams
            // homogeneous (only byte frames) is what makes the framing safe.
            if (track != nullptr && packet != nullptr) {
                long streamId;
                if (id == PUB_ANNOUNCE) {
                    streamId = CONTROL_STREAM;
                } else { // SUB_SUCCESS data
                    auto iter = trackToStreamMap.find(tid);
                    if (iter == trackToStreamMap.end()) {
                        trackToStreamMap[tid] = nextStreamId;
                        nextStreamId += 4;
                        iter = trackToStreamMap.find(tid);
                    }
                    streamId = iter->second;
                }
                // Use the 2-arg send: the 1-arg send(packet) resets the stream id to 0.
                socket.send(packet, streamId);
                if (id == SUB_SUCCESS) {
                    // Record one data object offered to the network for this track.
                    PubTrackStat& ps = pubStats[track->trackId];
                    ps.objectsSent++;
                    ps.bytesSent += track->packetSize;
                    if (ps.firstSendTime < SIMTIME_ZERO) ps.firstSendTime = omnetpp::simTime();
                    ps.lastSendTime = omnetpp::simTime();
                    emit(objectSentSignal, (long) track->packetSize);
                }
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

    objectSentSignal = registerSignal("objectSent");

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

    scheduleAt(inet::simTime() + par("connectTime"), timerConnect);
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
    // Record the stream id; the delivered packet carries no stream tag.
    pendingRecvStreams.push_back(dataInfo->getStreamID());
    socket->recv(static_cast<int64_t>(dataInfo->getAvaliableDataSize()), dataInfo->getStreamID());
}

void MoqPublisherApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
    sendTrackAnnouncementData();
}

void MoqPublisherApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    long streamId = 0;
    if (!pendingRecvStreams.empty()) { streamId = pendingRecvStreams.front(); pendingRecvStreams.pop_front(); }

    // The publisher only receives control (SUBSCRIBE_OK) on the control stream, as
    // length-prefixed byte frames. Accumulate and parse.
    if (streamId == CONTROL_STREAM) {
        auto bytes = packet->peekDataAsBytes();
        const auto& vec = bytes->getBytes();
        controlBuf.data.insert(controlBuf.data.end(), vec.begin(), vec.end());

        MoqControlFrame c;
        size_t consumed;
        while (MoqFraming::tryParseControl(controlBuf.data, c, consumed)) {
            if (c.type == CTRL_SUBSCRIBE_OK) {
                std::string trackAlias = c.trackNamespace + "/" + c.trackName;
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
            controlBuf.data.erase(controlBuf.data.begin(), controlBuf.data.begin() + consumed);
        }
    }
    delete packet;
}

void MoqPublisherApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqPublisherApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    EV_WARN << "Send queue full, QUIC will handle backpressure" << std::endl;
}

void MoqPublisherApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    EV_DEBUG << "Send queue drained" << std::endl;
}

void MoqPublisherApp::finish()
{
    // Per-track offered-load scalars, used as the denominator for object loss ratio.
    for (auto& track : tracks) {
        long tid = track.second.trackId;
        const PubTrackStat& ps = pubStats[tid];
        std::string prefix = "track[" + track.second.trackAlias + "].";
        recordScalar((prefix + "objectsSent").c_str(), ps.objectsSent);
        recordScalar((prefix + "bytesSent").c_str(), ps.bytesSent, "B");
        double span = (ps.lastSendTime - ps.firstSendTime).dbl();
        if (span > 0) {
            recordScalar((prefix + "offeredRate").c_str(), ps.bytesSent * 8.0 / span, "bps");
        }
    }
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
        scheduleAt(inet::simTime() + tm.sendInterval, tm.timer);
    }else{
        errorEvent = new omnetpp::cMessage("SUB_ERROR");
        errorEvent->setKind(SUB_ERROR);
        scheduleAt(inet::simTime(), errorEvent);
    }
}

}
