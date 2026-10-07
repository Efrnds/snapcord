#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

// Reorders incoming Opus packets of one speaker and smooths out network jitter. Packets are pushed from
// the network thread and pulled, one 20 ms frame at a time, from the playback thread.
class JitterBuffer
{
public:
    enum class Result {
        Packet,  // `packet` holds the next frame
        Recover, // a frame is missing, but `packet` holds the next one, whose FEC data can rebuild it
        Lost,    // a frame is missing: the decoder should conceal it
        Idle,    // nothing to play (the speaker is silent or still buffering)
    };

    void push(uint16_t sequence, std::vector<uint8_t> packet);
    Result pop(std::vector<uint8_t>& packet);

private:
    int64_t unwrap(uint16_t sequence);

    std::mutex m_mutex;
    std::map<int64_t, std::vector<uint8_t>> m_packets;
    int64_t m_lastExtended = -1;
    int64_t m_next = 0;
    bool m_playing = false;
    int m_consecutiveLosses = 0;
};
