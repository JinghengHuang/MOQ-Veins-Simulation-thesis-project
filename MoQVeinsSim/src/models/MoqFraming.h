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
#include <algorithm>
#include <omnetpp.h>

namespace moqveinssim {

// Underlying transport selected by the apps' "protocol" parameter. QUIC is the MoQ-native
// transport; TCP and UDP are added for thesis comparison. Only QUIC uses per-stream
// multiplexing (control on stream 0, data on per-track streams); TCP multiplexes control and
// data on one ordered byte stream via an envelope class byte; UDP uses self-describing
// datagrams with application-layer fragmentation.
enum MoqProtocol { PROTO_QUIC, PROTO_TCP, PROTO_UDP };

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

// Try to parse the first complete frame at the front of [data, data+size). On success fills
// `out`, sets `consumed` to the whole frame size (incl. the 4-byte length prefix), returns true.
inline bool tryParse(const uint8_t* data, size_t size, MoqObjectFrame& out, size_t& consumed) {
    if (size < 4) return false;
    uint32_t bodyLen = getU32(data);
    if (size < (size_t) 4 + bodyLen) return false;
    const uint8_t* p = data + 4;
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
inline bool tryParse(const std::vector<uint8_t>& buf, MoqObjectFrame& out, size_t& consumed) {
    return tryParse(buf.data(), buf.size(), out, consumed);
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

inline bool tryParseControl(const uint8_t* data, size_t size, MoqControlFrame& out, size_t& consumed) {
    if (size < 4) return false;
    uint32_t bodyLen = getU32(data);
    if (size < (size_t) 4 + bodyLen) return false;
    const uint8_t* p = data + 4;
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
inline bool tryParseControl(const std::vector<uint8_t>& buf, MoqControlFrame& out, size_t& consumed) {
    return tryParseControl(buf.data(), buf.size(), out, consumed);
}

// ----------------------------------------------------------------------------------------
// Single-stream transports (TCP): control and object frames share one ordered byte stream,
// so each frame is prefixed with a 1-byte message class to tell them apart. QUIC does not use
// this (it separates control vs data by stream id); these helpers are additive.
// ----------------------------------------------------------------------------------------
enum MoqMsgClass : uint8_t {
    MSG_CONTROL = 1,
    MSG_OBJECT  = 2,
};

// Wrap an already length-prefixed frame (from encode()/encodeControl()) with a class byte.
inline std::vector<uint8_t> encodeEnvelope(uint8_t msgClass, const std::vector<uint8_t>& frame) {
    std::vector<uint8_t> out;
    out.reserve(1 + frame.size());
    out.push_back(msgClass);
    out.insert(out.end(), frame.begin(), frame.end());
    return out;
}

// Try to parse one enveloped message at the front of buf.
// Returns: 0 = need more bytes, 1 = control frame (fills `ctrl`), 2 = object frame (fills `obj`).
// On a complete message, `consumed` is set to the whole envelope size (class byte + frame).
inline int tryParseEnvelope(const std::vector<uint8_t>& buf, MoqControlFrame& ctrl,
                            MoqObjectFrame& obj, size_t& consumed) {
    if (buf.empty()) return 0;
    uint8_t cls = buf[0];
    const uint8_t* framePtr = buf.data() + 1;
    size_t frameSize = buf.size() - 1;
    size_t frameConsumed = 0;
    if (cls == MSG_CONTROL) {
        if (!tryParseControl(framePtr, frameSize, ctrl, frameConsumed)) return 0;
        consumed = 1 + frameConsumed;
        return 1;
    } else if (cls == MSG_OBJECT) {
        if (!tryParse(framePtr, frameSize, obj, frameConsumed)) return 0;
        consumed = 1 + frameConsumed;
        return 2;
    }
    // Unknown class: skip the byte so the stream can resync (should not happen).
    consumed = 1;
    return -1;
}

// ----------------------------------------------------------------------------------------
// Datagram transport (UDP): no segmentation in the transport, so a frame larger than one
// datagram is split at the application layer into bounded fragments and reassembled by the
// receiver (mirroring DDS/RTPS DATA_FRAG and ETSI CPM segmentation; IP fragmentation is
// avoided per RFC 8900). Each datagram carries:
//   [u8 msgClass][i64 objectId][u32 fragIndex][u32 fragCount][u32 fragOffset][u32 totalLen]
//   [u32 aliasLen][alias][chunk bytes]
// totalLen is the size of the full inner (length-prefixed) frame; fragOffset is the byte
// offset of this chunk within it (so out-of-order datagrams reassemble correctly); fragIndex
// is used for de-duplication/counting; alias allows routing/keying without reassembling first.
// ----------------------------------------------------------------------------------------
inline std::vector<uint8_t> encodeUdpFragment(uint8_t msgClass, const std::string& trackAlias,
                                              int64_t objectId, uint32_t fragIndex,
                                              uint32_t fragCount, uint32_t fragOffset,
                                              uint32_t totalLen,
                                              const uint8_t* chunk, size_t chunkLen) {
    std::vector<uint8_t> b;
    b.reserve(1 + 8 + 4 * 4 + 4 + trackAlias.size() + chunkLen);
    b.push_back(msgClass);
    putI64(b, objectId);
    putU32(b, fragIndex);
    putU32(b, fragCount);
    putU32(b, fragOffset);
    putU32(b, totalLen);
    putU32(b, (uint32_t) trackAlias.size());
    b.insert(b.end(), trackAlias.begin(), trackAlias.end());
    b.insert(b.end(), chunk, chunk + chunkLen);
    return b;
}

// Split an already-encoded inner frame into datagrams of at most maxChunk inner bytes each.
inline std::vector<std::vector<uint8_t>> fragmentFrame(uint8_t msgClass,
                                                       const std::string& trackAlias,
                                                       int64_t objectId,
                                                       const std::vector<uint8_t>& frame,
                                                       size_t maxChunk) {
    if (maxChunk == 0) maxChunk = 1;
    uint32_t total = (uint32_t) frame.size();
    uint32_t fragCount = (uint32_t) ((frame.size() + maxChunk - 1) / maxChunk);
    if (fragCount == 0) fragCount = 1; // a zero-length frame still travels as one datagram
    std::vector<std::vector<uint8_t>> out;
    out.reserve(fragCount);
    for (uint32_t i = 0; i < fragCount; i++) {
        size_t off = (size_t) i * maxChunk;
        size_t len = std::min(maxChunk, frame.size() - off);
        out.push_back(encodeUdpFragment(msgClass, trackAlias, objectId, i, fragCount,
                                        (uint32_t) off, total, frame.data() + off, len));
    }
    return out;
}

struct MoqUdpFragment {
    uint8_t msgClass = 0;
    int64_t objectId = 0;
    uint32_t fragIndex = 0;
    uint32_t fragCount = 0;
    uint32_t fragOffset = 0;
    uint32_t totalLen = 0;
    std::string trackAlias;
    std::vector<uint8_t> chunk; // this fragment's slice of the inner frame
};

// Parse one whole datagram into a fragment descriptor. Returns false if malformed/too short.
inline bool parseUdpFragment(const uint8_t* data, size_t size, MoqUdpFragment& out) {
    if (size < 1 + 8 + 4 * 4 + 4) return false;
    const uint8_t* p = data;
    out.msgClass = *p; p += 1;
    out.objectId = getI64(p); p += 8;
    out.fragIndex = getU32(p); p += 4;
    out.fragCount = getU32(p); p += 4;
    out.fragOffset = getU32(p); p += 4;
    out.totalLen = getU32(p); p += 4;
    uint32_t aliasLen = getU32(p); p += 4;
    if ((size_t)(p - data) + aliasLen > size) return false;
    out.trackAlias.assign((const char*) p, aliasLen); p += aliasLen;
    out.chunk.assign(p, data + size);
    return true;
}
inline bool parseUdpFragment(const std::vector<uint8_t>& buf, MoqUdpFragment& out) {
    return parseUdpFragment(buf.data(), buf.size(), out);
}

// Reassembles the fragments of one object. Keyed externally (by sender + alias + objectId).
struct UdpObjectReassembler {
    uint32_t fragCount = 0;
    uint32_t totalLen = 0;
    uint32_t receivedFrags = 0;
    std::vector<uint8_t> data;          // reassembled inner frame (sized to totalLen)
    std::vector<bool> have;             // per-fragment presence (de-dup)
    omnetpp::simtime_t firstByteTime = -1;
    bool complete = false;

    // Returns true exactly once, when the last missing fragment fills the object.
    bool add(const MoqUdpFragment& f, omnetpp::simtime_t now) {
        if (firstByteTime < SIMTIME_ZERO) firstByteTime = now;
        if (fragCount == 0) {
            fragCount = f.fragCount;
            totalLen = f.totalLen;
            data.assign(totalLen, 0);
            have.assign(fragCount, false);
        }
        if (f.fragIndex >= fragCount) return false;       // malformed
        if (have[f.fragIndex]) return false;              // duplicate
        have[f.fragIndex] = true;
        receivedFrags++;
        if ((size_t) f.fragOffset + f.chunk.size() <= data.size())
            std::copy(f.chunk.begin(), f.chunk.end(), data.begin() + f.fragOffset);
        if (receivedFrags == fragCount && !complete) {
            complete = true;
            return true;
        }
        return false;
    }
};

} // namespace MoqFraming
} // namespace moqveinssim
