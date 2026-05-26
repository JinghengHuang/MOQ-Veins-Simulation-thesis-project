/* --- MoqPublisherApp.h --- */

/* ------------------------------------------
Author: Jingheng Huang
Date: 5/15/2026
------------------------------------------ */

#pragma once

#include <vector>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
#include "models/TrackInfo.h"


namespace moqveinssim {
class MoqSubscriberApp : public inet::ApplicationBase, public inet::QuicSocket::ICallback {
    
    public:
        MoqSubscriberApp();
        ~MoqSubscriberApp();
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
        std::unordered_map<int, TrackMeta> tracks;
        std::unordered_map<int, int> trackToStreamMap;
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
        virtual void sendTrackAnnouncementData();
        virtual void sendTrackData(long tid);
        void handleTimeout(omnetpp::cMessage *msg);
};
}