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
#include "inet/transportlayer/quic/Quic.h"
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

        // ---- QUIC send-side backpressure (MoQ/V2X-faithful priority shedding) ----
        // QUIC rejects sends once its send queue is full; rather than dropping objects
        // silently, hold them in a priority-ordered app buffer and flush on drain. On buffer
        // overflow the lowest-priority, oldest object is evicted (partial reliability: the bulk
        // low-priority track is shed first, protecting the small high-priority track).
        struct Pending {
            long tid = 0;
            long streamId = 0;
            long priority = 0;
            long payloadLength = 0;
            std::vector<uint8_t> bytes;
            omnetpp::simtime_t createdAt = 0;
            omnetpp::simtime_t timeout = 0; // sender-side stale-drop timeout (0 = off)
            // Bytes of this object already handed to QUIC. An object with sentOffset > 0 has bytes
            // on the wire and MUST be sent to completion: the receiver frames objects by a length
            // prefix alone, so a truncated object followed by the next one desyncs its parser.
            size_t sentOffset = 0;
        };
        bool quicBlocked = false;                         // QUIC send queue full (async signal)
        // QUIC's true send-queue occupancy, read straight from the transport before every write.
        // QUIC stops accepting data synchronously but only says so asynchronously, so writing
        // blind gets objects silently rejected. An app-side estimate cannot substitute: bytes
        // leave QUIC's queue on ACK, which the app never sees, so the estimate only ever grows
        // and eventually gates the app shut for good.
        inet::quic::Quic* quicModule = nullptr;
        long quicSendQueueLength();
        long quicQueueMaxObserved = 0; // peak occupancy driven into QUIC; must stay under the limit
        long quicSendQueueLimit = 64L * 1024 * 1024;
        // Largest slice of one object handed to QUIC per write. Kept well below quicSendQueueLimit:
        // QUIC's admission gate is connection-wide and all-or-nothing, so a single write bigger than
        // the limit shuts every stream out (including the high-priority track) until ACKs drain it.
        // Chunking keeps the backlog in the priority-ordered buffer below, where it can be reordered
        // and shed, instead of stranding it in the transport.
        long quicChunkBytes = 16L * 1024;
        std::map<long, std::deque<Pending>> sendBuffer;   // priority -> FIFO (oldest at front)
        long sendBufferCount = 0;
        size_t sendBufferLimit = 2000;
        long quicShed = 0;     // objects evicted from the send buffer (capacity overflow)
        std::unordered_map<long, long> quicShedStale; // per-track objects dropped past delivery timeout
        // Phase-0 diagnostic: how long objects dwell in the app send buffer before reaching QUIC
        // (tells us whether the standing queue is in our buffer or downstream in QUIC/radio).
        double sendDwellSum = 0; double sendDwellMax = 0; long sendDwellCount = 0;
        void enqueuePending(Pending&& p);
        void doSendQuicChunk(Pending& p);
        void flushSendBuffer();

        // Protocol-agnostic senders / handlers (branch on proto internally).
        void sendControlFrame(const MoqControlFrame& c);
        void sendObjectFrame(const MoqObjectFrame& f, long tid);
        void handleControlFrame(const MoqControlFrame& c);

        // ---- metrics ----
        // Emitted once per data object sent, carrying the object payload size in bytes.
        // Aggregated by @statistic into a count (objects offered) and sum (bytes offered).
        omnetpp::simsignal_t objectSentSignal = -1;
        struct PubTrackStat {
            long objectsOffered = 0;  // objects generated by the track timer
            long bytesOffered = 0;
            long objectsSent = 0;     // objects fully handed to QUIC (offered minus shed/evicted)
            long bytesSent = 0;
            omnetpp::simtime_t firstSendTime = -1;
            omnetpp::simtime_t lastSendTime = -1;
        };
        std::unordered_map<long, PubTrackStat> pubStats; // keyed by trackId
        long quicRejected = 0; // DIAGNOSTIC: objects dropped because QUIC send queue was full
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
        virtual void socketMsgRejected(inet::QuicSocket *socket) override { quicRejected++; }; // DIAGNOSTIC

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