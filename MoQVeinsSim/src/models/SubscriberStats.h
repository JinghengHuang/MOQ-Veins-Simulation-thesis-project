/* --- SubscriberStats.h --- */
/*
 * Per-track receive statistics, shared by the MoQ and MQTT subscribers.
 *
 * The point of sharing is comparability: RQ2 compares MoQ against MQTT/TCP/UDP, and that only
 * means anything if "latency", "loss" and "deadline miss" are computed identically on both
 * sides. Keeping one implementation makes that structural rather than a matter of discipline.
 */
#pragma once

#include <string>
#include <cmath>
#include <unordered_map>
#include <omnetpp.h>

namespace moqveinssim {

struct SubTrackStat {
    long received = 0;
    long bytes = 0;
    long highestSeq = -1;
    long lowestSeq = -1;
    omnetpp::simtime_t firstRecv = -1;
    omnetpp::simtime_t lastRecv = -1;
    double lastLatency = -1;
    long deadlineMisses = 0;
    omnetpp::simtime_t deadline = 0;
    double latencySum = 0;
    double latencyMax = 0;
    double jitterSum = 0;
    long jitterCount = 0;

    // Returns the jitter |L_i - L_{i-1}|, or -1 for the first message of the track.
    double add(long sequence, long payloadBytes, double latency, omnetpp::simtime_t now) {
        received++;
        bytes += payloadBytes;

        latencySum += latency;
        if (latency > latencyMax) latencyMax = latency;

        double jitter = -1;
        if (lastLatency >= 0) {
            jitter = std::fabs(latency - lastLatency);
            jitterSum += jitter;
            jitterCount++;
        }
        lastLatency = latency;

        if (deadline > SIMTIME_ZERO && latency > deadline.dbl()) deadlineMisses++;

        if (lowestSeq < 0 || sequence < lowestSeq) lowestSeq = sequence;
        if (sequence > highestSeq) highestSeq = sequence;
        if (firstRecv < SIMTIME_ZERO) firstRecv = now;
        lastRecv = now;
        return jitter;
    }

    bool missedDeadline(double latency) const {
        return deadline > SIMTIME_ZERO && latency > deadline.dbl();
    }
};

// Writes the per-track scalars. Names must not diverge between subscribers, or the protocol
// comparison silently compares different quantities.
inline void recordTrackStats(omnetpp::cSimpleModule& mod, const std::string& trackName,
                             const SubTrackStat& ts) {
    std::string prefix = "track[" + trackName + "].";
    mod.recordScalar((prefix + "objectsReceived").c_str(), ts.received);
    mod.recordScalar((prefix + "bytesReceived").c_str(), ts.bytes, "B");

    mod.recordScalar((prefix + "firstObjId").c_str(), ts.lowestSeq);
    mod.recordScalar((prefix + "lastObjId").c_str(), ts.highestSeq);
    long rangeSpan = ts.highestSeq - ts.lowestSeq + 1;
    mod.recordScalar((prefix + "rangeSpan").c_str(), rangeSpan);
    mod.recordScalar((prefix + "internalGaps").c_str(), rangeSpan - ts.received);

    // Counted from sequence 0, so it charges the subscriber for everything published before its
    // car spawned. Kept for continuity; gapLossRatio is the one to reason with.
    long expected = ts.highestSeq + 1;
    long lost = (expected > ts.received) ? (expected - ts.received) : 0;
    mod.recordScalar((prefix + "objectsExpected").c_str(), expected);
    mod.recordScalar((prefix + "objectsLost").c_str(), lost);
    if (expected > 0)
        mod.recordScalar((prefix + "lossRatio").c_str(), (double) lost / (double) expected);

    // Loss within the window the subscriber was actually subscribed for.
    if (ts.received > 0 && rangeSpan > 0)
        mod.recordScalar((prefix + "gapLossRatio").c_str(),
                         (double) (rangeSpan - ts.received) / (double) rangeSpan);

    double span = (ts.lastRecv - ts.firstRecv).dbl();
    if (span > 0)
        mod.recordScalar((prefix + "throughput").c_str(), ts.bytes * 8.0 / span, "bps");

    mod.recordScalar((prefix + "deadlineMisses").c_str(), ts.deadlineMisses);
    if (ts.received > 0) {
        mod.recordScalar((prefix + "deadlineMissRatio").c_str(),
                         (double) ts.deadlineMisses / (double) ts.received);
        mod.recordScalar((prefix + "meanLatency").c_str(), ts.latencySum / ts.received, "s");
        mod.recordScalar((prefix + "maxLatency").c_str(), ts.latencyMax, "s");
    }
    if (ts.jitterCount > 0)
        mod.recordScalar((prefix + "meanJitter").c_str(), ts.jitterSum / ts.jitterCount, "s");
}

} // namespace moqveinssim
