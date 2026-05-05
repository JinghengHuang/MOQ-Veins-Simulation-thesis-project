#pragma once

#include <vector>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"


namespace moqveinssim {
class MoqPublisherApp : public inet::ApplicationBase, public inet::QuicSocket::ICallback {
    
    public:
        MoqPublisherApp();
        ~MoqPublisherApp();
    private:
        struct TrackMeta{
            int trackId;
            std::string trackName;
            int packetSize;
            omnetpp::simtime_t sendInterval;
            int priority;
            long nextObjectId = 0;
            omnetpp::cMessage *timer = nullptr;
        };
        enum Timer {
            TIMER_CONNECT = -1,
            TIMER_RESET = -2,
            TIMER_LIMIT_RUNTIME = -3
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
        virtual void sendTrackData();
    private:
    
        void handleTimeout(omnetpp::cMessage *msg);
};
}