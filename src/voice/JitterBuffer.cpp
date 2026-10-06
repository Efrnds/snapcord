#include "voice/JitterBuffer.h"

namespace {

// Frames buffered before playback starts (40 ms): absorbs jitter at a small latency cost.
constexpr size_t StartDepth = 2;
// Above this depth, playback skips ahead to keep the latency low.
constexpr size_t MaxDepth = 10;
// After this many concealed frames in a row, assume the speaker stopped and go idle.
constexpr int MaxConcealedFrames = 5;

} // namespace

int64_t JitterBuffer::unwrap(uint16_t sequence)
{
    if (m_lastExtended < 0) {
        m_lastExtended = sequence;
        return sequence;
    }
    // Interpret the 16-bit sequence relative to the last one, so wrap-arounds keep counting up.
    const int16_t delta = static_cast<int16_t>(sequence - static_cast<uint16_t>(m_lastExtended));
    const int64_t extended = m_lastExtended + delta;
    if (extended > m_lastExtended)
        m_lastExtended = extended;
    return extended;
}

void JitterBuffer::push(uint16_t sequence, std::vector<uint8_t> packet)
{
    std::lock_guard lock(m_mutex);
    const int64_t extended = unwrap(sequence);
    if (m_playing && extended < m_next)
        return; // too late, already concealed
    m_packets[extended] = std::move(packet);

    if (m_packets.size() > MaxDepth) {
        // Fell too far behind (e.g. after a hiccup): drop the oldest frames.
        while (m_packets.size() > StartDepth)
            m_packets.erase(m_packets.begin());
        m_next = m_packets.begin()->first;
    }
}

JitterBuffer::Result JitterBuffer::pop(std::vector<uint8_t>& packet)
{
    std::lock_guard lock(m_mutex);
    if (!m_playing) {
        if (m_packets.size() < StartDepth)
            return Result::Idle;
        m_playing = true;
        m_next = m_packets.begin()->first;
        m_consecutiveLosses = 0;
    }

    auto it = m_packets.find(m_next);
    if (it != m_packets.end()) {
        packet = std::move(it->second);
        m_packets.erase(it);
        ++m_next;
        m_consecutiveLosses = 0;
        return Result::Packet;
    }

    ++m_next;
    if (m_packets.empty() && ++m_consecutiveLosses > MaxConcealedFrames) {
        m_playing = false;
        return Result::Idle;
    }
    return Result::Lost;
}
