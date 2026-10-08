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
// Other clients may send longer packets (music bots often send 60 or 120 ms); 120 ms is Opus' maximum.
constexpr int MaxFrameSamples = 5760;
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

    // The duration of a packet in samples per channel, or 0 if it isn't valid Opus.
    static int packetSamples(const uint8_t* packet, int size);

    // Decodes one packet into interleaved stereo samples. Returns the number of samples per channel.
    int decode(const uint8_t* packet, int size, float* stereoOut, int maxSamples);
    // Rebuilds a lost frame of `frameSamples` (its duration) from the forward error correction data
    // carried by the packet after it.
    int recover(const uint8_t* nextPacket, int size, float* stereoOut, int frameSamples);
    // Packet loss concealment for one missing frame of `frameSamples`.
    int conceal(float* stereoOut, int frameSamples);

private:
    OpusDecoder* m_decoder = nullptr;
};
