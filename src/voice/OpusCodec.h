#pragma once

#include <cstdint>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

// Discord voice is Opus, 48 kHz stereo, 20 ms frames.
namespace OpusFormat {
constexpr int SampleRate = 48000;
constexpr int Channels = 2;
constexpr int FrameSamples = 960; // per channel, 20 ms
constexpr int MaxPacketSize = 1276;
} // namespace OpusFormat

class OpusEncoderWrapper
{
public:
    OpusEncoderWrapper();
    ~OpusEncoderWrapper();

    OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
    OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

    bool isValid() const { return m_encoder != nullptr; }
    void setBitrate(int bitsPerSecond);

    // Encodes one 20 ms frame of interleaved stereo samples. Returns the packet size, or 0 on failure.
    int encode(const float* stereoFrame, uint8_t* packet, int packetCapacity);

private:
    OpusEncoder* m_encoder = nullptr;
};

class OpusDecoderWrapper
{
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();

    OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
    OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

    bool isValid() const { return m_decoder != nullptr; }

    // Decodes one packet into interleaved stereo samples. Returns the number of samples per channel.
    int decode(const uint8_t* packet, int size, float* stereoOut, int maxSamples);
    // Rebuilds a lost 20 ms frame from the forward error correction data carried by the packet after it.
    int recover(const uint8_t* nextPacket, int size, float* stereoOut, int maxSamples);
    // Packet loss concealment for one missing 20 ms frame.
    int conceal(float* stereoOut, int maxSamples);

private:
    OpusDecoder* m_decoder = nullptr;
};
