/* --- MqttBrokerApp.h --- */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include <omnetpp.h>
#include "inet/applications/base/ApplicationBase.h"
#include "inet/common/socket/SocketMap.h"
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"
#include "models/MqttFraming.h"
#include "models/MoqFraming.h"   // MoqProtocol

namespace moqveinssim {

/**
 * MQTT v5.0 broker, the counterpart to MoqRelayApp.
 *
 * Enforces the Message Expiry Interval exactly as the spec defines it (v5.0 section 3.3.2.3.3):
 * a message is deleted only if it expires *before the broker has started onward delivery*. Once
 * delivery has begun there is no way to abandon it -- MQTT has no equivalent of RESET_STREAM.
 * That, plus the absence of any per-message priority, is what distinguishes it from the MoQ relay.
 */
class MqttBrokerApp : public inet::ApplicationBase,
                      public inet::QuicSocket::ICallback,
                      public inet::TcpSocket::ICallback {
public:
    MqttBrokerApp() = default;

private:
    MoqProtocol proto = PROTO_QUIC;
    inet::QuicSocket quicSocket;
    inet::TcpSocket tcpSocket;
    inet::SocketMap socketMap;

    // Per connection: buffered bytes, and the subscriptions taken out on it.
    std::map<int, std::vector<uint8_t>> recvBufs;
    std::map<std::string, std::set<int>> subscribersByTopic;  // topic -> socket ids
    std::map<int, inet::ISocket *> sockets;                   // socket id -> socket

    uint8_t qos = 0;
    uint16_t nextPacketId = 1;

    long connectsReceived = 0;
    long subscribesReceived = 0;
    long publishesReceived = 0;
    long rejected = 0;
    long forwarded = 0;
    long expired = 0;                                  // deleted before onward delivery started
    std::map<std::string, long> expiredByTopic;

    static const long CONTROL_STREAM = 0;

    void sendTo(inet::ISocket *socket, const std::vector<uint8_t>& bytes, const char *name);
    void handleIncoming(inet::ISocket *socket, int socketId, const std::vector<uint8_t>& bytes);
    void onPublish(inet::ISocket *from, const MqttFraming::MqttPacket& pkt);

protected:
    virtual void handleMessageWhenUp(inet::cMessage *msg) override;
    virtual void handleStartOperation(inet::LifecycleOperation *op) override;
    virtual void handleStopOperation(inet::LifecycleOperation *op) override {}
    virtual void handleCrashOperation(inet::LifecycleOperation *op) override {}
    virtual void finish() override;

    // QUIC
    virtual void socketConnectionAvailable(inet::QuicSocket *listenSocket) override;
    virtual void socketDataAvailable(inet::QuicSocket *socket, inet::QuicDataInfo *info) override;
    virtual void socketDataArrived(inet::QuicSocket *socket, inet::Packet *packet) override;
    virtual void socketEstablished(inet::QuicSocket *socket) override {}
    virtual void socketClosed(inet::QuicSocket *socket) override;
    virtual void socketDestroyed(inet::QuicSocket *socket) override {}
    virtual void socketSendQueueFull(inet::QuicSocket *socket) override {}
    virtual void socketSendQueueDrain(inet::QuicSocket *socket) override {}
    virtual void socketMsgRejected(inet::QuicSocket *socket) override { rejected++; }

    // TCP
    virtual void socketAvailable(inet::TcpSocket *listenSocket, inet::TcpAvailableInfo *info) override;
    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
    virtual void socketEstablished(inet::TcpSocket *socket) override {}
    virtual void socketPeerClosed(inet::TcpSocket *socket) override {}
    virtual void socketClosed(inet::TcpSocket *socket) override;
    virtual void socketFailure(inet::TcpSocket *socket, int code) override {}
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override {}
    virtual void socketDeleted(inet::TcpSocket *socket) override {}
};

} // namespace moqveinssim
