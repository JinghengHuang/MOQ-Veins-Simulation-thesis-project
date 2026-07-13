/* --- MqttPublisherApp.cc --- */
#include "MqttPublisherApp.h"

#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"

namespace moqveinssim {
Define_Module(MqttPublisherApp);

MqttPublisherApp::~MqttPublisherApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerFlush);
    for (auto& t : topics) cancelAndDelete(t.second.timer);
}

// Write as much of the outbound queue as the transport will accept. Strict FIFO: MQTT offers no
// way to reorder or drop, so a small message waits behind a large one.
void MqttPublisherApp::flushSendQueue() {
    if (proto != PROTO_QUIC) return;

    // Two guards, because neither alone is enough:
    //  - quicBlocked is the authoritative cross-event signal, but QUIC reports "full"
    //    asynchronously, so a burst written in one event overruns it before it arrives.
    //  - bytesThisEvent caps what we hand over within a single event. It is reset per call, since
    //    across events quicBlocked is what tells us whether QUIC has room.
    // Without the second guard a 300 KB burst is accepted in part and silently discarded in part.
    long bytesThisEvent = 0;

    while (!quicBlocked && !sendQueue.empty() && bytesThisEvent < quicSendQueueLimit) {
        auto& front = sendQueue.front();
        auto *packet = new inet::Packet("MQTT");
        packet->insertAtBack(inet::makeShared<inet::BytesChunk>(front));
        quicSocket.send(packet, CONTROL_STREAM);
        bytesThisEvent += (long) front.size();
        sendQueue.pop_front();
    }
}

void MqttPublisherApp::handleStartOperation(inet::LifecycleOperation *op) {
    proto = par("protocol").stdstringValue() == "tcp" ? PROTO_TCP : PROTO_QUIC;
    qos = (uint8_t) par("qos").intValue();

    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));
    connectPort = par("connectPort");
    inet::L3Address local = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");

    if (proto == PROTO_QUIC) {
        quicSocket.setOutputGate(gate("socketOut"));
        quicSocket.setCallback(this);
        quicSocket.bind(local, localPort);
        quicSocket.listen();

        if (auto *host = getParentModule()) {
            if (auto *q = host->getSubmodule("quic")) {
                if (q->hasPar("sendQueueLimit")) quicSendQueueLimit = q->par("sendQueueLimit").intValue();
            }
        }
        sendQueueLimitMsgs = (size_t) par("sendQueueLimit").intValue();
        timerFlush = new omnetpp::cMessage("MQTT flush", TIMER_FLUSH);
    }
    else {
        tcpSocket.setOutputGate(gate("socketOut"));
        tcpSocket.setCallback(this);
        tcpSocket.bind(local, localPort);
    }

    std::string vId = getParentModule()->getFullName();
    const auto *arr = dynamic_cast<const omnetpp::cValueArray *>(par("tracks").objectValue());
    if (arr != nullptr) {
        for (int i = 0; i < arr->size(); i++) {
            const auto *m = dynamic_cast<const omnetpp::cValueMap *>(arr->get(i).objectValue());
            if (m == nullptr) continue;
            TopicConfig t;
            t.topic = vId + "/" + (*m)["trackName"].stringValue();
            t.payloadSize = (*m)["packetSize"].intValueInUnit("B");
            t.sendInterval = (*m)["sendInterval"].doubleValueInUnit("s");
            t.messagesPerBurst = m->containsKey("objectsPerGroup") ? (*m)["objectsPerGroup"].intValue() : 1;
            // MQTT expiry is a whole number of seconds (v5.0 section 3.3.2.3.3), so a sub-second
            // deadline simply cannot be expressed; the config gives the value in seconds.
            t.expiryIntervalSec = m->containsKey("messageExpiryInterval")
                                  ? (uint32_t) (*m)["messageExpiryInterval"].intValue() : 0;
            t.timer = new omnetpp::cMessage(t.topic.c_str(), (short) i);
            topics[i] = t;
        }
    }

    timerConnect = new omnetpp::cMessage("MQTT connect", TIMER_CONNECT);
    scheduleAt(omnetpp::simTime() + par("connectTime").doubleValue(), timerConnect);
}

void MqttPublisherApp::handleMessageWhenUp(omnetpp::cMessage *msg) {
    if (msg->isSelfMessage()) {
        if (msg->getKind() == TIMER_CONNECT) {
            if (proto == PROTO_QUIC) quicSocket.connect(connectAddress, connectPort);
            else tcpSocket.connect(connectAddress, connectPort);
            return;
        }
        if (msg->getKind() == TIMER_FLUSH) {
            flushSendQueue();
            if (!sendQueue.empty())
                scheduleAt(omnetpp::simTime() + par("flushInterval").doubleValue(), timerFlush);
            return;
        }
        if (connected) publishBurst(msg->getKind());
        return;
    }
    if (proto == PROTO_QUIC) quicSocket.processMessage(msg);
    else tcpSocket.processMessage(msg);
}

void MqttPublisherApp::sendBytes(const std::vector<uint8_t>& bytes, const char *name) {
    if (proto == PROTO_TCP) {
        auto *packet = new inet::Packet(name);
        packet->insertAtBack(inet::makeShared<inet::BytesChunk>(bytes));
        tcpSocket.send(packet);   // TCP's own send buffer holds what the network cannot take yet
        return;
    }

    // QUIC rejects (and silently discards) writes once its send queue is full, so hold them here
    // instead. Bounded, because a real client's queue is: on overflow the oldest is dropped.
    sendQueue.push_back(bytes);
    if (sendQueueLimitMsgs > 0 && sendQueue.size() > sendQueueLimitMsgs) {
        sendQueue.pop_front();
        droppedOnOverflow++;
    }
    flushSendQueue();

    if (!sendQueue.empty() && timerFlush != nullptr && !timerFlush->isScheduled())
        scheduleAt(omnetpp::simTime() + par("flushInterval").doubleValue(), timerFlush);
}

void MqttPublisherApp::socketEstablished(inet::QuicSocket *) {
    sendBytes(MqttFraming::encodeConnect(getParentModule()->getFullName()), "CONNECT");
}

void MqttPublisherApp::socketEstablished(inet::TcpSocket *) {
    sendBytes(MqttFraming::encodeConnect(getParentModule()->getFullName()), "CONNECT");
}

void MqttPublisherApp::onConnack() {
    connected = true;
    for (auto& t : topics) scheduleAt(omnetpp::simTime(), t.second.timer);
}

// One burst = one sensor frame, matching the MoQ publisher's group-per-interval model, so both
// protocols offer the same load with the same shape.
void MqttPublisherApp::publishBurst(long topicIndex) {
    auto it = topics.find(topicIndex);
    if (it == topics.end()) return;
    TopicConfig& t = it->second;
    TopicStat& st = stats[t.topic];

    for (long i = 0; i < t.messagesPerBurst; i++) {
        MqttMessage m;
        m.topic = t.topic;
        m.qos = qos;
        m.packetId = qos > 0 ? nextPacketId++ : 0;
        if (nextPacketId == 0) nextPacketId = 1; // packet id 0 is not valid
        m.messageExpiryInterval = t.expiryIntervalSec;
        m.sequence = t.nextSequence++;
        m.payloadLength = t.payloadSize;
        m.creationTime = omnetpp::simTime();

        sendBytes(MqttFraming::encodePublish(m), "PUBLISH");

        st.offered++;
        st.sent++;   // MQTT has no sender-side drop: everything offered is handed to the transport
        st.bytesOffered += t.payloadSize;
    }
    if (st.firstSend < SIMTIME_ZERO) st.firstSend = omnetpp::simTime();
    st.lastSend = omnetpp::simTime();

    scheduleAt(omnetpp::simTime() + t.sendInterval, t.timer);
}

void MqttPublisherApp::handleIncoming(const std::vector<uint8_t>& bytes) {
    recvBuf.insert(recvBuf.end(), bytes.begin(), bytes.end());

    MqttFraming::MqttPacket pkt;
    size_t consumed = 0;
    while (MqttFraming::tryParse(recvBuf, pkt, consumed)) {
        if (pkt.type == MQTT_CONNACK && !connected) onConnack();
        else if (pkt.type == MQTT_PUBACK) pubacksReceived++;
        recvBuf.erase(recvBuf.begin(), recvBuf.begin() + consumed);
    }
}

void MqttPublisherApp::socketDataAvailable(inet::QuicSocket *socket, inet::QuicDataInfo *info) {
    socket->recv((int64_t) info->getAvaliableDataSize(), info->getStreamID());
}

void MqttPublisherApp::socketDataArrived(inet::QuicSocket *, inet::Packet *packet) {
    handleIncoming(packet->peekDataAsBytes()->getBytes());
    delete packet;
}

void MqttPublisherApp::socketDataArrived(inet::TcpSocket *, inet::Packet *packet, bool) {
    handleIncoming(packet->peekDataAsBytes()->getBytes());
    delete packet;
}

void MqttPublisherApp::handleStopOperation(inet::LifecycleOperation *) { cancelEvent(timerConnect); }
void MqttPublisherApp::handleCrashOperation(inet::LifecycleOperation *) { cancelEvent(timerConnect); }

void MqttPublisherApp::finish() {
    recordScalar("pubacksReceived", pubacksReceived);
    recordScalar("quicSendRejected", rejected);        // must be 0: nonzero means silent data loss
    recordScalar("droppedOnQueueOverflow", droppedOnOverflow);
    recordScalar("sendQueueAtEnd", (long) sendQueue.size());
    for (auto& e : stats) {
        std::string prefix = "track[" + e.first + "].";
        const TopicStat& st = e.second;
        recordScalar((prefix + "objectsOffered").c_str(), st.offered);
        recordScalar((prefix + "objectsSent").c_str(), st.sent);
        recordScalar((prefix + "bytesOffered").c_str(), st.bytesOffered, "B");
        double span = (st.lastSend - st.firstSend).dbl();
        if (span > 0)
            recordScalar((prefix + "offeredRate").c_str(), st.bytesOffered * 8.0 / span, "bps");
    }
}

} // namespace moqveinssim
