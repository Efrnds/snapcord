#pragma once

#include "voice/AudioEngine.h"

#include <QObject>
#include <QTimer>

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

// Short interface sounds (joining, muting, someone entering the channel, incoming call ringtone).
// They are synthesized at startup instead of shipped as files, and played on their own output stream
// that is opened only while a sound is playing.
class SoundEffects : public QObject
{
    Q_OBJECT

public:
    enum class Sound { Join, Leave, UserJoin, UserLeave, Mute, Unmute, Deafen, Undeafen, Ringtone, Count };

    explicit SoundEffects(QObject* parent = nullptr);
    ~SoundEffects() override;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setOutputDevice(const QString& device) { m_outputDevice = device; }
    void setVolume(float volume) { m_volume = volume; }

    void play(Sound sound);
    void startRinging();
    void stopRinging();

    // Synthesized samples (mono, 48 kHz), exposed for tests.
    const std::vector<float>& samples(Sound sound) const { return m_sounds[static_cast<size_t>(sound)]; }

private:
    struct Voice
    {
        const std::vector<float>* samples;
        size_t position;
        bool loop;
    };

    void start(Sound sound, bool loop);
    void render(float* output, int frameCount);
    void stopDeviceIfIdle();

    AudioEngine m_audio;
    std::array<std::vector<float>, static_cast<size_t>(Sound::Count)> m_sounds;
    std::mutex m_mutex;
    std::vector<Voice> m_voices;
    QTimer m_idleTimer;
    QString m_outputDevice;
    std::atomic<float> m_volume{1.0f};
    bool m_enabled = true;
    bool m_deviceOpen = false;
};
