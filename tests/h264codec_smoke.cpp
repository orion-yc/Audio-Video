#include <QGuiApplication>
#include <QDebug>
#include <QPainter>

#include <cmath>
#include <cstdio>

#include "h264codec.h"

namespace {

QImage desktopTestImage()
{
    QImage image(2560, 1440, QImage::Format_RGB888);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(24, 28, 32));
    painter.setFont(QFont("Microsoft YaHei UI", 12));
    for (int row = 0; row < 34; ++row) {
        painter.drawText(80, 80 + row * 34,
                         QString("Audio-Video desktop text clarity row %1  中文小字显示测试")
                         .arg(row + 1));
    }
    for (int x = 1450; x < 2380; ++x) {
        const QColor color = ((x - 1450) % 3 == 0) ? QColor(220, 25, 45)
                           : ((x - 1450) % 3 == 1) ? QColor(20, 155, 70)
                                                   : QColor(20, 75, 220);
        painter.setPen(color);
        painter.drawLine(x, 160, x, 1180);
    }
    painter.end();
    return image;
}

bool encodeAndDecode(const QImage &source, bool desktopContent,
                     QImage *decoded, QString *backend)
{
    H264Encoder encoder;
    H264Decoder decoder;
    // A receiver that joins an existing room must be able to start from a
    // later IDR frame.  Do not feed this decoder the initial sequence so the
    // test catches an encoder that only makes its very first key frame usable.
    H264Decoder joiningDecoder;
    const int bitRate = desktopContent ? 48000000 : 32000000;
    bool joiningDecoderStarted = false;
    bool joiningDecoderProducedImage = false;
    int joiningDecoderKeyFrame = -1;
    bool requestedKeyFrameProduced = false;
    for (int frame = 0; frame < 130; ++frame) {
        if (frame == 10) encoder.requestKeyFrame();
        QByteArray encoded;
        bool keyFrame = false;
        QSize encodedSize;
        const bool produced = encoder.encode(source, 60, bitRate, desktopContent,
                                             &encoded, &keyFrame, &encodedSize);
        if (frame == 0) {
            *backend = encoder.backendName();
        }
        if (produced && decoder.decode(encoded, decoded)
                && decoded->size() == encodedSize) {
            *backend = encoder.backendName();
        }
        if (produced && keyFrame && frame > 0) {
            if (frame >= 10 && frame <= 12) requestedKeyFrameProduced = true;
            joiningDecoderStarted = true;
            if (joiningDecoderKeyFrame < 0) {
                joiningDecoderKeyFrame = frame;
            }
        }
        if (produced && joiningDecoderStarted) {
            QImage joinedImage;
            if (joiningDecoder.decode(encoded, &joinedImage)
                    && joinedImage.size() == encodedSize) {
                joiningDecoderProducedImage = true;
            }
        }
    }
    return !decoded->isNull() && requestedKeyFrameProduced && joiningDecoderStarted && joiningDecoderProducedImage
            && joiningDecoderKeyFrame <= 30;
}

double psnr(const QImage &reference, const QImage &decoded)
{
    const QImage left = reference.convertToFormat(QImage::Format_RGB888);
    const QImage right = decoded.convertToFormat(QImage::Format_RGB888);
    if (left.size() != right.size()) {
        return 0.0;
    }

    long double squaredError = 0.0;
    const qsizetype bytesPerRow = static_cast<qsizetype>(left.width()) * 3;
    for (int y = 0; y < left.height(); ++y) {
        const uchar *a = left.constScanLine(y);
        const uchar *b = right.constScanLine(y);
        for (qsizetype x = 0; x < bytesPerRow; ++x) {
            const int difference = static_cast<int>(a[x]) - static_cast<int>(b[x]);
            squaredError += static_cast<long double>(difference * difference);
        }
    }
    const long double samples = static_cast<long double>(left.width())
            * left.height() * 3;
    const long double meanSquaredError = squaredError / samples;
    return meanSquaredError <= 0.0
            ? 99.0
            : 10.0 * std::log10((255.0 * 255.0) /
                                static_cast<double>(meanSquaredError));
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    const QImage source = desktopTestImage();

    QImage cameraDecoded;
    QString cameraBackend;
    if (!encodeAndDecode(source, false, &cameraDecoded, &cameraBackend)) {
        qCritical() << "H.264 4:2:0 encode/decode failed.";
        return 1;
    }

    QImage desktopDecoded;
    QString desktopBackend;
    if (!encodeAndDecode(source, true, &desktopDecoded, &desktopBackend)) {
        qCritical() << "H.264 desktop encode/decode failed.";
        return 2;
    }

    const bool requireNvenc = qEnvironmentVariableIntValue("AUDIO_VIDEO_REQUIRE_NVENC") != 0;
    if (requireNvenc && desktopBackend != "h264_nvenc") {
        qCritical() << "Desktop encoding did not retain NVENC:" << desktopBackend;
        return 3;
    }

    const QRect detailRegion(1450, 160, 930, 1020);
    const double cameraPsnr = psnr(source.copy(detailRegion),
                                   cameraDecoded.copy(detailRegion));
    const double desktopPsnr = psnr(source.copy(detailRegion),
                                    desktopDecoded.copy(detailRegion));
    std::printf("detail_quality camera=%s %.2f dB desktop=%s %.2f dB\n",
                cameraBackend.toLocal8Bit().constData(), cameraPsnr,
                desktopBackend.toLocal8Bit().constData(), desktopPsnr);
    std::fflush(stdout);

    std::printf("H.264 smoke test passed. Camera: %s %.2f dB; "
                "desktop: %s %.2f dB.\n",
                cameraBackend.toLocal8Bit().constData(), cameraPsnr,
                desktopBackend.toLocal8Bit().constData(), desktopPsnr);
    return 0;
}
