#include "voice/AudioEngine.h"

#include <miniaudio.h>

#include <optional>

struct AudioEngine::Private
{
    ma_context context{};
    bool contextReady = false;

    ma_device captureDevice{};
    bool captureRunning = false;
    CaptureCallback captureCallback;

    ma_device playbackDevice{};
    bool playbackRunning = false;
    PlaybackCallback playbackCallback;

    std::optional<ma_device_id> findDevice(ma_device_type type, const QString& name)
    {
        if (name.isEmpty() || !contextReady)
            return std::nullopt;
        ma_device_info* playbackInfos = nullptr;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS)
            return std::nullopt;
        ma_device_info* infos = type == ma_device_type_capture ? captureInfos : playbackInfos;
        const ma_uint32 count = type == ma_device_type_capture ? captureCount : playbackCount;
        for (ma_uint32 i = 0; i < count; ++i) {
            if (QString::fromUtf8(infos[i].name) == name)
                return infos[i].id;
        }
        return std::nullopt; // the saved device was unplugged: fall back to the default one
    }

    QStringList deviceNames(ma_device_type type)
    {
        QStringList names;
        if (!contextReady)
            return names;
        ma_device_info* playbackInfos = nullptr;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS)
            return names;
        ma_device_info* infos = type == ma_device_type_capture ? captureInfos : playbackInfos;
        const ma_uint32 count = type == ma_device_type_capture ? captureCount : playbackCount;
        for (ma_uint32 i = 0; i < count; ++i)
            names.append(QString::fromUtf8(infos[i].name));
        return names;
    }

    static void captureData(ma_device* device, void*, const void* input, ma_uint32 frameCount)
    {
        auto* self = static_cast<Private*>(device->pUserData);
        if (self->captureCallback && input)
            self->captureCallback(static_cast<const float*>(input), static_cast<int>(frameCount));
    }

    static void playbackData(ma_device* device, void* output, const void*, ma_uint32 frameCount)
    {
        auto* self = static_cast<Private*>(device->pUserData);
        if (self->playbackCallback)
            self->playbackCallback(static_cast<float*>(output), static_cast<int>(frameCount));
    }
};

AudioEngine::AudioEngine()
    : d(std::make_unique<Private>())
{
    d->contextReady = ma_context_init(nullptr, 0, nullptr, &d->context) == MA_SUCCESS;
}

AudioEngine::~AudioEngine()
{
    stopCapture();
    stopPlayback();
    if (d->contextReady)
        ma_context_uninit(&d->context);
}

QStringList AudioEngine::inputDevices() const
{
    return d->deviceNames(ma_device_type_capture);
}

QStringList AudioEngine::outputDevices() const
{
    return d->deviceNames(ma_device_type_playback);
}

bool AudioEngine::startCapture(const QString& deviceName, CaptureCallback callback)
{
    stopCapture();
    if (!d->contextReady)
        return false;

    const auto deviceId = d->findDevice(ma_device_type_capture, deviceName);
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = deviceId ? &*deviceId : nullptr;
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.sampleRate = SampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = Private::captureData;
    config.pUserData = d.get();

    d->captureCallback = std::move(callback);
    if (ma_device_init(&d->context, &config, &d->captureDevice) != MA_SUCCESS)
        return false;
    if (ma_device_start(&d->captureDevice) != MA_SUCCESS) {
        ma_device_uninit(&d->captureDevice);
        return false;
    }
    d->captureRunning = true;
    return true;
}

void AudioEngine::stopCapture()
{
    if (!d->captureRunning)
        return;
    ma_device_uninit(&d->captureDevice);
    d->captureRunning = false;
    d->captureCallback = nullptr;
}

bool AudioEngine::startPlayback(const QString& deviceName, PlaybackCallback callback)
{
    stopPlayback();
    if (!d->contextReady)
        return false;

    const auto deviceId = d->findDevice(ma_device_type_playback, deviceName);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.pDeviceID = deviceId ? &*deviceId : nullptr;
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = SampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = Private::playbackData;
    config.pUserData = d.get();

    d->playbackCallback = std::move(callback);
    if (ma_device_init(&d->context, &config, &d->playbackDevice) != MA_SUCCESS)
        return false;
    if (ma_device_start(&d->playbackDevice) != MA_SUCCESS) {
        ma_device_uninit(&d->playbackDevice);
        return false;
    }
    d->playbackRunning = true;
    return true;
}

void AudioEngine::stopPlayback()
{
    if (!d->playbackRunning)
        return;
    ma_device_uninit(&d->playbackDevice);
    d->playbackRunning = false;
    d->playbackCallback = nullptr;
}
