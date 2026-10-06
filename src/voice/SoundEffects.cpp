#include "voice/SoundEffects.h"

#include <cmath>
#include <numbers>

namespace {

constexpr int SampleRate = AudioEngine::SampleRate;

struct Note
{
    double frequency; // 0 = rest
    int milliseconds;
};

// A soft bell: fundamental plus a quickly fading octave, with a short attack and exponential decay.
std::vector<float> synthesize(std::initializer_list<Note> notes, float gain)
{
    std::vector<float> samples;
    size_t cursor = 0; // where the next note starts
    for (const Note& note : notes) {
        const size_t length = static_cast<size_t>(SampleRate) * note.milliseconds / 1000;
        if (note.frequency > 0.0) {
            // Each note keeps ringing for a moment over the next one.
            const size_t ring = length + SampleRate * 60 / 1000;
            if (samples.size() < cursor + ring)
                samples.resize(cursor + ring, 0.0f);
            for (size_t i = 0; i < ring; ++i) {
                const double t = double(i) / SampleRate;
                const double attack = std::min(1.0, t / 0.004);
                const double decay = std::exp(-t * 9.0);
                const double phase = 2.0 * std::numbers::pi * note.frequency * t;
                const double tone = std::sin(phase) * 0.75 + std::sin(2.0 * phase) * 0.25 * std::exp(-t * 20.0);
                samples[cursor + i] += static_cast<float>(tone * attack * decay * gain);
            }
        }
        cursor += length;
        if (samples.size() < cursor)
            samples.resize(cursor, 0.0f); // rests extend the sound with silence
    }
    return samples;
}

} // namespace

SoundEffects::SoundEffects(QObject* parent)
    : QObject(parent)
{
    constexpr double C4 = 261.63, E4 = 329.63, F4 = 349.23, A4 = 440.0, C5 = 523.25, E5 = 659.25, G5 = 783.99,
                     A5 = 880.0, B5 = 987.77;
    auto set = [this](Sound sound, std::vector<float> samples) { m_sounds[static_cast<size_t>(sound)] = std::move(samples); };
    set(Sound::Join, synthesize({{C5, 90}, {G5, 220}}, 0.35f));
    set(Sound::Leave, synthesize({{G5, 90}, {C5, 220}}, 0.35f));
    set(Sound::UserJoin, synthesize({{E5, 70}, {A5, 180}}, 0.25f));
    set(Sound::UserLeave, synthesize({{A5, 70}, {E5, 180}}, 0.25f));
    set(Sound::Mute, synthesize({{A4, 70}, {E4, 160}}, 0.3f));
    set(Sound::Unmute, synthesize({{E4, 70}, {A4, 160}}, 0.3f));
    set(Sound::Deafen, synthesize({{F4, 90}, {C4, 220}}, 0.3f));
    set(Sound::Undeafen, synthesize({{C4, 90}, {F4, 220}}, 0.3f));
    // Ringtone: a short repeating motif followed by a pause; played in a loop while a call rings.
    set(Sound::Ringtone, synthesize({{E5, 120}, {B5, 120}, {E5, 120}, {B5, 240}, {0, 1400}}, 0.3f));
    // New message: a short, soft high blip.
    set(Sound::Message, synthesize({{B5, 60}, {E5 * 2, 160}}, 0.2f));

    // Close the output stream shortly after the last sound ends, so it doesn't keep the audio device busy.
    m_idleTimer.setInterval(1500);
    connect(&m_idleTimer, &QTimer::timeout, this, &SoundEffects::stopDeviceIfIdle);
}

SoundEffects::~SoundEffects()
{
    m_audio.stopPlayback();
}

void SoundEffects::play(Sound sound)
{
    if (m_enabled)
        start(sound, false);
}

void SoundEffects::startRinging()
{
    stopRinging();
    start(Sound::Ringtone, true);
}

void SoundEffects::stopRinging()
{
    std::lock_guard lock(m_mutex);
    const auto* ringtone = &m_sounds[static_cast<size_t>(Sound::Ringtone)];
    std::erase_if(m_voices, [ringtone](const Voice& voice) { return voice.samples == ringtone; });
}

void SoundEffects::start(Sound sound, bool loop)
{
    {
        std::lock_guard lock(m_mutex);
        m_voices.push_back({&m_sounds[static_cast<size_t>(sound)], 0, loop});
    }
    if (!m_deviceOpen)
        m_deviceOpen = m_audio.startPlayback(m_outputDevice, [this](float* output, int frames) { render(output, frames); });
    m_idleTimer.start();
}

void SoundEffects::render(float* output, int frameCount)
{
    std::fill(output, output + static_cast<size_t>(frameCount) * 2, 0.0f);
    std::lock_guard lock(m_mutex);
    const float volume = m_volume;
    for (Voice& voice : m_voices) {
        for (int i = 0; i < frameCount; ++i) {
            if (voice.position >= voice.samples->size()) {
                if (!voice.loop)
                    break;
                voice.position = 0;
            }
            const float sample = (*voice.samples)[voice.position++] * volume;
            output[2 * i] += sample;
            output[2 * i + 1] += sample;
        }
    }
    std::erase_if(m_voices, [](const Voice& voice) { return !voice.loop && voice.position >= voice.samples->size(); });
}

void SoundEffects::stopDeviceIfIdle()
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_voices.empty())
            return;
    }
    m_idleTimer.stop();
    m_audio.stopPlayback();
    m_deviceOpen = false;
}
