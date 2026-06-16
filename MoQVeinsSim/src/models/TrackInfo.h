/**
 * Relay track/publisher/subscriber relation structs
 */
#pragma once

#include <vector>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"
namespace moqveinssim
{
    
    inline void hashCombine(std::size_t& seed, std::size_t value)
    {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct TrackMeta{
        int publisherId = -1;
        std::string trackNamespace = "";
        int trackId = -1;
        std::string trackName = "";
        std::string trackAlias = "";
        int packetSize = -1;
        omnetpp::simtime_t sendInterval = omnetpp::simTime();
        int priority = 0;
        long nextObjectId = 0;
        omnetpp::simtime_t deadline = 0; // per-track latency deadline (0 = no deadline)
        omnetpp::cMessage *timer = nullptr;


        bool operator==(const TrackMeta& other) const {
            return publisherId == other.publisherId
                && trackNamespace == other.trackNamespace
                && trackId == other.trackId
                && trackName == other.trackName
                && trackAlias == other.trackAlias
                && packetSize == other.packetSize
                && priority == other.priority
                && nextObjectId == other.nextObjectId;
        }
    };
    
    struct TrackMetaHash {
        std::size_t operator()(const TrackMeta& key) const {
            std::size_t h1 = std::hash<std::string>{}(key.trackNamespace);
            std::size_t h2 = std::hash<std::string>{}(key.trackName);
            return h1 ^ (h2 << 1);
        }
    };
    struct TrackKey {
        std::string trackNamespace = "";
        std::string trackName = "";
        TrackKey(std::string tNamespace, std::string tName){
            trackNamespace = tNamespace;
            trackName = tName;
        }
        bool operator==(const TrackKey& other) const {
            return trackNamespace == other.trackNamespace
                && trackName == other.trackName;
        }
    };
    struct TrackKeyHash {
        std::size_t operator()(const TrackKey& key) const {
            std::size_t h1 = std::hash<std::string>{}(key.trackNamespace);
            std::size_t h2 = std::hash<std::string>{}(key.trackName);
            return h1 ^ (h2 << 1);
        }
    };


    struct UpstreamStreamKey {
        int publisherConnectionId = -1;
        int streamId = -1;
    };

    struct RelayItem {
        TrackKey track;
        int subscriberId = -1;
        int priority = -1;
        omnetpp::simtime_t enqueueTime = omnetpp::simTime();
        inet::Packet *packet = nullptr;
    };
    struct DownstreamTrackKey {
        int subscriberId = -1;
        TrackKey track;

        bool operator==(const DownstreamTrackKey& other) const {
            return subscriberId == other.subscriberId
                && track == other.track;
        }
    };
    struct StreamBinding {
        inet::QuicSocket *socket = nullptr;
        int peerId = -1;          // publisherId or subscriberId
        int streamId = -1;

        TrackKey track;

        enum class Direction {
            UPSTREAM,    // Publisher -> Relay
            DOWNSTREAM   // Relay -> Subscriber
        };

        Direction direction;

        bool operator==(const StreamBinding& other) const {
            return peerId == other.peerId
                && streamId == other.streamId
                && track == other.track;
        }
    };
    
    struct StreamBindingHash {
        std::size_t operator()(const StreamBinding& key) const {
            std::size_t seed = 0;
            hashCombine(seed, std::hash<int>{}(key.peerId));
            hashCombine(seed, std::hash<int>{}(key.streamId));
            hashCombine(seed, TrackKeyHash{}(key.track));
            return seed;
        }
    };
    struct SubscriberTrackKey {
        int subscriberId = -1;
        TrackKey track;

        bool operator==(const SubscriberTrackKey& other) const {
            return subscriberId == other.subscriberId && track == other.track;
        }
    };
    struct SubscriberTrackKeyHash {
        std::size_t operator()(const SubscriberTrackKey& key) const {
            std::size_t seed = 0;
            hashCombine(seed, std::hash<int>{}(key.subscriberId));
            hashCombine(seed, TrackKeyHash{}(key.track));
            return seed;
        }
    };

} // namespace moqveinssim
