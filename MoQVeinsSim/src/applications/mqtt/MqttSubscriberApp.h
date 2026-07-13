/* --- MqttSubscriberApp.h --- */
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
#include "models/MoqFraming.h"       // MoqProtocol
#include "models/SubscriberStats.h"  // shared with the MoQ subscriber, so metrics match exactly

namespace moqveinssim {

/** MQTT v5.0 subscriber, the counterpart to MoqSubscriberApp. */
class MqttSubscriberApp : public inet::ApplicationBase,
                          public inet::QuicSocket::ICallback,
                          public inet::TcpSocket::ICallback {
public:
    MqttSubscriberApp() = default;
    ~MqttSubscriberApp();

private:
    enum Timer { TIMER_CONNECT = -1 };

    inet::cMessage *timerConnect = nullptr;
    MoqProtocol proto = PROTO_QUIC;
    inet::QuicSocket quicSocket;
    inet::TcpSocket tcpSocket;

    inet::L3Address connectAddress;
    int connectPort = 0;
    uint8_t qos = 0;
    uint16_t nextPacketId = 1;
    bool subscribed = false;
    long connacksReceived = 0;
    long publishesReceived = 0;

    std::vector<std::string> topicFilters;
    std::map<std::string, omnetpp::simtime_t> deadlines;  // topic -> deadline
    std::map<std::string, SubTrackStat> stats;            // topic -> receive statistics
    std::vector<uint8_t> recvBuf;

    omnetpp::simsignal_t endToEndLatencySignal = -1;
    omnetpp::simsignal_t e2eJitterSignal = -1;
    omnetpp::simsignal_t deadlineMissSignal = -1;

    static const long CONTROL_STREAM = 0;

    void sendBytes(const std::vector<uint8_t>& bytes, const char *name);
    void onConnack();
    void handleIncoming(const std::vector<uint8_t>& bytes);
    void recordMessage(const MqttMessage& m);

protected:
    virtual void handleMessageWhenUp(inet::cMessage *msg) override;
    virtual void handleStartOperation(inet::LifecycleOperation *op) override;
    virtual void handleStopOperation(inet::LifecycleOperation *op) override { cancelEvent(timerConnect); }
    virtual void handleCrashOperation(inet::LifecycleOperation *op) override { cancelEvent(timerConnect); }
    virtual void finish() override;

    // QUIC
    virtual void socketEstablished(inet::QuicSocket *socket) override;
    virtual void socketDataAvailable(inet::QuicSocket *socket, inet::QuicDataInfo *info) override;
    virtual void socketDataArrived(inet::QuicSocket *socket, inet::Packet *packet) override;
    virtual void socketConnectionAvailable(inet::QuicSocket *socket) override {}
    virtual void socketClosed(inet::QuicSocket *socket) override {}
    virtual void socketDestroyed(inet::QuicSocket *socket) override {}
    virtual void socketSendQueueFull(inet::QuicSocket *socket) override {}
    virtual void socketSendQueueDrain(inet::QuicSocket *socket) override {}
    virtual void socketMsgRejected(inet::QuicSocket *socket) override {}

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
