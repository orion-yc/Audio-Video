#include "video_read.h"

#include <QPainter>

CameraCaptureWorker::CameraCaptureWorker()
    : m_timer(nullptr)
    , m_captureFrameRate(CAMERA_FRAME_RATE)
    , m_funnyPic(0)
    , m_faceDetectionReady(false)
    , m_tuer(":/images/tuer.png")
    , m_hat(":/images/hat.png")
{
}

void CameraCaptureWorker::start()
{
    if (m_capture.isOpened()) {
        if (m_timer && !m_timer->isActive()) {
            m_timer->start(qMax(1, 1000 / m_captureFrameRate));
        }
        Q_EMIT SIG_captureStateChanged(true);
        return;
    }

    if (!m_capture.open(0, cv::CAP_DSHOW)) {
        m_capture.release();
        m_capture.open(0);
    }
    if (!m_capture.isOpened()) {
        Q_EMIT SIG_captureStateChanged(false);
        return;
    }

    m_capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
    m_capture.set(cv::CAP_PROP_FRAME_WIDTH, CAMERA_IMAGE_WIDTH);
    m_capture.set(cv::CAP_PROP_FRAME_HEIGHT, CAMERA_IMAGE_HEIGHT);
    m_capture.set(cv::CAP_PROP_FPS, CAMERA_FRAME_RATE);
    m_capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

    const int reportedFrameRate = qRound(m_capture.get(cv::CAP_PROP_FPS));
    m_captureFrameRate = reportedFrameRate > 0
            ? qMin(CAMERA_FRAME_RATE, reportedFrameRate)
            : CAMERA_FRAME_RATE;
    m_captureFrameRate = qMax(1, m_captureFrameRate);

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &CameraCaptureWorker::slot_readMore);
    }
    m_timer->start(qMax(1, 1000 / m_captureFrameRate));
    QTimer::singleShot(0, this, &CameraCaptureWorker::slot_readMore);
    Q_EMIT SIG_captureStateChanged(true);
}

void CameraCaptureWorker::pause()
{
    const bool wasActive = m_capture.isOpened() || (m_timer && m_timer->isActive());
    if (m_timer) {
        m_timer->stop();
    }
    if (m_capture.isOpened()) {
        m_capture.release();
    }
    m_vecLastFace.clear();
    if (wasActive) {
        Q_EMIT SIG_captureStateChanged(false);
    }
}

void CameraCaptureWorker::setFunnyPic(int funnyPic)
{
    if (funnyPic != 0 && !m_faceDetectionReady) {
        // Normal camera use does not need cascade loading. Defer it until an
        // effect actually needs face detection.
        MyFaceDetect::FaceDetectInit();
        m_faceDetectionReady = true;
    }
    m_funnyPic = funnyPic;
}

void CameraCaptureWorker::slot_readMore()
{
    Mat frame;
    if (!m_capture.read(frame) || frame.empty()) {
        return;
    }

    cvtColor(frame, frame, cv::COLOR_BGR2RGB);
    std::vector<Rect> faces;
    if (m_funnyPic) {
        if (!m_faceDetectionReady) {
            MyFaceDetect::FaceDetectInit();
            m_faceDetectionReady = true;
        }
        MyFaceDetect::detectAndDisplay(frame, faces);
    }

    QImage image(reinterpret_cast<const uchar *>(frame.data), frame.cols, frame.rows,
                 QImage::Format_RGB888);

    QImage *overlay = nullptr;
    if (m_funnyPic == Video_Read::fp_tuer) {
        overlay = &m_tuer;
    } else if (m_funnyPic == Video_Read::fp_hat) {
        overlay = &m_hat;
    }

    if (!faces.empty()) {
        m_vecLastFace = faces;
    }
    if (overlay) {
        QPainter painter(&image);
        for (const Rect &face : m_vecLastFace) {
            const int x = face.x + face.width / 2 - overlay->width() / 2 + 20;
            const int y = face.y - overlay->height();
            painter.drawImage(QPoint(x, y), *overlay);
        }
    }

    if (image.width() > CAMERA_IMAGE_WIDTH || image.height() > CAMERA_IMAGE_HEIGHT) {
        image = image.scaled(CAMERA_IMAGE_WIDTH, CAMERA_IMAGE_HEIGHT,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    Q_EMIT SIG_videoFrame(image.copy());
}

Video_Read::Video_Read(QObject *parent)
    : QObject(parent)
    , m_worker(new CameraCaptureWorker)
    , m_startRequested(false)
{
    m_worker->moveToThread(&m_captureThread);
    connect(&m_captureThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CameraCaptureWorker::SIG_videoFrame,
            this, &Video_Read::SIG_videoFrame);
    connect(m_worker, &CameraCaptureWorker::SIG_captureStateChanged,
            this, &Video_Read::slot_captureStateChanged);
    m_captureThread.start();
}

Video_Read::~Video_Read()
{
    if (m_captureThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, "pause", Qt::BlockingQueuedConnection);
        m_captureThread.quit();
        m_captureThread.wait(3000);
    }
    m_worker = nullptr;
}

void Video_Read::start()
{
    if (m_startRequested) {
        return;
    }
    m_startRequested = true;
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
}

void Video_Read::pause()
{
    m_startRequested = false;
    if (m_captureThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, "pause", Qt::QueuedConnection);
    }
}

void Video_Read::setFunnyPic(int newFunnyPic)
{
    if (m_captureThread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, "setFunnyPic", Qt::QueuedConnection,
                                  Q_ARG(int, newFunnyPic));
    }
}

void Video_Read::slot_captureStateChanged(bool active)
{
    if (!active) {
        m_startRequested = false;
    }
    Q_EMIT SIG_captureStateChanged(active);
}
