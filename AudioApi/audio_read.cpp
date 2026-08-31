#include "audio_read.h"

#include <cstdint>
#include <cmath>
#include <cstring>

namespace {
constexpr qsizetype kPcmFrameBytes = 640;
constexpr qsizetype kMaxPendingPcmBytes = kPcmFrameBytes * 8;

void trimPendingPcm(QByteArray &buffer)
{
    if (buffer.size() <= kMaxPendingPcmBytes) {
        return;
    }
    const qsizetype excess = buffer.size() - kMaxPendingPcmBytes;
    const qsizetype framesToDrop = (excess + kPcmFrameBytes - 1) / kPcmFrameBytes;
    buffer.remove(0, qMin(buffer.size(), framesToDrop * kPcmFrameBytes));
}
}

Audio_Read::Audio_Read(QObject *parent)
    : QObject(parent),
      m_timer(nullptr),
      audio_in(nullptr),
      myBuffer_in(nullptr),
      m_recordState(audio_state::_Stop),
      m_systemAudioEnabled(false),
      Enc_State(nullptr)
{
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!device.isNull() && !device.isFormatSupported(format))
        qWarning() << "The input device does not report native 8 kHz mono support";

    speex_bits_init(&bits_enc);
    Enc_State = speex_encoder_init(speex_lib_get_mode(SPEEX_MODEID_NB));
    if (Enc_State) {
        int quality = SPEEX_QUALITY;
        speex_encoder_ctl(Enc_State, SPEEX_SET_QUALITY, &quality);
    }
}

Audio_Read::~Audio_Read()
{
    pause();
    speex_bits_destroy(&bits_enc);
    if (Enc_State)
        speex_encoder_destroy(Enc_State);
}

void Audio_Read::start()
{
    if (m_recordState == audio_state::_Record)
        return;

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        qWarning() << "No audio input device is available";
        return;
    }

    audio_in = new QAudioSource(device, format, this);
    myBuffer_in = audio_in->start();
    if (!myBuffer_in) {
        delete audio_in;
        audio_in = nullptr;
        qWarning() << "Unable to start audio input";
        return;
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Audio_Read::slot_readMore);
    m_timer->start(20);
    m_recordState = audio_state::_Record;
}

void Audio_Read::pause()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    if (audio_in) {
        audio_in->stop();
        delete audio_in;
        audio_in = nullptr;
    }
    myBuffer_in = nullptr;
    m_micPcmPending.clear();
    m_mixWaitTimer.invalidate();
    m_recordState = audio_state::_Pause;
}

void Audio_Read::setSystemAudioEnabled(bool enabled)
{
    if (m_systemAudioEnabled == enabled) {
        return;
    }
    m_systemAudioEnabled = enabled;
    if (!enabled) {
        m_systemPcmPending.clear();
        m_mixWaitTimer.invalidate();
    }
    drainMixedFrames();
}

void Audio_Read::slot_systemAudioFrame(const QByteArray &pcm)
{
    if (!m_systemAudioEnabled || pcm.isEmpty()) {
        return;
    }

    m_systemPcmPending.append(pcm);
    trimPendingPcm(m_systemPcmPending);
    drainMixedFrames();
}

void Audio_Read::slot_readMore()
{
    if (!audio_in || !myBuffer_in || audio_in->bytesAvailable() <= 0)
        return;

    const qint64 available = audio_in->bytesAvailable();
    const qint64 readSize = qMin(available, static_cast<qint64>(kMaxPendingPcmBytes));
    if (readSize <= 0)
        return;

    const QByteArray pcm = myBuffer_in->read(readSize);
    if (pcm.isEmpty())
        return;

    m_micPcmPending.append(pcm);
    trimPendingPcm(m_micPcmPending);
    drainMixedFrames();
}

void Audio_Read::drainMixedFrames()
{
    const bool micEnabled = m_recordState == audio_state::_Record;
    const bool systemEnabled = m_systemAudioEnabled;
    if (!micEnabled && !systemEnabled) {
        return;
    }

    while (true) {
        const bool hasMic = micEnabled && m_micPcmPending.size() >= kPcmFrameBytes;
        const bool hasSystem = systemEnabled && m_systemPcmPending.size() >= kPcmFrameBytes;
        if (!hasMic && !hasSystem) {
            return;
        }
        // Keep the two live sources in the same 20 ms transport frame. When
        // both controls are enabled, wait for both sides instead of emitting
        // two sequential frames (which would play back at double speed).
        if (micEnabled && systemEnabled && (!hasMic || !hasSystem)) {
            if (!m_mixWaitTimer.isValid()) {
                m_mixWaitTimer.start();
            }
            // A faulty/permission-denied input must not silence the system
            // stream forever. After a short grace period, zero-fill the
            // missing side until it catches up.
            if (m_mixWaitTimer.elapsed() < 100) {
                return;
            }
        } else {
            m_mixWaitTimer.invalidate();
        }

        QByteArray mixed(kPcmFrameBytes, 0);
        const QByteArray micFrame = hasMic ? m_micPcmPending.left(kPcmFrameBytes) : QByteArray();
        const QByteArray systemFrame = hasSystem ? m_systemPcmPending.left(kPcmFrameBytes)
                                                  : QByteArray();
        if (hasMic) {
            m_micPcmPending.remove(0, kPcmFrameBytes);
        }
        if (hasSystem) {
            m_systemPcmPending.remove(0, kPcmFrameBytes);
        }

        constexpr int sampleCount = static_cast<int>(kPcmFrameBytes / sizeof(std::int16_t));
        const bool attenuate = hasMic && hasSystem;
        for (int index = 0; index < sampleCount; ++index) {
            std::int16_t micSample = 0;
            std::int16_t systemSample = 0;
            if (hasMic) {
                std::memcpy(&micSample, micFrame.constData() + index * sizeof(micSample),
                            sizeof(micSample));
            }
            if (hasSystem) {
                std::memcpy(&systemSample,
                            systemFrame.constData() + index * sizeof(systemSample),
                            sizeof(systemSample));
            }
            int value = static_cast<int>(micSample) + static_cast<int>(systemSample);
            if (attenuate) {
                value = static_cast<int>(std::lround(value * 0.70710678));
            }
            value = qBound(-32768, value, 32767);
            const auto output = static_cast<std::int16_t>(value);
            std::memcpy(mixed.data() + index * sizeof(output), &output, sizeof(output));
        }
        encodeAndEmit(mixed);
        if (!micEnabled || !systemEnabled || (hasMic && hasSystem)) {
            m_mixWaitTimer.invalidate();
        }
    }
}

void Audio_Read::encodeAndEmit(const QByteArray &pcm)
{
    if (pcm.size() < kPcmFrameBytes) {
        return;
    }

#ifndef USE_SPEEX
    QByteArray frame = pcm;
    Q_EMIT SIG_audioFrame(frame);
#else
    if (!Enc_State)
        return;

    QByteArray encoded;
    char packet[800] = {0};
    float samples[160] = {0};

    for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
        const char *frame = pcm.constData() + frameIndex * 320;
        for (int i = 0; i < 160; ++i) {
            std::int16_t sample = 0;
            std::memcpy(&sample, frame + i * 2, sizeof(sample));
            samples[i] = sample;
        }

        speex_bits_reset(&bits_enc);
        speex_encode(Enc_State, samples, &bits_enc);
        const int packetSize = speex_bits_write(&bits_enc, packet, sizeof(packet));
        if (packetSize <= 0)
            return;
        encoded.append(packet, packetSize);
    }

    Q_EMIT SIG_audioFrame(encoded);
#endif
}
