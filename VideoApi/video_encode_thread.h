#ifndef VIDEO_ENCODE_THREAD_H
#define VIDEO_ENCODE_THREAD_H

#include "h264codec.h"
#include "workthread.h"

class VideoEncodeThread : public WorkThread
{
    Q_OBJECT
public:
    VideoEncodeThread();

signals:
    void SIG_videoEncoded(QByteArray encoded, int flags, int width, int height,
                          quint64 generation, QString encoderName);
    void SIG_encodeFinished();

public slots:
    void slot_encodeFrame(QImage image, int frameRate, int bitRate, quint64 generation);
    void slot_reset();

private:
    H264Encoder m_encoder;
};

#endif // VIDEO_ENCODE_THREAD_H
