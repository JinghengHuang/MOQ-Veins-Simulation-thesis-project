/* --- MoqRelayApp.cpp --- */

/* ------------------------------------------
author: Jingheng Huang
date: 5/15/2026
------------------------------------------ */

#include "MoqRelayApp.h"
#include "utils/StringUtils.h"
#include <regex.h>
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/transportlayer/contract/tcp/TcpCommand_m.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "models/MoqFraming.h"
#include <algorithm>

namespace moqveinssim
{
    Define_Module(MoqRelayApp);

    MoqRelayApp::MoqRelayApp()
    {
        timerConnect = new inet::cMessage("MOQ Relay Timer - Connect");
        timerConnect->setKind(TIMER_CONNECT);

        timerLimitRuntime = new inet::cMessage("MOQ Relay Timer - Runtime limit");
        timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);

        timerTimeoutCheck = new inet::cMessage("MOQ Relay Timer - Delivery timeout sweep");
        timerTimeoutCheck->setKind(TIMER_TIMEOUT_CHECK);
    }

    MoqRelayApp::~MoqRelayApp()
    {
        cancelAndDelete(timerConnect);
        cancelAndDelete(timerLimitRuntime);
        cancelAndDelete(timerTimeoutCheck);
        // Clear pointer maps before deleteSockets() frees the objects
        publisherSockets.clear();
        publisherSocketsByTrackKey.clear();
        subscriberSockets.clear();
        socketMap.deleteSockets();
        publisherTcpSockets.clear();
        publisherTcpSocketsByTrackKey.clear();
        subscriberTcpSockets.clear();
        tcpSocketMap.deleteSockets();
    }

    void MoqRelayApp::handleStartOperation(inet::LifecycleOperation *operation)
    {
        EV_DEBUG << "initialize MoqRelayApp" << std::endl;

        relayQueueDepthSignal = registerSignal("relayQueueDepth");
        relayForwardDelaySignal = registerSignal("relayForwardDelay");
        objectForwardedSignal = registerSignal("objectForwarded");
        emit(relayQueueDepthSignal, (long) 0); // seed so timeavg/max aren't nan if never blocked

        std::string protoStr = par("protocol").stdstringValue();
        if (protoStr == "tcp") proto = PROTO_TCP;
        else if (protoStr == "udp") proto = PROTO_UDP;
        else proto = PROTO_QUIC;
        udpFragmentSize = (int) par("udpFragmentSize").intValue();
        quicChunkBytes = par("quicChunkBytes").intValue();
        quicForwardBufferPerSubscriberLimit = par("quicForwardBufferPerSubscriberLimit").intValue();
        timeoutCheckInterval = par("deliveryTimeoutCheckInterval").doubleValue();
        if (proto == PROTO_QUIC)
            scheduleAt(omnetpp::simTime() + timeoutCheckInterval, timerTimeoutCheck);
        // Mirror QUIC's own send-queue limit, and keep a handle on the module so the forward path
        // can read each subscriber connection's true queue occupancy before every write.
        if (auto* host = getParentModule()) {
            if (auto* q = host->getSubmodule("quic")) {
                if (q->hasPar("sendQueueLimit")) quicSendQueueLimit = q->par("sendQueueLimit").intValue();
                quicModule = dynamic_cast<inet::quic::Quic*>(q);
            }
        }

        inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
        int localPort = par("localPort");

        switch (proto) {
            case PROTO_QUIC:
                socket.setOutputGate(gate("socketOut"));
                socket.setCallback(this);
                socket.bind(localAddress, localPort);
                socket.listen();
                break;
            case PROTO_TCP:
                tcpSocket.setOutputGate(gate("socketOut"));
                tcpSocket.setCallback(this);
                tcpSocket.bind(localAddress, localPort);
                tcpSocket.listen();
                break;
            case PROTO_UDP:
                udpSocket.setOutputGate(gate("socketOut"));
                udpSocket.setCallback(this);
                udpSocket.bind(localAddress, localPort);
                break;
        }
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
            tm.deliveryTimeout = c.deliveryTimeout;
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
            // Downstream streams are no longer per (subscriber, track): they are allocated per
            // subgroup as objects arrive (see forwardToSubscriber), because MoQ carries one
            // subgroup per stream.
            if (subscriberByTrack.find(tKey) == subscriberByTrack.end())
                subscriberByTrack[tKey] = std::vector<std::string>();
            subscriberByTrack[tKey].push_back(sid);

            EV_INFO << "Subscriber " << sid << " subscribed to " << trackAlias << std::endl;

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
                pubIt->second->send(pkt, CONTROL_STREAM, CONTROL_STREAM_PRIORITY);
            } else {
                EV_WARN << "SUBSCRIBE for unknown track " << trackAlias << std::endl;
            }
        }
        else if (c.type == CTRL_PUBLISH_DONE) {
            // Upstream ended this track's subscription (draft-14 9.12). This relay holds no cache
            // of future objects, so it can no longer serve the track and must end its own
            // downstream subscriptions -- the same pattern section 2.5 mandates for a malformed
            // track. Downstream the reason is TRACK_ENDED, not TOO_FAR_BEHIND: those subscribers
            // are not the ones that fell behind.
            endTrackDownstream(TrackKey{c.trackNamespace, c.trackName}, PUBDONE_TRACK_ENDED);
        }
    }

    // Send PUBLISH_DONE to one subscriber and forget its state for this track. Per draft-14
    // section 9.12 every stream the relay will ever open for the subscription must be closed
    // first, so the caller resets them before calling.
    void MoqRelayApp::sendPublishDone(const std::string &subscriberId, const TrackKey &tKey,
                                      long statusCode)
    {
        auto sockIt = subscriberSockets.find(subscriberId);
        if (sockIt == subscriberSockets.end()) return;
        MoqControlFrame done;
        done.type = CTRL_PUBLISH_DONE;
        done.statusCode = statusCode;
        done.trackNamespace = tKey.trackNamespace;
        done.trackName = tKey.trackName;
        done.trackAlias = tKey.trackNamespace + "/" + tKey.trackName;
        done.subscriberId = subscriberId;
        auto pkt = new inet::Packet("PUBLISH_DONE");
        pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(MoqFraming::encodeControl(done)));
        sockIt->second->send(pkt, CONTROL_STREAM, CONTROL_STREAM_PRIORITY);
        publishDoneSent++;
    }

    // End ONE track for ONE subscriber: abandon what is queued or in flight for it, then
    // PUBLISH_DONE. Streams are reset before the message, as section 9.12 requires.
    void MoqRelayApp::endSubscriberTrack(const std::string &subscriberId, const TrackKey &tKey,
                                         long statusCode)
    {
        const std::string alias = tKey.trackNamespace + "/" + tKey.trackName;
        auto sockIt = subscriberSockets.find(subscriberId);
        if (sockIt == subscriberSockets.end()) return;
        auto stIt = sockSend.find(sockIt->second->getSocketId());
        if (stIt != sockSend.end()) {
            for (auto &prioQueue : stIt->second.buffer) {
                auto &q = prioQueue.second;
                for (auto &item : q) {
                    if (item.trackAlias != alias) continue;
                    if (item.sentOffset > 0)
                        resetDownstreamStream(item, MOQ_ERR_SEND_BUFFER_OVERFLOW);
                    relayShedOverflow[item.trackAlias]++;
                }
                long before = (long) q.size();
                q.erase(std::remove_if(q.begin(), q.end(),
                                       [&](const FwdItem &i) { return i.trackAlias == alias; }),
                        q.end());
                long removed = before - (long) q.size();
                stIt->second.count -= removed;
                pendingForwardCount -= removed;
            }
        }
        sendPublishDone(subscriberId, tKey, statusCode);

        auto tbIt = subscriberByTrack.find(tKey);
        if (tbIt != subscriberByTrack.end()) {
            auto &v = tbIt->second;
            v.erase(std::remove(v.begin(), v.end(), subscriberId), v.end());
            if (v.empty()) subscriberByTrack.erase(tbIt);
        }
    }

    // End a track for every subscriber of it.
    void MoqRelayApp::endTrackDownstream(const TrackKey &tKey, long statusCode)
    {
        auto subsIt = subscriberByTrack.find(tKey);
        if (subsIt == subscriberByTrack.end()) return;
        std::vector<std::string> subs = subsIt->second;   // copy: endSubscriberTrack mutates the map
        for (const auto &sid : subs)
            endSubscriberTrack(sid, tKey, statusCode);
        EV_WARN << "PUBLISH_DONE(status=" << statusCode << ") sent downstream for "
                << tKey.trackNamespace << "/" << tKey.trackName << " to " << subs.size()
                << " subscriber(s) at " << omnetpp::simTime() << std::endl;
    }

    // ===================== TCP server path (proto == PROTO_TCP) =====================

    void MoqRelayApp::socketAvailable(inet::TcpSocket *listenSocket, inet::TcpAvailableInfo *info)
    {
        auto *newSocket = new inet::TcpSocket(info);
        newSocket->setOutputGate(gate("socketOut"));
        newSocket->setCallback(this);
        tcpSocketMap.addSocket(newSocket);
        listenSocket->accept(info->getNewSocketId());
        EV_INFO << "Accepted TCP connection, socketId=" << newSocket->getSocketId() << std::endl;
    }

    void MoqRelayApp::socketDataArrived(inet::TcpSocket *peerSocket, inet::Packet *packet, bool)
    {
        int sid = peerSocket->getSocketId();
        auto& buf = tcpRecvBuf[sid];
        omnetpp::simtime_t now = omnetpp::simTime();
        if (buf.empty()) tcpFrameStart[sid] = now;
        auto bytes = packet->peekDataAsBytes();
        const auto& vec = bytes->getBytes();
        buf.insert(buf.end(), vec.begin(), vec.end());

        MoqControlFrame c;
        MoqObjectFrame f;
        size_t consumed;
        int kind;
        while ((kind = MoqFraming::tryParseEnvelope(buf, c, f, consumed)) != 0) {
            if (kind == 1) {
                handleControlFrameTcp(peerSocket, c);
            } else if (kind == 2) {
                // Inner length-prefixed frame (strip the 1-byte envelope class) for forwarding.
                std::vector<uint8_t> frameBytes(buf.begin() + 1, buf.begin() + consumed);
                onObjectFrame(f, frameBytes, tcpFrameStart[sid]);
            }
            buf.erase(buf.begin(), buf.begin() + consumed);
            tcpFrameStart[sid] = now;
        }
        delete packet;
    }

    // Control handling for TCP (mirrors the QUIC handler but tracks TcpSocket peers).
    void MoqRelayApp::handleControlFrameTcp(inet::TcpSocket *peerSocket, const MoqControlFrame &c)
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
            tm.deliveryTimeout = c.deliveryTimeout;
            tm.nextObjectId = 0;
            publishedTracks[tKey] = tm;
            publisherTcpSockets[pid] = peerSocket;
            publisherTcpSocketsByTrackKey[tKey] = peerSocket;
            EV_INFO << "Registered TCP track " << tm.trackAlias << " from publisher " << pid << std::endl;
        }
        else if (c.type == CTRL_SUBSCRIBE) {
            std::string sid = c.subscriberId;
            std::string trackAlias = c.trackNamespace + "/" + c.trackName;
            TrackKey tKey{c.trackNamespace, c.trackName};

            subscriberTcpSockets[sid] = peerSocket;
            if (subscriberByTrack.find(tKey) == subscriberByTrack.end())
                subscriberByTrack[tKey] = std::vector<std::string>();
            subscriberByTrack[tKey].push_back(sid);
            EV_INFO << "TCP subscriber " << sid << " subscribed to " << trackAlias << std::endl;

            auto pubIt = publisherTcpSocketsByTrackKey.find(tKey);
            auto trackIt = publishedTracks.find(tKey);
            if (pubIt != publisherTcpSocketsByTrackKey.end() && trackIt != publishedTracks.end()) {
                MoqControlFrame ok;
                ok.type = CTRL_SUBSCRIBE_OK;
                ok.trackNamespace = c.trackNamespace;
                ok.trackName = c.trackName;
                ok.trackAlias = trackAlias;
                ok.subscriberId = sid;
                ok.startObjectId = trackIt->second.nextObjectId;
                auto pkt = new inet::Packet("SUBSCRIBE_OK");
                pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(
                    MoqFraming::encodeEnvelope(MoqFraming::MSG_CONTROL, MoqFraming::encodeControl(ok))));
                pubIt->second->send(pkt);
            } else {
                EV_WARN << "TCP SUBSCRIBE for unknown track " << trackAlias << std::endl;
            }
        }
    }

    void MoqRelayApp::socketClosed(inet::TcpSocket *closedSocket)
    {
        int sid = closedSocket->getSocketId();
        EV_INFO << "TCP socketClosed, socketId=" << sid << std::endl;
        tcpRecvBuf.erase(sid);
        tcpFrameStart.erase(sid);
        for (auto it = subscriberTcpSockets.begin(); it != subscriberTcpSockets.end(); )
            (it->second == closedSocket) ? it = subscriberTcpSockets.erase(it) : ++it;
        for (auto it = publisherTcpSockets.begin(); it != publisherTcpSockets.end(); )
            (it->second == closedSocket) ? it = publisherTcpSockets.erase(it) : ++it;
        for (auto it = publisherTcpSocketsByTrackKey.begin(); it != publisherTcpSocketsByTrackKey.end(); )
            (it->second == closedSocket) ? it = publisherTcpSocketsByTrackKey.erase(it) : ++it;
        if (auto *s = tcpSocketMap.removeSocket(closedSocket)) delete s;
    }

    // ===================== UDP server path (proto == PROTO_UDP) =====================

    void MoqRelayApp::socketDataArrived(inet::UdpSocket *, inet::Packet *packet)
    {
        auto srcAddr = packet->getTag<inet::L3AddressInd>()->getSrcAddress();
        int srcPort = packet->getTag<inet::L4PortInd>()->getSrcPort();
        omnetpp::simtime_t now = omnetpp::simTime();

        auto bytes = packet->peekDataAsBytes();
        const auto& vec = bytes->getBytes();
        MoqFraming::MoqUdpFragment frag;
        if (MoqFraming::parseUdpFragment(vec, frag)) {
            std::string srcKey = srcAddr.str() + "#" + std::to_string(srcPort);
            auto key = std::make_tuple(srcKey, frag.trackAlias, (long) frag.objectId);
            auto& r = udpReasm[key];
            if (r.add(frag, now)) {
                if (frag.msgClass == MoqFraming::MSG_CONTROL) {
                    MoqControlFrame c;
                    size_t consumed;
                    if (MoqFraming::tryParseControl(r.data, c, consumed))
                        handleControlFrameUdp(c, srcAddr, srcPort);
                } else if (frag.msgClass == MoqFraming::MSG_OBJECT) {
                    MoqObjectFrame f;
                    size_t consumed;
                    if (MoqFraming::tryParse(r.data, f, consumed))
                        onObjectFrame(f, r.data, r.firstByteTime);
                }
                udpReasm.erase(key);
            }
        }
        delete packet;
    }

    // Control handling for UDP (peers identified by source address instead of socket).
    void MoqRelayApp::handleControlFrameUdp(const MoqControlFrame &c, inet::L3Address srcAddr, int srcPort)
    {
        if (c.type == CTRL_ANNOUNCE) {
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
            tm.deliveryTimeout = c.deliveryTimeout;
            tm.nextObjectId = 0;
            publishedTracks[tKey] = tm;
            publisherUdpAddrByTrackKey[tKey] = {srcAddr, srcPort};
            EV_INFO << "Registered UDP track " << tm.trackAlias << " from " << srcAddr.str() << std::endl;
        }
        else if (c.type == CTRL_SUBSCRIBE) {
            std::string sid = c.subscriberId;
            std::string trackAlias = c.trackNamespace + "/" + c.trackName;
            TrackKey tKey{c.trackNamespace, c.trackName};

            subscriberUdpAddrs[sid] = {srcAddr, srcPort};
            if (subscriberByTrack.find(tKey) == subscriberByTrack.end())
                subscriberByTrack[tKey] = std::vector<std::string>();
            // Avoid duplicate registration if SUBSCRIBE is retransmitted.
            auto& subs = subscriberByTrack[tKey];
            if (std::find(subs.begin(), subs.end(), sid) == subs.end()) subs.push_back(sid);
            EV_INFO << "UDP subscriber " << sid << " subscribed to " << trackAlias << std::endl;

            auto pubIt = publisherUdpAddrByTrackKey.find(tKey);
            auto trackIt = publishedTracks.find(tKey);
            if (pubIt != publisherUdpAddrByTrackKey.end() && trackIt != publishedTracks.end()) {
                MoqControlFrame ok;
                ok.type = CTRL_SUBSCRIBE_OK;
                ok.trackNamespace = c.trackNamespace;
                ok.trackName = c.trackName;
                ok.trackAlias = trackAlias;
                ok.subscriberId = sid;
                ok.startObjectId = trackIt->second.nextObjectId;
                auto frags = MoqFraming::fragmentFrame(MoqFraming::MSG_CONTROL, trackAlias,
                                                       -1, MoqFraming::encodeControl(ok), udpFragmentSize);
                for (auto& d : frags) {
                    auto pkt = new inet::Packet("SUBSCRIBE_OK");
                    pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(d));
                    udpSocket.sendTo(pkt, pubIt->second.first, pubIt->second.second);
                }
            } else {
                EV_WARN << "UDP SUBSCRIBE for unknown track " << trackAlias << std::endl;
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
        // Localization: count distinct object frames received from the publisher for this track,
        // so any residual subscriber-side loss can be attributed to pub->relay vs relay->sub.
        recvFromPubByTrack[f.trackAlias]++;
        TrackKey tKey{f.trackAlias.substr(0, slash), f.trackAlias.substr(slash + 1)};
        auto subsIt = subscriberByTrack.find(tKey);
        if (subsIt == subscriberByTrack.end() || subsIt->second.empty()) {
            EV_DEBUG << "No subscribers for track " << f.trackAlias << ", dropping object" << std::endl;
            return;
        }
        for (const auto& sid : subsIt->second)
            forwardToSubscriber(sid, f, frameBytes, firstByteTime);
    }

    // Forward one object frame to a subscriber over the active transport.
    void MoqRelayApp::forwardToSubscriber(const std::string &subscriberId, const MoqObjectFrame &f,
                                          const std::vector<uint8_t> &frameBytes, omnetpp::simtime_t firstByteTime)
    {
        switch (proto) {
        case PROTO_QUIC: {
            // QUIC: one downstream stream per forwarded object, mirroring the publisher's
            // group/subgroup-per-object model (draft-14 sections 2.2-2.3). A relay MUST NOT
            // combine or split object payloads (section 8.5), and giving each object its own
            // stream is what lets the relay reset a stale object's stream (section 10.4.3)
            // without corrupting the subscriber's view of every other object on the track.
            auto sockIt = subscriberSockets.find(subscriberId);
            if (sockIt == subscriberSockets.end()) {
                EV_WARN << "Subscriber " << subscriberId << " has no socket, skipping" << std::endl;
                return;
            }

            // One downstream stream per subgroup, mirroring the publisher's structure. A relay
            // MUST NOT combine or split object payloads (section 8.5), and preserving the
            // subgroup-to-stream mapping is what lets a stale object's stream be reset.
            DownSubgroup key{subscriberId, f.trackAlias, f.groupId, f.subgroupId};

            // Stream already reset: the rest of the subgroup went with it. Charged to the cause of
            // the reset, so overflow collateral does not masquerade as timeout shedding.
            auto rdIt = resetDownstream.find(key);
            if (rdIt != resetDownstream.end()) {
                if (rdIt->second == MOQ_ERR_SEND_BUFFER_OVERFLOW) relayShedOverflow[f.trackAlias]++;
                else relayShedStale[f.trackAlias]++;
                return;
            }

            auto dIt = downstreamStreams.find(key);
            if (dIt == downstreamStreams.end())
                dIt = downstreamStreams.emplace(key, (nextDataStreamId += 4) - 4).first;

            FwdItem item;
            item.sock = sockIt->second;
            item.subgroup = key;
            item.streamId = dIt->second;
            item.bytes = frameBytes;
            item.payloadLength = f.payloadLength;
            item.priority = f.priority;
            item.trackAlias = f.trackAlias;
            item.firstByteTime = firstByteTime;
            item.createdAt = f.creationTime;
            { auto slash = f.trackAlias.find('/');
              if (slash != std::string::npos) {
                  auto tIt = publishedTracks.find(TrackKey{f.trackAlias.substr(0, slash), f.trackAlias.substr(slash + 1)});
                  if (tIt != publishedTracks.end()) item.timeout = tIt->second.deliveryTimeout;
              } }
            item.subscriberId = subscriberId;

            int sockId = item.sock->getSocketId();
            SockSendState& st = sockSend[sockId];
            st.buffer[item.priority].push_back(std::move(item));
            st.count++;
            pendingForwardCount++;

            // This subscriber's queue exceeded the implementation-defined limit, which is exactly
            // the TOO_FAR_BEHIND condition (draft-14 9.2.1.2 / 9.12): end its subscriptions rather
            // than silently drop objects. Same reasoning as MoqPublisherApp::terminateSubscriptions
            // -- the limit is a property of the peer, so every track it subscribes to ends.
            if (st.count > quicForwardBufferPerSubscriberLimit) {
                relayDroppedTotal++;
                EV_WARN << "Forward queue for socketId=" << sockId << " exceeded "
                        << quicForwardBufferPerSubscriberLimit << " objects: TOO_FAR_BEHIND"
                        << std::endl;
                std::vector<TrackKey> ended;
                for (auto &tb : subscriberByTrack)
                    if (std::find(tb.second.begin(), tb.second.end(), subscriberId) != tb.second.end())
                        ended.push_back(tb.first);
                for (const auto &tk : ended)
                    endSubscriberTrack(subscriberId, tk, PUBDONE_TOO_FAR_BEHIND);
            }
            emit(relayQueueDepthSignal, pendingForwardCount);
            flushSocket(sockId);
            break;
        }
        case PROTO_TCP: {
            // TCP: single ordered stream per subscriber; TCP provides its own flow control,
            // so the object is sent directly (no app-level forwarding queue).
            auto it = subscriberTcpSockets.find(subscriberId);
            if (it == subscriberTcpSockets.end()) {
                EV_WARN << "Subscriber " << subscriberId << " has no TCP socket, skipping" << std::endl;
                return;
            }
            auto pkt = new inet::Packet("TRACK_OBJ_FWD");
            pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(
                MoqFraming::encodeEnvelope(MoqFraming::MSG_OBJECT, frameBytes)));
            it->second->send(pkt);
            recordForward(subscriberId, f.payloadLength, firstByteTime);
            break;
        }
        case PROTO_UDP: {
            // UDP: re-fragment the object toward the subscriber's learned address.
            auto it = subscriberUdpAddrs.find(subscriberId);
            if (it == subscriberUdpAddrs.end()) {
                EV_WARN << "Subscriber " << subscriberId << " has no UDP address, skipping" << std::endl;
                return;
            }
            auto frags = MoqFraming::fragmentFrame(MoqFraming::MSG_OBJECT, f.trackAlias,
                                                   f.objectId, frameBytes, udpFragmentSize);
            for (auto& d : frags) {
                auto pkt = new inet::Packet("TRACK_OBJ_FWD");
                pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(d));
                udpSocket.sendTo(pkt, it->second.first, it->second.second);
            }
            recordForward(subscriberId, f.payloadLength, firstByteTime);
            break;
        }
        }
    }

    // Update forwarding metrics for one object sent to one subscriber (TCP/UDP paths; the QUIC
    // path records the same metrics inline in doForwardSend).
    void MoqRelayApp::recordForward(const std::string &subscriberId, long payloadLength,
                                    omnetpp::simtime_t firstByteTime)
    {
        forward_count[subscriberId] += 1;
        objectsForwardedTotal++;
        double delay = (omnetpp::simTime() - firstByteTime).dbl();
        emit(relayForwardDelaySignal, delay);
        fwdDelaySum += delay;
        if (delay > fwdDelayMax) fwdDelayMax = delay;
        fwdDelayCount++;
        emit(objectForwardedSignal, (long) payloadLength);
    }

    // Hand the next slice of one object to the subscriber's QUIC socket. Objects are written in
    // quicChunkBytes pieces so a bulk object cannot exceed QUIC's connection-wide send-queue limit
    // on its own and lock out every stream, the latency-critical one included.
    // Bytes sitting in one subscriber connection's QUIC send queue (unsent + sent-but-unacked).
    long MoqRelayApp::quicSendQueueLength(int socketId)
    {
        return quicModule == nullptr ? 0 : (long) quicModule->getSendQueueLength(socketId);
    }

    void MoqRelayApp::doForwardSendChunk(FwdItem &item)
    {
        size_t remaining = item.bytes.size() - item.sentOffset;
        size_t n = std::min(remaining, (size_t) quicChunkBytes);
        auto begin = item.bytes.begin() + item.sentOffset;
        auto pkt = new inet::Packet("TRACK_OBJ_FWD");
        pkt->insertAtBack(inet::makeShared<inet::BytesChunk>(std::vector<uint8_t>(begin, begin + n)));
        // Carry the object's MoQ priority into QUIC's stream scheduler, so the relay's downstream
        // send order follows MoQ send order (draft-14 section 7.2) rather than round-robin.
        // Relays SHOULD prioritize sending Objects per section 8.5.
        item.sock->send(pkt, item.streamId, quicPriorityOf(item.priority));
        item.sentOffset += n;

        if (item.sentOffset < item.bytes.size()) return; // object not fully forwarded yet

        // Fully written to QUIC, but not necessarily delivered: keep it under the delivery timeout
        // so a stale object queued in the transport can still be reclaimed (draft-14 10.4.3).
        if (item.timeout > SIMTIME_ZERO)
            outstanding.push_back({item.subgroup, item.sock, item.streamId, item.trackAlias,
                                   item.createdAt, item.timeout});

        forward_count[item.subscriberId] += 1;
        objectsForwardedTotal++;
        double delay = (omnetpp::simTime() - item.firstByteTime).dbl();
        emit(relayForwardDelaySignal, delay);
        fwdDelaySum += delay;
        if (delay > fwdDelayMax) fwdDelayMax = delay;
        fwdDelayCount++;
        emit(objectForwardedSignal, (long) item.payloadLength);
    }

    // Reset the downstream stream carrying a subgroup (draft-14 section 10.4.3). Abandons the
    // object's in-flight bytes toward this subscriber and, with the stream, the rest of that
    // subgroup: objects already queued are dropped in flushSocket, later ones in forwardToSubscriber.
    void MoqRelayApp::resetDownstreamStream(const FwdItem &item, int errorCode)
    {
        item.sock->resetStream(item.streamId, errorCode);
        resetDownstream[item.subgroup] = errorCode;
        downstreamStreams.erase(item.subgroup);
        downstreamResets++;
    }

    // Send queued objects to one subscriber, highest priority (lowest number) first, until QUIC's
    // send queue is full or the backlog empties.
    void MoqRelayApp::flushSocket(int socketId)
    {
        auto stIt = sockSend.find(socketId);
        if (stIt == sockSend.end()) return;
        SockSendState& st = stIt->second;

        // Bytes written to this socket during the current event. QUIC has not enqueued them yet, so
        // the queue length it reports does not include them; and flushSocket is called once per
        // object, so the tally must be per event, not per call (see MoqPublisherApp).
        if (quicWriteEvent != omnetpp::simTime()) {
            quicWriteEvent = omnetpp::simTime();
            quicBytesThisEvent.clear();
        }
        long& bytesThisEvent = quicBytesThisEvent[socketId];

        while (!st.blocked && st.count > 0) {
            if (quicSendQueueLength(socketId) + bytesThisEvent >= quicSendQueueLimit) break;

            auto it = st.buffer.begin();
            while (it != st.buffer.end() && it->second.empty()) it = st.buffer.erase(it);
            if (it == st.buffer.end()) break;
            FwdItem& item = it->second.front();

            // The subgroup's stream was already reset, so this object went with it -- charged to
            // whatever caused the reset (timeout = MoQ shedding, overflow = buffer artifact).
            auto rdIt = resetDownstream.find(item.subgroup);
            if (rdIt != resetDownstream.end()) {
                if (rdIt->second == MOQ_ERR_SEND_BUFFER_OVERFLOW) relayShedOverflow[item.trackAlias]++;
                else relayShedStale[item.trackAlias]++;
                it->second.pop_front();
                st.count--;
                pendingForwardCount--;
                if (it->second.empty()) st.buffer.erase(it);
                continue;
            }

            // Partial reliability: if the object is already past its delivery timeout, drop it
            // instead of forwarding stale data (MoQ DELIVERY_TIMEOUT, true age from creationTime).
            // If it has already started transmitting, section 10.4.3 requires resetting its
            // subgroup's stream, abandoning its in-flight bytes and the rest of the subgroup.
            bool stale = item.timeout > SIMTIME_ZERO
                         && (omnetpp::simTime() - item.createdAt) > item.timeout;
            if (stale) {
                if (item.sentOffset > 0)
                    resetDownstreamStream(item, MOQ_ERR_DELIVERY_TIMEOUT);
                relayShedStale[item.trackAlias]++;
            }
            else {
                size_t before = item.sentOffset;
                doForwardSendChunk(item);
                bytesThisEvent += (long) (item.sentOffset - before);
                // Do not set st.blocked from this estimate; see MoqPublisherApp::flushSendBuffer.
                // It is cleared only by the drain indication, which never comes if we stay below
                // the low-water mark, so setting it here deadlocks the relay's send path.
            }

            if (stale || item.sentOffset == item.bytes.size()) {
                it->second.pop_front();
                st.count--;
                pendingForwardCount--;
                if (it->second.empty()) st.buffer.erase(it);
            }
        }
        emit(relayQueueDepthSignal, pendingForwardCount);
    }

    void MoqRelayApp::finish()
    {
        recordScalar("objectsForwardedTotal", objectsForwardedTotal);
        // Times a subscriber's forward queue hit the limit, i.e. TOO_FAR_BEHIND teardowns.
        recordScalar("subscribersTooFarBehind", relayDroppedTotal);
        recordScalar("publishDoneSent", publishDoneSent);
        recordScalar("objectsResetByPublisher", upstreamResets);
        recordScalar("subgroupStreamResets", downstreamResets); // RESET_STREAM sent downstream
        recordScalar("quicSendRejected", relayRejected); // must be 0: nonzero = silent loss in QUIC
        recordScalar("objectsPendingForwardAtEnd", pendingForwardCount);
        // Objects dropped past their delivery timeout (partial reliability), per track: this is
        // where the intentional loss should land, and it should land on the bulk track.
        long shedTotal = 0;
        for (auto& s : relayShedStale) {
            recordScalar(("track[" + s.first + "].objectsShedStale").c_str(), s.second);
            shedTotal += s.second;
        }
        recordScalar("objectsShedStale", shedTotal);
        // Collateral of overflow-triggered resets: an artifact of the finite forward buffer, not
        // MoQ shedding, so it is reported apart from objectsShedStale.
        long shedOverflowTotal = 0;
        for (auto& s : relayShedOverflow) {
            recordScalar(("track[" + s.first + "].objectsShedOverflow").c_str(), s.second);
            shedOverflowTotal += s.second;
        }
        recordScalar("objectsShedOverflow", shedOverflowTotal);
        if (fwdDelayCount > 0) {
            recordScalar("meanForwardDelay", fwdDelaySum / fwdDelayCount, "s");
            recordScalar("maxForwardDelay", fwdDelayMax, "s");
        }
        for (auto& fc : forward_count) {
            recordScalar(("subscriber[" + fc.first + "].objectsForwarded").c_str(), fc.second);
        }
        // Localization: objects the relay received from the publisher, per track. Compare with
        // the publisher's objectsSent (pub->relay loss) and the subscribers' counts (relay->sub).
        for (auto& rr : relayResetAfterSend) {
            recordScalar(("track[" + rr.first + "].objectsResetAfterSend").c_str(), rr.second);
        }
        for (auto& rc : recvFromPubByTrack) {
            recordScalar(("track[" + rc.first + "].objectsReceivedFromPub").c_str(), rc.second);
        }
    }

    void MoqRelayApp::socketClosed(inet::QuicSocket *closedSocket)
    {
        EV_INFO << "socketClosed, socketId=" << closedSocket->getSocketId() << std::endl;

        // Release forwarding/receive state held for this connection so it doesn't leak
        // and the queue-depth metric stays accurate.
        int sockId = closedSocket->getSocketId();
        auto fit = sockSend.find(sockId);
        if (fit != sockSend.end()) {
            pendingForwardCount -= fit->second.count;
            sockSend.erase(fit);
        }
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
            // Drop this subscriber's per-subgroup downstream stream state.
            for (auto it = downstreamStreams.begin(); it != downstreamStreams.end(); )
                (it->first.subscriberId == sid) ? it = downstreamStreams.erase(it) : ++it;
            for (auto it = resetDownstream.begin(); it != resetDownstream.end(); )
                (it->first.subscriberId == sid) ? it = resetDownstream.erase(it) : ++it;
            subscriberSockets.erase(sid);
        }
        emit(relayQueueDepthSignal, pendingForwardCount);

        socketMap.removeSocket(closedSocket);
        delete closedSocket;
    }

    // A peer reset one of its streams. Upstream (publisher->relay) this means the object aged
    // past its delivery timeout and was abandoned, so the partial bytes we have for that stream
    // can never form a complete object frame and must be discarded -- otherwise they would be
    // prepended to whatever arrives next and desynchronise the length-prefix parser.
    void MoqRelayApp::socketStreamReset(inet::QuicSocket *socket, uint64_t streamId,
                                        uint64_t applicationErrorCode)
    {
        int sockId = socket->getSocketId();
        auto it = recvBuffers.find({sockId, (long) streamId});
        if (it != recvBuffers.end()) {
            recvBuffers.erase(it);
            upstreamResets++;
            EV_INFO << "Publisher reset stream " << streamId << " (errorCode="
                    << applicationErrorCode << "); dropped partial object" << std::endl;
        }
    }

    void MoqRelayApp::socketSendQueueFull(inet::QuicSocket *socket)
    {
        sockSend[socket->getSocketId()].blocked = true;
        EV_DEBUG << "Send queue full on socketId=" << socket->getSocketId() << std::endl;
    }

    void MoqRelayApp::socketSendQueueDrain(inet::QuicSocket *socket)
    {
        int sockId = socket->getSocketId();
        sockSend[sockId].blocked = false;
        flushSocket(sockId); // QUIC has room again; top the queue back up
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
            switch (proto) {
                case PROTO_QUIC: {
                    inet::ISocket *sock = socketMap.findSocketFor(msg);
                    if (sock) sock->processMessage(msg);
                    else socket.processMessage(msg);
                    break;
                }
                case PROTO_TCP: {
                    inet::ISocket *sock = tcpSocketMap.findSocketFor(msg);
                    if (sock) sock->processMessage(msg);
                    else tcpSocket.processMessage(msg);
                    break;
                }
                case PROTO_UDP:
                    udpSocket.processMessage(msg);
                    break;
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
        case TIMER_TIMEOUT_CHECK:
            checkOutstandingTimeouts();
            for (auto& e : sockSend) flushSocket(e.first); // liveness, as in the publisher
            scheduleAt(omnetpp::simTime() + timeoutCheckInterval, timerTimeoutCheck);
            break;
        }
    }

    // Reset the downstream stream of any object that has outrun its delivery timeout after being
    // written to QUIC. Mirrors MoqPublisherApp::checkOutstandingTimeouts; see the note there about
    // objects that may already have arrived.
    void MoqRelayApp::checkOutstandingTimeouts()
    {
        omnetpp::simtime_t now = omnetpp::simTime();
        for (auto it = outstanding.begin(); it != outstanding.end(); ) {
            if (resetDownstream.count(it->subgroup)) {   // stream already gone
                it = outstanding.erase(it);
                continue;
            }
            if (now - it->createdAt > it->timeout) {
                it->sock->resetStream(it->streamId, MOQ_ERR_DELIVERY_TIMEOUT);
                resetDownstream[it->subgroup] = MOQ_ERR_DELIVERY_TIMEOUT;
                downstreamStreams.erase(it->subgroup);
                downstreamResets++;
                relayResetAfterSend[it->trackAlias]++;
                it = outstanding.erase(it);
                continue;
            }
            ++it;
        }
    }

}
