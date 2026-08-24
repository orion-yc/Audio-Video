#include "audio_read.h"

#include <cstdint>
#include <cstring>

Audio_Read::Audio_Read(QObject *parent)
    : QObject(parent),
      m_timer(nullptr),
      audio_in(nullptr),
      myBuffer_in(nullptr),
      m_recordState(audio_state::_Stop),
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
    m_recordState = audio_state::_Pause;
}

void Audio_Read::slot_readMore()
{
    constexpr qsizetype pcmBytes = 640;
    if (!audio_in || !myBuffer_in || audio_in->bytesAvailable() < pcmBytes)
        return;

    QByteArray pcm(pcmBytes, 0);
    if (myBuffer_in->read(pcm.data(), pcmBytes) != pcmBytes)
        return;

#ifndef USE_SPEEX
    Q_EMIT SIG_audioFrame(pcm);
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
