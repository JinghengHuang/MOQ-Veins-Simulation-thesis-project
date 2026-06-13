#include "MoqSubscriberApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/chunk/SliceChunk.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "models/MoqObjectChunk_m.h"
#include "models/MoqObjectAck_m.h"
#include "models/MoqPublisherAnnounce_m.h"
#include "models/MoqSubscriber_m.h"
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

                // Each track subscribes on its own unique stream so the relay can map
                // streams to tracks unambiguously and per-track data never shares a buffer.
                auto iter = trackToStreamMap.find(tid);
                if (iter == trackToStreamMap.end()) {
                    trackToStreamMap[tid] = nextStreamId;
                    nextStreamId += 4;
                    iter = trackToStreamMap.find(tid);
                }
                if (iter != trackToStreamMap.end() && track != nullptr && subscribeRequest != nullptr) {
                    int streamId = iter->second;
                    // Use the 2-arg send: the 1-arg send(packet) internally calls
                    // send(packet, 0) and would overwrite the stream ID back to 0.
                    socket.send(subscribeRequest, streamId);
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
    socket->recv(static_cast<int64_t>(dataInfo->getAvaliableDataSize()), dataInfo->getStreamID());
}

void MoqSubscriberApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    auto frontChunk = packet->peekAtFront<inet::Chunk>();
    EV_DEBUG << "Received packet: " << packet->getFullName() << " With header: " << frontChunk.get()->getClassName() << std::endl;

    // A large object is delivered as a sequence of SliceChunk fragments; a small object
    // arrives as a single complete MoqObjectChunk. Resolve the underlying object and how
    // many bytes this delivery carries, then reassemble per (trackAlias, objectId).
    const MoqObjectChunk *chunk = dynamic_cast<const MoqObjectChunk *>(frontChunk.get());
    long sliceBytes = 0;
    if (chunk != nullptr) {
        sliceBytes = inet::B(chunk->getChunkLength()).get();
    } else {
        const auto *sliceChunk = dynamic_cast<const inet::SliceChunk *>(frontChunk.get());
        if (sliceChunk != nullptr) {
            chunk = dynamic_cast<const MoqObjectChunk *>(sliceChunk->getChunk().get());
            sliceBytes = inet::B(sliceChunk->getLength()).get();
        }
    }
    if (chunk == nullptr) {
        delete packet;
        return;
    }

    std::string trackAlias = chunk->getTrackAlias();
    long objId = chunk->getObjectId();
    auto key = std::make_pair(trackAlias, objId);
    SubObjReasm& st = reasm[key];
    if (st.totalBytes == 0) {
        st.totalBytes = 64 + chunk->getPayloadLength();
        st.firstSliceTime = omnetpp::simTime();
        st.creationTime = chunk->getCreationTime();
        st.payloadLength = chunk->getPayloadLength();
        st.trackId = chunk->getTrackId();
    }
    st.receivedBytes += sliceBytes;

    if (st.receivedBytes < st.totalBytes) {
        // Object not yet complete — wait for more fragments.
        delete packet;
        return;
    }

    // ---- object fully received: record metrics ----
    omnetpp::simtime_t now = omnetpp::simTime();
    double latency = (now - st.creationTime).dbl();
    double completion = (now - st.firstSliceTime).dbl();
    emit(endToEndLatencySignal, latency);
    emit(objectCompletionTimeSignal, completion);

    SubTrackStat& ts = trackStat(trackAlias);
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
    ts.bytes += st.payloadLength;
    if (objId > ts.highestObjId) ts.highestObjId = objId;
    if (ts.firstRecv < SIMTIME_ZERO) ts.firstRecv = now;
    ts.lastRecv = now;

    receive_count += 1;
    long completedTrackId = st.trackId;
    reasm.erase(key);

    // Acknowledge the fully-received object back to the relay so it forwards the
    // next object on this track's stream. Sent on the same stream we subscribed on.
    auto trackEntry = tracks.find(completedTrackId);
    auto streamEntry = trackToStreamMap.find(completedTrackId);
    if (trackEntry != tracks.end() && streamEntry != trackToStreamMap.end()) {
        auto ackPacket = new inet::Packet("OBJECT_ACK");
        inet::Ptr<MoqObjectAck> ack = inet::makeShared<MoqObjectAck>();
        ack->setSubscriberId(getParentModule()->getFullName());
        ack->setTrackNamespace(trackEntry->second.trackNamespace.c_str());
        ack->setTrackName(trackEntry->second.trackName.c_str());
        ack->setObjectId(objId);
        ack->setChunkLength(inet::B(64));
        ackPacket->insertAtBack(ack);
        socket->send(ackPacket, streamEntry->second);
    }

    EV_INFO << "Track object completed"
            << " | trackAlias=" << trackAlias
            << " | objectId=" << objId
            << " | payloadLength=" << st.payloadLength
            << " | e2eLatency=" << latency << "s"
            << " | completionTime=" << completion << "s"
            << " | deadlineMiss=" << miss
            << std::endl;
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
