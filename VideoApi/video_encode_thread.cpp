#include "video_encode_thread.h"

VideoEncodeThread::VideoEncodeThread()
{
}

void VideoEncodeThread::slot_encodeFrame(QImage image, int frameRate, int bitRate,
                                         quint64 generation)
{
    QByteArray encoded;
    bool isKeyFrame = false;
    QSize encodedSize;
    if (m_encoder.encode(image, frameRate, bitRate,
                         &encoded, &isKeyFrame, &encodedSize)) {
        const int flags = isKeyFrame ? H264_PAYLOAD_FLAG_KEYFRAME : 0;
        Q_EMIT SIG_videoEncoded(encoded, flags,
                                 encodedSize.width(), encodedSize.height(), generation,
                                 m_encoder.backendName());
    }
    Q_EMIT SIG_encodeFinished();
}

void VideoEncodeThread::slot_reset()
{
    m_encoder.reset();
}
