#pragma once

#include <vector>
#include <omnetpp.h>
#include <unordered_map>
#include "inet/transportlayer/contract/quic/QuicSocket.h"
#include "inet/applications/base/ApplicationBase.h"
#include "inet/networklayer/common/L3Address.h"

namespace moqveinssim
{
    struct TrackMeta{
        int publisherId;
        std::string trackNamespace;
        int trackId;
        std::string trackName;
        std::string trackAlias;
        int packetSize;
        omnetpp::simtime_t sendInterval;
        int priority;
        long nextObjectId = 0;
        omnetpp::cMessage *timer = nullptr;
        TrackMeta() = default;
    };
} // namespace moqveinssim
