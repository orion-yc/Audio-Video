#include "video_decode_worker.h"

VideoDecodeWorker::VideoDecodeWorker(QObject *parent)
    : QObject(parent)
{
}

void VideoDecodeWorker::slot_decodeH264(int userId, int roomId, quint64 generation,
                                        QByteArray encoded, int flags)
{
    auto &decoder = m_decoders[userId];
    if (!decoder) {
        decoder = std::make_unique<H264Decoder>();
    }

    QImage image;
    bool decoded = decoder->decode(encoded, &image);
    if (!decoded && (flags & H264_PAYLOAD_FLAG_KEYFRAME)) {
        decoder = std::make_unique<H264Decoder>();
        decoded = decoder->decode(encoded, &image);
    }
    Q_EMIT SIG_frameDecoded(userId, roomId, generation, image, decoded && !image.isNull());
}

void VideoDecodeWorker::slot_decodeImage(int userId, int roomId, quint64 generation,
                                         QByteArray encoded)
{
    QImage image;
    const bool decoded = image.loadFromData(encoded);
    Q_EMIT SIG_frameDecoded(userId, roomId, generation, image, decoded && !image.isNull());
}

void VideoDecodeWorker::slot_resetUser(int userId)
{
    m_decoders.erase(userId);
}

void VideoDecodeWorker::slot_resetAll()
{
    m_decoders.clear();
}
