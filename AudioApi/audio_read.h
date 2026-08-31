#ifndef AUDIO_READ_H
#define AUDIO_READ_H

#include <QObject>
#include "audio_common.h"
#include <QQueue>
#include <QElapsedTimer>
#include <QTimer>

class Audio_Read : public QObject
{
    Q_OBJECT
public:
    explicit Audio_Read(QObject *parent = nullptr);
    ~Audio_Read();

    //方便控制开始暂停  添加状态
    enum audio_state{ _Stop , _Record , _Pause };
signals:
    //定时采集的数据, 以信号形式发送
    void SIG_audioFrame( QByteArray &ba);
public slots:
//开始采集
    void start();
//暂停采集
    void pause();
    // Enable/disable mixing of the WASAPI system-output stream.
    void setSystemAudioEnabled(bool enabled);
    void slot_systemAudioFrame(const QByteArray &pcm);
//定时超时槽函数
    void slot_readMore();
private:
    void drainMixedFrames();
    void encodeAndEmit(const QByteArray &pcm);

    QAudioFormat    format;
    QTimer*         m_timer;
    QAudioSource*   audio_in ;
    QIODevice*      myBuffer_in ;
    int             m_recordState;
    bool            m_systemAudioEnabled;
    QByteArray      m_micPcmPending;
    QByteArray      m_systemPcmPending;
    QElapsedTimer   m_mixWaitTimer;
    ///编码
    //SPEEX 相关变量
    SpeexBits bits_enc;
    void *Enc_State;



};

#endif // AUDIO_READ_H
