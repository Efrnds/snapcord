#include "voice/Rtp.h"

namespace Rtp {

std::optional<Header> parse(const uint8_t* packet, size_t size)
{
    if (size < FixedHeaderSize)
        return std::nullopt;
    // RTP version 2 only; this also filters out IP discovery and UDP ping packets.
    if ((packet[0] >> 6) != 2)
        return std::nullopt;
    // RTCP shares the socket; its packet types (200-206) sit where RTP has marker + payload type.
    if (packet[1] >= 200 && packet[1] <= 206)
        return std::nullopt;

    Header header;
    header.payloadType = packet[1] & 0x7f;
    header.sequence = readU16(packet + 2);
    header.timestamp = readU32(packet + 4);
    header.ssrc = readU32(packet + 8);

    const size_t csrcCount = packet[0] & 0x0f;
    const bool hasExtension = (packet[0] & 0x10) != 0;
    header.clearSize = FixedHeaderSize + csrcCount * 4;
    if (hasExtension) {
        if (size < header.clearSize + 4)
            return std::nullopt;
        // Preamble: 16-bit profile (0xBEDE) followed by the body length in 32-bit words.
        header.extensionBodySize = size_t(readU16(packet + header.clearSize + 2)) * 4;
        header.clearSize += 4;
    }
    if (size < header.clearSize)
        return std::nullopt;
    return header;
}

void writeHeader(uint8_t* out, uint16_t sequence, uint32_t timestamp, uint32_t ssrc)
{
    out[0] = 0x80; // version 2, no padding, no extension, no CSRCs
    out[1] = OpusPayloadType;
    writeU16(out + 2, sequence);
    writeU32(out + 4, timestamp);
    writeU32(out + 8, ssrc);
}

} // namespace Rtp
