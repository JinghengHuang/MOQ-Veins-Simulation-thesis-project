/* --- MqttSubscriberApp.cc --- */
#include "MqttSubscriberApp.h"

#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"

namespace moqveinssim {
Define_Module(MqttSubscriberApp);

MqttSubscriberApp::~MqttSubscriberApp() { cancelAndDelete(timerConnect); }

void MqttSubscriberApp::handleStartOperation(inet::LifecycleOperation *) {
    endToEndLatencySignal = registerSignal("endToEndLatency");
    e2eJitterSignal = registerSignal("e2eJitter");
    deadlineMissSignal = registerSignal("deadlineMiss");

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
    }
    else {
        tcpSocket.setOutputGate(gate("socketOut"));
        tcpSocket.setCallback(this);
        tcpSocket.bind(local, localPort);
    }

    const auto *arr = dynamic_cast<const omnetpp::cValueArray *>(par("tracks").objectValue());
    if (arr != nullptr) {
        for (int i = 0; i < arr->size(); i++) {
            const auto *m = dynamic_cast<const omnetpp::cValueMap *>(arr->get(i).objectValue());
            if (m == nullptr) continue;
            std::string topic = std::string((*m)["trackNamespace"].stringValue()) + "/"
                              + (*m)["trackName"].stringValue();
            topicFilters.push_back(topic);
            omnetpp::simtime_t deadline = m->containsKey("deadline")
                                          ? (*m)["deadline"].doubleValueInUnit("s") : 0.0;
            deadlines[topic] = deadline;
            stats[topic].deadline = deadline;   // seed so the deadline is known before any message
        }
    }

    timerConnect = new omnetpp::cMessage("MQTT connect", TIMER_CONNECT);
    scheduleAt(omnetpp::simTime() + par("connectTime").doubleValue(), timerConnect);
}

void MqttSubscriberApp::handleMessageWhenUp(omnetpp::cMessage *msg) {
    if (msg->isSelfMessage()) {
        if (msg->getKind() == TIMER_CONNECT) {
            if (proto == PROTO_QUIC) quicSocket.connect(connectAddress, connectPort);
            else tcpSocket.connect(connectAddress, connectPort);
        }
        return;
    }
    if (proto == PROTO_QUIC) quicSocket.processMessage(msg);
    else tcpSocket.processMessage(msg);
}

void MqttSubscriberApp::sendBytes(const std::vector<uint8_t>& bytes, const char *name) {
    auto *packet = new inet::Packet(name);
    packet->insertAtBack(inet::makeShared<inet::BytesChunk>(bytes));
    if (proto == PROTO_QUIC) quicSocket.send(packet, CONTROL_STREAM);
    else tcpSocket.send(packet);
}

void MqttSubscriberApp::socketEstablished(inet::QuicSocket *) {
    sendBytes(MqttFraming::encodeConnect(getParentModule()->getFullName()), "CONNECT");
}

void MqttSubscriberApp::socketEstablished(inet::TcpSocket *) {
    sendBytes(MqttFraming::encodeConnect(getParentModule()->getFullName()), "CONNECT");
}

void MqttSubscriberApp::onConnack() {
    if (subscribed || topicFilters.empty()) return;
    subscribed = true;
    sendBytes(MqttFraming::encodeSubscribe(nextPacketId++, topicFilters, qos), "SUBSCRIBE");
}

void MqttSubscriberApp::handleIncoming(const std::vector<uint8_t>& bytes) {
    recvBuf.insert(recvBuf.end(), bytes.begin(), bytes.end());

    MqttFraming::MqttPacket pkt;
    size_t consumed = 0;
    while (MqttFraming::tryParse(recvBuf, pkt, consumed)) {
        if (pkt.type == MQTT_CONNACK) {
            connacksReceived++;
            onConnack();
        }
        else if (pkt.type == MQTT_PUBLISH) {
            publishesReceived++;
            MqttMessage m;
            if (MqttFraming::parsePublish(pkt, m)) {
                if (m.qos > 0) sendBytes(MqttFraming::encodePuback(m.packetId), "PUBACK");
                recordMessage(m);
            }
        }
        recvBuf.erase(recvBuf.begin(), recvBuf.begin() + consumed);
    }
}

void MqttSubscriberApp::recordMessage(const MqttMessage& m) {
    omnetpp::simtime_t now = omnetpp::simTime();
    double latency = (now - m.creationTime).dbl();

    SubTrackStat& ts = stats[m.topic];
    ts.deadline = deadlines.count(m.topic) ? deadlines[m.topic] : SIMTIME_ZERO;
    double jitter = ts.add(m.sequence, m.payloadLength, latency, now);

    emit(endToEndLatencySignal, latency);
    if (jitter >= 0) emit(e2eJitterSignal, jitter);
    emit(deadlineMissSignal, (long) (ts.missedDeadline(latency) ? 1 : 0));
}

void MqttSubscriberApp::socketDataAvailable(inet::QuicSocket *socket, inet::QuicDataInfo *info) {
    socket->recv((int64_t) info->getAvaliableDataSize(), info->getStreamID());
}

void MqttSubscriberApp::socketDataArrived(inet::QuicSocket *, inet::Packet *packet) {
    handleIncoming(packet->peekDataAsBytes()->getBytes());
    delete packet;
}

void MqttSubscriberApp::socketDataArrived(inet::TcpSocket *, inet::Packet *packet, bool) {
    handleIncoming(packet->peekDataAsBytes()->getBytes());
    delete packet;
}

void MqttSubscriberApp::finish() {
    recordScalar("connacksReceived", connacksReceived);
    recordScalar("subscribeSent", subscribed ? 1 : 0);
    recordScalar("publishesReceived", publishesReceived);
    for (auto& e : stats) recordTrackStats(*this, e.first, e.second);
}

} // namespace moqveinssim
