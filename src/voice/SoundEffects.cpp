#include "voice/SoundEffects.h"

#include <QSettings>

#include <cmath>
#include <numbers>

namespace {

constexpr int SampleRate = AudioEngine::SampleRate;

struct Note
{
    double frequency; // 0 = rest
    int milliseconds;
};

// How a note sounds: overtones, envelope and length scale.
struct Timbre
{
    double attack;      // seconds
    double decay;       // exponential decay rate (per second)
    double ringMs;      // how long each note keeps ringing over the next one
    double pitch;       // frequency multiplier
    double tempo;       // note length multiplier
    float gain;         // relative loudness
    double (*wave)(double phase, double t);
};

double bellWave(double phase, double t)
{
    // Fundamental plus a quickly fading octave.
    return std::sin(phase) * 0.75 + std::sin(2.0 * phase) * 0.25 * std::exp(-t * 20.0);
}

double sineWave(double phase, double)
{
    return std::sin(phase);
}

double squareWave(double phase, double)
{
    // Band-limited square (odd harmonics), for a retro "beep".
    return (std::sin(phase) + std::sin(3.0 * phase) / 3.0 + std::sin(5.0 * phase) / 5.0) * 0.8;
}

double marimbaWave(double phase, double t)
{
    // Woody pluck: a bright fourth harmonic that dies out almost immediately.
    return std::sin(phase) * 0.8 + std::sin(4.0 * phase) * 0.35 * std::exp(-t * 60.0);
}

Timbre timbre(SoundEffects::Style style)
{
    switch (style) {
    case SoundEffects::Style::Soft:
        return {0.015, 5.0, 120.0, 1.0, 1.15, 0.9f, sineWave};
    case SoundEffects::Style::Digital:
        return {0.002, 14.0, 20.0, 1.0, 0.8, 0.55f, squareWave};
    case SoundEffects::Style::Pop:
        return {0.001, 22.0, 40.0, 1.0, 0.75, 1.0f, marimbaWave};
    default:
        return {0.004, 9.0, 60.0, 1.0, 1.0, 1.0f, bellWave};
    }
}

std::vector<float> melody(std::initializer_list<Note> notes, float gain, const Timbre& sound)
{
    std::vector<float> samples;
    size_t cursor = 0; // where the next note starts
    for (const Note& note : notes) {
        const size_t length = static_cast<size_t>(SampleRate * note.milliseconds * sound.tempo / 1000.0);
        if (note.frequency > 0.0) {
            // Each note keeps ringing for a moment over the next one.
            const size_t ring = length + static_cast<size_t>(SampleRate * sound.ringMs / 1000.0);
            if (samples.size() < cursor + ring)
                samples.resize(cursor + ring, 0.0f);
            const double frequency = note.frequency * sound.pitch;
            for (size_t i = 0; i < ring; ++i) {
                const double t = double(i) / SampleRate;
                const double attack = std::min(1.0, t / sound.attack);
                const double decay = std::exp(-t * sound.decay);
                const double phase = 2.0 * std::numbers::pi * frequency * t;
                samples[cursor + i] += static_cast<float>(sound.wave(phase, t) * attack * decay * gain * sound.gain);
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
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i) {
        m_styles[i] = Style::Classic;
        m_sounds[i] = std::make_shared<const std::vector<float>>(synthesize(static_cast<Sound>(i), Style::Classic));
    }

    // Close the output stream shortly after the last sound ends, so it doesn't keep the audio device busy.
    m_idleTimer.setInterval(1500);
    connect(&m_idleTimer, &QTimer::timeout, this, &SoundEffects::stopDeviceIfIdle);
}

SoundEffects::~SoundEffects()
{
    m_audio.stopPlayback();
}

std::vector<float> SoundEffects::synthesize(Sound sound, Style style)
{
    if (style == Style::Off)
        return {};
    constexpr double C4 = 261.63, E4 = 329.63, F4 = 349.23, G4 = 392.0, A4 = 440.0, C5 = 523.25, E5 = 659.25,
                     G5 = 783.99, A5 = 880.0, B5 = 987.77;
    const Timbre t = timbre(style);
    switch (sound) {
    case Sound::Join:
        return melody({{C5, 90}, {G5, 220}}, 0.35f, t);
    case Sound::Leave:
        return melody({{G5, 90}, {C5, 220}}, 0.35f, t);
    case Sound::UserJoin:
        return melody({{E5, 70}, {A5, 180}}, 0.25f, t);
    case Sound::UserLeave:
        return melody({{A5, 70}, {E5, 180}}, 0.25f, t);
    case Sound::Mute:
        return melody({{A4, 70}, {E4, 160}}, 0.3f, t);
    case Sound::Unmute:
        return melody({{E4, 70}, {A4, 160}}, 0.3f, t);
    case Sound::Deafen:
        return melody({{F4, 90}, {C4, 220}}, 0.3f, t);
    case Sound::Undeafen:
        return melody({{C4, 90}, {F4, 220}}, 0.3f, t);
    // Someone else muting: softer and higher than your own mute, so the two are easy to tell apart.
    case Sound::UserMute:
        return melody({{C5, 60}, {G4, 150}}, 0.2f, t);
    case Sound::UserUnmute:
        return melody({{G4, 60}, {C5, 150}}, 0.2f, t);
    // Ringtone: a short repeating motif followed by a pause; played in a loop while a call rings.
    case Sound::Ringtone:
        switch (style) {
        case Style::Soft:
            return melody({{C5, 200}, {E5, 200}, {G5, 400}, {0, 1400}}, 0.3f, t);
        case Style::Digital:
            return melody({{A5, 100}, {0, 50}, {A5, 100}, {0, 50}, {A5, 100}, {0, 50}, {A5, 100}, {0, 1500}}, 0.3f, t);
        case Style::Pop:
            return melody({{G5, 110}, {E5, 110}, {C5, 110}, {E5, 110}, {G5, 220}, {0, 1400}}, 0.3f, t);
        default:
            return melody({{E5, 120}, {B5, 120}, {E5, 120}, {B5, 240}, {0, 1400}}, 0.3f, t);
        }
    // New message: a short, soft high blip.
    case Sound::Message:
    case Sound::Count:
        break;
    }
    return melody({{B5, 60}, {E5 * 2, 160}}, 0.2f, t);
}

QString SoundEffects::key(Sound sound)
{
    static const char* const keys[] = {"join",     "leave",   "userJoin",  "userLeave",  "mute",     "unmute",
                                       "deafen",   "undeafen", "userMute", "userUnmute", "ringtone", "message"};
    static_assert(std::size(keys) == static_cast<size_t>(Sound::Count));
    return QString::fromLatin1(keys[static_cast<size_t>(sound)]);
}

QString SoundEffects::key(Style style)
{
    static const char* const keys[] = {"classic", "soft", "digital", "pop", "off"};
    static_assert(std::size(keys) == static_cast<size_t>(Style::Count));
    return QString::fromLatin1(keys[static_cast<size_t>(style)]);
}

void SoundEffects::setStyle(Sound sound, Style style)
{
    const size_t index = static_cast<size_t>(sound);
    if (m_styles[index] == style)
        return;
    auto samples = std::make_shared<const std::vector<float>>(synthesize(sound, style));
    std::lock_guard lock(m_mutex);
    m_styles[index] = style;
    // Voices already playing keep their own reference to the old samples.
    m_sounds[index] = std::move(samples);
}

void SoundEffects::loadStyles()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("sounds"));
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i) {
        const QString value = settings.value(key(static_cast<Sound>(i))).toString();
        Style style = Style::Classic;
        for (int s = 0; s < static_cast<int>(Style::Count); ++s) {
            if (value == key(static_cast<Style>(s)))
                style = static_cast<Style>(s);
        }
        setStyle(static_cast<Sound>(i), style);
    }
}

void SoundEffects::saveStyles() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("sounds"));
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i)
        settings.setValue(key(static_cast<Sound>(i)), key(m_styles[i]));
}

void SoundEffects::play(Sound sound)
{
    if (!m_enabled)
        return;
    Samples samples;
    {
        std::lock_guard lock(m_mutex);
        samples = m_sounds[static_cast<size_t>(sound)];
    }
    start(std::move(samples), false);
}

void SoundEffects::preview(Sound sound, Style style)
{
    start(std::make_shared<const std::vector<float>>(synthesize(sound, style)), false);
}

void SoundEffects::startRinging()
{
    stopRinging();
    if (m_styles[static_cast<size_t>(Sound::Ringtone)] == Style::Off)
        return;
    Samples samples;
    {
        std::lock_guard lock(m_mutex);
        samples = m_sounds[static_cast<size_t>(Sound::Ringtone)];
    }
    start(std::move(samples), true);
}

void SoundEffects::stopRinging()
{
    // The ringtone is the only looping sound.
    std::lock_guard lock(m_mutex);
    std::erase_if(m_voices, [](const Voice& voice) { return voice.loop; });
}

void SoundEffects::start(Samples samples, bool loop)
{
    if (!samples || samples->empty())
        return;
    {
        std::lock_guard lock(m_mutex);
        m_voices.push_back({std::move(samples), 0, loop});
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
        const std::vector<float>& samples = *voice.samples;
        for (int i = 0; i < frameCount; ++i) {
            if (voice.position >= samples.size()) {
                if (!voice.loop)
                    break;
                voice.position = 0;
            }
            const float sample = samples[voice.position++] * volume;
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
