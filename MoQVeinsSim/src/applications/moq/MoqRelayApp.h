/* --- MoqRelayApp.h --- */

/* ------------------------------------------
Author: Jingheng Huang
Date: 5/15/2026
------------------------------------------ */

#ifndef MOQRELAYAPP_H
#define MOQRELAYAPP_H

#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include <deque>
#include <string>
#include <tuple>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/transportlayer/quic/Quic.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"
#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/common/socket/SocketMap.h"
#include "models/TrackInfo.h"
#include "models/MoqFraming.h"

namespace moqveinssim {
class MoqRelayApp : public inet::ApplicationBase,
                    public inet::QuicSocket::ICallback,
                    public inet::TcpSocket::ICallback,
                    public inet::UdpSocket::ICallback {
public:
    MoqRelayApp();
    ~MoqRelayApp();

private:
    omnetpp::cMessage *errorEvent = nullptr;


    enum Timer {
        TIMER_CONNECT = -1,
        TIMER_TIMEOUT_CHECK = -4,
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
    inet::cMessage *timerTimeoutCheck;   // periodic delivery-timeout sweep over outstanding objects

    std::unordered_map<TrackKey, TrackMeta, TrackKeyHash> publishedTracks;
    std::unordered_map<TrackKey, std::vector<std::string>, TrackKeyHash> subscriberByTrack;
    std::unordered_map<std::string, int> forward_count;
    std::unordered_map<std::string, inet::QuicSocket *> publisherSockets;
    std::unordered_map<TrackKey, inet::QuicSocket *, TrackKeyHash> publisherSocketsByTrackKey;
    std::unordered_map<std::string, inet::QuicSocket *> subscriberSockets;
    // Maps (socketId, streamId) -> (trackNamespace, trackName) for incoming publisher data streams
    std::map<std::pair<int, long>, std::pair<std::string, std::string>> publisherStreamToTrack;
    // Maps (subscriberId, trackAlias) -> streamId for outgoing data forwarding.
    // A subscriber subscribes to each track on its own stream, so forwarding must use the
    // stream that matches the object's track.
    // Downstream streams follow MoQ's subgroup-per-stream rule (draft-14 section 2.2): one
    // stream per (subscriber, trackAlias, groupId, subgroupId), carrying every object of that
    // subgroup. Keeping the subgroup structure end to end is what lets the relay reset a stale
    // object's stream (section 10.4.3) without corrupting the rest of the track.
    struct DownSubgroup {
        std::string subscriberId, trackAlias;
        long groupId = 0, subgroupId = 0;
        bool operator<(const DownSubgroup& o) const {
            return std::tie(subscriberId, trackAlias, groupId, subgroupId)
                 < std::tie(o.subscriberId, o.trackAlias, o.groupId, o.subgroupId);
        }
    };
    std::map<DownSubgroup, long> downstreamStreams;  // subgroup -> downstream stream id
    // Subgroups whose stream we reset, keyed to WHY: a reset abandons the whole subgroup, and the
    // collateral drops must be charged to the cause. MOQ_ERR_DELIVERY_TIMEOUT is standard MoQ
    // shedding; MOQ_ERR_SEND_BUFFER_OVERFLOW is our finite-buffer artifact. See MoqPublisherApp.
    std::map<DownSubgroup, int> resetDownstream;     // subgroup -> reset error code
    long downstreamResets = 0;                       // RESET_STREAM sent to subscribers

    // Objects already written to a subscriber's QUIC socket but not yet known delivered. The
    // delivery timeout keeps applying to them (draft-14 10.4.3); a stale object still queued in
    // the transport is what a stream reset reclaims. Without this the relay's timeout only sees
    // its own forward buffer, and does nothing once the transport buffer is deep.
    struct OutstandingFwd {
        DownSubgroup subgroup;
        inet::QuicSocket* sock = nullptr;
        long streamId = 0;
        std::string trackAlias;
        omnetpp::simtime_t createdAt = 0;
        omnetpp::simtime_t timeout = 0;
    };
    std::deque<OutstandingFwd> outstanding;
    double timeoutCheckInterval = 0.05;
    void checkOutstandingTimeouts();
    std::map<std::string, long> relayResetAfterSend; // per-track objects reset after being written
    inet::L3Address connectAddress;
    unsigned int connectPort;
    bool sendingAllowed = false;

    // ---- metrics ----
    // Number of objects queued for forwarding because a downstream socket is blocked
    // (relay queue depth). Emitted on every change.
    omnetpp::simsignal_t relayQueueDepthSignal = -1;
    // Per forwarded object: time from arrival of the object's first byte at the relay to
    // the moment it is forwarded downstream (upstream reception + any queueing wait).
    omnetpp::simsignal_t relayForwardDelaySignal = -1;
    // Emitted once per object forwarded to (each) subscriber, carrying payload bytes.
    omnetpp::simsignal_t objectForwardedSignal = -1;
    long objectsForwardedTotal = 0;
    double fwdDelaySum = 0; double fwdDelayMax = 0; long fwdDelayCount = 0;
    // Localization: per-track count of object frames received from the publisher.
    std::unordered_map<std::string, long> recvFromPubByTrack;

    // The delivered packet carries no stream tag, so receives are serialized (one in flight per
    // socket, draining a whole stream each time) and the delivered stream id is
    // recvInFlight[socketId].
    std::map<int, std::set<long>> recvPending; // socketId -> streams with undelivered data
    std::map<int, long> recvInFlight;          // socketId -> stream of the outstanding recv
    static const long CONTROL_STREAM = 0;
    void startNextRecv(inet::QuicSocket* peerSocket);

    // Per-(socketId, streamId) byte buffers: control frames on stream 0, object frames on
    // the others. Everything on the wire is a length-prefixed BytesChunk.
    std::map<std::pair<int, long>, StreamReassembler> recvBuffers;

    // Downstream forwarding: pipelined. If a subscriber socket's QUIC send queue is full
    // (socketSendQueueFull), objects are buffered here and flushed on drain.
    struct FwdItem {
        inet::QuicSocket* sock = nullptr;
        DownSubgroup subgroup;
        long streamId = 0;
        std::vector<uint8_t> bytes;
        long payloadLength = 0;
        long priority = 0;                  // MoQ track priority (lower number = higher priority)
        std::string trackAlias;             // for per-track shed accounting
        omnetpp::simtime_t firstByteTime = 0;
        omnetpp::simtime_t createdAt = 0;   // object creation time (for deadline shedding)
        omnetpp::simtime_t timeout = 0;     // per-track delivery timeout (0 = off)
        // Bytes already handed to QUIC. Same invariant as the publisher: an item with
        // sentOffset > 0 has bytes on the stream and must be sent to completion, or the
        // subscriber's length-prefix parser desynchronises permanently.
        size_t sentOffset = 0;
        std::string subscriberId;
    };
    // Downstream send state, one per subscriber socket (each is its own QUIC connection, so each
    // has its own send queue). Objects wait here in priority order rather than being dumped into
    // QUIC, so a high-priority object can overtake a bulk one and stale objects can still be shed.
    struct SockSendState {
        std::map<long, std::deque<FwdItem>> buffer; // priority -> FIFO (oldest at front)
        long count = 0;
        bool blocked = false;   // QUIC send queue full (async signal)
    };
    std::map<int, SockSendState> sockSend;  // socketId -> downstream send state
    long pendingForwardCount = 0;   // total queued objects (relay queue depth)
    long relayDroppedTotal = 0;     // dropped due to forwarding-queue overflow
    long upstreamResets = 0;        // objects abandoned by the publisher via RESET_STREAM
    std::map<std::string, long> relayShedStale; // trackAlias -> dropped past the delivery timeout
    // trackAlias -> lost as collateral of an OVERFLOW-triggered reset. Not MoQ shedding.
    std::map<std::string, long> relayShedOverflow;
    long relayRejected = 0;         // DIAGNOSTIC: sends refused by QUIC (silent loss); must stay 0
    // Objects held for ONE subscriber connection before the overflow eviction kicks in (NED
    // parameter quicForwardBufferPerSubscriberLimit). Counted across all priority queues of that
    // socket, not per stream.
    long quicForwardBufferPerSubscriberLimit = 1024;
    int nextDataStreamId = 1;       // relay->subscriber data stream ids (1,5,9,...)
    long quicSendQueueLimit = 64L * 1024 * 1024;
    long quicChunkBytes = 16L * 1024;
    // Bytes written to each subscriber socket during the current event. flushSocket runs once per
    // forwarded object, and a group burst forwards several in one event, so this must persist
    // across those calls or each one re-overshoots QUIC's limit (see MoqPublisherApp).
    omnetpp::simtime_t quicWriteEvent = -1;
    std::map<int, long> quicBytesThisEvent;   // socketId -> bytes written this event
    // QUIC's true send-queue occupancy per subscriber socket, read from the transport before every
    // write. See MoqPublisherApp: an app-side estimate cannot work, because bytes leave the queue on
    // ACK, which the app never observes.
    inet::quic::Quic* quicModule = nullptr;
    long quicSendQueueLength(int socketId);

    void handleControlFrame(inet::QuicSocket* peerSocket, const MoqControlFrame& c);
    void onObjectFrame(const MoqObjectFrame& f, const std::vector<uint8_t>& frameBytes,
                       omnetpp::simtime_t firstByteTime);
    void forwardToSubscriber(const std::string& subscriberId, const MoqObjectFrame& f,
                             const std::vector<uint8_t>& frameBytes, omnetpp::simtime_t firstByteTime);
    void doForwardSendChunk(FwdItem& item);
    void resetDownstreamStream(const FwdItem& item, int errorCode);
    void flushSocket(int socketId);

    // ---- transport selection (additive; QUIC path unchanged) ----
    MoqProtocol proto = PROTO_QUIC;
    int udpFragmentSize = 1200;

    // TCP server state (proto == PROTO_TCP). publishedTracks / subscriberByTrack / forward_count
    // are shared with the QUIC path; only the socket bookkeeping is transport-specific.
    inet::TcpSocket tcpSocket;                 // listening socket
    inet::SocketMap tcpSocketMap;              // accepted connections
    std::map<int, std::vector<uint8_t>> tcpRecvBuf;          // per-connection byte buffer
    std::map<int, omnetpp::simtime_t> tcpFrameStart;         // per-connection first-byte time
    std::unordered_map<std::string, inet::TcpSocket*> publisherTcpSockets;
    std::unordered_map<TrackKey, inet::TcpSocket*, TrackKeyHash> publisherTcpSocketsByTrackKey;
    std::unordered_map<std::string, inet::TcpSocket*> subscriberTcpSockets;

    // UDP server state (proto == PROTO_UDP). Peers are identified by address instead of socket.
    inet::UdpSocket udpSocket;
    std::unordered_map<TrackKey, std::pair<inet::L3Address, int>, TrackKeyHash> publisherUdpAddrByTrackKey;
    std::unordered_map<std::string, std::pair<inet::L3Address, int>> subscriberUdpAddrs;
    // Object/control reassembly keyed by (srcKey, trackAlias, objectId).
    std::map<std::tuple<std::string, std::string, long>, MoqFraming::UdpObjectReassembler> udpReasm;

    void handleControlFrameTcp(inet::TcpSocket* peerSocket, const MoqControlFrame& c);
    void handleControlFrameUdp(const MoqControlFrame& c, inet::L3Address srcAddr, int srcPort);
    void recordForward(const std::string& subscriberId, long payloadLength,
                       omnetpp::simtime_t firstByteTime);
protected:
    inet::QuicSocket socket;
    inet::SocketMap socketMap;

    virtual void handleMessageWhenUp(inet::cMessage *msg) override;

    TrackKey getTrackKey(std::string trackAlias);

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;
    virtual void socketDataArrived(inet::QuicSocket* peerSocket, inet::Packet *packet) override;
    virtual void socketConnectionAvailable(inet::QuicSocket *socket) override;
    virtual void socketDataAvailable(inet::QuicSocket* socket, inet::QuicDataInfo *dataInfo) override;
    virtual void socketEstablished(inet::QuicSocket *socket) override;
    virtual void socketClosed(inet::QuicSocket *socket) override;
    virtual void socketDestroyed(inet::QuicSocket *socket) override { };

    virtual void socketSendQueueFull(inet::QuicSocket *socket) override;
    virtual void socketSendQueueDrain(inet::QuicSocket *socket) override;
    // QUIC refused the write and dropped it. Must never happen now that the relay paces its writes
    // against QUIC's true queue occupancy; a nonzero count means data is being lost silently.
    virtual void socketMsgRejected(inet::QuicSocket *socket) override { relayRejected++; };
    // The publisher reset an object's stream (its delivery timeout expired upstream): the object
    // can never complete, so drop whatever partial bytes we buffered for it.
    virtual void socketStreamReset(inet::QuicSocket *socket, uint64_t streamId,
                                   uint64_t applicationErrorCode) override;

    // ---- TcpSocket::ICallback (used when proto == PROTO_TCP) ----
    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
    virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *info) override;
    virtual void socketEstablished(inet::TcpSocket *socket) override { sendingAllowed = true; }
    virtual void socketPeerClosed(inet::TcpSocket *socket) override { socket->close(); }
    virtual void socketClosed(inet::TcpSocket *socket) override;
    virtual void socketFailure(inet::TcpSocket *socket, int code) override { }
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override { }
    virtual void socketDeleted(inet::TcpSocket *socket) override { }

    // ---- UdpSocket::ICallback (used when proto == PROTO_UDP) ----
    virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override { delete indication; }
    virtual void socketClosed(inet::UdpSocket *socket) override { }

    virtual void onSubscribe(std::string sid, std::string trackAlias, long streamId);
    virtual void onPublish(std::string pid, TrackMeta);
    virtual void relayTrackData(std::string trackAlias, std::string sid);
    virtual void finish() override;
    void handleTimeout(omnetpp::cMessage *msg);
};
}
#endif // MOQRELAYAPP_H
