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
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/common/socket/SocketMap.h"
#include "models/TrackInfo.h"
#include "models/MoqFraming.h"

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
    std::unordered_map<std::string, int> forward_count;
    std::unordered_map<std::string, inet::QuicSocket *> publisherSockets;
    std::unordered_map<TrackKey, inet::QuicSocket *, TrackKeyHash> publisherSocketsByTrackKey;
    std::unordered_map<std::string, inet::QuicSocket *> subscriberSockets;
    // Maps (socketId, streamId) -> (trackNamespace, trackName) for incoming publisher data streams
    std::map<std::pair<int, long>, std::pair<std::string, std::string>> publisherStreamToTrack;
    // Maps (subscriberId, trackAlias) -> streamId for outgoing data forwarding.
    // A subscriber subscribes to each track on its own stream, so forwarding must use the
    // stream that matches the object's track.
    std::map<std::pair<std::string, std::string>, long> subscriberStreamIds;
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

    // The delivered packet carries no stream tag and the QUIC "data available" size is
    // cumulative, so receives are serialized (one in flight per socket, draining a whole
    // stream each time) and the delivered stream id is recvInFlight[socketId].
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
        long streamId = 0;
        std::vector<uint8_t> bytes;
        long payloadLength = 0;
        omnetpp::simtime_t firstByteTime = 0;
        std::string subscriberId;
    };
    // Pipelined forwarding (QUIC governs in-flight via flow + congestion control). If a
    // subscriber socket's QUIC send queue is full (socketSendQueueFull), objects are buffered
    // here and flushed on drain; relayQueueDepth = total buffered.
    std::map<int, std::deque<FwdItem>> socketFifo; // socketId -> backlog when blocked
    std::map<int, bool> socketBlocked;             // socketId -> send queue full
    long pendingForwardCount = 0;   // total queued objects (relay queue depth)
    long relayDroppedTotal = 0;     // dropped due to forwarding-queue overflow
    static const size_t maxFifoPerStream = 1024;
    int nextDataStreamId = 1;       // relay->subscriber data stream ids (1,5,9,...)

    void handleControlFrame(inet::QuicSocket* peerSocket, const MoqControlFrame& c);
    void onObjectFrame(const MoqObjectFrame& f, const std::vector<uint8_t>& frameBytes,
                       omnetpp::simtime_t firstByteTime);
    void forwardToSubscriber(const std::string& subscriberId, const MoqObjectFrame& f,
                             const std::vector<uint8_t>& frameBytes, omnetpp::simtime_t firstByteTime);
    void doForwardSend(const FwdItem& item);
    void flushSocket(int socketId);
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
    virtual void socketMsgRejected(inet::QuicSocket *socket) override { };
    virtual void onSubscribe(std::string sid, std::string trackAlias, long streamId);
    virtual void onPublish(std::string pid, TrackMeta);
    virtual void relayTrackData(std::string trackAlias, std::string sid);
    virtual void finish() override;
    void handleTimeout(omnetpp::cMessage *msg);
};
}
#endif // MOQRELAYAPP_H
