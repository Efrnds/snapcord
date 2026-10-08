#pragma once

#include "voice/AudioEngine.h"

#include <QObject>
#include <QTimer>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// Short interface sounds (joining, muting, someone entering the channel, incoming call ringtone).
// They are synthesized instead of shipped as files, and played on their own output stream that is opened
// only while a sound is playing. Each sound can use one of a few styles (or be turned off), chosen in the
// settings; only the chosen style is kept in memory.
class SoundEffects : public QObject
{
    Q_OBJECT

public:
    enum class Sound {
        Join,
        Leave,
        UserJoin,
        UserLeave,
        Mute,
        Unmute,
        Deafen,
        Undeafen,
        UserMute,   // someone else in the call muted
        UserUnmute, // someone else in the call unmuted
        Ringtone,
        Message,
        Count
    };
    enum class Style { Classic, Soft, Digital, Pop, Off, Count };

    explicit SoundEffects(QObject* parent = nullptr);
    ~SoundEffects() override;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setOutputDevice(const QString& device) { m_outputDevice = device; }
    void setVolume(float volume) { m_volume = volume; }

    Style style(Sound sound) const { return m_styles[static_cast<size_t>(sound)]; }
    void setStyle(Sound sound, Style style);
    // Styles are persisted with QSettings ("sounds/<key>").
    void loadStyles();
    void saveStyles() const;

    void play(Sound sound);
    // Plays a sound in the given style once, even when sound effects are disabled (settings preview).
    void preview(Sound sound, Style style);
    void startRinging();
    void stopRinging();

    // Synthesized samples (mono, 48 kHz); empty for Style::Off. Exposed for tests.
    static std::vector<float> synthesize(Sound sound, Style style);
    static QString key(Sound sound);
    static QString key(Style style);

private:
    using Samples = std::shared_ptr<const std::vector<float>>;

    struct Voice
    {
        Samples samples;
        size_t position;
        bool loop;
    };

    void start(Samples samples, bool loop);
    void render(float* output, int frameCount);
    void stopDeviceIfIdle();

    AudioEngine m_audio;
    std::array<Samples, static_cast<size_t>(Sound::Count)> m_sounds;
    std::array<Style, static_cast<size_t>(Sound::Count)> m_styles;
    std::mutex m_mutex;
    std::vector<Voice> m_voices;
    QTimer m_idleTimer;
    QString m_outputDevice;
    std::atomic<float> m_volume{1.0f};
    bool m_enabled = true;
    bool m_deviceOpen = false;
};
