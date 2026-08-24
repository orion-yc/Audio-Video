#include <QCoreApplication>
#include <QDebug>

#include "h264codec.h"

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    QImage source(640, 360, QImage::Format_RGB888);
    for (int y = 0; y < source.height(); ++y) {
        auto *line = source.scanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            line[x * 3] = static_cast<uchar>((x * 255) / source.width());
            line[x * 3 + 1] = static_cast<uchar>((y * 255) / source.height());
            line[x * 3 + 2] = static_cast<uchar>(96);
        }
    }

    H264Encoder encoder;
    QByteArray encoded;
    bool keyFrame = false;
    QSize encodedSize;
    if (!encoder.encode(source, 30, 2000000, &encoded, &keyFrame, &encodedSize)) {
        qCritical() << "H.264 encode failed.";
        return 1;
    }

    H264Decoder decoder;
    QImage decoded;
    if (!decoder.decode(encoded, &decoded) || decoded.size() != encodedSize) {
        qCritical() << "H.264 decode failed.";
        return 2;
    }

    qInfo() << "H.264 smoke test passed with" << encoder.backendName()
            << encoded.size() << "bytes.";
    return 0;
}
