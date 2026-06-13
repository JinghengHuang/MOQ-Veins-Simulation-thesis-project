/* --- MoqFraming.h --- */
/*
 * MoQ-style self-delimiting object framing carried over a QUIC stream as raw bytes.
 *
 * Each object is encoded as a length-prefixed record:
 *   [uint32 bodyLen]
 *   body: [int64 trackId][int64 groupId][int64 objectId][int64 priority]
 *         [int64 payloadLength][int64 creationTimeRaw]
 *         [uint32 aliasLen][aliasLen bytes trackAlias]
 *         [payloadLength bytes payload]   // zero-filled; content is irrelevant in sim
 *   bodyLen = 6*8 + 4 + aliasLen + payloadLength
 *
 * Because every object is a homogeneous BytesChunk, consecutive objects on a stream
 * coalesce into one BytesChunk (instead of a SequenceChunk, which INET QUIC mis-parses).
 * The receiver accumulates the byte stream and delimits objects via bodyLen, mirroring
 * how MoQ Transport frames objects on a subgroup/group stream.
 */
#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <omnetpp.h>

namespace moqveinssim {

struct MoqObjectFrame {
    long trackId = 0;
    long groupId = 0;
    long objectId = 0;
    long priority = 0;
    long payloadLength = 0;
    omnetpp::simtime_t creationTime = 0;
    std::string trackAlias;
};

// Per-stream byte accumulator. frameStartTime is the arrival time of the first byte
// currently buffered (i.e. the start of the next object), used for object completion time.
struct StreamReassembler {
    std::vector<uint8_t> data;
    omnetpp::simtime_t frameStartTime = -1;
};

// Control messages, also carried as length-prefixed byte frames (so they coalesce like
// data frames and never form a SequenceChunk). All control travels on stream 0.
enum MoqControlType : uint8_t {
    CTRL_ANNOUNCE     = 1, // publisher -> relay: register a track
    CTRL_SUBSCRIBE    = 2, // subscriber -> relay: request a track
    CTRL_SUBSCRIBE_OK = 3, // relay -> publisher: start sending a track
};

struct MoqControlFrame {
    uint8_t type = 0;
    long trackId = 0;
    long publisherId = 0;
    long priority = 0;
    long payloadSize = 0;
    long startObjectId = 0;
    long subscriberPriority = 0;
    omnetpp::simtime_t sendInterval = 0;
    std::string trackNamespace;
    std::string trackName;
    std::string trackAlias;
    std::string subscriberId;
};

namespace MoqFraming {

inline void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xff));
    b.push_back((uint8_t)((v >> 8) & 0xff));
    b.push_back((uint8_t)((v >> 16) & 0xff));
    b.push_back((uint8_t)((v >> 24) & 0xff));
}
inline void putI64(std::vector<uint8_t>& b, int64_t v) {
    uint64_t u = (uint64_t) v;
    for (int i = 0; i < 8; i++) { b.push_back((uint8_t)(u & 0xff)); u >>= 8; }
}
inline uint32_t getU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline int64_t getI64(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--) u = (u << 8) | p[i];
    return (int64_t) u;
}

// Encode an object into a self-delimiting frame (header fields + zero-filled payload).
inline std::vector<uint8_t> encode(const MoqObjectFrame& f) {
    std::vector<uint8_t> body;
    body.reserve(6 * 8 + 4 + f.trackAlias.size() + (size_t) f.payloadLength);
    putI64(body, f.trackId);
    putI64(body, f.groupId);
    putI64(body, f.objectId);
    putI64(body, f.priority);
    putI64(body, f.payloadLength);
    putI64(body, f.creationTime.raw());
    putU32(body, (uint32_t) f.trackAlias.size());
    body.insert(body.end(), f.trackAlias.begin(), f.trackAlias.end());
    body.insert(body.end(), (size_t) f.payloadLength, (uint8_t) 0);

    std::vector<uint8_t> frame;
    frame.reserve(4 + body.size());
    putU32(frame, (uint32_t) body.size());
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

// Try to parse the first complete frame at the front of buf. On success fills `out`,
// sets `consumed` to the whole frame size (incl. the 4-byte length prefix), returns true.
inline bool tryParse(const std::vector<uint8_t>& buf, MoqObjectFrame& out, size_t& consumed) {
    if (buf.size() < 4) return false;
    uint32_t bodyLen = getU32(buf.data());
    if (buf.size() < (size_t) 4 + bodyLen) return false;
    const uint8_t* p = buf.data() + 4;
    out.trackId = getI64(p); p += 8;
    out.groupId = getI64(p); p += 8;
    out.objectId = getI64(p); p += 8;
    out.priority = getI64(p); p += 8;
    out.payloadLength = getI64(p); p += 8;
    out.creationTime = omnetpp::SimTime::fromRaw(getI64(p)); p += 8;
    uint32_t aliasLen = getU32(p); p += 4;
    out.trackAlias.assign((const char*) p, aliasLen);
    consumed = (size_t) 4 + bodyLen;
    return true;
}

inline void putStr(std::vector<uint8_t>& b, const std::string& s) {
    putU32(b, (uint32_t) s.size());
    b.insert(b.end(), s.begin(), s.end());
}
inline std::string getStr(const uint8_t*& p) {
    uint32_t n = getU32(p); p += 4;
    std::string s((const char*) p, n); p += n;
    return s;
}

// Encode a control message as a length-prefixed byte frame (uniform layout for all types).
inline std::vector<uint8_t> encodeControl(const MoqControlFrame& c) {
    std::vector<uint8_t> body;
    body.push_back(c.type);
    putI64(body, c.trackId);
    putI64(body, c.publisherId);
    putI64(body, c.priority);
    putI64(body, c.payloadSize);
    putI64(body, c.startObjectId);
    putI64(body, c.subscriberPriority);
    putI64(body, c.sendInterval.raw());
    putStr(body, c.trackNamespace);
    putStr(body, c.trackName);
    putStr(body, c.trackAlias);
    putStr(body, c.subscriberId);

    std::vector<uint8_t> frame;
    frame.reserve(4 + body.size());
    putU32(frame, (uint32_t) body.size());
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

inline bool tryParseControl(const std::vector<uint8_t>& buf, MoqControlFrame& out, size_t& consumed) {
    if (buf.size() < 4) return false;
    uint32_t bodyLen = getU32(buf.data());
    if (buf.size() < (size_t) 4 + bodyLen) return false;
    const uint8_t* p = buf.data() + 4;
    out.type = *p; p += 1;
    out.trackId = getI64(p); p += 8;
    out.publisherId = getI64(p); p += 8;
    out.priority = getI64(p); p += 8;
    out.payloadSize = getI64(p); p += 8;
    out.startObjectId = getI64(p); p += 8;
    out.subscriberPriority = getI64(p); p += 8;
    out.sendInterval = omnetpp::SimTime::fromRaw(getI64(p)); p += 8;
    out.trackNamespace = getStr(p);
    out.trackName = getStr(p);
    out.trackAlias = getStr(p);
    out.subscriberId = getStr(p);
    consumed = (size_t) 4 + bodyLen;
    return true;
}

} // namespace MoqFraming
} // namespace moqveinssim
