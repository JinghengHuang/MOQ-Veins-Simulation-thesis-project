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
class MoqPublisherApp : public inet::ApplicationBase, public inet::QuicSocket::ICallback {
    
    public:
        MoqPublisherApp();
        ~MoqPublisherApp();
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
        int nextStreamId = 0; // next QUIC stream id to assign (client bidi: 0,4,8,...)
        inet::L3Address connectAddress;
        unsigned int connectPort;
        bool sendingAllowed = false;

        // ---- metrics ----
        // Emitted once per data object sent, carrying the object payload size in bytes.
        // Aggregated by @statistic into a count (objects offered) and sum (bytes offered).
        omnetpp::simsignal_t objectSentSignal = -1;
        struct PubTrackStat {
            long objectsSent = 0;
            long bytesSent = 0;
            omnetpp::simtime_t firstSendTime = -1;
            omnetpp::simtime_t lastSendTime = -1;
        };
        std::unordered_map<long, PubTrackStat> pubStats; // keyed by trackId
    protected:
        inet::QuicSocket socket;
        virtual void handleMessageWhenUp(inet::cMessage *msg) override;

        virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
        virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
        virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;
        virtual void socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) override;
        virtual void socketConnectionAvailable(inet::QuicSocket *socket) override { };
        virtual void socketDataAvailable(inet::QuicSocket* socket, inet::QuicDataInfo *dataInfo) override;

        virtual void socketEstablished(inet::QuicSocket *socket) override;
        virtual void socketClosed(inet::QuicSocket *socket) override;
        virtual void socketDestroyed(inet::QuicSocket *socket) override { };

        virtual void socketSendQueueFull(inet::QuicSocket *socket) override;
        virtual void socketSendQueueDrain(inet::QuicSocket *socket) override;
        virtual void socketMsgRejected(inet::QuicSocket *socket) override { };
        virtual void sendTrackAnnouncementData();
        virtual void sendTrackData(long tid);
        virtual void finish() override;
        void handleTimeout(omnetpp::cMessage *msg);
};
}