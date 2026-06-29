/* --- MoqPublisherApp.h --- */

/* ------------------------------------------
Author: Jingheng Huang
Date: 5/15/2026
------------------------------------------ */

#pragma once

#include <vector>
#include <map>
#include <set>
#include <string>
#include <utility>
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
class MoqSubscriberApp : public inet::ApplicationBase,
                         public inet::QuicSocket::ICallback,
                         public inet::TcpSocket::ICallback,
                         public inet::UdpSocket::ICallback {
    
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
        static const long CONTROL_STREAM = 0; // SUBSCRIBE sent here; data arrives on others
        inet::L3Address connectAddress;
        int receive_count = 0;
        unsigned int connectPort;
        bool sendingAllowed = false;

        // ---- transport selection (additive; QUIC path unchanged) ----
        MoqProtocol proto = PROTO_QUIC;
        int udpFragmentSize = 1200;
        inet::TcpSocket tcpSocket;        // used when proto == PROTO_TCP
        inet::UdpSocket udpSocket;        // used when proto == PROTO_UDP
        std::vector<uint8_t> tcpRecvBuf;  // TCP: single ordered byte stream, enveloped frames
        omnetpp::simtime_t tcpFrameStart = -1; // arrival of the current object's first byte (TCP)
        // UDP object reassembly, keyed by (trackAlias, objectId).
        std::map<std::pair<std::string, long>, MoqFraming::UdpObjectReassembler> udpReasm;
        void sendControlFrame(const MoqControlFrame& c);

        // Incoming data is a length-prefixed byte stream per QUIC stream; accumulate and frame.
        // recv()s are serialized (one in flight) so the delivered stream id is always known:
        // the QUIC "data available" size is cumulative, so issuing one recv per notification
        // and matching deliveries by FIFO desyncs.
        std::set<long> recvPending;                          // streams with undelivered data
        long recvInFlight = -1;                              // stream of the outstanding recv
        std::map<long, StreamReassembler> streamBuffers;     // per recv stream byte buffer
        void startNextRecv(inet::QuicSocket* socket);

        // ---- metrics ----
        // Per completed object: end-to-end latency (creation->fully received) in seconds.
        omnetpp::simsignal_t endToEndLatencySignal = -1;
        // Per completed object: completion time (first fragment->last fragment) in seconds.
        omnetpp::simsignal_t objectCompletionTimeSignal = -1;
        // Per completed object (from the 2nd onward, per track): |L_i - L_{i-1}| in seconds.
        omnetpp::simsignal_t e2eJitterSignal = -1;
        // Per completed object: 1 if latency exceeded the track deadline, else 0.
        // The @statistic mean of this signal is the deadline miss ratio.
        omnetpp::simsignal_t deadlineMissSignal = -1;

        struct SubTrackStat {
            long received = 0;
            long bytes = 0;
            long highestObjId = -1;
            long lowestObjId = -1;   // DIAGNOSTIC: smallest objectId ever received
            omnetpp::simtime_t firstRecv = -1;
            omnetpp::simtime_t lastRecv = -1;
            double lastLatency = -1;
            long deadlineMisses = 0;
            omnetpp::simtime_t deadline = 0;
            double latencySum = 0;   // sum of end-to-end latencies (for per-track mean)
            double latencyMax = 0;   // worst-case per-track end-to-end latency
            double completionSum = 0; double completionMax = 0; // object completion time
            double jitterSum = 0; long jitterCount = 0;         // |L_i - L_i-1| jitter
        };
        std::unordered_map<std::string, SubTrackStat> subStats; // keyed by trackAlias
        SubTrackStat& trackStat(const std::string& trackAlias);
        void recordObject(const MoqObjectFrame& f, omnetpp::simtime_t frameStartTime, omnetpp::simtime_t now);
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

        virtual void sendTrackSubscribeData();
        virtual void finish() override;
        void handleTimeout(omnetpp::cMessage *msg);
};
}