#include "voice/AudioProcessor.h"

#include <rnnoise.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr int SampleRate = 48000;
// Echo tail covered by the canceller: speaker-to-microphone delay plus room reverb (150 ms).
constexpr int EchoTailSamples = AudioProcessor::FrameSize * 15;
// Keep at most 200 ms of speaker audio queued for the canceller.
constexpr size_t MaxReferenceBacklog = AudioProcessor::FrameSize * 20;

short toPcm16(float sample)
{
    return static_cast<short>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
}

} // namespace

AudioProcessor::AudioProcessor()
    : m_input(FrameSize)
    , m_reference(FrameSize)
    , m_output(FrameSize)
    , m_scaled(FrameSize)
{
    configure(m_options);
}

AudioProcessor::~AudioProcessor()
{
    release();
}

void AudioProcessor::release()
{
    if (m_denoise)
        rnnoise_destroy(m_denoise);
    if (m_preprocess)
        speex_preprocess_state_destroy(m_preprocess);
    if (m_echo)
        speex_echo_state_destroy(m_echo);
    m_denoise = nullptr;
    m_preprocess = nullptr;
    m_echo = nullptr;
}

void AudioProcessor::configure(const Options& options)
{
    release();
    m_options = options;

    if (options.noiseSuppression || options.voiceDetection)
        m_denoise = rnnoise_create(nullptr);

    if (options.echoCancellation) {
        m_echo = speex_echo_state_init(FrameSize, EchoTailSamples);
        int rate = SampleRate;
        speex_echo_ctl(m_echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    }

    // The preprocessor provides AGC and suppresses the echo the adaptive filter leaves behind. Its own noise
    // suppression stays off because RNNoise does a much better job.
    if (options.automaticGainControl || options.echoCancellation) {
        m_preprocess = speex_preprocess_state_init(FrameSize, SampleRate);
        int off = 0;
        int on = 1;
        speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_DENOISE, &off);
        speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_DEREVERB, &off);
        if (options.automaticGainControl) {
            speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_AGC, &on);
            float level = 12000.0f;
            speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_AGC_LEVEL, &level);
            int maxGainDb = 24;
            speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &maxGainDb);
        } else {
            speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_AGC, &off);
        }
        if (m_echo)
            speex_preprocess_ctl(m_preprocess, SPEEX_PREPROCESS_SET_ECHO_STATE, m_echo);
    }
}

float AudioProcessor::process(float* frame, const float* echoReference)
{
    if (m_echo) {
        for (int i = 0; i < FrameSize; ++i) {
            m_input[i] = toPcm16(frame[i]);
            m_reference[i] = echoReference ? toPcm16(echoReference[i]) : 0;
        }
        speex_echo_cancellation(m_echo, m_input.data(), m_reference.data(), m_output.data());
        for (int i = 0; i < FrameSize; ++i)
            frame[i] = m_output[i] / 32768.0f;
    }

    float voiceProbability = -1.0f;
    if (m_denoise) {
        // RNNoise works on float samples in the 16-bit range.
        for (int i = 0; i < FrameSize; ++i)
            m_scaled[i] = frame[i] * 32768.0f;
        voiceProbability = rnnoise_process_frame(m_denoise, m_scaled.data(), m_scaled.data());
        if (m_options.noiseSuppression) {
            for (int i = 0; i < FrameSize; ++i)
                frame[i] = m_scaled[i] / 32768.0f;
        }
    }

    if (m_preprocess) {
        for (int i = 0; i < FrameSize; ++i)
            m_input[i] = toPcm16(frame[i]);
        speex_preprocess_run(m_preprocess, m_input.data());
        for (int i = 0; i < FrameSize; ++i)
            frame[i] = m_input[i] / 32768.0f;
    }
    return voiceProbability;
}

void EchoReference::push(const float* stereo, size_t frames)
{
    std::lock_guard lock(m_mutex);
    for (size_t i = 0; i < frames; ++i)
        m_samples.push_back(0.5f * (stereo[2 * i] + stereo[2 * i + 1]));
    if (m_samples.size() > MaxReferenceBacklog)
        m_samples.erase(m_samples.begin(), m_samples.end() - static_cast<ptrdiff_t>(MaxReferenceBacklog));
}

void EchoReference::pop(float* mono)
{
    std::lock_guard lock(m_mutex);
    const size_t available = std::min<size_t>(m_samples.size(), AudioProcessor::FrameSize);
    std::copy(m_samples.begin(), m_samples.begin() + static_cast<ptrdiff_t>(available), mono);
    std::fill(mono + available, mono + AudioProcessor::FrameSize, 0.0f);
    m_samples.erase(m_samples.begin(), m_samples.begin() + static_cast<ptrdiff_t>(available));
}

void EchoReference::clear()
{
    std::lock_guard lock(m_mutex);
    m_samples.clear();
}
