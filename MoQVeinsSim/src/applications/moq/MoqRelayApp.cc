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
#include "models/MoqFraming.h"

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
        emit(relayQueueDepthSignal, (long) 0); // seed so timeavg/max aren't nan if never blocked

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
        int sock = peerSocket->getSocketId();
        long s = dataInfo->getStreamID();
        if (recvInFlight.count(sock) && recvInFlight[sock] == s)
            return; // the outstanding recv will drain whatever is available on this stream
        recvPending[sock].insert(s);
        if (!recvInFlight.count(sock) || recvInFlight[sock] < 0)
            startNextRecv(peerSocket);
    }

    // Serialize receives per socket: one recv outstanding, draining a whole stream each time,
    // so the (untagged) delivered packet always maps to recvInFlight[socketId].
    void MoqRelayApp::startNextRecv(inet::QuicSocket *peerSocket)
    {
        int sock = peerSocket->getSocketId();
        auto& pend = recvPending[sock];
        if (pend.empty()) { recvInFlight[sock] = -1; return; }
        long s = *pend.begin();
        pend.erase(pend.begin());
        recvInFlight[sock] = s;
        peerSocket->recv((int64_t) 1 << 40, s); // large size => drain all available
    }

    void MoqRelayApp::socketDataArrived(inet::QuicSocket *peerSocket, inet::Packet *packet)
    {
        // The delivered packet maps to the serialized outstanding recv. Stream 0 carries
        // control frames; all others carry object frames.
        int socketId = peerSocket->getSocketId();
        long recvStreamId = recvInFlight.count(socketId) ? recvInFlight[socketId] : 0;
        recvInFlight[socketId] = -1;

        auto bytes = packet->peekDataAsBytes();
        const auto& vec = bytes->getBytes();
        omnetpp::simtime_t now = omnetpp::simTime();
        StreamReassembler& rb = recvBuffers[{socketId, recvStreamId}];
        if (rb.data.empty()) rb.frameStartTime = now;
        rb.data.insert(rb.data.end(), vec.begin(), vec.end());

        if (recvStreamId == CONTROL_STREAM) {
            MoqControlFrame c;
            size_t consumed;
            while (MoqFraming::tryParseControl(rb.data, c, consumed)) {
                handleControlFrame(peerSocket, c);
                rb.data.erase(rb.data.begin(), rb.data.begin() + consumed);
            }
        } else {
            MoqObjectFrame f;
            size_t consumed;
            while (MoqFraming::tryParse(rb.data, f, consumed)) {
                std::vector<uint8_t> frameBytes(rb.data.begin(), rb.data.begin() + consumed);
                onObjectFrame(f, frameBytes, rb.frameStartTime);
                rb.data.erase(rb.data.begin(), rb.data.begin() + consumed);
                rb.frameStartTime = now;
            }
        }
        delete packet;
        startNextRecv(peerSocket);
    }

    // Handle a control byte frame received on a peer's control stream.
    void MoqRelayApp::handleControlFrame(inet::QuicSocket *peerSocket, const MoqControlFrame &c)
    {
        if (c.type == CTRL_ANNOUNCE) {
            std::string pid = std::to_string(c.publisherId);
            TrackKey tKey{c.trackNamespace, c.trackName};
            TrackMeta tm;
            tm.publisherId = c.publisherId;
            tm.trackId = c.trackId;
            tm.trackNamespace = c.trackNamespace;
            tm.trackName = c.trackName;
            tm.trackAlias = c.trackAlias;
            tm.packetSize = c.payloadSize;
            tm.sendInterval = c.sendInterval;
            tm.priority = c.priority;
            tm.nextObjectId = 0;
            publishedTracks[tKey] = tm;
            publisherSockets[pid] = peerSocket;
            publisherSocketsByTrackKey[tKey] = peerSocket;
            EV_INFO << "Registered track " << tm.trackAlias << " from publisher " << pid
                    << " (tracks=" << publishedTracks.size() << ")" << std::endl;
        }
        else if (c.type == CTRL_SUBSCRIBE) {
            std::string sid = c.subscriberId;
            std::string trackAlias = c.trackNamespace + "/" + c.trackName;
            TrackKey tKey{c.trackNamespace, c.trackName};

            subscriberSockets[sid] = peerSocket;
            // Allocate a dedicated relay->subscriber data stream for this (subscriber, track).
            long dataStream = nextDataStreamId;
            nextDataStreamId += 4;
            subscriberStreamIds[{sid, trackAlias}] = dataStream;
            if (subscriberByTrack.find(tKey) == subscriberByTrack.end())
                subscriberByTrack[tKey] = std::vector<std::string>();
            subscriberByTrack[tKey].push_back(sid);

            EV_INFO << "Subscriber " << sid << " subscribed to " << trackAlias
                    << " -> data stream " << dataStream << std::endl;

            // Tell the publisher to start sending this track (control frame on its stream 0).
            auto pubIt = publisherSocketsByTrackKey.find(tKey);
            auto trackIt = publishedTracks.find(tKey);
            if (pubIt != publisherSocketsByTrackKey.end() && trackIt != publishedTracks.end()) {
                MoqControlFrame ok;
                ok.type = CTRL_SUBSCRIBE_OK;
                ok.trackNamespace = c.trackNamespace;
                ok.trackName = c.trackName;
                ok.trackAlias = trackAlias;
                ok.subscriberId = sid;
                ok.startObjectId = trackIt->second.nextObjectId;
                auto pkt = new inet::Packet("SUBSCRIBE_OK");
                pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(MoqFraming::encodeControl(ok)));
                pubIt->second->send(pkt, CONTROL_STREAM);
            } else {
                EV_WARN << "SUBSCRIBE for unknown track " << trackAlias << std::endl;
            }
        }
    }

    // A complete object frame arrived from a publisher; forward it to every subscriber of
    // its track (routing uses the self-describing trackAlias inside the frame).
    void MoqRelayApp::onObjectFrame(const MoqObjectFrame &f, const std::vector<uint8_t> &frameBytes,
                                    omnetpp::simtime_t firstByteTime)
    {
        auto slash = f.trackAlias.find('/');
        if (slash == std::string::npos) {
            EV_WARN << "Object with malformed trackAlias '" << f.trackAlias << "', dropping" << std::endl;
            return;
        }
        TrackKey tKey{f.trackAlias.substr(0, slash), f.trackAlias.substr(slash + 1)};
        auto subsIt = subscriberByTrack.find(tKey);
        if (subsIt == subscriberByTrack.end() || subsIt->second.empty()) {
            EV_DEBUG << "No subscribers for track " << f.trackAlias << ", dropping object" << std::endl;
            return;
        }
        for (const auto& sid : subsIt->second)
            forwardToSubscriber(sid, f, frameBytes, firstByteTime);
    }

    // Forward one object frame to a subscriber, sending immediately or queueing if the
    // subscriber's downstream QUIC send queue is currently full.
    void MoqRelayApp::forwardToSubscriber(const std::string &subscriberId, const MoqObjectFrame &f,
                                          const std::vector<uint8_t> &frameBytes, omnetpp::simtime_t firstByteTime)
    {
        auto sockIt = subscriberSockets.find(subscriberId);
        auto streamIt = subscriberStreamIds.find({subscriberId, f.trackAlias});
        if (sockIt == subscriberSockets.end() || streamIt == subscriberStreamIds.end()) {
            EV_WARN << "Subscriber " << subscriberId << " has no socket/stream, skipping" << std::endl;
            return;
        }
        FwdItem item;
        item.sock = sockIt->second;
        item.streamId = streamIt->second;
        item.bytes = frameBytes;
        item.payloadLength = f.payloadLength;
        item.firstByteTime = firstByteTime;
        item.subscriberId = subscriberId;

        int sockId = item.sock->getSocketId();
        if (socketBlocked[sockId]) {
            auto& fifo = socketFifo[sockId];
            // Finite relay buffer: drop the oldest queued object on overflow (counts as loss).
            if (fifo.size() >= maxFifoPerStream) {
                fifo.pop_front();
                pendingForwardCount--;
                relayDroppedTotal++;
                EV_WARN << "Forward queue overflow for socketId=" << sockId << ", dropping oldest" << std::endl;
            }
            fifo.push_back(std::move(item));
            pendingForwardCount++;
            emit(relayQueueDepthSignal, pendingForwardCount);
        } else {
            doForwardSend(item);
        }
    }

    void MoqRelayApp::doForwardSend(const FwdItem &item)
    {
        auto pkt = new inet::Packet("TRACK_OBJ_FWD");
        pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(item.bytes));
        item.sock->send(pkt, item.streamId);

        forward_count[item.subscriberId] += 1;
        objectsForwardedTotal++;
        double delay = (omnetpp::simTime() - item.firstByteTime).dbl();
        emit(relayForwardDelaySignal, delay);
        fwdDelaySum += delay;
        if (delay > fwdDelayMax) fwdDelayMax = delay;
        fwdDelayCount++;
        emit(objectForwardedSignal, (long) item.payloadLength);
    }

    // The subscriber socket's send queue drained — flush queued objects for it.
    void MoqRelayApp::flushSocket(int socketId)
    {
        auto it = socketFifo.find(socketId);
        if (it == socketFifo.end()) return;
        auto& fifo = it->second;
        while (!socketBlocked[socketId] && !fifo.empty()) {
            FwdItem item = std::move(fifo.front());
            fifo.pop_front();
            pendingForwardCount--;
            doForwardSend(item);
        }
        emit(relayQueueDepthSignal, pendingForwardCount);
    }

    void MoqRelayApp::finish()
    {
        recordScalar("objectsForwardedTotal", objectsForwardedTotal);
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

        // Release forwarding/receive state held for this connection so it doesn't leak
        // and the queue-depth metric stays accurate.
        int sockId = closedSocket->getSocketId();
        auto fit = socketFifo.find(sockId);
        if (fit != socketFifo.end()) {
            pendingForwardCount -= (long) fit->second.size();
            socketFifo.erase(fit);
        }
        socketBlocked.erase(sockId);
        recvPending.erase(sockId);
        recvInFlight.erase(sockId);
        for (auto it = recvBuffers.begin(); it != recvBuffers.end(); ) {
            if (it->first.first == sockId) it = recvBuffers.erase(it);
            else ++it;
        }
        std::vector<std::string> goneSubscribers;
        for (auto& entry : subscriberSockets)
            if (entry.second == closedSocket) goneSubscribers.push_back(entry.first);
        for (const auto& sid : goneSubscribers) {
            for (auto it = subscriberStreamIds.begin(); it != subscriberStreamIds.end(); )
                (it->first.first == sid) ? it = subscriberStreamIds.erase(it) : ++it;
            subscriberSockets.erase(sid);
        }
        emit(relayQueueDepthSignal, pendingForwardCount);

        socketMap.removeSocket(closedSocket);
        delete closedSocket;
    }

    void MoqRelayApp::socketSendQueueFull(inet::QuicSocket *socket)
    {
        socketBlocked[socket->getSocketId()] = true;
        EV_DEBUG << "Send queue full on socketId=" << socket->getSocketId() << std::endl;
    }

    void MoqRelayApp::socketSendQueueDrain(inet::QuicSocket *socket)
    {
        int sockId = socket->getSocketId();
        socketBlocked[sockId] = false;
        flushSocket(sockId);
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

    // Subscribe handling now lives in handleControlFrame (CTRL_SUBSCRIBE). Kept as an empty
    // override to satisfy the declaration.
    void MoqRelayApp::onSubscribe(std::string sid, std::string trackAlias, long streamId)
    {
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
