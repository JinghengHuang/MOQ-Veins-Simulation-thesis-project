#include "MoqSubscriberApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "models/MoqFraming.h"
#include <algorithm>
#include <cmath>

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
    // Note: cOutVector instances are owned by the module and deleted automatically by the
    // OMNeT++ kernel; deleting them here would be a double free.
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
                // Subscribe via a length-prefixed control byte frame on the control stream.
                std::string vId = getParentModule()->getFullName();
                MoqControlFrame c;
                c.type = CTRL_SUBSCRIBE;
                c.subscriberId = vId;
                c.trackNamespace = track->trackNamespace;
                c.trackName = track->trackName;
                c.subscriberPriority = track->priority;
                c.startObjectId = 0;
                auto subscribeRequest = new inet::Packet("SUBSCRIBE");
                subscribeRequest->insertAtBack(inet::makeShared<inet::BytesChunk>(MoqFraming::encodeControl(c)));
                socket.send(subscribeRequest, CONTROL_STREAM);
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

    endToEndLatencySignal     = registerSignal("endToEndLatency");
    objectCompletionTimeSignal = registerSignal("objectCompletionTime");
    e2eJitterSignal           = registerSignal("e2eJitter");
    deadlineMissSignal        = registerSignal("deadlineMiss");

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
                track.deadline       = map->containsKey("deadline")
                                       ? (*map)["deadline"].doubleValueInUnit("s") : 0.0;
                track.timer = new omnetpp::cMessage(std::to_string(i).c_str(), PUB_ANNOUNCE);
                tracks[i] = track;
                // Seed per-track stats so the deadline is known before any object arrives.
                trackStat(track.trackAlias).deadline = track.deadline;
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
    long s = dataInfo->getStreamID();
    if (recvInFlight == s) return; // the outstanding recv will drain whatever is available
    recvPending.insert(s);
    if (recvInFlight < 0) startNextRecv(socket);
}

// Serialize receives: one recv outstanding at a time, draining a whole stream per recv, so
// the delivered packet (which carries no stream tag) always maps to recvInFlight.
void MoqSubscriberApp::startNextRecv(inet::QuicSocket* socket) {
    if (recvPending.empty()) { recvInFlight = -1; return; }
    long s = *recvPending.begin();
    recvPending.erase(recvPending.begin());
    recvInFlight = s;
    socket->recv((int64_t) 1 << 40, s); // large size => drain all available for this stream
}

void MoqSubscriberApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    long streamId = recvInFlight;
    recvInFlight = -1;

    auto bytes = packet->peekDataAsBytes();
    const auto& vec = bytes->getBytes();
    omnetpp::simtime_t now = omnetpp::simTime();
    StreamReassembler& rb = streamBuffers[streamId];
    if (rb.data.empty()) rb.frameStartTime = now;
    rb.data.insert(rb.data.end(), vec.begin(), vec.end());

    if (streamId == CONTROL_STREAM) {
        // Control (e.g. SUBSCRIBE_OK) — parse and ignore; the subscriber needs no ack.
        MoqControlFrame c;
        size_t consumed;
        while (MoqFraming::tryParseControl(rb.data, c, consumed)) {
            rb.data.erase(rb.data.begin(), rb.data.begin() + consumed);
        }
    } else {
        // Data stream: frame length-delimited objects and record metrics for each.
        MoqObjectFrame f;
        size_t consumed;
        while (MoqFraming::tryParse(rb.data, f, consumed)) {
            recordObject(f, rb.frameStartTime, now);
            rb.data.erase(rb.data.begin(), rb.data.begin() + consumed);
            rb.frameStartTime = now; // next object's first byte arrived now
        }
    }
    delete packet;
    startNextRecv(socket);
}

// Record per-object metrics for a fully-received object frame.
void MoqSubscriberApp::recordObject(const MoqObjectFrame& f, omnetpp::simtime_t frameStartTime, omnetpp::simtime_t now)
{
    double latency = (now - f.creationTime).dbl();
    double completion = (now - frameStartTime).dbl();
    emit(endToEndLatencySignal, latency);
    emit(objectCompletionTimeSignal, completion);

    SubTrackStat& ts = trackStat(f.trackAlias);
    ts.latencySum += latency;
    if (latency > ts.latencyMax) ts.latencyMax = latency;
    ts.completionSum += completion;
    if (completion > ts.completionMax) ts.completionMax = completion;

    if (ts.lastLatency >= 0) {
        double jitter = std::fabs(latency - ts.lastLatency);
        emit(e2eJitterSignal, jitter);
        ts.jitterSum += jitter;
        ts.jitterCount++;
    }
    ts.lastLatency = latency;

    int miss = (ts.deadline > SIMTIME_ZERO && latency > ts.deadline.dbl()) ? 1 : 0;
    emit(deadlineMissSignal, (long) miss);
    if (miss) ts.deadlineMisses++;

    ts.received++;
    ts.bytes += f.payloadLength;
    if (f.objectId > ts.highestObjId) ts.highestObjId = f.objectId;
    if (ts.firstRecv < SIMTIME_ZERO) ts.firstRecv = now;
    ts.lastRecv = now;

    receive_count += 1;
    EV_INFO << "Track object completed"
            << " | trackAlias=" << f.trackAlias
            << " | objectId=" << f.objectId
            << " | payloadLength=" << f.payloadLength
            << " | e2eLatency=" << latency << "s"
            << " | completionTime=" << completion << "s"
            << " | deadlineMiss=" << miss
            << std::endl;
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

MoqSubscriberApp::SubTrackStat& MoqSubscriberApp::trackStat(const std::string& trackAlias)
{
    return subStats[trackAlias];
}

void MoqSubscriberApp::finish()
{
    for (auto& entry : subStats) {
        const std::string& trackAlias = entry.first;
        SubTrackStat& ts = entry.second;
        std::string prefix = "track[" + trackAlias + "].";

        recordScalar((prefix + "objectsReceived").c_str(), ts.received);
        recordScalar((prefix + "bytesReceived").c_str(), ts.bytes, "B");

        // Object loss ratio: objects missing in the [0 .. highestObjId] range that were
        // never fully received. Objects still in flight at sim end are not counted.
        long expected = ts.highestObjId + 1;
        long lost = (expected > ts.received) ? (expected - ts.received) : 0;
        recordScalar((prefix + "objectsExpected").c_str(), expected);
        recordScalar((prefix + "objectsLost").c_str(), lost);
        if (expected > 0) {
            recordScalar((prefix + "lossRatio").c_str(), (double) lost / (double) expected);
        }

        // Per-track goodput over the active reception window.
        double span = (ts.lastRecv - ts.firstRecv).dbl();
        if (span > 0) {
            recordScalar((prefix + "throughput").c_str(), ts.bytes * 8.0 / span, "bps");
        }

        recordScalar((prefix + "deadlineMisses").c_str(), ts.deadlineMisses);
        if (ts.received > 0) {
            recordScalar((prefix + "deadlineMissRatio").c_str(),
                         (double) ts.deadlineMisses / (double) ts.received);
            recordScalar((prefix + "meanLatency").c_str(), ts.latencySum / ts.received, "s");
            recordScalar((prefix + "maxLatency").c_str(), ts.latencyMax, "s");
            recordScalar((prefix + "meanCompletionTime").c_str(), ts.completionSum / ts.received, "s");
            recordScalar((prefix + "maxCompletionTime").c_str(), ts.completionMax, "s");
        }
        if (ts.jitterCount > 0) {
            recordScalar((prefix + "meanJitter").c_str(), ts.jitterSum / ts.jitterCount, "s");
        }
    }
}

// Based on track configurations, send track announcement data
void MoqSubscriberApp::sendTrackSubscribeData(){
    
    for (auto & track : tracks){

        scheduleAt(inet::simTime(), track.second.timer);
    }
}

}
