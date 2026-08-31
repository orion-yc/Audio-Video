#include "system_audio_read.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <propidl.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#endif

namespace {
constexpr int kTargetSampleRate = 8000;
constexpr int kPcmFrameBytes = 640;

#ifdef Q_OS_WIN
// Keep the COM/property identifiers local. This avoids relying on optional
// uuid.lib definitions, which differ between MinGW and MSVC SDKs.
const CLSID kMmDeviceEnumeratorClsid =
        {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
const IID kMmDeviceEnumeratorIid =
        {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
const IID kAudioClientIid =
        {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
const IID kAudioCaptureClientIid =
        {0xc8adbd64, 0xe71e, 0x48a0, {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}};
const PROPERTYKEY kDeviceFriendlyNameKey =
        {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

const GUID kPcmSubFormat =
        {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kFloatSubFormat =
        {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

template <typename T>
void releaseCom(T *&pointer)
{
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

QString hresultMessage(const QString &operation, HRESULT result)
{
    return QString("%1 failed (0x%2)")
            .arg(operation)
            .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

class ComScope
{
public:
    ComScope()
        : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        , m_shouldUninitialize(SUCCEEDED(m_result))
    {
    }

    ~ComScope()
    {
        if (m_shouldUninitialize) {
            CoUninitialize();
        }
    }

private:
    HRESULT m_result;
    bool m_shouldUninitialize;
};

IMMDeviceEnumerator *createDeviceEnumerator()
{
    IMMDeviceEnumerator *enumerator = nullptr;
    const HRESULT result = CoCreateInstance(kMmDeviceEnumeratorClsid, nullptr,
                                            CLSCTX_ALL, kMmDeviceEnumeratorIid,
                                            reinterpret_cast<void **>(&enumerator));
    if (FAILED(result)) {
        return nullptr;
    }
    return enumerator;
}

QString endpointIdForDevice(IMMDevice *device)
{
    if (!device) {
        return {};
    }

    LPWSTR endpointId = nullptr;
    const HRESULT result = device->GetId(&endpointId);
    if (FAILED(result) || !endpointId) {
        return {};
    }

    const QString id = QString::fromWCharArray(endpointId);
    CoTaskMemFree(endpointId);
    return id;
}

QString displayNameForDevice(IMMDevice *device)
{
    if (!device) {
        return {};
    }

    IPropertyStore *propertyStore = nullptr;
    const HRESULT openResult = device->OpenPropertyStore(STGM_READ, &propertyStore);
    if (FAILED(openResult) || !propertyStore) {
        return {};
    }

    PROPVARIANT value;
    PropVariantInit(&value);
    QString name;
    const HRESULT valueResult = propertyStore->GetValue(kDeviceFriendlyNameKey, &value);
    if (SUCCEEDED(valueResult) && value.vt == VT_LPWSTR && value.pwszVal) {
        name = QString::fromWCharArray(value.pwszVal);
    }
    PropVariantClear(&value);
    releaseCom(propertyStore);
    return name;
}

bool formatIsFloat(const WAVEFORMATEX *format)
{
    if (!format) {
        return false;
    }
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        return IsEqualGUID(extensible->SubFormat, kFloatSubFormat);
    }
    return false;
}

bool formatIsPcm(const WAVEFORMATEX *format)
{
    if (!format) {
        return false;
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM) {
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        return IsEqualGUID(extensible->SubFormat, kPcmSubFormat);
    }
    return false;
}

int validBitsForFormat(const WAVEFORMATEX *format, int containerBits)
{
    if (format && format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
        const int validBits = extensible->Samples.wValidBitsPerSample;
        if (validBits > 0 && validBits <= containerBits) {
            return validBits;
        }
    }
    return containerBits;
}

float normalizedSample(const BYTE *data, int bytesPerSample, bool floatingPoint,
                       int validBits)
{
    if (!data) {
        return 0.0f;
    }

    if (floatingPoint && bytesPerSample == 4) {
        float sample = 0.0f;
        std::memcpy(&sample, data, sizeof(sample));
        return std::clamp(sample, -1.0f, 1.0f);
    }

    const int containerBits = bytesPerSample * 8;
    validBits = qBound(1, validBits, containerBits);
    switch (bytesPerSample) {
    case 1:
        return (static_cast<int>(data[0]) - 128) / 128.0f;
    case 2: {
        std::int16_t sample = 0;
        std::memcpy(&sample, data, sizeof(sample));
        if (validBits < containerBits) {
            sample >>= containerBits - validBits;
        }
        return sample / std::ldexp(1.0f, validBits - 1);
    }
    case 3: {
        std::int32_t sample = static_cast<std::int32_t>(data[0]) |
                (static_cast<std::int32_t>(data[1]) << 8) |
                (static_cast<std::int32_t>(data[2]) << 16);
        if (sample & 0x00800000) {
            sample |= static_cast<std::int32_t>(0xFF000000);
        }
        if (validBits < containerBits) {
            sample >>= containerBits - validBits;
        }
        return sample / std::ldexp(1.0f, validBits - 1);
    }
    case 4: {
        std::int32_t sample = 0;
        std::memcpy(&sample, data, sizeof(sample));
        if (validBits < containerBits) {
            sample >>= containerBits - validBits;
        }
        return sample / std::ldexp(1.0f, validBits - 1);
    }
    default:
        return 0.0f;
    }
}
#endif
}

SystemAudio_Read::SystemAudio_Read(QObject *parent)
    : QThread(parent)
{
    m_defaultDeviceTimer.setInterval(2000);
    m_retryTimer.setSingleShot(true);
    connect(&m_defaultDeviceTimer, &QTimer::timeout,
            this, &SystemAudio_Read::slot_checkDefaultDevice);
    connect(this, &QThread::finished, this, [this]() {
        if (m_captureRequested.load()) {
            m_retryTimer.start(1000);
        }
    });
    connect(&m_retryTimer, &QTimer::timeout, this, [this]() {
        if (!m_captureRequested.load() || isRunning()) {
            return;
        }
        const QString selectedId = selectedDeviceId();
        {
            QMutexLocker locker(&m_deviceMutex);
            m_activeDeviceId = selectedId.isEmpty() ? defaultOutputDeviceId() : selectedId;
        }
        QThread::start();
    });
}

SystemAudio_Read::~SystemAudio_Read()
{
    stopCapture();
}

QList<SystemAudioDevice> SystemAudio_Read::availableOutputDevices()
{
    QList<SystemAudioDevice> devices;
#ifdef Q_OS_WIN
    ComScope com;
    IMMDeviceEnumerator *enumerator = createDeviceEnumerator();
    if (!enumerator) {
        return devices;
    }

    IMMDevice *defaultDevice = nullptr;
    QString defaultId;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                       &defaultDevice))) {
        defaultId = endpointIdForDevice(defaultDevice);
    }
    releaseCom(defaultDevice);

    IMMDeviceCollection *collection = nullptr;
    const HRESULT enumResult = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                                                &collection);
    if (SUCCEEDED(enumResult) && collection) {
        UINT count = 0;
        collection->GetCount(&count);
        for (UINT index = 0; index < count; ++index) {
            IMMDevice *device = nullptr;
            if (FAILED(collection->Item(index, &device)) || !device) {
                continue;
            }

            SystemAudioDevice outputDevice;
            outputDevice.id = endpointIdForDevice(device);
            outputDevice.name = displayNameForDevice(device);
            outputDevice.isDefault = outputDevice.id == defaultId;
            if (!outputDevice.id.isEmpty()) {
                if (outputDevice.name.isEmpty()) {
                    outputDevice.name = QString("播放设备 %1").arg(index + 1);
                }
                devices.append(outputDevice);
            }
            releaseCom(device);
        }
    }
    releaseCom(collection);
    releaseCom(enumerator);
#endif
    return devices;
}

QString SystemAudio_Read::defaultOutputDeviceId()
{
#ifdef Q_OS_WIN
    ComScope com;
    IMMDeviceEnumerator *enumerator = createDeviceEnumerator();
    if (!enumerator) {
        return {};
    }

    IMMDevice *device = nullptr;
    QString id;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
        id = endpointIdForDevice(device);
    }
    releaseCom(device);
    releaseCom(enumerator);
    return id;
#else
    return {};
#endif
}

QString SystemAudio_Read::selectedDeviceId() const
{
    QMutexLocker locker(&m_deviceMutex);
    return m_selectedDeviceId;
}

bool SystemAudio_Read::followsDefaultDevice() const
{
    return selectedDeviceId().isEmpty();
}

void SystemAudio_Read::startCapture()
{
    if (m_captureRequested.load()) {
        return;
    }

    m_retryTimer.stop();
    m_captureRequested.store(true);
    const QString selectedId = selectedDeviceId();
    {
        QMutexLocker locker(&m_deviceMutex);
        m_activeDeviceId = selectedId.isEmpty() ? defaultOutputDeviceId() : selectedId;
    }

    if (selectedId.isEmpty()) {
        m_defaultDeviceTimer.start();
    }
    QThread::start();
}

void SystemAudio_Read::stopCapture()
{
    m_captureRequested.store(false);
    m_defaultDeviceTimer.stop();
    m_retryTimer.stop();
    if (isRunning()) {
        requestInterruption();
        wait();
    }
    QMutexLocker locker(&m_deviceMutex);
    m_activeDeviceId.clear();
}

void SystemAudio_Read::setDeviceId(const QString &deviceId)
{
    const QString normalizedId = deviceId;
    bool changed = false;
    {
        QMutexLocker locker(&m_deviceMutex);
        changed = m_selectedDeviceId != normalizedId;
        m_selectedDeviceId = normalizedId;
    }
    if (!changed) {
        return;
    }

    const bool restart = m_captureRequested.load();
    if (restart) {
        stopCapture();
        startCapture();
    }
}

void SystemAudio_Read::slot_checkDefaultDevice()
{
    if (!m_captureRequested.load() || !followsDefaultDevice()) {
        return;
    }

    const QString defaultId = defaultOutputDeviceId();
    QString activeId;
    {
        QMutexLocker locker(&m_deviceMutex);
        activeId = m_activeDeviceId;
    }
    if (defaultId.isEmpty() || defaultId == activeId) {
        return;
    }

    stopCapture();
    startCapture();
}

void SystemAudio_Read::run()
{
    QString deviceId;
    {
        QMutexLocker locker(&m_deviceMutex);
        deviceId = m_activeDeviceId;
    }
    captureLoop(deviceId);
}

void SystemAudio_Read::captureLoop(const QString &deviceId)
{
#ifndef Q_OS_WIN
    Q_UNUSED(deviceId);
    Q_EMIT SIG_captureError("当前平台不支持系统声音采集");
    return;
#else
    ComScope com;
    IMMDeviceEnumerator *enumerator = createDeviceEnumerator();
    if (!enumerator) {
        Q_EMIT SIG_captureError("无法创建 Windows 音频设备枚举器");
        return;
    }

    IMMDevice *device = nullptr;
    HRESULT result = E_FAIL;
    if (deviceId.isEmpty()) {
        result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    } else {
        result = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device);
    }
    releaseCom(enumerator);
    if (FAILED(result) || !device) {
        Q_EMIT SIG_captureError("无法打开所选的系统播放设备");
        return;
    }

    IAudioClient *audioClient = nullptr;
    result = device->Activate(kAudioClientIid, CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&audioClient));
    releaseCom(device);
    if (FAILED(result) || !audioClient) {
        Q_EMIT SIG_captureError(hresultMessage("激活系统声音采集", result));
        return;
    }

    WAVEFORMATEX *mixFormat = nullptr;
    result = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(result) || !mixFormat) {
        Q_EMIT SIG_captureError(hresultMessage("读取播放设备格式", result));
        releaseCom(audioClient);
        return;
    }

    const bool floatingPoint = formatIsFloat(mixFormat);
    const bool pcm = formatIsPcm(mixFormat);
    const int channels = qMax(1, static_cast<int>(mixFormat->nChannels));
    const int bytesPerSample = mixFormat->nBlockAlign / channels;
    const int blockAlign = static_cast<int>(mixFormat->nBlockAlign);
    const int sourceRate = static_cast<int>(mixFormat->nSamplesPerSec);
    const int validBits = validBitsForFormat(mixFormat, bytesPerSample * 8);
    const bool supportedSampleSize = floatingPoint ? bytesPerSample == 4
                                                   : bytesPerSample >= 1 && bytesPerSample <= 4;
    if ((!floatingPoint && !pcm) || !supportedSampleSize || sourceRate <= 0 ||
            bytesPerSample < 1 || blockAlign < channels * bytesPerSample ||
            blockAlign % channels != 0) {
        Q_EMIT SIG_captureError("播放设备使用了不支持的音频格式");
        CoTaskMemFree(mixFormat);
        releaseCom(audioClient);
        return;
    }

    result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                     AUDCLNT_STREAMFLAGS_LOOPBACK,
                                     0, 0, mixFormat, nullptr);
    CoTaskMemFree(mixFormat);
    if (FAILED(result)) {
        Q_EMIT SIG_captureError(hresultMessage("启动 WASAPI loopback", result));
        releaseCom(audioClient);
        return;
    }

    IAudioCaptureClient *captureClient = nullptr;
    result = audioClient->GetService(kAudioCaptureClientIid,
                                     reinterpret_cast<void **>(&captureClient));
    if (FAILED(result) || !captureClient) {
        Q_EMIT SIG_captureError(hresultMessage("获取系统声音数据", result));
        releaseCom(audioClient);
        return;
    }

    result = audioClient->Start();
    if (FAILED(result)) {
        Q_EMIT SIG_captureError(hresultMessage("开始系统声音采集", result));
        releaseCom(captureClient);
        releaseCom(audioClient);
        return;
    }

    std::vector<float> sourceSamples;
    QByteArray pcmBuffer;
    double sourcePosition = 0.0;
    const double sourceStep = static_cast<double>(sourceRate) / kTargetSampleRate;

    auto appendMonoSamples = [this, &sourceSamples, &pcmBuffer, &sourcePosition, sourceStep]
            (const std::vector<float> &samples) {
        sourceSamples.insert(sourceSamples.end(), samples.begin(), samples.end());
        while (sourcePosition + 1.0 < static_cast<double>(sourceSamples.size())) {
            const int first = static_cast<int>(sourcePosition);
            const double fraction = sourcePosition - first;
            const float sample = static_cast<float>(sourceSamples[first] * (1.0 - fraction)
                                                    + sourceSamples[first + 1] * fraction);
            const int value = qBound(-32768, static_cast<int>(std::lround(sample * 32767.0f)),
                                     32767);
            const std::int16_t pcm = static_cast<std::int16_t>(value);
            pcmBuffer.append(reinterpret_cast<const char *>(&pcm), sizeof(pcm));
            sourcePosition += sourceStep;
        }

        const int consumed = static_cast<int>(sourcePosition);
        if (consumed > 0) {
            sourceSamples.erase(sourceSamples.begin(), sourceSamples.begin() + consumed);
            sourcePosition -= consumed;
        }
        while (pcmBuffer.size() >= kPcmFrameBytes) {
            const QByteArray frame = pcmBuffer.left(kPcmFrameBytes);
            pcmBuffer.remove(0, kPcmFrameBytes);
            Q_EMIT SIG_audioFrame(frame);
        }
    };

    while (!isInterruptionRequested()) {
        UINT32 packetSize = 0;
        result = captureClient->GetNextPacketSize(&packetSize);
        if (FAILED(result)) {
            Q_EMIT SIG_captureError(hresultMessage("读取系统声音数据", result));
            break;
        }
        if (packetSize == 0) {
            QThread::msleep(5);
            continue;
        }

        while (packetSize > 0 && !isInterruptionRequested()) {
            BYTE *data = nullptr;
            UINT32 frameCount = 0;
            DWORD flags = 0;
            result = captureClient->GetBuffer(&data, &frameCount, &flags, nullptr, nullptr);
            if (FAILED(result)) {
                Q_EMIT SIG_captureError(hresultMessage("获取系统声音缓冲区", result));
                packetSize = 0;
                break;
            }

            if (flags & (AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
                         | AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
                // Do not interpolate across a device/sleep discontinuity.
                sourceSamples.clear();
                pcmBuffer.clear();
                sourcePosition = 0.0;
            }

            std::vector<float> monoSamples(frameCount, 0.0f);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                for (UINT32 frame = 0; frame < frameCount; ++frame) {
                    const BYTE *frameData = data + frame * blockAlign;
                    float mixed = 0.0f;
                    for (int channel = 0; channel < channels; ++channel) {
                        mixed += normalizedSample(frameData + channel * bytesPerSample,
                                                  bytesPerSample, floatingPoint, validBits);
                    }
                    monoSamples[frame] = std::clamp(mixed / channels, -1.0f, 1.0f);
                }
            }
            const HRESULT releaseResult = captureClient->ReleaseBuffer(frameCount);
            if (FAILED(releaseResult)) {
                Q_EMIT SIG_captureError(hresultMessage("释放系统声音缓冲区", releaseResult));
                packetSize = 0;
                break;
            }
            appendMonoSamples(monoSamples);

            result = captureClient->GetNextPacketSize(&packetSize);
            if (FAILED(result)) {
                Q_EMIT SIG_captureError(hresultMessage("继续读取系统声音数据", result));
                packetSize = 0;
            }
        }
    }

    audioClient->Stop();
    releaseCom(captureClient);
    releaseCom(audioClient);
#endif
}
