#include "audio_write.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

Audio_Write::Audio_Write(QObject *parent)
    : QObject(parent),
      audio_out(nullptr),
      myBuffer_out(nullptr),
      Dec_State(nullptr)
{
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!device.isNull()) {
        if (!device.isFormatSupported(format))
            qWarning() << "The output device does not report native 8 kHz mono support";
        audio_out = new QAudioSink(device, format, this);
        myBuffer_out = audio_out->start();
    } else {
        qWarning() << "No audio output device is available";
    }

    speex_bits_init(&bits_dec);
    Dec_State = speex_decoder_init(speex_lib_get_mode(SPEEX_MODEID_NB));
}

Audio_Write::~Audio_Write()
{
    if (audio_out)
        audio_out->stop();
    delete audio_out;
    speex_bits_destroy(&bits_dec);
    if (Dec_State)
        speex_decoder_destroy(Dec_State);
}

void Audio_Write::slot_net_rx(QByteArray ba)
{
    if (!myBuffer_out || !Dec_State || ba.isEmpty())
        return;

#ifndef USE_SPEEX
    myBuffer_out->write(ba.constData(), std::min<qsizetype>(ba.size(), 640));
#else
    const qsizetype packetSize = ba.size() / 2;
    if (packetSize <= 0 || packetSize > 800 || ba.size() % 2 != 0)
        return;

    char pcm[640] = {0};
    float samples[160] = {0};
    for (int frameIndex = 0; frameIndex < 2; ++frameIndex) {
        char packet[800] = {0};
        std::memcpy(packet, ba.constData() + frameIndex * packetSize, packetSize);
        speex_bits_read_from(&bits_dec, packet, static_cast<int>(packetSize));
        if (speex_decode(Dec_State, &bits_dec, samples) != 0)
            return;

        for (int i = 0; i < 160; ++i) {
            const auto sample = static_cast<std::int16_t>(samples[i]);
            std::memcpy(pcm + frameIndex * 320 + i * 2, &sample, sizeof(sample));
        }
    }

    myBuffer_out->write(pcm, sizeof(pcm));
#endif
}
