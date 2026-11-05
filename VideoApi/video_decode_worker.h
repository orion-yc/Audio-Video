#ifndef VIDEO_DECODE_WORKER_H
#define VIDEO_DECODE_WORKER_H

#include <QByteArray>
#include <QImage>
#include <QObject>

#include <map>
#include <memory>

#include "h264codec.h"

class VideoDecodeWorker : public QObject
{
    Q_OBJECT
public:
    explicit VideoDecodeWorker(QObject *parent = nullptr);

public slots:
    void slot_decodeH264(int userId, int roomId, quint64 generation,
                         QByteArray encoded, int flags);
    void slot_decodeImage(int userId, int roomId, quint64 generation,
                          QByteArray encoded);
    void slot_resetUser(int userId);
    void slot_resetAll();

signals:
    void SIG_frameDecoded(int userId, int roomId, quint64 generation,
                          QImage image, bool decoded);

private:
    std::map<int, std::unique_ptr<H264Decoder>> m_decoders;
};

#endif // VIDEO_DECODE_WORKER_H
