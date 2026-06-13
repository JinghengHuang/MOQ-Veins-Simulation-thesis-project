/* --- MoqRelayApp.h --- */

/* ------------------------------------------
Author: Jingheng Huang
Date: 5/15/2026
------------------------------------------ */

#ifndef MOQRELAYAPP_H
#define MOQRELAYAPP_H

#include <map>
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
    // Number of objects currently being reassembled from upstream (publisher) streams,
    // i.e. the relay's store-and-forward backlog. Emitted on every change.
    omnetpp::simsignal_t relayQueueDepthSignal = -1;
    // Per forwarded object: wall-clock time the object spent in the relay,
    // from arrival of its first slice to the moment it is forwarded downstream.
    omnetpp::simsignal_t relayForwardDelaySignal = -1;
    // Emitted once per object forwarded to (each) subscriber, carrying payload bytes.
    omnetpp::simsignal_t objectForwardedSignal = -1;
    long objectsForwardedTotal = 0;
    double fwdDelaySum = 0; double fwdDelayMax = 0; long fwdDelayCount = 0; // store-and-forward delay

    // Store-and-forward reassembly of an upstream object. An object is keyed by
    // (publisher socketId, streamId, objectId); it is forwarded once fully received.
    struct RelayObjReasm {
        long totalBytes = 0;       // 64 (header) + payloadLength
        long receivedBytes = 0;
        omnetpp::simtime_t firstSliceTime = 0;
        // metadata snapshot taken from the first slice
        long trackId = -1;
        std::string trackAlias;
        long groupId = 0;
        long objectId = 0;
        long priority = 0;
        long payloadLength = 0;
        omnetpp::simtime_t creationTime = 0;
        std::string trackNamespace;
        std::string trackName;
    };
    std::map<std::tuple<int, long, long>, RelayObjReasm> reasm; // (socketId, streamId, objectId)

    // The QuicStreamReq tag is a send-side request and is NOT present on received packets,
    // so the receive stream must be taken from QuicDataInfo at data-available time. recv()
    // and socketDataArrived() are 1:1 and FIFO per connection, so we queue the stream id of
    // each recv and dequeue it when the corresponding data is delivered.
    std::map<int, std::deque<long>> pendingRecvStreams; // socketId -> queued recv stream ids

    // A completed object queued for forwarding to one subscriber on one track stream.
    struct ForwardItem {
        std::string subscriberId;
        long streamId = 0;
        omnetpp::simtime_t firstSliceTime = 0; // when the relay first received the object
        long trackId = -1;
        std::string trackAlias;
        long groupId = 0;
        long objectId = 0;
        long priority = 0;
        long payloadLength = 0;
        omnetpp::simtime_t creationTime = 0;
    };
    // Per (subscriberId, trackAlias) forwarding queue and "object in flight" flag.
    // Stop-and-wait: at most one object is in flight per stream; the next is sent only
    // after the subscriber ACKs the previous one, guaranteeing the stream send buffer is
    // empty so two objects never merge into a SequenceChunk.
    std::map<std::pair<std::string, std::string>, std::deque<ForwardItem>> streamFifo;
    std::map<std::pair<std::string, std::string>, bool> streamBusy;
    long pendingForwardCount = 0;          // objects queued or in flight (relay queue depth)
    long relayDroppedTotal = 0;            // objects dropped due to forwarding-queue overflow
    static const size_t maxFifoPerStream = 1024; // finite relay buffer per stream

    void forwardCompletedObject(const RelayObjReasm& obj);
    void tryFlushStream(const std::string& subscriberId, const std::string& trackAlias);
    void handleObjectAck(const std::string& subscriberId, const std::string& trackAlias);
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
