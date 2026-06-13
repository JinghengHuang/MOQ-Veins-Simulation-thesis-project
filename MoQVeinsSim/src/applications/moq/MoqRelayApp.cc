/* --- MoqRelayApp.cpp --- */

/* ------------------------------------------
author: Jingheng Huang
date: 5/15/2026
------------------------------------------ */

#include "MoqRelayApp.h"
#include "utils/StringUtils.h"
#include <regex.h>
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/common/packet/chunk/SliceChunk.h"
#include "models/MoqSubscriber_m.h"
#include "models/MoqPublisherAnnounce_m.h"
#include "models/MoqObjectChunk_m.h"
#include "models/MoqObjectAck_m.h"

namespace moqveinssim
{
    Define_Module(MoqRelayApp);

    MoqRelayApp::MoqRelayApp()
    {
        timerConnect = new inet::cMessage("MOQ Relay Timer - Connect");
        timerConnect->setKind(TIMER_CONNECT);

        timerLimitRuntime = new inet::cMessage("MOQ Relay Timer - Runtime limit");
        timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);
    }

    MoqRelayApp::~MoqRelayApp()
    {
        cancelAndDelete(timerConnect);
        cancelAndDelete(timerLimitRuntime);
        // Clear pointer maps before deleteSockets() frees the objects
        publisherSockets.clear();
        publisherSocketsByTrackKey.clear();
        subscriberSockets.clear();
        socketMap.deleteSockets();
    }

    void MoqRelayApp::handleStartOperation(inet::LifecycleOperation *operation)
    {
        EV_DEBUG << "initialize MoqRelayApp" << std::endl;

        relayQueueDepthSignal = registerSignal("relayQueueDepth");
        relayForwardDelaySignal = registerSignal("relayForwardDelay");
        objectForwardedSignal = registerSignal("objectForwarded");

        socket.setOutputGate(gate("socketOut"));
        socket.setCallback(this);

        inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
        int localPort = par("localPort");
        socket.bind(localAddress, localPort);
        socket.listen();
    }

    void MoqRelayApp::handleStopOperation(inet::LifecycleOperation *operation)
    {
        EV_INFO << "handleStopOperation" << std::endl;
        cancelEvent(timerConnect);
        cancelEvent(timerLimitRuntime);
    }

    void MoqRelayApp::handleCrashOperation(inet::LifecycleOperation *operation)
    {
        EV_ERROR << "MOQ Relay FAILED!" << std::endl;
        cancelEvent(timerConnect);
        cancelEvent(timerLimitRuntime);
    }

    // Accept incoming QUIC connections and register them in the socket map.
    void MoqRelayApp::socketConnectionAvailable(inet::QuicSocket *listenSocket)
    {
        inet::QuicSocket *newSocket = listenSocket->accept();
        newSocket->setCallback(this);
        socketMap.addSocket(newSocket);
        EV_INFO << "Accepted connection, socketId=" << newSocket->getSocketId() << std::endl;
    }

    void MoqRelayApp::socketEstablished(inet::QuicSocket *socket)
    {
        EV_INFO << "socketEstablished" << std::endl;
        sendingAllowed = true;
    }

    void MoqRelayApp::socketDataAvailable(inet::QuicSocket *peerSocket, inet::QuicDataInfo *dataInfo)
    {
        // QUIC signals that stream data is ready; request delivery as a QUIC_I_DATA packet.
        // Remember the stream id: the delivered packet carries no stream tag, so we map it
        // back via this FIFO queue in socketDataArrived.
        pendingRecvStreams[peerSocket->getSocketId()].push_back(dataInfo->getStreamID());
        peerSocket->recv(static_cast<int64_t>(dataInfo->getAvaliableDataSize()), dataInfo->getStreamID());
    }

    void MoqRelayApp::socketDataArrived(inet::QuicSocket *peerSocket, inet::Packet *packet)
    {
        // QUIC recv() delivers data in a "data" wrapper regardless of the original name.
        // Identify message type by dynamic-casting the leading chunk.
        auto frontChunk = packet->peekAtFront<inet::Chunk>();
        EV_DEBUG << "Received packet: " << packet->getFullName() << " With header: " << frontChunk.get()->getClassName() << std::endl;

        // Resolve the receive stream id from the FIFO recorded at data-available time
        // (the QuicStreamReq tag does not survive to the receiver).
        long recvStreamId = 0;
        {
            auto& q = pendingRecvStreams[peerSocket->getSocketId()];
            if (!q.empty()) { recvStreamId = q.front(); q.pop_front(); }
        }

        const auto *announceHeader = dynamic_cast<const MoqPublisherAnnounce *>(frontChunk.get());
        const auto *subHeader = dynamic_cast<const MoqSubscriber *>(frontChunk.get());
        const auto *ackHeader = dynamic_cast<const MoqObjectAck *>(frontChunk.get());

        if (ackHeader != nullptr)
        {
            // Subscriber confirmed delivery of an object — release the stream to forward
            // the next queued object for this (subscriber, track).
            std::string sid = ackHeader->getSubscriberId();
            std::string trackAlias = std::string(ackHeader->getTrackNamespace()) + "/" + ackHeader->getTrackName();
            EV_DEBUG << "OBJECT_ACK from " << sid << " for " << trackAlias
                     << " objectId=" << ackHeader->getObjectId() << std::endl;
            handleObjectAck(sid, trackAlias);
        }
        else if (announceHeader != nullptr)
        {
            EV_DEBUG << "ANNOUNCE packet received" << std::endl;
            std::string pid = std::to_string(announceHeader->getPublisherId());
            TrackKey tKey{announceHeader->getTrackNamespace(), announceHeader->getTrackName()};

            TrackMeta tm;
            tm.publisherId = announceHeader->getPublisherId();
            tm.trackId = announceHeader->getTrackId();
            tm.trackNamespace = announceHeader->getTrackNamespace();
            tm.trackName = announceHeader->getTrackName();
            tm.trackAlias = announceHeader->getTrackAlias();
            tm.packetSize = announceHeader->getPayloadSize();
            tm.sendInterval = announceHeader->getSendInterval();
            tm.priority = announceHeader->getPriority();
            tm.nextObjectId = 0;

            publishedTracks[tKey] = tm;
            publisherSockets[pid] = peerSocket;
            publisherSocketsByTrackKey[tKey] = peerSocket;

            EV_INFO << "Registered track " << tm.trackAlias << " from publisher " << pid << std::endl;
            EV_INFO << "Track counts: " << publishedTracks.size() << std::endl;

            long streamId = recvStreamId;

            // Record which (socket, stream) carries data objects for this track.
            // The publisher reuses the same stream for TRACK_OBJ after ANNOUNCE.
            publisherStreamToTrack[{peerSocket->getSocketId(), streamId}] = {tm.trackNamespace, tm.trackName};
            EV_DEBUG << "Mapped socketId=" << peerSocket->getSocketId()
                     << " streamId=" << streamId
                     << " -> track " << tm.trackAlias << std::endl;

            auto response = new inet::Packet("ANNOUNCE_OK");
            response->insertAtBack(inet::makeShared<inet::BytesChunk>(std::vector<uint8_t>{0x01}));
            response->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
            peerSocket->send(response);
        }
        else if (subHeader != nullptr)
        {
            EV_DEBUG << "SUBSCRIBE packet received" << std::endl;

            std::string sid = subHeader->getSubscriberId();
            std::string trackAlias = std::string(subHeader->getTrackNamespace()) + "/" + subHeader->getTrackName();

            long streamId = recvStreamId;

            subscriberSockets[sid] = peerSocket;
            subscriberStreamIds[{sid, trackAlias}] = streamId;

            EV_INFO << "Subscriber " << sid << " subscribed to " << trackAlias
                    << " on streamId=" << streamId << std::endl;

            onSubscribe(sid, trackAlias, streamId);
        }
        else
        {
            // TRACK_OBJ data. The QUIC stream is a byte stream, so a large object
            // (e.g. PCloud) is delivered as a sequence of SliceChunk fragments while a
            // small object (e.g. BBox) arrives as a single complete MoqObjectChunk.
            // We store-and-forward: accumulate fragments per object and, once the whole
            // object has been received, forward a single full-size MoqObjectChunk
            // downstream (mirroring the publisher's send pattern so QUIC re-fragments it
            // safely as SliceChunks rather than a SequenceChunk).
            long streamId = recvStreamId;
            int socketId = peerSocket->getSocketId();

            // Resolve the underlying MoqObjectChunk and how many payload+header bytes
            // this particular delivery carries.
            const MoqObjectChunk *srcObj = dynamic_cast<const MoqObjectChunk *>(frontChunk.get());
            long sliceBytes = 0;
            if (srcObj != nullptr) {
                sliceBytes = inet::B(srcObj->getChunkLength()).get();
            } else {
                const auto *sc = dynamic_cast<const inet::SliceChunk *>(frontChunk.get());
                if (sc != nullptr) {
                    srcObj = dynamic_cast<const MoqObjectChunk *>(sc->getChunk().get());
                    sliceBytes = inet::B(sc->getLength()).get();
                }
            }
            if (srcObj == nullptr) {
                EV_WARN << "Received non-MoqObjectChunk data on socketId=" << socketId
                        << " streamId=" << streamId << ", discarding" << std::endl;
                delete packet;
                return;
            }

            auto key = std::make_tuple(socketId, streamId, (long) srcObj->getObjectId());
            RelayObjReasm& st = reasm[key];
            if (st.totalBytes == 0) {
                // First fragment of this object: snapshot metadata, start the relay timer
                // and grow the reassembly backlog (relay queue depth).
                st.totalBytes = 64 + srcObj->getPayloadLength();
                st.firstSliceTime = omnetpp::simTime();
                st.trackId = srcObj->getTrackId();
                st.trackAlias = srcObj->getTrackAlias();
                st.groupId = srcObj->getGroupId();
                st.objectId = srcObj->getObjectId();
                st.priority = srcObj->getPriority();
                st.payloadLength = srcObj->getPayloadLength();
                st.creationTime = srcObj->getCreationTime();
                auto trackIt = publisherStreamToTrack.find({socketId, streamId});
                if (trackIt != publisherStreamToTrack.end()) {
                    st.trackNamespace = trackIt->second.first;
                    st.trackName = trackIt->second.second;
                } else {
                    EV_WARN << "Object on unknown stream (socketId=" << socketId
                            << " streamId=" << streamId << "), track namespace unresolved" << std::endl;
                }
                emit(relayQueueDepthSignal, (long) reasm.size());
            }
            st.receivedBytes += sliceBytes;

            if (st.receivedBytes >= st.totalBytes) {
                // Object fully reassembled — forward it and shrink the backlog.
                RelayObjReasm completed = st;
                reasm.erase(key);
                forwardCompletedObject(completed);
                emit(relayQueueDepthSignal, (long) reasm.size());
            }
        }
        delete packet;
    }

    // Queue a fully reassembled object for forwarding to every subscriber of its track.
    // Actual transmission is paced per (subscriber, track) stream by tryFlushStream so that
    // only one object is in flight per stream at a time (see ForwardItem docs).
    void MoqRelayApp::forwardCompletedObject(const RelayObjReasm& obj)
    {
        TrackKey tKey{obj.trackNamespace, obj.trackName};
        auto subsIt = subscriberByTrack.find(tKey);
        if (subsIt == subscriberByTrack.end() || subsIt->second.empty()) {
            EV_DEBUG << "No subscribers for track " << obj.trackAlias
                     << ", dropping object data" << std::endl;
            return;
        }

        for (const auto& subscriberId : subsIt->second) {
            auto subStreamIt = subscriberStreamIds.find({subscriberId, obj.trackAlias});
            if (subscriberSockets.find(subscriberId) == subscriberSockets.end()
                || subStreamIt == subscriberStreamIds.end()) {
                EV_WARN << "Subscriber " << subscriberId << " has no registered socket or stream, skipping" << std::endl;
                continue;
            }

            ForwardItem item;
            item.subscriberId = subscriberId;
            item.streamId = subStreamIt->second;
            item.firstSliceTime = obj.firstSliceTime;
            item.trackId = obj.trackId;
            item.trackAlias = obj.trackAlias;
            item.groupId = obj.groupId;
            item.objectId = obj.objectId;
            item.priority = obj.priority;
            item.payloadLength = obj.payloadLength;
            item.creationTime = obj.creationTime;

            auto key = std::make_pair(subscriberId, obj.trackAlias);
            auto& fifo = streamFifo[key];
            // Finite relay buffer: drop the oldest queued object on overflow (counts as loss).
            if (fifo.size() >= maxFifoPerStream) {
                fifo.pop_front();
                pendingForwardCount--;
                relayDroppedTotal++;
                EV_WARN << "Forward queue overflow for " << subscriberId << "/" << obj.trackAlias
                        << ", dropping oldest object" << std::endl;
            }
            fifo.push_back(item);
            pendingForwardCount++;
            emit(relayQueueDepthSignal, pendingForwardCount);

            tryFlushStream(subscriberId, obj.trackAlias);
        }
    }

    // Send the next queued object on a (subscriber, track) stream if none is in flight.
    void MoqRelayApp::tryFlushStream(const std::string& subscriberId, const std::string& trackAlias)
    {
        auto key = std::make_pair(subscriberId, trackAlias);
        if (streamBusy[key]) {
            return; // an object is already in flight on this stream
        }
        auto fit = streamFifo.find(key);
        if (fit == streamFifo.end() || fit->second.empty()) {
            return;
        }
        auto subSocketIt = subscriberSockets.find(subscriberId);
        if (subSocketIt == subscriberSockets.end()) {
            // Subscriber gone — discard its queue.
            pendingForwardCount -= fit->second.size();
            fit->second.clear();
            emit(relayQueueDepthSignal, pendingForwardCount);
            return;
        }

        ForwardItem item = fit->second.front();
        fit->second.pop_front();

        // Forward a full-size object: inflate chunkLength to header + payload so QUIC
        // re-fragments it into SliceChunks downstream, letting the subscriber measure
        // true object completion time.
        auto fwdHeader = inet::makeShared<MoqObjectChunk>();
        fwdHeader->setTrackId(item.trackId);
        fwdHeader->setTrackAlias(item.trackAlias.c_str());
        fwdHeader->setGroupId(item.groupId);
        fwdHeader->setObjectId(item.objectId);
        fwdHeader->setPriority(item.priority);
        fwdHeader->setPayloadLength(item.payloadLength);
        fwdHeader->setCreationTime(item.creationTime);
        fwdHeader->setChunkLength(inet::B(64 + item.payloadLength));

        auto fwdPacket = new inet::Packet("TRACK_OBJ_FWD");
        fwdPacket->insertAtBack(fwdHeader);
        subSocketIt->second->send(fwdPacket, item.streamId);
        streamBusy[key] = true;

        forward_count[subscriberId] += 1;
        objectsForwardedTotal++;
        omnetpp::simtime_t forwardDelay = omnetpp::simTime() - item.firstSliceTime;
        emit(relayForwardDelaySignal, forwardDelay.dbl()); // includes queueing wait
        fwdDelaySum += forwardDelay.dbl();
        if (forwardDelay.dbl() > fwdDelayMax) fwdDelayMax = forwardDelay.dbl();
        fwdDelayCount++;
        emit(objectForwardedSignal, (long) item.payloadLength);
        EV_INFO << "Forwarded object to subscriber " << subscriberId
                << " trackAlias=" << item.trackAlias
                << " objectId=" << item.objectId
                << " payloadLength=" << item.payloadLength
                << " relayDelay=" << forwardDelay
                << " count=" << forward_count[subscriberId] << std::endl;
    }

    // Subscriber acknowledged an object — release the stream and forward the next one.
    void MoqRelayApp::handleObjectAck(const std::string& subscriberId, const std::string& trackAlias)
    {
        auto key = std::make_pair(subscriberId, trackAlias);
        if (streamBusy[key]) {
            streamBusy[key] = false;
            pendingForwardCount--; // the in-flight object has left the relay
            emit(relayQueueDepthSignal, pendingForwardCount);
        }
        tryFlushStream(subscriberId, trackAlias);
    }

    void MoqRelayApp::finish()
    {
        recordScalar("objectsForwardedTotal", objectsForwardedTotal);
        recordScalar("objectsInReassemblyAtEnd", (long) reasm.size());
        recordScalar("objectsDroppedQueueOverflow", relayDroppedTotal);
        recordScalar("objectsPendingForwardAtEnd", pendingForwardCount);
        if (fwdDelayCount > 0) {
            recordScalar("meanForwardDelay", fwdDelaySum / fwdDelayCount, "s");
            recordScalar("maxForwardDelay", fwdDelayMax, "s");
        }
        for (auto& fc : forward_count) {
            recordScalar(("subscriber[" + fc.first + "].objectsForwarded").c_str(), fc.second);
        }
    }

    void MoqRelayApp::socketClosed(inet::QuicSocket *closedSocket)
    {
        EV_INFO << "socketClosed, socketId=" << closedSocket->getSocketId() << std::endl;

        // Release any forwarding state held for subscribers on this connection so their
        // queues do not leak and the queue-depth metric stays accurate.
        std::vector<std::string> goneSubscribers;
        for (auto& entry : subscriberSockets) {
            if (entry.second == closedSocket) {
                goneSubscribers.push_back(entry.first);
            }
        }
        for (const auto& sid : goneSubscribers) {
            for (auto it = streamFifo.begin(); it != streamFifo.end(); ) {
                if (it->first.first == sid) {
                    pendingForwardCount -= it->second.size();
                    if (streamBusy[it->first]) pendingForwardCount--; // in-flight object lost
                    streamBusy.erase(it->first);
                    it = streamFifo.erase(it);
                } else {
                    ++it;
                }
            }
            subscriberSockets.erase(sid);
        }
        if (!goneSubscribers.empty()) {
            emit(relayQueueDepthSignal, pendingForwardCount);
        }

        socketMap.removeSocket(closedSocket);
        delete closedSocket;
    }

    void MoqRelayApp::socketSendQueueFull(inet::QuicSocket *socket)
    {
        EV_WARN << "Send queue full, QUIC will handle backpressure" << std::endl;
    }

    void MoqRelayApp::socketSendQueueDrain(inet::QuicSocket *socket)
    {
        EV_DEBUG << "Send queue drained" << std::endl;
    }

    void MoqRelayApp::handleMessageWhenUp(omnetpp::cMessage *msg)
    {
        EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
        if (msg->isSelfMessage())
        {
            int id = msg->getKind();
            if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME)
            {
                handleTimeout(msg);
            }
        }
        else if (msg->arrivedOn("socketIn"))
        {
            // Route to the accepted client socket that owns this message.
            // Falls back to the listening socket for connection-available events.
            inet::ISocket *sock = socketMap.findSocketFor(msg);
            if (sock)
            {
                sock->processMessage(msg);
            }
            else
            {
                socket.processMessage(msg);
            }
        }
        else
        {
            throw omnetpp::cRuntimeError("Invalid message: %d", (int)msg->getKind());
        }
    }

    TrackKey MoqRelayApp::getTrackKey(std::string trackAlias)
    {

        std::vector<std::string> trackinfo = StringUtils::splitString(trackAlias, "/");
        if (trackinfo.size() != 2)
        {
            throw "Invalid track alias";
        }
        TrackKey tKey{trackinfo[0], trackinfo[1]};
        return tKey;
    }

    void MoqRelayApp::onSubscribe(std::string sid, std::string trackAlias, long streamId)
    {
        if (subscriberSockets.find(sid) != subscriberSockets.end())
        {

            inet::QuicSocket *subscriberSocket = subscriberSockets.find(sid)->second;
            try
            {
                TrackKey tKey = getTrackKey(trackAlias);
                auto it = publishedTracks.find(tKey);
                auto publisherSocketIt = publisherSocketsByTrackKey.find(tKey);
                if (it != publishedTracks.end() && publisherSocketIt != publisherSocketsByTrackKey.end())
                {
                    TrackMeta track = it->second;
                    inet::QuicSocket *publisherSocket = publisherSocketIt->second;

                    if (subscriberByTrack.find(tKey) == subscriberByTrack.end())
                    {
                        subscriberByTrack[tKey] = std::vector<std::string>();
                    }
                    subscriberByTrack[tKey].push_back(sid);

                    auto responseSubscriber = new inet::Packet("SUBSCRIBE_OK");
                    responseSubscriber->insertAtBack(inet::makeShared<inet::BytesChunk>(std::vector<uint8_t>{0x01}));
                    responseSubscriber->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                    subscriberSocket->send(responseSubscriber);

                    auto responsePublisher = new inet::Packet("SUBSCRIBE_OK");
                    inet::Ptr<MoqSubscriber> header = inet::makeShared<MoqSubscriber>();
                    header->setTrackNamespace(track.trackNamespace.c_str());
                    header->setTrackName(track.trackName.c_str());
                    header->setSubscriberId(sid.c_str());
                    header->setSubscriberPriority(track.priority);
                    header->setStartObjectId(track.nextObjectId);
                    header->setChunkLength(inet::B(64));
                    responsePublisher->insertAtBack(header);
                    responsePublisher->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                    publisherSocket->send(responsePublisher);
                }
                else
                {
                    auto response = new inet::Packet("SUBSCRIBE_ERROR");
                    response->insertAtBack(inet::makeShared<inet::BytesChunk>(std::vector<uint8_t>{0x01}));
                    response->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                    subscriberSocket->send(response);
                }
            }
            catch (const std::string msg)
            {
                auto response = new inet::Packet("SUBSCRIBE_ERROR");
                response->insertAtBack(inet::makeShared<inet::BytesChunk>(std::vector<uint8_t>{0x01}));
                response->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);
                subscriberSocket->send(response);
            }
        }
    }

    void MoqRelayApp::onPublish(std::string pid, TrackMeta tm)
    {
    }

    void MoqRelayApp::relayTrackData(std::string trackAlias, std::string sid)
    {
    }

    void MoqRelayApp::handleTimeout(omnetpp::cMessage *msg)
    {
        EV_DETAIL << "handle timeout of kind " << msg->getKind() << std::endl;
        switch (msg->getKind())
        {
        }
    }

}
