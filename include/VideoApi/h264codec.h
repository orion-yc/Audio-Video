#ifndef H264CODEC_H
#define H264CODEC_H

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QtGlobal>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

constexpr quint32 H264_PAYLOAD_MAGIC = 0x34363248u; // "H264" in little-endian byte order.
constexpr quint16 H264_PAYLOAD_VERSION = 1;
constexpr quint16 H264_PAYLOAD_FLAG_KEYFRAME = 0x0001;

#pragma pack(push, 1)
struct H264PayloadHeader
{
    quint32 magic;
    quint16 version;
    quint16 flags;
    quint16 width;
    quint16 height;
    quint32 dataSize;
};
#pragma pack(pop)

static_assert(sizeof(H264PayloadHeader) == 16, "Unexpected H.264 payload header size");

class H264Encoder
{
public:
    H264Encoder();
    ~H264Encoder();

    bool encode(const QImage &image, int frameRate, int bitRate, bool desktopContent,
                QByteArray *encoded, bool *isKeyFrame, QSize *encodedSize);
    void reset();
    void requestKeyFrame();
    QString backendName() const;

private:
    bool openForImage(const QImage &image, int frameRate, int bitRate,
                      bool desktopContent);
    bool openSoftwareFallback(const QImage &image, int frameRate, int bitRate,
                              bool desktopContent);
    bool openEncoder(const char *encoderName, const QImage &image,
                     int frameRate, int bitRate, bool desktopContent);

    AVCodecContext *m_context;
    AVFrame *m_frame;
    AVPacket *m_packet;
    SwsContext *m_scaler;
    QSize m_size;
    int m_frameRate;
    int m_bitRate;
    bool m_desktopContent;
    qint64 m_nextPts;
    QString m_backendName;
    bool m_forceKeyFrame = false;
};

class H264Decoder
{
public:
    H264Decoder();
    ~H264Decoder();

    bool decode(const QByteArray &encoded, QImage *image);

private:
    bool ensureOpened();

    AVCodecContext *m_context;
    AVFrame *m_frame;
    AVPacket *m_packet;
    SwsContext *m_scaler;
    QSize m_size;
    int m_pixelFormat;
};

#endif // H264CODEC_H
