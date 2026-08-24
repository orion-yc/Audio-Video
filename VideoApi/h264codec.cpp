#include "h264codec.h"

#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace {

QString ffmpegError(int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

bool isNvenc(const char *name)
{
    return qstrcmp(name, "h264_nvenc") == 0;
}

bool isOpenH264(const char *name)
{
    return qstrcmp(name, "libopenh264") == 0;
}

} // namespace

H264Encoder::H264Encoder()
    : m_context(nullptr)
    , m_frame(nullptr)
    , m_packet(nullptr)
    , m_scaler(nullptr)
    , m_frameRate(0)
    , m_bitRate(0)
    , m_nextPts(0)
{
}

H264Encoder::~H264Encoder()
{
    reset();
}

QString H264Encoder::backendName() const
{
    return m_backendName;
}

void H264Encoder::reset()
{
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_context) {
        avcodec_free_context(&m_context);
    }
    if (m_scaler) {
        sws_freeContext(m_scaler);
        m_scaler = nullptr;
    }
    m_size = QSize();
    m_frameRate = 0;
    m_bitRate = 0;
    m_nextPts = 0;
    m_backendName.clear();
}

bool H264Encoder::openEncoder(const char *encoderName, const QImage &image,
                              int frameRate, int bitRate)
{
    const AVCodec *codec = avcodec_find_encoder_by_name(encoderName);
    if (!codec) {
        return false;
    }

    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (!context) {
        return false;
    }

    const AVPixelFormat pixelFormat = isOpenH264(encoderName)
            ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
    context->codec_type = AVMEDIA_TYPE_VIDEO;
    context->codec_id = AV_CODEC_ID_H264;
    context->width = image.width() & ~1;
    context->height = image.height() & ~1;
    context->pix_fmt = pixelFormat;
    context->time_base = AVRational{1, frameRate};
    context->framerate = AVRational{frameRate, 1};
    context->bit_rate = bitRate;
    context->rc_max_rate = bitRate;
    context->rc_buffer_size = bitRate;
    context->gop_size = frameRate * 2;
    context->max_b_frames = 0;
    context->flags |= AV_CODEC_FLAG_LOW_DELAY;

    av_opt_set(context->priv_data, "repeat_headers", "1", 0);
    if (isNvenc(encoderName)) {
        av_opt_set(context->priv_data, "preset", "p4", 0);
        av_opt_set(context->priv_data, "tune", "ll", 0);
        av_opt_set(context->priv_data, "rc", "cbr", 0);
        av_opt_set(context->priv_data, "zerolatency", "1", 0);
        av_opt_set(context->priv_data, "forced-idr", "1", 0);
    } else if (isOpenH264(encoderName)) {
        av_opt_set(context->priv_data, "rc_mode", "bitrate", 0);
        av_opt_set(context->priv_data, "allow_skip_frames", "1", 0);
    } else {
        av_opt_set(context->priv_data, "rate_control", "ld_vbr", 0);
        av_opt_set(context->priv_data, "scenario", "video_conference", 0);
        av_opt_set(context->priv_data, "hw_encoding", "0", 0);
    }

    const int openResult = avcodec_open2(context, codec, nullptr);
    if (openResult < 0) {
        qInfo().noquote() << "H.264 encoder" << encoderName
                          << "is unavailable:" << ffmpegError(openResult);
        avcodec_free_context(&context);
        return false;
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (!frame || !packet) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
        return false;
    }

    frame->format = context->pix_fmt;
    frame->width = context->width;
    frame->height = context->height;
    const int frameResult = av_frame_get_buffer(frame, 32);
    if (frameResult < 0) {
        qWarning().noquote() << "Unable to allocate H.264 frame:" << ffmpegError(frameResult);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
        return false;
    }

    m_context = context;
    m_frame = frame;
    m_packet = packet;
    m_size = QSize(context->width, context->height);
    m_frameRate = frameRate;
    m_bitRate = bitRate;
    m_backendName = QString::fromLatin1(encoderName);
    qInfo() << "H.264 encoder selected:" << m_backendName
            << m_size << frameRate << "fps" << bitRate / 1000000 << "Mbps";
    return true;
}

bool H264Encoder::openForImage(const QImage &image, int frameRate, int bitRate)
{
    // Keep the primary hardware path first. libopenh264 is the software fallback.
    return openEncoder("h264_nvenc", image, frameRate, bitRate)
            || openSoftwareFallback(image, frameRate, bitRate);
}

bool H264Encoder::openSoftwareFallback(const QImage &image, int frameRate, int bitRate)
{
    return openEncoder("libopenh264", image, frameRate, bitRate)
            || openEncoder("h264_mf", image, frameRate, bitRate);
}

bool H264Encoder::encode(const QImage &image, int frameRate, int bitRate,
                         QByteArray *encoded, bool *isKeyFrame, QSize *encodedSize)
{
    if (!encoded || !isKeyFrame || !encodedSize || image.isNull()) {
        return false;
    }

    const QSize requiredSize(image.width() & ~1, image.height() & ~1);
    if (requiredSize.width() < 2 || requiredSize.height() < 2) {
        return false;
    }
    if (!m_context || requiredSize != m_size || frameRate != m_frameRate || bitRate != m_bitRate) {
        reset();
        if (!openForImage(image, frameRate, bitRate)) {
            qWarning() << "No H.264 encoder is available.";
            return false;
        }
    }

    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    if (rgb.width() != m_size.width() || rgb.height() != m_size.height()) {
        rgb = rgb.copy(0, 0, m_size.width(), m_size.height());
    }

    m_scaler = sws_getCachedContext(m_scaler,
                                    rgb.width(), rgb.height(), AV_PIX_FMT_RGB24,
                                    m_size.width(), m_size.height(), m_context->pix_fmt,
                                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_scaler) {
        qWarning() << "Unable to create the H.264 color converter.";
        return false;
    }

    const int writableResult = av_frame_make_writable(m_frame);
    if (writableResult < 0) {
        qWarning().noquote() << "Unable to write H.264 frame:" << ffmpegError(writableResult);
        return false;
    }

    const uint8_t *sourceData[] = { rgb.constBits() };
    const int sourceStride[] = { static_cast<int>(rgb.bytesPerLine()) };
    sws_scale(m_scaler, sourceData, sourceStride, 0, rgb.height(),
              m_frame->data, m_frame->linesize);
    m_frame->pts = m_nextPts++;

    const int sendResult = avcodec_send_frame(m_context, m_frame);
    if (sendResult < 0) {
        qWarning().noquote() << "Unable to encode H.264 frame:" << ffmpegError(sendResult);
        if (isNvenc(m_backendName.toLatin1().constData())) {
            qInfo() << "NVENC frame encoding failed; switching to software H.264.";
            reset();
            if (openSoftwareFallback(image, frameRate, bitRate)) {
                return encode(image, frameRate, bitRate, encoded, isKeyFrame, encodedSize);
            }
        }
        return false;
    }

    encoded->clear();
    *isKeyFrame = false;
    while (true) {
        const int receiveResult = avcodec_receive_packet(m_context, m_packet);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
            break;
        }
        if (receiveResult < 0) {
            qWarning().noquote() << "Unable to read H.264 packet:" << ffmpegError(receiveResult);
            if (isNvenc(m_backendName.toLatin1().constData())) {
                qInfo() << "NVENC packet encoding failed; switching to software H.264.";
                reset();
                if (openSoftwareFallback(image, frameRate, bitRate)) {
                    return encode(image, frameRate, bitRate, encoded, isKeyFrame, encodedSize);
                }
            }
            return false;
        }
        encoded->append(reinterpret_cast<const char *>(m_packet->data), m_packet->size);
        *isKeyFrame = *isKeyFrame || (m_packet->flags & AV_PKT_FLAG_KEY);
        av_packet_unref(m_packet);
    }

    *encodedSize = m_size;
    if (!encoded->isEmpty()) {
        return true;
    }
    if (isNvenc(m_backendName.toLatin1().constData())) {
        qInfo() << "NVENC produced no packet; switching to software H.264.";
        reset();
        if (openSoftwareFallback(image, frameRate, bitRate)) {
            return encode(image, frameRate, bitRate, encoded, isKeyFrame, encodedSize);
        }
    }
    return false;
}

H264Decoder::H264Decoder()
    : m_context(nullptr)
    , m_frame(nullptr)
    , m_packet(nullptr)
    , m_scaler(nullptr)
    , m_pixelFormat(-1)
{
}

H264Decoder::~H264Decoder()
{
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_context) {
        avcodec_free_context(&m_context);
    }
    if (m_scaler) {
        sws_freeContext(m_scaler);
    }
}

bool H264Decoder::ensureOpened()
{
    if (m_context) {
        return true;
    }

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qWarning() << "H.264 decoder is unavailable.";
        return false;
    }

    m_context = avcodec_alloc_context3(codec);
    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_context || !m_frame || !m_packet) {
        return false;
    }

    const int openResult = avcodec_open2(m_context, codec, nullptr);
    if (openResult < 0) {
        qWarning().noquote() << "Unable to open H.264 decoder:" << ffmpegError(openResult);
        return false;
    }
    return true;
}

bool H264Decoder::decode(const QByteArray &encoded, QImage *image)
{
    if (!image || encoded.isEmpty() || !ensureOpened()) {
        return false;
    }

    av_packet_unref(m_packet);
    const int allocationResult = av_new_packet(m_packet, encoded.size());
    if (allocationResult < 0) {
        qWarning().noquote() << "Unable to allocate H.264 packet:" << ffmpegError(allocationResult);
        return false;
    }
    memcpy(m_packet->data, encoded.constData(), static_cast<size_t>(encoded.size()));

    const int sendResult = avcodec_send_packet(m_context, m_packet);
    if (sendResult < 0) {
        qWarning().noquote() << "Unable to decode H.264 packet:" << ffmpegError(sendResult);
        return false;
    }

    bool decoded = false;
    while (true) {
        const int receiveResult = avcodec_receive_frame(m_context, m_frame);
        if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) {
            break;
        }
        if (receiveResult < 0) {
            qWarning().noquote() << "Unable to read H.264 frame:" << ffmpegError(receiveResult);
            return false;
        }

        const QSize frameSize(m_frame->width, m_frame->height);
        m_scaler = sws_getCachedContext(m_scaler,
                                        frameSize.width(), frameSize.height(),
                                        static_cast<AVPixelFormat>(m_frame->format),
                                        frameSize.width(), frameSize.height(),
                                        AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR,
                                        nullptr, nullptr, nullptr);
        if (!m_scaler) {
            qWarning() << "Unable to create the H.264 display converter.";
            return false;
        }

        QImage rgb(frameSize, QImage::Format_RGB888);
        uint8_t *targetData[] = { rgb.bits() };
        const int targetStride[] = { static_cast<int>(rgb.bytesPerLine()) };
        sws_scale(m_scaler, m_frame->data, m_frame->linesize, 0, frameSize.height(),
                  targetData, targetStride);
        *image = rgb;
        m_size = frameSize;
        m_pixelFormat = m_frame->format;
        decoded = true;
    }
    return decoded;
}
