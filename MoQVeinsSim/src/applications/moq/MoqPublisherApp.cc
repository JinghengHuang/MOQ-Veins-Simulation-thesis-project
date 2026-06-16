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
            switch (proto) {
                case PROTO_QUIC:
                    //socket.connect(connectAddress, connectPort, 0, 0, 0);
                    socket.connect(connectAddress, connectPort);
                    break;
                case PROTO_TCP:
                    tcpSocket.connect(connectAddress, connectPort);
                    break;
                case PROTO_UDP:
                    // UDP is connectionless: there is no establishment callback, so begin
                    // announcing as soon as the (would-be) connect time elapses.
                    sendingAllowed = true;
                    sendTrackAnnouncementData();
                    break;
            }
            break;
        case TIMER_LIMIT_RUNTIME:
            switch (proto) {
                case PROTO_QUIC: socket.close(); break;
                case PROTO_TCP:  tcpSocket.close(); break;
                case PROTO_UDP:  udpSocket.close(); break;
            }
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
            TrackMeta* track = nullptr;

            EV_DEBUG << "ID: " << id << std::endl;
            switch (id)
            {
            case PUB_ANNOUNCE:
                if (it != tracks.end()) {
                    track = &it->second;
                    // Announce as a length-prefixed control byte frame.
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
                    sendControlFrame(c);
                }
                break;
            case SUB_SUCCESS:
                if (it != tracks.end()) {
                    track = &it->second;
                    // Send the object as a MoQ-style length-prefixed byte frame (header +
                    // payload), self-delimiting so the receiver frames by length.
                    MoqObjectFrame f;
                    f.trackId = track->trackId;
                    f.trackAlias = track->trackAlias;
                    f.groupId = 0; // single group for now
                    f.objectId = track->nextObjectId;
                    f.priority = track->priority;
                    f.payloadLength = track->packetSize;
                    f.creationTime = omnetpp::simTime();
                    track->nextObjectId++;
                    sendObjectFrame(f, tid);
                    EV_INFO << "Send track object data: " << track->trackAlias.c_str() << std::endl;

                    // Record one data object offered to the network for this track.
                    PubTrackStat& ps = pubStats[track->trackId];
                    ps.objectsSent++;
                    ps.bytesSent += track->packetSize;
                    if (ps.firstSendTime < SIMTIME_ZERO) ps.firstSendTime = omnetpp::simTime();
                    ps.lastSendTime = omnetpp::simTime();
                    emit(objectSentSignal, (long) track->packetSize);

                    // Arrange sending another packet of the same event
                    sendTrackData(tid);
                }
                break;
            case SUB_ERROR:
                // Do nothing on error packet
                EV_DEBUG << "Getting sub error: " << msg->getKind() << std::endl;
                break;
            }
        }
    } else if (msg->arrivedOn("socketIn")) { // from the transport layer
        // TODO: Add and handle events: case QUIC_I_SENDQUEUE_DRAINING and QUIC_I_SENDQUEUE_FULL
        switch (proto) {
            case PROTO_QUIC: socket.processMessage(msg); break;
            case PROTO_TCP:  tcpSocket.processMessage(msg); break;
            case PROTO_UDP:  udpSocket.processMessage(msg); break;
        }
        //delete msg;
    } else { // something really strange...
        throw omnetpp::cRuntimeError("Invalid message: %d", (int) msg->getKind());
    }
}


void MoqPublisherApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqPublisherApp" << std::endl;

    objectSentSignal = registerSignal("objectSent");

    // Select the underlying transport. QUIC keeps its original setup; TCP/UDP are additive.
    std::string protoStr = par("protocol").stdstringValue();
    if (protoStr == "tcp") proto = PROTO_TCP;
    else if (protoStr == "udp") proto = PROTO_UDP;
    else proto = PROTO_QUIC;
    udpFragmentSize = (int) par("udpFragmentSize").intValue();

    connectPort = par("connectPort");
    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");

    switch (proto) {
        case PROTO_QUIC:
            socket.setOutputGate(gate("socketOut"));
            socket.setCallback(this);
            socket.bind(localAddress, localPort);
            socket.listen();
            break;
        case PROTO_TCP:
            tcpSocket.setOutputGate(gate("socketOut"));
            tcpSocket.setCallback(this);
            tcpSocket.bind(localAddress, localPort);
            break;
        case PROTO_UDP:
            udpSocket.setOutputGate(gate("socketOut"));
            udpSocket.setCallback(this);
            udpSocket.bind(localAddress, localPort);
            break;
    }

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
            handleControlFrame(c);
            controlBuf.data.erase(controlBuf.data.begin(), controlBuf.data.begin() + consumed);
        }
    }
    delete packet;
}

// React to a control frame from the relay (SUBSCRIBE_OK starts this track's transmission).
void MoqPublisherApp::handleControlFrame(const MoqControlFrame& c) {
    if (c.type != CTRL_SUBSCRIBE_OK) return;
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

// ---- protocol-agnostic senders ----

// Send a control frame. QUIC uses the control stream; TCP prepends an envelope class byte on
// the single ordered stream; UDP sends it as (one or more) self-describing datagrams.
void MoqPublisherApp::sendControlFrame(const MoqControlFrame& c) {
    auto frame = MoqFraming::encodeControl(c);
    switch (proto) {
        case PROTO_QUIC: {
            auto packet = new inet::Packet("ANNOUNCE");
            packet->insertAtBack(inet::makeShared<inet::BytesChunk>(frame));
            socket.send(packet, CONTROL_STREAM);
            break;
        }
        case PROTO_TCP: {
            auto packet = new inet::Packet("ANNOUNCE");
            packet->insertAtBack(inet::makeShared<inet::BytesChunk>(
                MoqFraming::encodeEnvelope(MoqFraming::MSG_CONTROL, frame)));
            tcpSocket.send(packet);
            break;
        }
        case PROTO_UDP: {
            auto frags = MoqFraming::fragmentFrame(MoqFraming::MSG_CONTROL, c.trackAlias,
                                                   -1 /*control objectId*/, frame, udpFragmentSize);
            for (auto& d : frags) {
                auto packet = new inet::Packet("ANNOUNCE");
                packet->insertAtBack(inet::makeShared<inet::BytesChunk>(d));
                udpSocket.sendTo(packet, connectAddress, connectPort);
            }
            break;
        }
    }
}

// Send a data object. QUIC uses a per-track data stream (client-bidi 4,8,...); TCP envelopes
// it on the single stream; UDP fragments it into bounded datagrams.
void MoqPublisherApp::sendObjectFrame(const MoqObjectFrame& f, long tid) {
    auto frame = MoqFraming::encode(f);
    switch (proto) {
        case PROTO_QUIC: {
            auto packet = new inet::Packet("TRACK_OBJ");
            packet->insertAtBack(inet::makeShared<inet::BytesChunk>(frame));
            auto iter = trackToStreamMap.find(tid);
            if (iter == trackToStreamMap.end()) {
                trackToStreamMap[tid] = nextStreamId;
                nextStreamId += 4;
                iter = trackToStreamMap.find(tid);
            }
            // Use the 2-arg send: the 1-arg send(packet) resets the stream id to 0.
            socket.send(packet, iter->second);
            break;
        }
        case PROTO_TCP: {
            auto packet = new inet::Packet("TRACK_OBJ");
            packet->insertAtBack(inet::makeShared<inet::BytesChunk>(
                MoqFraming::encodeEnvelope(MoqFraming::MSG_OBJECT, frame)));
            tcpSocket.send(packet);
            break;
        }
        case PROTO_UDP: {
            auto frags = MoqFraming::fragmentFrame(MoqFraming::MSG_OBJECT, f.trackAlias,
                                                   f.objectId, frame, udpFragmentSize);
            for (auto& d : frags) {
                auto packet = new inet::Packet("TRACK_OBJ");
                packet->insertAtBack(inet::makeShared<inet::BytesChunk>(d));
                udpSocket.sendTo(packet, connectAddress, connectPort);
            }
            break;
        }
    }
}

// ---- TCP callbacks ----
void MoqPublisherApp::socketEstablished(inet::TcpSocket *) {
    EV_INFO << "TCP socketEstablished" << std::endl;
    sendingAllowed = true;
    sendTrackAnnouncementData();
}

void MoqPublisherApp::socketDataArrived(inet::TcpSocket *, inet::Packet *packet, bool) {
    auto bytes = packet->peekDataAsBytes();
    const auto& vec = bytes->getBytes();
    tcpRecvBuf.insert(tcpRecvBuf.end(), vec.begin(), vec.end());

    MoqControlFrame c;
    MoqObjectFrame obj;
    size_t consumed;
    int kind;
    while ((kind = MoqFraming::tryParseEnvelope(tcpRecvBuf, c, obj, consumed)) != 0) {
        if (kind == 1) handleControlFrame(c); // publisher only expects control (SUBSCRIBE_OK)
        tcpRecvBuf.erase(tcpRecvBuf.begin(), tcpRecvBuf.begin() + consumed);
    }
    delete packet;
}

// ---- UDP callbacks ----
void MoqPublisherApp::socketDataArrived(inet::UdpSocket *, inet::Packet *packet) {
    auto bytes = packet->peekDataAsBytes();
    const auto& vec = bytes->getBytes();
    MoqFraming::MoqUdpFragment frag;
    if (MoqFraming::parseUdpFragment(vec, frag) && frag.msgClass == MoqFraming::MSG_CONTROL) {
        auto key = std::make_pair(frag.trackAlias, (long) frag.objectId);
        auto& r = udpReasm[key];
        if (r.add(frag, omnetpp::simTime())) {
            MoqControlFrame c;
            size_t consumed;
            if (MoqFraming::tryParseControl(r.data, c, consumed)) handleControlFrame(c);
            udpReasm.erase(key);
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
