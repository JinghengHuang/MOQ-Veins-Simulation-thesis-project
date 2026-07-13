/* --- MqttPublisherApp.h --- */
#pragma once

#include <map>
#include <string>
#include <vector>
#include <omnetpp.h>
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"
#include "models/MqttFraming.h"
#include <deque>
#include "models/MoqFraming.h"   // MoqProtocol (transport selection)

namespace moqveinssim {

/**
 * MQTT v5.0 publisher. Publishes one topic per configured track, mirroring MoqPublisherApp's
 * workload so the two are directly comparable.
 *
 * MQTT has no per-message priority and no way to abandon a message already being delivered, so
 * unlike the MoQ publisher there is no priority-ordered send buffer and no stream reset: messages
 * are handed to the transport in the order they are produced. That is the protocol's behaviour,
 * not an omission.
 */
class MqttPublisherApp : public inet::ApplicationBase,
                         public inet::QuicSocket::ICallback,
                         public inet::TcpSocket::ICallback {
public:
    MqttPublisherApp() = default;
    ~MqttPublisherApp();

private:
    enum Timer { TIMER_CONNECT = -1, TIMER_FLUSH = -2 };

    struct TopicConfig {
        std::string topic;
        long payloadSize = 0;
        omnetpp::simtime_t sendInterval = 0;
        long messagesPerBurst = 1;          // one sensor frame = this many messages
        uint32_t expiryIntervalSec = 0;     // MQTT Message Expiry Interval (seconds; 0 = absent)
        long nextSequence = 0;
        omnetpp::cMessage *timer = nullptr;
    };

    struct TopicStat {
        long offered = 0;
        long sent = 0;
        long bytesOffered = 0;
        omnetpp::simtime_t firstSend = -1;
        omnetpp::simtime_t lastSend = -1;
    };

    inet::cMessage *timerConnect = nullptr;
    std::map<long, TopicConfig> topics;      // keyed by topic index
    std::map<std::string, TopicStat> stats;  // keyed by topic

    MoqProtocol proto = PROTO_QUIC;
    inet::QuicSocket quicSocket;
    inet::TcpSocket tcpSocket;

    inet::L3Address connectAddress;
    int connectPort = 0;
    bool connected = false;
    uint8_t qos = 0;
    uint16_t nextPacketId = 1;
    long pubacksReceived = 0;
    long rejected = 0;

    std::vector<uint8_t> recvBuf;

    // Outbound queue. A real MQTT client holds messages it cannot yet write to the transport;
    // without this, INET QUIC silently discards writes once its send queue is full and MQTT would
    // look good only by throwing load away. It is a plain FIFO: MQTT has no per-message priority
    // and no way to drop a stale message, which is exactly the point of the comparison.
    std::deque<std::vector<uint8_t>> sendQueue;
    inet::cMessage *timerFlush = nullptr;
    long quicSendQueueLimit = 0;
    long droppedOnOverflow = 0;
    // QUIC stops accepting at sendQueueLimit and only resumes at the low-water mark, telling us
    // asynchronously. Between the two our own queue-length check still sees room, so without this
    // flag those writes are rejected and silently discarded.
    bool quicBlocked = false;
    size_t sendQueueLimitMsgs = 0;

    void flushSendQueue();

    static const long CONTROL_STREAM = 0;    // MQTT over QUIC: one ordered stream, as over TCP

    void sendBytes(const std::vector<uint8_t>& bytes, const char *name);
    void publishBurst(long topicIndex);
    void onConnack();
    void handleIncoming(const std::vector<uint8_t>& bytes);

protected:
    virtual void handleMessageWhenUp(inet::cMessage *msg) override;
    virtual void handleStartOperation(inet::LifecycleOperation *op) override;
    virtual void handleStopOperation(inet::LifecycleOperation *op) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *op) override;
    virtual void finish() override;

    // QUIC
    virtual void socketEstablished(inet::QuicSocket *socket) override;
    virtual void socketDataArrived(inet::QuicSocket *socket, inet::Packet *packet) override;
    virtual void socketDataAvailable(inet::QuicSocket *socket, inet::QuicDataInfo *info) override;
    virtual void socketConnectionAvailable(inet::QuicSocket *socket) override {}
    virtual void socketClosed(inet::QuicSocket *socket) override {}
    virtual void socketDestroyed(inet::QuicSocket *socket) override {}
    virtual void socketSendQueueFull(inet::QuicSocket *socket) override { quicBlocked = true; }
    virtual void socketSendQueueDrain(inet::QuicSocket *socket) override { quicBlocked = false; flushSendQueue(); }
    virtual void socketMsgRejected(inet::QuicSocket *socket) override { rejected++; }

    // TCP
    virtual void socketEstablished(inet::TcpSocket *socket) override;
    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
    virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *info) override {}
    virtual void socketPeerClosed(inet::TcpSocket *socket) override {}
    virtual void socketClosed(inet::TcpSocket *socket) override {}
    virtual void socketFailure(inet::TcpSocket *socket, int code) override {}
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override {}
    virtual void socketDeleted(inet::TcpSocket *socket) override {}
};

} // namespace moqveinssim
