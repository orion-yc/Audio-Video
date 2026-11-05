#include "video_encode_thread.h"
#include "../diagnostics.h"

#include <QElapsedTimer>

VideoEncodeThread::VideoEncodeThread()
{
}

void VideoEncodeThread::slot_encodeFrame(QImage image, int frameRate, int bitRate,
                                         bool desktopContent, quint64 generation)
{
    QElapsedTimer timer;
    timer.start();
    QByteArray encoded;
    bool isKeyFrame = false;
    QSize encodedSize;
    if (m_encoder.encode(image, frameRate, bitRate, desktopContent,
                         &encoded, &isKeyFrame, &encodedSize)) {
        if (desktopContent) {
            DiagnosticLogger::instance().recordEncodedFrame(timer.nsecsElapsed() / 1000,
                                                            encodedSize.width(),
                                                            encodedSize.height(),
                                                            encoded.size(),
                                                            m_encoder.backendName());
        }
        const int flags = isKeyFrame ? H264_PAYLOAD_FLAG_KEYFRAME : 0;
        Q_EMIT SIG_videoEncoded(encoded, flags,
                                 encodedSize.width(), encodedSize.height(), generation,
                                 m_encoder.backendName());
    } else if (desktopContent) {
        DiagnosticLogger::instance().recordDroppedFrame(QStringLiteral("h264_encode_failed"));
    }
    Q_EMIT SIG_encodeFinished();
}

void VideoEncodeThread::slot_reset()
{
    m_encoder.reset();
}
