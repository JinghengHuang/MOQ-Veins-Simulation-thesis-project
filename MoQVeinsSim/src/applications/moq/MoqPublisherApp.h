/* --- MoqPublisherApp.h --- */

/* ------------------------------------------
Author: Jingheng Huang
Date: 5/15/2026
------------------------------------------ */

#pragma once

#include <vector>
#include <deque>
#include <map>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
#include "models/TrackInfo.h"
#include "models/MoqFraming.h"


namespace moqveinssim {
class MoqPublisherApp : public inet::ApplicationBase,
                        public inet::QuicSocket::ICallback,
                        public inet::TcpSocket::ICallback,
                        public inet::UdpSocket::ICallback {
    
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
        std::unordered_map<int, int> trackToStreamMap; // trackId -> per-track DATA stream id
        // Stream 0 is the control stream; data streams are client-bidi 4,8,...
        static const long CONTROL_STREAM = 0;
        int nextStreamId = 4; // next DATA stream id to assign (client bidi: 4,8,...)
        inet::L3Address connectAddress;
        unsigned int connectPort;
        bool sendingAllowed = false;

        // ---- transport selection (additive; QUIC path unchanged) ----
        MoqProtocol proto = PROTO_QUIC;
        int udpFragmentSize = 1200;       // max inner-frame bytes per UDP datagram
        inet::TcpSocket tcpSocket;        // used when proto == PROTO_TCP
        inet::UdpSocket udpSocket;        // used when proto == PROTO_UDP
        std::vector<uint8_t> tcpRecvBuf;  // TCP: single ordered byte stream, enveloped frames
        // UDP control reassembly, keyed by (trackAlias, objectId); control is usually 1 fragment.
        std::map<std::pair<std::string, long>, MoqFraming::UdpObjectReassembler> udpReasm;

        // Control is received as length-prefixed byte frames on the control stream.
        std::deque<long> pendingRecvStreams; // recv stream ids (tag does not survive)
        StreamReassembler controlBuf;        // byte buffer for the control stream

        // Protocol-agnostic senders / handlers (branch on proto internally).
        void sendControlFrame(const MoqControlFrame& c);
        void sendObjectFrame(const MoqObjectFrame& f, long tid);
        void handleControlFrame(const MoqControlFrame& c);

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

        // ---- TcpSocket::ICallback (used when proto == PROTO_TCP) ----
        virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
        virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *info) override { } // client: never listens
        virtual void socketEstablished(inet::TcpSocket *socket) override;
        virtual void socketPeerClosed(inet::TcpSocket *socket) override { }
        virtual void socketClosed(inet::TcpSocket *socket) override { }
        virtual void socketFailure(inet::TcpSocket *socket, int code) override { }
        virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override { }
        virtual void socketDeleted(inet::TcpSocket *socket) override { }

        // ---- UdpSocket::ICallback (used when proto == PROTO_UDP) ----
        virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
        virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override { delete indication; }
        virtual void socketClosed(inet::UdpSocket *socket) override { }

        virtual void sendTrackAnnouncementData();
        virtual void sendTrackData(long tid);
        virtual void finish() override;
        void handleTimeout(omnetpp::cMessage *msg);
};
}