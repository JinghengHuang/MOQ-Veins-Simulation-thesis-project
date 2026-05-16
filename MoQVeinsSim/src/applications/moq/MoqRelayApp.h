/* --- MoqRelayApp.h --- */

/* ------------------------------------------
Author: Jingheng Huang
Date: 5/15/2026
------------------------------------------ */

#ifndef MOQRELAYAPP_H
#define MOQRELAYAPP_H

#include <vector>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
#include "models/TrackInfo.h"

namespace moqveinssim {
class MoqRelayApp : public inet::ApplicationBase, public inet::QuicSocket::ICallback {
public:
    MoqRelayApp();
    ~MoqRelayApp();

private:
    omnetpp::cMessage *errorEvent = nullptr;


    enum Timer {
        TIMER_CONNECT = -1,
        TIMER_RESET = -2,
        TIMER_LIMIT_RUNTIME = -3
    };
    enum Event {
        PUB_ANNOUNCE,
        SUB_SUCCESS,
        SUB_ERROR
    };
    inet::cMessage *timerConnect;
    inet::cMessage *timerLimitRuntime;

    std::unordered_map<TrackKey, TrackMeta, TrackKeyHash> publishedTracks;
    std::unordered_map<TrackKey, std::vector<std::string>, TrackKeyHash> subscriberByTrack;
    std::unordered_map<StreamBinding, TrackKey, StreamBindingHash> socketStreamToTrack;
    std::unordered_map<SubscriberTrackKey, StreamBinding, SubscriberTrackKeyHash> subscriberToStream;
    inet::L3Address connectAddress;
    unsigned int connectPort;
    bool sendingAllowed = false;
protected:
    inet::QuicSocket socket;
    virtual void handleMessageWhenUp(inet::cMessage *msg) override;

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;
    virtual void socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) override;
    virtual void socketConnectionAvailable(inet::QuicSocket *socket) override { };
    virtual void socketDataAvailable(inet::QuicSocket* socket, inet::QuicDataInfo *dataInfo) override { };
    virtual void socketEstablished(inet::QuicSocket *socket) override;
    virtual void socketClosed(inet::QuicSocket *socket) override;
    virtual void socketDestroyed(inet::QuicSocket *socket) override { };

    virtual void socketSendQueueFull(inet::QuicSocket *socket) override;
    virtual void socketSendQueueDrain(inet::QuicSocket *socket) override;
    virtual void socketMsgRejected(inet::QuicSocket *socket) override { };
    virtual void onSubscribe(std::string sid, std::string trackAlias, long streamId);
    virtual void onPublish(std::string pid, TrackMeta);
    virtual void relayTrackData(std::string trackAlias, std::string sid);
    void handleTimeout(omnetpp::cMessage *msg);
};
}
#endif // MOQRELAYAPP_H
