/* --- MqttBrokerApp.cc --- */
#include "MqttBrokerApp.h"

#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"

namespace moqveinssim {
Define_Module(MqttBrokerApp);

void MqttBrokerApp::handleStartOperation(inet::LifecycleOperation *) {
    proto = par("protocol").stdstringValue() == "tcp" ? PROTO_TCP : PROTO_QUIC;
    qos = (uint8_t) par("qos").intValue();

    inet::L3Address local = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");

    if (proto == PROTO_QUIC) {
        quicSocket.setOutputGate(gate("socketOut"));
        quicSocket.setCallback(this);
        quicSocket.bind(local, localPort);
        quicSocket.listen();
    }
    else {
        tcpSocket.setOutputGate(gate("socketOut"));
        tcpSocket.setCallback(this);
        tcpSocket.bind(local, localPort);
        tcpSocket.listen();
    }
}

void MqttBrokerApp::handleMessageWhenUp(omnetpp::cMessage *msg) {
    if (msg->isSelfMessage()) { delete msg; return; }

    if (proto == PROTO_QUIC) {
        inet::ISocket *s = socketMap.findSocketFor(msg);
        if (s) s->processMessage(msg);
        else quicSocket.processMessage(msg);
    }
    else {
        inet::ISocket *s = socketMap.findSocketFor(msg);
        if (s) s->processMessage(msg);
        else tcpSocket.processMessage(msg);
    }
}

void MqttBrokerApp::socketConnectionAvailable(inet::QuicSocket *listenSocket) {
    inet::QuicSocket *newSocket = listenSocket->accept();
    newSocket->setCallback(this);
    socketMap.addSocket(newSocket);
    sockets[newSocket->getSocketId()] = newSocket;
}

void MqttBrokerApp::socketAvailable(inet::TcpSocket *listenSocket, inet::TcpAvailableInfo *info) {
    auto *newSocket = new inet::TcpSocket(info);
    newSocket->setOutputGate(gate("socketOut"));
    newSocket->setCallback(this);
    socketMap.addSocket(newSocket);
    sockets[newSocket->getSocketId()] = newSocket;
    listenSocket->accept(info->getNewSocketId());
}

void MqttBrokerApp::sendTo(inet::ISocket *socket, const std::vector<uint8_t>& bytes, const char *name) {
    auto *packet = new inet::Packet(name);
    packet->insertAtBack(inet::makeShared<inet::BytesChunk>(bytes));
    if (proto == PROTO_QUIC)
        static_cast<inet::QuicSocket *>(socket)->send(packet, CONTROL_STREAM);
    else
        static_cast<inet::TcpSocket *>(socket)->send(packet);
}

void MqttBrokerApp::socketDataAvailable(inet::QuicSocket *socket, inet::QuicDataInfo *info) {
    socket->recv((int64_t) info->getAvaliableDataSize(), info->getStreamID());
}

void MqttBrokerApp::socketDataArrived(inet::QuicSocket *socket, inet::Packet *packet) {
    handleIncoming(socket, socket->getSocketId(), packet->peekDataAsBytes()->getBytes());
    delete packet;
}

void MqttBrokerApp::socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool) {
    handleIncoming(socket, socket->getSocketId(), packet->peekDataAsBytes()->getBytes());
    delete packet;
}

void MqttBrokerApp::handleIncoming(inet::ISocket *socket, int socketId,
                                   const std::vector<uint8_t>& bytes) {
    auto& buf = recvBufs[socketId];
    buf.insert(buf.end(), bytes.begin(), bytes.end());

    MqttFraming::MqttPacket pkt;
    size_t consumed = 0;
    while (MqttFraming::tryParse(buf, pkt, consumed)) {
        switch (pkt.type) {
            case MQTT_CONNECT:
                connectsReceived++;
                sendTo(socket, MqttFraming::encodeConnack(), "CONNACK");
                break;

            case MQTT_SUBSCRIBE: {
                uint16_t packetId = 0;
                std::vector<std::string> topics;
                uint8_t requestedQos = 0;
                subscribesReceived++;
                if (MqttFraming::parseSubscribe(pkt, packetId, topics, requestedQos)) {
                    for (const auto& t : topics) subscribersByTopic[t].insert(socketId);
                    sendTo(socket, MqttFraming::encodeSuback(packetId, topics.size(), requestedQos),
                           "SUBACK");
                }
                break;
            }

            case MQTT_PUBLISH:
                onPublish(socket, pkt);
                break;

            default:
                break; // PUBACK from a subscriber needs no action
        }
        buf.erase(buf.begin(), buf.begin() + consumed);
    }
}

void MqttBrokerApp::onPublish(inet::ISocket *from, const MqttFraming::MqttPacket& pkt) {
    omnetpp::simtime_t receivedAt = omnetpp::simTime();
    publishesReceived++;
    MqttMessage m;
    if (!MqttFraming::parsePublish(pkt, m)) return;

    if (m.qos > 0) sendTo(from, MqttFraming::encodePuback(m.packetId), "PUBACK");

    // Message Expiry Interval (v5.0 3.3.2.3.3, [MQTT-3.3.2-5]): delete the message only if the
    // interval passes before onward delivery starts. The clock is the time the message waits IN
    // THE SERVER, not since it was published: [MQTT-3.3.2-6] requires the onward PUBLISH to carry
    // "the received value minus the time that the Application Message has been waiting in the
    // Server". We forward on receipt, so the wait here is zero and expiry rarely fires -- MQTT's
    // backlog lives in the transport, where the protocol gives the broker no way to reach it.
    if (m.messageExpiryInterval > 0) {
        double waitedInBroker = (omnetpp::simTime() - receivedAt).dbl();
        if (waitedInBroker > (double) m.messageExpiryInterval) {
            expired++;
            expiredByTopic[m.topic]++;
            return;
        }
    }

    auto it = subscribersByTopic.find(m.topic);
    if (it == subscribersByTopic.end()) return;

    for (int subId : it->second) {
        auto sockIt = sockets.find(subId);
        if (sockIt == sockets.end()) continue;

        MqttMessage out = m;
        out.qos = qos;
        out.packetId = qos > 0 ? nextPacketId++ : 0;
        if (nextPacketId == 0) nextPacketId = 1;
        sendTo(sockIt->second, MqttFraming::encodePublish(out), "PUBLISH");
        forwarded++;
    }
}

void MqttBrokerApp::socketClosed(inet::QuicSocket *socket) {
    int id = socket->getSocketId();
    for (auto& e : subscribersByTopic) e.second.erase(id);
    recvBufs.erase(id);
    sockets.erase(id);
    socketMap.removeSocket(socket);
}

void MqttBrokerApp::socketClosed(inet::TcpSocket *socket) {
    int id = socket->getSocketId();
    for (auto& e : subscribersByTopic) e.second.erase(id);
    recvBufs.erase(id);
    sockets.erase(id);
    if (auto *s = socketMap.removeSocket(socket)) delete s;
}

void MqttBrokerApp::finish() {
    recordScalar("connectsReceived", connectsReceived);
    recordScalar("subscribesReceived", subscribesReceived);
    recordScalar("publishesReceived", publishesReceived);
    recordScalar("quicSendRejected", rejected);   // must be 0: nonzero means silent data loss
    recordScalar("objectsForwardedTotal", forwarded);
    recordScalar("objectsExpired", expired);
    for (auto& e : expiredByTopic)
        recordScalar(("track[" + e.first + "].objectsExpired").c_str(), e.second);
}

} // namespace moqveinssim
