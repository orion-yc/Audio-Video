#ifndef AUDIO_WRITE_H
#define AUDIO_WRITE_H

#include <QObject>
#include "audio_common.h"

class Audio_Write : public QObject
{
    Q_OBJECT
public:
    explicit Audio_Write(QObject *parent = nullptr, bool pcmMode = false);
    ~Audio_Write();
signals:

public slots:
    void slot_net_rx( QByteArray ba );
    void slot_pcm_rx(const QByteArray &pcm);
private:
    QAudioFormat format;

    QAudioSink*     audio_out;
    QIODevice*      myBuffer_out;

    ///解码
    SpeexBits bits_dec;
    void *Dec_State;
};

#endif // AUDIO_WRITE_H
