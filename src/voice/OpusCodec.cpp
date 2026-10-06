#include "voice/OpusCodec.h"

#include <opus.h>

OpusEncoderWrapper::OpusEncoderWrapper()
{
    int error = OPUS_OK;
    m_encoder = opus_encoder_create(OpusFormat::SampleRate, OpusFormat::Channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK) {
        m_encoder = nullptr;
        return;
    }
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(64000));
    // In-band forward error correction lets receivers rebuild a lost packet from the next one.
    opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(10));
}

OpusEncoderWrapper::~OpusEncoderWrapper()
{
    if (m_encoder)
        opus_encoder_destroy(m_encoder);
}

void OpusEncoderWrapper::setBitrate(int bitsPerSecond)
{
    if (m_encoder)
        opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitsPerSecond));
}

int OpusEncoderWrapper::encode(const float* stereoFrame, uint8_t* packet, int packetCapacity)
{
    if (!m_encoder)
        return 0;
    const int size = opus_encode_float(m_encoder, stereoFrame, OpusFormat::FrameSamples, packet, packetCapacity);
    return size > 0 ? size : 0;
}

OpusDecoderWrapper::OpusDecoderWrapper()
{
    int error = OPUS_OK;
    m_decoder = opus_decoder_create(OpusFormat::SampleRate, OpusFormat::Channels, &error);
    if (error != OPUS_OK)
        m_decoder = nullptr;
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
    if (m_decoder)
        opus_decoder_destroy(m_decoder);
}

int OpusDecoderWrapper::decode(const uint8_t* packet, int size, float* stereoOut, int maxSamples)
{
    if (!m_decoder)
        return 0;
    const int samples = opus_decode_float(m_decoder, packet, size, stereoOut, maxSamples, 0);
    return samples > 0 ? samples : 0;
}

int OpusDecoderWrapper::conceal(float* stereoOut, int maxSamples)
{
    if (!m_decoder)
        return 0;
    const int frame = maxSamples < OpusFormat::FrameSamples ? maxSamples : OpusFormat::FrameSamples;
    const int samples = opus_decode_float(m_decoder, nullptr, 0, stereoOut, frame, 0);
    return samples > 0 ? samples : 0;
}
