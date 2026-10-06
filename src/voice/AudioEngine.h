#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

// Microphone capture and speaker playback through miniaudio, always at 48 kHz (Opus' native rate).
// The callbacks run on real-time audio threads: they must not block or allocate.
class AudioEngine
{
public:
    static constexpr int SampleRate = 48000;

    // Mono samples from the microphone.
    using CaptureCallback = std::function<void(const float* samples, int frameCount)>;
    // Interleaved stereo samples to fill for the speakers.
    using PlaybackCallback = std::function<void(float* samples, int frameCount)>;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    QStringList inputDevices() const;
    QStringList outputDevices() const;

    // An empty device name selects the system default device.
    bool startCapture(const QString& deviceName, CaptureCallback callback);
    void stopCapture();
    bool startPlayback(const QString& deviceName, PlaybackCallback callback);
    void stopPlayback();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
