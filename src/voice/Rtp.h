#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Rtp {

constexpr uint8_t OpusPayloadType = 120;
constexpr size_t FixedHeaderSize = 12;

struct Header
{
    uint8_t payloadType = 0;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    // Size of the clear part of the packet in "rtpsize" encryption modes: the fixed header, CSRCs and,
    // when present, the 4-byte extension preamble. The extension body itself is encrypted.
    size_t clearSize = 0;
    // Length in bytes of the (encrypted) extension body that precedes the payload after decryption.
    size_t extensionBodySize = 0;
};

// Parses an incoming RTP packet. Returns nothing for RTCP, Discord control packets and malformed data.
std::optional<Header> parse(const uint8_t* packet, size_t size);

void writeHeader(uint8_t* out, uint16_t sequence, uint32_t timestamp, uint32_t ssrc);

inline uint16_t readU16(const uint8_t* p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t readU32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline void writeU16(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value);
}

inline void writeU32(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

} // namespace Rtp
