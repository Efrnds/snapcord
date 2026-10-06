#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

struct DenoiseState;
struct SpeexEchoState_;
struct SpeexPreprocessState_;

// Microphone clean-up, applied to 10 ms mono frames at 48 kHz before encoding:
// echo cancellation (SpeexDSP), noise suppression (RNNoise) and automatic gain control (SpeexDSP).
// RNNoise also provides the voice probability used for automatic input sensitivity.
// Not thread-safe: use it from the capture thread only.
class AudioProcessor
{
public:
    static constexpr int FrameSize = 480; // 10 ms

    struct Options
    {
        bool noiseSuppression = true;
        bool echoCancellation = false;
        bool automaticGainControl = false;
        bool voiceDetection = true; // compute voice probability even without noise suppression

        bool operator==(const Options&) const = default;
    };

    AudioProcessor();
    ~AudioProcessor();

    AudioProcessor(const AudioProcessor&) = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;

    void configure(const Options& options);
    const Options& options() const { return m_options; }

    // Processes one frame in place (samples in -1..1). `echoReference` is what the speakers played during the
    // same 10 ms (may be null). Returns the voice probability (0..1), or -1 if voice detection is off.
    float process(float* frame, const float* echoReference);

private:
    void release();

    Options m_options;
    DenoiseState* m_denoise = nullptr;
    SpeexEchoState_* m_echo = nullptr;
    SpeexPreprocessState_* m_preprocess = nullptr;
    std::vector<short> m_input;
    std::vector<short> m_reference;
    std::vector<short> m_output;
    std::vector<float> m_scaled;
};

// What the speakers are playing, handed from the playback thread to the capture thread as the
// echo cancellation reference. Keeps at most a short backlog so the two clocks can't drift apart.
class EchoReference
{
public:
    void push(const float* stereo, size_t frames);
    // Fills `mono` with the next FrameSize samples, or silence if not enough audio was played.
    void pop(float* mono);
    void clear();

private:
    std::mutex m_mutex;
    std::vector<float> m_samples;
};
