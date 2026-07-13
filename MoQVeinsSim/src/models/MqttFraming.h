/* --- MqttFraming.h --- */
/*
 * MQTT v5.0 control packet encoding (OASIS MQTT v5.0, chapter 2-3).
 *
 * A control packet is: fixed header [type|flags][Remaining Length varint], then a variable
 * header and payload. Integers are big-endian and strings are 2-byte-length-prefixed UTF-8,
 * which is why this does not share primitives with MoqFraming (whose custom framing is
 * little-endian).
 *
 * Implemented: CONNECT/CONNACK, PUBLISH/PUBACK, SUBSCRIBE/SUBACK, DISCONNECT, with the Message
 * Expiry Interval property. QoS 0 and 1 only. Not implemented (none affect steady-state latency
 * or throughput): QoS 2, retained messages, wills, session state, auth, topic aliases, wildcards.
 */
#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <omnetpp.h>

namespace moqveinssim {

// MQTT control packet types (v5.0 section 2.1.2).
enum MqttPacketType : uint8_t {
    MQTT_CONNECT     = 1,
    MQTT_CONNACK     = 2,
    MQTT_PUBLISH     = 3,
    MQTT_PUBACK      = 4,
    MQTT_SUBSCRIBE   = 8,
    MQTT_SUBACK      = 9,
    MQTT_DISCONNECT  = 14,
};

// Property identifiers we use (v5.0 section 2.2.2.2).
enum MqttProperty : uint8_t {
    MQTT_PROP_MESSAGE_EXPIRY_INTERVAL = 0x02,
};

// One decoded application message. The MQTT payload carries a small application header
// (creation time, sequence number) so the subscriber can measure latency and detect gaps, the
// same role the object header plays in MoQ. A real CPM carries a generation time for the same
// reason.
struct MqttMessage {
    std::string topic;
    uint16_t packetId = 0;
    uint8_t qos = 0;
    uint32_t messageExpiryInterval = 0;   // seconds; 0 = absent
    long sequence = 0;
    long payloadLength = 0;
    omnetpp::simtime_t creationTime = 0;
};

// Byte accumulator for one connection: MQTT runs over an ordered byte stream (TCP, or a single
// QUIC stream), so packets must be delimited by Remaining Length.
struct MqttStreamBuffer {
    std::vector<uint8_t> data;
};

namespace MqttFraming {

inline void putU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }

inline void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)(v >> 8));
    b.push_back((uint8_t)(v & 0xff));
}

inline void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v >> 24));
    b.push_back((uint8_t)((v >> 16) & 0xff));
    b.push_back((uint8_t)((v >> 8) & 0xff));
    b.push_back((uint8_t)(v & 0xff));
}

inline void putI64(std::vector<uint8_t>& b, int64_t v) {
    uint64_t u = (uint64_t) v;
    for (int i = 7; i >= 0; i--) b.push_back((uint8_t)((u >> (i * 8)) & 0xff));
}

inline void putString(std::vector<uint8_t>& b, const std::string& s) {
    putU16(b, (uint16_t) s.size());
    b.insert(b.end(), s.begin(), s.end());
}

// Variable Byte Integer (section 1.5.5): 7 bits per byte, high bit is a continuation flag.
inline void putVarInt(std::vector<uint8_t>& b, uint32_t v) {
    do {
        uint8_t byte = v % 128;
        v /= 128;
        if (v > 0) byte |= 0x80;
        b.push_back(byte);
    } while (v > 0);
}

inline uint16_t getU16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

inline uint32_t getU32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline int64_t getI64(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; i++) u = (u << 8) | p[i];
    return (int64_t) u;
}

// Returns false if the buffer does not yet hold a complete Variable Byte Integer.
inline bool getVarInt(const uint8_t* p, size_t avail, uint32_t& out, size_t& used) {
    uint32_t multiplier = 1;
    out = 0;
    used = 0;
    for (int i = 0; i < 4; i++) {
        if (used >= avail) return false;
        uint8_t byte = p[used++];
        out += (byte & 0x7f) * multiplier;
        if ((byte & 0x80) == 0) return true;
        multiplier *= 128;
    }
    return false; // malformed: more than 4 bytes
}

inline size_t varIntSize(uint32_t v) {
    size_t n = 0;
    do { n++; v /= 128; } while (v > 0);
    return n;
}

// Wrap a variable header + payload in the fixed header (section 2.1.1).
inline std::vector<uint8_t> frame(uint8_t type, uint8_t flags, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> pkt;
    pkt.reserve(1 + 4 + body.size());
    putU8(pkt, (uint8_t)((type << 4) | (flags & 0x0f)));
    putVarInt(pkt, (uint32_t) body.size());
    pkt.insert(pkt.end(), body.begin(), body.end());
    return pkt;
}

inline std::vector<uint8_t> encodeConnect(const std::string& clientId) {
    std::vector<uint8_t> b;
    putString(b, "MQTT");
    putU8(b, 5);      // protocol version 5
    putU8(b, 0x02);   // connect flags: clean start
    putU16(b, 0);     // keep alive: disabled
    putVarInt(b, 0);  // no properties
    putString(b, clientId);
    return frame(MQTT_CONNECT, 0, b);
}

inline std::vector<uint8_t> encodeConnack() {
    std::vector<uint8_t> b;
    putU8(b, 0);      // no session present
    putU8(b, 0);      // reason code: success
    putVarInt(b, 0);  // no properties
    return frame(MQTT_CONNACK, 0, b);
}

// PUBLISH (section 3.3). messageExpiryInterval is in SECONDS per the spec, and is omitted when 0.
inline std::vector<uint8_t> encodePublish(const MqttMessage& m) {
    std::vector<uint8_t> b;
    putString(b, m.topic);
    if (m.qos > 0) putU16(b, m.packetId);

    std::vector<uint8_t> props;
    if (m.messageExpiryInterval > 0) {
        putU8(props, MQTT_PROP_MESSAGE_EXPIRY_INTERVAL);
        putU32(props, m.messageExpiryInterval);
    }
    putVarInt(b, (uint32_t) props.size());
    b.insert(b.end(), props.begin(), props.end());

    // Application payload: timestamp + sequence, then zero-filled bulk (content is irrelevant).
    putI64(b, m.creationTime.raw());
    putI64(b, m.sequence);
    b.insert(b.end(), (size_t) m.payloadLength, (uint8_t) 0);

    uint8_t flags = (uint8_t)((m.qos & 0x03) << 1);
    return frame(MQTT_PUBLISH, flags, b);
}

inline std::vector<uint8_t> encodePuback(uint16_t packetId) {
    std::vector<uint8_t> b;
    putU16(b, packetId);
    putU8(b, 0);      // reason code: success
    putVarInt(b, 0);
    return frame(MQTT_PUBACK, 0, b);
}

// SUBSCRIBE (section 3.8). Bits 0-1 of the options byte are the requested maximum QoS.
inline std::vector<uint8_t> encodeSubscribe(uint16_t packetId, const std::vector<std::string>& topics,
                                            uint8_t qos) {
    std::vector<uint8_t> b;
    putU16(b, packetId);
    putVarInt(b, 0);
    for (const auto& t : topics) {
        putString(b, t);
        putU8(b, (uint8_t)(qos & 0x03));
    }
    return frame(MQTT_SUBSCRIBE, 0x02, b); // SUBSCRIBE requires flags 0010
}

inline std::vector<uint8_t> encodeSuback(uint16_t packetId, size_t topicCount, uint8_t qos) {
    std::vector<uint8_t> b;
    putU16(b, packetId);
    putVarInt(b, 0);
    for (size_t i = 0; i < topicCount; i++) putU8(b, qos); // reason code = granted QoS
    return frame(MQTT_SUBACK, 0, b);
}

// A parsed control packet. `body` is the variable header + payload, i.e. the packet without its
// fixed header, so each handler can decode the parts it cares about.
struct MqttPacket {
    uint8_t type = 0;
    uint8_t flags = 0;
    std::vector<uint8_t> body;
};

// Pull one whole control packet off the front of the stream buffer. Returns false if incomplete.
inline bool tryParse(const std::vector<uint8_t>& buf, MqttPacket& out, size_t& consumed) {
    if (buf.empty()) return false;
    uint32_t remaining = 0;
    size_t lenBytes = 0;
    if (!getVarInt(buf.data() + 1, buf.size() - 1, remaining, lenBytes)) return false;

    size_t total = 1 + lenBytes + remaining;
    if (buf.size() < total) return false;

    out.type = (uint8_t)(buf[0] >> 4);
    out.flags = (uint8_t)(buf[0] & 0x0f);
    const uint8_t* start = buf.data() + 1 + lenBytes;
    out.body.assign(start, start + remaining);
    consumed = total;
    return true;
}

// Decode a PUBLISH body (its fixed-header flags carry the QoS).
inline bool parsePublish(const MqttPacket& pkt, MqttMessage& out) {
    const std::vector<uint8_t>& b = pkt.body;
    size_t i = 0;
    if (b.size() < 2) return false;

    uint16_t topicLen = getU16(b.data());
    i = 2;
    if (b.size() < i + topicLen) return false;
    out.topic.assign((const char*) b.data() + i, topicLen);
    i += topicLen;

    out.qos = (uint8_t)((pkt.flags >> 1) & 0x03);
    if (out.qos > 0) {
        if (b.size() < i + 2) return false;
        out.packetId = getU16(b.data() + i);
        i += 2;
    }

    uint32_t propLen = 0;
    size_t propLenBytes = 0;
    if (!getVarInt(b.data() + i, b.size() - i, propLen, propLenBytes)) return false;
    i += propLenBytes;

    size_t propEnd = i + propLen;
    if (b.size() < propEnd) return false;
    while (i < propEnd) {
        uint8_t id = b[i++];
        if (id == MQTT_PROP_MESSAGE_EXPIRY_INTERVAL) {
            if (b.size() < i + 4) return false;
            out.messageExpiryInterval = getU32(b.data() + i);
            i += 4;
        }
        else return false; // we emit no other properties
    }

    if (b.size() < i + 16) return false;
    out.creationTime = omnetpp::SimTime::fromRaw(getI64(b.data() + i)); i += 8;
    out.sequence = (long) getI64(b.data() + i); i += 8;
    out.payloadLength = (long) (b.size() - i);
    return true;
}

inline bool parseSubscribe(const MqttPacket& pkt, uint16_t& packetId,
                           std::vector<std::string>& topics, uint8_t& qos) {
    const std::vector<uint8_t>& b = pkt.body;
    if (b.size() < 2) return false;
    packetId = getU16(b.data());
    size_t i = 2;

    uint32_t propLen = 0;
    size_t propLenBytes = 0;
    if (!getVarInt(b.data() + i, b.size() - i, propLen, propLenBytes)) return false;
    i += propLenBytes + propLen;

    qos = 0;
    while (i + 2 <= b.size()) {
        uint16_t len = getU16(b.data() + i);
        i += 2;
        if (b.size() < i + len + 1) return false;
        topics.emplace_back((const char*) b.data() + i, len);
        i += len;
        qos = (uint8_t)(b[i++] & 0x03);
    }
    return !topics.empty();
}

inline bool parsePuback(const MqttPacket& pkt, uint16_t& packetId) {
    if (pkt.body.size() < 2) return false;
    packetId = getU16(pkt.body.data());
    return true;
}

} // namespace MqttFraming
} // namespace moqveinssim
