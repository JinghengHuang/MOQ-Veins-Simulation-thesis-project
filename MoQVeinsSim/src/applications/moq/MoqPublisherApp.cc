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

    timerTimeoutCheck = new inet::cMessage("MOQ Publisher Timer - Delivery timeout sweep");
    timerTimeoutCheck->setKind(TIMER_TIMEOUT_CHECK);
}

MoqPublisherApp::~MoqPublisherApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
    cancelAndDelete(timerTimeoutCheck);
    
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
        case TIMER_TIMEOUT_CHECK:
            checkOutstandingTimeouts();
            sweepSendBufferTimeouts();  // age-based shedding, independent of transport backpressure
            flushSendBuffer(); // liveness: never depend solely on the drain indication
            scheduleAt(omnetpp::simTime() + timeoutCheckInterval, timerTimeoutCheck);
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
        if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME || id == TIMER_TIMEOUT_CHECK){
            // Connect/limit timers are not track timers; handle and return so the data block
            // below (which parses the message name as a track id) never sees them. This matters
            // for UDP, where the connect timer flips sendingAllowed itself.
            handleTimeout(msg);
            return;
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
                    c.deliveryTimeout = track->deliveryTimeout;
                    c.trackNamespace = track->trackNamespace;
                    c.trackName = track->trackName;
                    c.trackAlias = track->trackAlias;
                    sendControlFrame(c);
                }
                break;
            case SUB_SUCCESS:
                if (subscriptionsEnded) break;   // nothing is offered after PUBLISH_DONE
                if (it != tracks.end()) {
                    track = &it->second;
                    // One Group per sendInterval, holding objectsPerGroup Objects (draft-14
                    // 2.2-2.3). A Group is a sensor frame: a LiDAR sweep is emitted as a burst of
                    // independently usable segments rather than one monolithic object, which is
                    // what real cooperative-perception systems do (EMP uploads 30-38KB chunks so
                    // the edge can use partial frames; ETSI TS 103 324 segments a CPM into
                    // independently interpretable parts). The Group's segments share one subgroup
                    // stream, so a delivery-timeout reset abandons the tail of a stale frame while
                    // the segments already delivered stay usable.
                    long perGroup = track->objectsPerGroup > 0 ? track->objectsPerGroup : 1;
                    long groupId = track->nextObjectId / perGroup;
                    PubTrackStat& ps = pubStats[track->trackId];

                    for (long i = 0; i < perGroup; i++) {
                        MoqObjectFrame f;
                        f.trackId = track->trackId;
                        f.trackAlias = track->trackAlias;
                        f.groupId = groupId;
                        f.subgroupId = 0; // one subgroup per group: no layered structure here
                        f.objectId = track->nextObjectId;
                        f.priority = track->priority;
                        f.payloadLength = track->packetSize;
                        f.creationTime = omnetpp::simTime();
                        track->nextObjectId++;
                        sendObjectFrame(f, tid);

                        // Objects offered to the network. What reaches QUIC is counted separately
                        // in doSendQuicChunk: the two differ by objects shed past their delivery
                        // timeout or evicted on buffer overflow.
                        ps.objectsOffered++;
                        ps.bytesOffered += track->packetSize;
                        emit(objectSentSignal, (long) track->packetSize);
                    }
                    EV_INFO << "Sent group " << groupId << " (" << perGroup << " objects) of "
                            << track->trackAlias.c_str() << std::endl;

                    if (ps.firstSendTime < SIMTIME_ZERO) ps.firstSendTime = omnetpp::simTime();
                    ps.lastSendTime = omnetpp::simTime();

                    sendTrackData(tid); // schedule the next group
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
    sendBufferDepthSignal = registerSignal("sendBufferDepth");
    emit(sendBufferDepthSignal, (long) 0);  // seed, so timeavg/max are defined if it never fills

    // Select the underlying transport. QUIC keeps its original setup; TCP/UDP are additive.
    std::string protoStr = par("protocol").stdstringValue();
    if (protoStr == "tcp") proto = PROTO_TCP;
    else if (protoStr == "udp") proto = PROTO_UDP;
    else proto = PROTO_QUIC;
    udpFragmentSize = (int) par("udpFragmentSize").intValue();
    sendBufferLimit = (size_t) par("sendBufferLimit").intValue();
    quicChunkBytes = par("quicChunkBytes").intValue();
    // Mirror QUIC's own send-queue limit, and keep a handle on the module so the send path can read
    // the queue's true occupancy before each write.
    if (auto* host = getParentModule()) {
        if (auto* q = host->getSubmodule("quic")) {
            if (q->hasPar("sendQueueLimit")) quicSendQueueLimit = q->par("sendQueueLimit").intValue();
            quicModule = dynamic_cast<inet::quic::Quic*>(q);
        }
    }

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
                track.deliveryTimeout = map->containsKey("objectDeliveryTimeout")
                                        ? (*map)["objectDeliveryTimeout"].doubleValueInUnit("s") : 0.0;
                // How many objects form one Group/Subgroup, i.e. one stream (draft-14 2.2-2.3).
                track.objectsPerGroup = map->containsKey("objectsPerGroup")
                                        ? (*map)["objectsPerGroup"].intValue() : 1;
                track.nextObjectId = 0;
                track.timer = new omnetpp::cMessage((std::to_string(track.trackId)).c_str(), PUB_ANNOUNCE);
                tracks[i] = track;
            }
        }
    }

    timeoutCheckInterval = par("deliveryTimeoutCheckInterval").doubleValue();

    scheduleAt(inet::simTime() + par("connectTime"), timerConnect);
    if (proto == PROTO_QUIC)
        scheduleAt(inet::simTime() + timeoutCheckInterval, timerTimeoutCheck);
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
            socket.send(packet, CONTROL_STREAM, CONTROL_STREAM_PRIORITY);
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
    // The subscription is over (PUBLISH_DONE already sent): draft-14 section 9.12 requires that a
    // sender close every stream it will ever open BEFORE sending PUBLISH_DONE, so nothing may go
    // out afterwards.
    if (subscriptionsEnded) return;
    auto frame = MoqFraming::encode(f);
    PubTrackStat& ps = pubStats[tid];
    switch (proto) {
        case PROTO_QUIC: {
            // MoQ maps one Subgroup onto one stream (draft-14 section 2.2). Objects of the same
            // subgroup share a stream, in ascending object id; a new subgroup opens a new stream.
            // This is what makes the delivery timeout enforceable: a stale object's subgroup
            // stream can be reset (section 10.4.3), abandoning its in-flight bytes. On one
            // long-lived per-track stream a reset would kill the track forever, and a truncated
            // object would permanently desynchronise the receiver's length-prefix parser.
            SubgroupKey key{tid, f.groupId, f.subgroupId};

            // The subgroup's stream was reset, so the rest of the subgroup is gone with it --
            // section 10.4.3's reset abandons the whole stream. Charge the drop to whatever caused
            // the reset, not unconditionally to timeout shedding.
            auto rsIt = resetSubgroups.find(key);
            if (rsIt != resetSubgroups.end()) {
                if (rsIt->second == MOQ_ERR_SEND_BUFFER_OVERFLOW) quicShedOverflow[tid]++;
                else quicShedStale[tid]++;
                return;
            }

            auto sIt = subgroupStreams.find(key);
            if (sIt == subgroupStreams.end())
                sIt = subgroupStreams.emplace(key, (nextStreamId += 4) - 4).first;

            Pending p;
            p.tid = tid;
            p.subgroup = key;
            p.streamId = sIt->second;
            p.priority = f.priority;
            p.payloadLength = f.payloadLength;
            p.bytes = std::move(frame);
            p.createdAt = f.creationTime;
            { auto tIt = tracks.find(tid); if (tIt != tracks.end()) p.timeout = tIt->second.deliveryTimeout; }
            enqueuePending(std::move(p));
            flushSendBuffer(); // sends in priority order while QUIC is accepting
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

// Hand the next slice of one buffered object to QUIC on its per-track data stream. Objects are
// written in quicChunkBytes-sized pieces so a bulk object can never single-handedly exceed QUIC's
// connection-wide send-queue limit and lock every stream out; the remainder waits in the send
// buffer, where a higher-priority object can overtake it. Stream byte order is preserved because
// each track owns its own stream, so interleaving slices of different tracks is safe.
// Bytes currently sitting in QUIC's send queue (unsent + sent-but-unacked), read from the transport.
long MoqPublisherApp::quicSendQueueLength() {
    long len = quicModule == nullptr ? 0 : (long) quicModule->getSendQueueLength(socket.getSocketId());
    if (len > quicQueueMaxObserved) quicQueueMaxObserved = len;
    return len;
}

void MoqPublisherApp::doSendQuicChunk(Pending& p) {
    size_t remaining = p.bytes.size() - p.sentOffset;
    size_t n = std::min(remaining, (size_t) quicChunkBytes);
    auto begin = p.bytes.begin() + p.sentOffset;
    auto packet = new inet::Packet("TRACK_OBJ");
    packet->insertAtBack(inet::makeShared<inet::BytesChunk>(
        std::vector<uint8_t>(begin, begin + n)));
    // 2-arg send: the 1-arg send(packet) resets the stream id to 0. The third argument carries
    // the MoQ publisher priority down to QUIC's stream scheduler (MoQ send order, draft-14
    // section 7.2), which only has an effect when quic.streamScheduler = "Priority"; with the
    // default round-robin scheduler QUIC ignores it and all streams are served equally.
    socket.send(packet, p.streamId, quicPriorityOf(p.priority));
    p.sentOffset += n;

    if (p.sentOffset == p.bytes.size()) { // object fully committed to QUIC
        // The object is now queued inside QUIC, beyond the reach of the send buffer. Keep
        // tracking it: MoQ's delivery timeout applies until it is actually delivered, and a
        // stale object still sitting in the transport queue is exactly what a stream reset is
        // for (draft-14 sections 9.2.1.2 and 10.4.3).
        if (p.timeout > SIMTIME_ZERO)
            outstanding.push_back({p.subgroup, p.streamId, p.tid, p.createdAt, p.timeout});

        PubTrackStat& ps = pubStats[p.tid];
        ps.objectsSent++;
        ps.bytesSent += (long) p.payloadLength;
        // Phase-0 diagnostic: time spent waiting in the app send buffer (creation->fully sent).
        double dwell = (omnetpp::simTime() - p.createdAt).dbl();
        sendDwellSum += dwell;
        if (dwell > sendDwellMax) sendDwellMax = dwell;
        sendDwellCount++;
    }
}

// Sweep the objects already written to QUIC and reset the stream of any that have outrun their
// delivery timeout. draft-14 section 9.2.1.2 states the obligation -- "If an object in a subgroup
// exceeds the delivery timeout, the publisher MUST reset the underlying transport stream" -- and
// section 10.4.3 gives the mechanism and the DELIVERY_TIMEOUT (0x2) reset code. Without this the
// timeout only ever sees the app
// send buffer, so it does nothing whenever the transport buffer is deep enough that objects age
// out *after* being handed to QUIC -- which is precisely when abandoning them is worth doing.
//
// We get no delivery notification from QUIC, so an object may in fact have arrived by the time we
// reset its stream. That is harmless: the receiver has already parsed and recorded it, and the
// reset only discards reassembly data it has not yet consumed.
void MoqPublisherApp::checkOutstandingTimeouts() {
    // An empty send queue means every byte we handed to QUIC has been acknowledged, so nothing
    // outstanding is still in flight and none of it needs resetting. Without this the list only
    // ever grows and every object is eventually reset on age, long after it was delivered.
    if (quicSendQueueLength() == 0) {
        outstanding.clear();
        return;
    }

    omnetpp::simtime_t now = omnetpp::simTime();
    for (auto it = outstanding.begin(); it != outstanding.end(); ) {
        if (resetSubgroups.count(it->subgroup)) {   // stream already gone
            it = outstanding.erase(it);
            continue;
        }
        if (now - it->createdAt > it->timeout) {
            resetSubgroupStream(it->subgroup, it->streamId, MOQ_ERR_DELIVERY_TIMEOUT);
            resetAfterSend[it->tid]++;
            it = outstanding.erase(it);
            continue;
        }
        ++it;
    }
}

// Reset the QUIC stream carrying a subgroup (MoQ draft-14 section 10.4.3, "Closing Subgroup
// Streams": a sender that closes a stream before delivering all its objects MUST use RESET_STREAM,
// which includes an open Subgroup exceeding its Delivery Timeout). The
// reset discards the object's bytes still queued inside QUIC and stops them being retransmitted,
// and tells the receiver to discard the partial object. Because a stream carries a whole
// subgroup, the rest of that subgroup goes with it -- objects already buffered are dropped when
// they surface in flushSendBuffer, and later objects of the subgroup are refused in
// sendObjectFrame.
void MoqPublisherApp::resetSubgroupStream(const SubgroupKey& key, long streamId, int errorCode) {
    socket.resetStream(streamId, errorCode);
    resetSubgroups[key] = errorCode;
    subgroupStreams.erase(key);
    subgroupResets++;
}

// Drop every buffered object past its delivery timeout, regardless of transport state.
//
// flushSendBuffer also tests staleness, but only for objects that reach the head of the priority
// queue, and its loop exits as soon as QUIC is backpressured -- so under heavy congestion, exactly
// when the backlog is growing, the timeout stopped firing and the buffer ran away. This sweep runs
// off the delivery-timeout timer instead, so shedding is driven by object age (draft-14 9.2.1.2)
// rather than by whether the transport happens to be accepting data.
void MoqPublisherApp::sweepSendBufferTimeouts() {
    if (subscriptionsEnded) return;
    omnetpp::simtime_t now = omnetpp::simTime();
    for (auto prioIt = sendBuffer.begin(); prioIt != sendBuffer.end(); ) {
        auto& queue = prioIt->second;
        for (auto it = queue.begin(); it != queue.end(); ) {
            // timeout == 0 means the track configured none: it is fully reliable and never shed.
            bool stale = it->timeout > SIMTIME_ZERO && (now - it->createdAt) > it->timeout;
            if (!stale) { ++it; continue; }
            if (it->sentOffset > 0)
                resetSubgroupStream(it->subgroup, it->streamId, MOQ_ERR_DELIVERY_TIMEOUT);
            quicShedStale[it->tid]++;
            it = queue.erase(it);
            sendBufferCount--;
        }
        prioIt = queue.empty() ? sendBuffer.erase(prioIt) : std::next(prioIt);
    }
    emit(sendBufferDepthSignal, sendBufferCount);
}

// Buffer an object while QUIC is blocked, keeping FIFO order within each priority. When the buffer
// exceeds its limit the subscription is over -- see terminateSubscriptions().
void MoqPublisherApp::enqueuePending(Pending&& p) {
    long prio = p.priority;
    sendBuffer[prio].push_back(std::move(p));
    sendBufferCount++;
    emit(sendBufferDepthSignal, sendBufferCount);
    if (sendBufferCount <= (long) sendBufferLimit) return;

    // draft-14 section 9.2.1.2: "If a subscriber fails to consume Objects at a sufficient rate,
    // causing the publisher to exceed its resource limits, the publisher MAY terminate the
    // subscription with error TOO_FAR_BEHIND." sendBufferLimit is the "implementation defined
    // limit" of section 9.12's TOO_FAR_BEHIND. This replaces an earlier priority-ordered eviction,
    // which was not a MoQ behaviour: priority governs transmission ORDER only (section 7.2), and
    // dropping the lowest-priority object silently made a track with no DELIVERY_TIMEOUT -- i.e.
    // one the draft says delivers every object -- quietly lossy instead of failing loudly.
    terminateSubscriptions(PUBDONE_TOO_FAR_BEHIND);
}

// End every subscription this publisher is serving, per draft-14 section 9.12. Ordering matters:
// "A sender MUST NOT send PUBLISH_DONE until it has closed all streams it will ever open, and has
// no further datagrams to send, for a subscription", so reset the open subgroup streams first.
// The resource limit is a property of the peer ("the publisher's queue of objects to be sent to
// the given subscriber"), so all of that peer's subscriptions end together rather than picking one
// track -- choosing a victim by priority would reintroduce the very thing this replaced.
void MoqPublisherApp::terminateSubscriptions(long statusCode) {
    if (subscriptionsEnded) return;         // PUBLISH_DONE is sent once per subscription
    subscriptionsEnded = true;
    terminationTime = omnetpp::simTime();
    terminationStatus = statusCode;

    // 1. Abandon everything still in flight or queued for this peer. Snapshot first:
    //    resetSubgroupStream erases from subgroupStreams, which would invalidate the iterator.
    std::vector<std::pair<SubgroupKey, long>> openStreams(subgroupStreams.begin(),
                                                          subgroupStreams.end());
    for (auto& entry : openStreams)
        resetSubgroupStream(entry.first, entry.second, MOQ_ERR_SEND_BUFFER_OVERFLOW);
    for (auto& prioQueue : sendBuffer)
        for (auto& pend : prioQueue.second) {
            quicShedOverflow[pend.tid]++;
            quicShed++;
        }
    sendBuffer.clear();
    sendBufferCount = 0;
    outstanding.clear();
    emit(sendBufferDepthSignal, (long) 0);

    // 2. One PUBLISH_DONE per subscription, i.e. per announced track.
    for (auto& t : tracks) {
        MoqControlFrame done;
        done.type = CTRL_PUBLISH_DONE;
        done.statusCode = statusCode;
        done.trackId = t.second.trackId;
        done.publisherId = t.second.publisherId;
        done.trackNamespace = t.second.trackNamespace;
        done.trackName = t.second.trackName;
        done.trackAlias = t.second.trackAlias;
        sendControlFrame(done);
        // 3. Stop producing: the subscription state is gone, so nothing more may be sent for it.
        if (t.second.timer && t.second.timer->isScheduled())
            cancelEvent(t.second.timer);
    }
    EV_WARN << "PUBLISH_DONE(status=" << statusCode << ") sent for all tracks at "
            << terminationTime << ": send buffer exceeded " << sendBufferLimit << " objects"
            << std::endl;
}

// QUIC drained: flush buffered objects highest-priority (lowest number) first, oldest within a
// priority, until QUIC blocks again or the buffer empties.
void MoqPublisherApp::flushSendBuffer() {
    // Stop as soon as the estimated occupancy reaches the limit (predicts QUIC's synchronous
    // "full"), so the next object is buffered rather than rejected. !quicBlocked is a safety net.
    // QUIC only enqueues a write when it processes the message, in a later event, so the queue
    // length it reports here does not yet include anything written during this event. Track that
    // ourselves, keyed on the event: flushSendBuffer is called once per object, and a group burst
    // produces several objects in one event, so a per-call counter would let each call overshoot
    // the limit again and QUIC would silently reject (and discard) the excess.
    if (quicWriteEvent != omnetpp::simTime()) {
        quicWriteEvent = omnetpp::simTime();
        quicBytesThisEvent = 0;
    }

    while (!quicBlocked && sendBufferCount > 0) {
        if (quicSendQueueLength() + quicBytesThisEvent >= quicSendQueueLimit) break;

        auto it = sendBuffer.begin();
        while (it != sendBuffer.end() && it->second.empty()) it = sendBuffer.erase(it);
        if (it == sendBuffer.end()) break;
        Pending& p = it->second.front();

        // This object's subgroup stream was already reset, so the object went with it: a reset
        // abandons the whole stream, and with it the rest of the subgroup (section 10.4.3).
        // Dropped lazily here rather than by walking the buffer at reset time. Attributed to the
        // reset's cause -- timeout shedding is MoQ, overflow eviction is our buffer artifact.
        auto rsIt = resetSubgroups.find(p.subgroup);
        if (rsIt != resetSubgroups.end()) {
            if (rsIt->second == MOQ_ERR_SEND_BUFFER_OVERFLOW) quicShedOverflow[p.tid]++;
            else quicShedStale[p.tid]++;
            it->second.pop_front();
            sendBufferCount--;
            if (it->second.empty()) sendBuffer.erase(it);
            continue;
        }

        // Partial reliability: drop objects already older than their delivery timeout instead of
        // sending stale data (MoQ DELIVERY_TIMEOUT, draft-14 section 9.2.1.2). Frees capacity for
        // fresh objects. If the object has already started transmitting, section 10.4.3 requires
        // the publisher to RESET its subgroup's stream: that discards the bytes still queued in
        // QUIC, stops them being retransmitted, and tells the receiver to drop the partial object.
        bool stale = p.timeout > SIMTIME_ZERO
                     && (omnetpp::simTime() - p.createdAt) > p.timeout;
        if (stale) {
            if (p.sentOffset > 0)
                resetSubgroupStream(p.subgroup, p.streamId, MOQ_ERR_DELIVERY_TIMEOUT);
            quicShedStale[p.tid]++;
        }
        else {
            size_t before = p.sentOffset;
            doSendQuicChunk(p);
            quicBytesThisEvent += (long) (p.sentOffset - before);
            // Do NOT set quicBlocked from this estimate. It is cleared only by QUIC's drain
            // indication, and QUIC only fires that once its queue has first risen above the
            // low-water mark. Pacing ourselves below that mark therefore blocks us forever.
            // The loop's occupancy check above is what keeps writes inside QUIC's window.
        }

        if (stale || p.sentOffset == p.bytes.size()) { // object finished with, one way or the other
            it->second.pop_front();
            sendBufferCount--;
            if (it->second.empty()) sendBuffer.erase(it);
        }
    }
    emit(sendBufferDepthSignal, sendBufferCount);
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
    // QUIC won't accept more data; buffer subsequent objects instead of letting QUIC reject
    // (and silently drop) them.
    quicBlocked = true;
    EV_DEBUG << "Send queue full; buffering objects" << std::endl;
}

void MoqPublisherApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    quicBlocked = false;
    flushSendBuffer(); // QUIC has room again; top the queue back up
    EV_DEBUG << "Send queue drained; flushed buffer" << std::endl;
}

void MoqPublisherApp::finish()
{
    // Must stay 0: a rejected write is silently dropped by QUIC, which would tear a hole in the
    // middle of an object and desync the receiver's parser.
    recordScalar("quicSendRejected", quicRejected);
    // Peak occupancy the app ever drove QUIC's send queue to; stays under sendQueueLimit when the
    // write pacing is working.
    recordScalar("quicSendQueueMaxObserved", quicQueueMaxObserved, "B");
    recordScalar("objectsDiscardedAtTeardown", quicShed);  // queued objects dropped when the subscription ended
    recordScalar("subgroupStreamResets", subgroupResets); // RESET_STREAM sent (section 10.4.3)
    // Subscription teardown at the resource limit (draft-14 9.2.1.2 / 9.12). survivalTime is the
    // headline for a track with no DELIVERY_TIMEOUT: how long the publisher sustained the
    // subscription before its queue exceeded the implementation-defined limit. -1 = never ended.
    recordScalar("subscriptionEnded", subscriptionsEnded ? 1 : 0);
    recordScalar("subscriptionEndStatus", subscriptionsEnded ? terminationStatus : -1);
    recordScalar("survivalTime", subscriptionsEnded ? terminationTime.dbl() : -1.0, "s");
    if (sendDwellCount > 0) {
        recordScalar("sendBufferDwellMean", sendDwellSum / sendDwellCount, "s");
        recordScalar("sendBufferDwellMax", sendDwellMax, "s");
    }
    // Per-track offered-load scalars, used as the denominator for object loss ratio.
    EV_DEBUG << "Writing scalar to file" << std::endl;
    for (auto& track : tracks) {
        long tid = track.second.trackId;
        const PubTrackStat& ps = pubStats[tid];
        std::string prefix = "track[" + track.second.trackAlias + "].";
        recordScalar((prefix + "objectsOffered").c_str(), ps.objectsOffered);
        recordScalar((prefix + "bytesOffered").c_str(), ps.bytesOffered, "B");
        recordScalar((prefix + "objectsSent").c_str(), ps.objectsSent);
        recordScalar((prefix + "bytesSent").c_str(), ps.bytesSent, "B");
        double span = (ps.lastSendTime - ps.firstSendTime).dbl();
        if (span > 0) {
            recordScalar((prefix + "offeredRate").c_str(), ps.bytesOffered * 8.0 / span, "bps");
        }
        auto sIt = quicShedStale.find(tid);
        recordScalar((prefix + "objectsShedStale").c_str(), sIt != quicShedStale.end() ? sIt->second : 0);
        // Collateral of a send-buffer-overflow reset: NOT MoQ shedding, kept out of objectsShedStale.
        auto oIt = quicShedOverflow.find(tid);
        recordScalar((prefix + "objectsShedOverflow").c_str(), oIt != quicShedOverflow.end() ? oIt->second : 0);
        // Objects abandoned after they had already been written to QUIC (stream reset).
        auto rIt = resetAfterSend.find(tid);
        recordScalar((prefix + "objectsResetAfterSend").c_str(), rIt != resetAfterSend.end() ? rIt->second : 0);
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
    // The subscription is over (PUBLISH_DONE sent), so stop producing. Without this the generator
    // re-arms the timer that terminateSubscriptions just cancelled -- it is called at the end of
    // the same event that tripped the limit. sendObjectFrame would still drop the objects, but
    // objectsOffered would keep climbing after teardown and skew every ratio measured against it.
    if (subscriptionsEnded) return;

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
