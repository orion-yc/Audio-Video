#ifndef VIDEO_READ_H
#define VIDEO_READ_H

#include <QObject>
#include <QImage>
#include <QThread>
#include <QTimer>
#include "video_common.h"
#include<myfacedetect.h>

class CameraCaptureWorker : public QObject
{
    Q_OBJECT

public:
    CameraCaptureWorker();

signals:
    void SIG_videoFrame(QImage image);
    void SIG_captureStateChanged(bool active);

public slots:
    void start();
    void pause();
    void setFunnyPic(int funnyPic);

private slots:
    void slot_readMore();

private:
    cv::VideoCapture m_capture;
    QTimer *m_timer;
    int m_captureFrameRate;
    int m_funnyPic;
    bool m_faceDetectionReady;
    QImage m_tuer;
    QImage m_hat;
    std::vector<cv::Rect> m_vecLastFace;
};

// The controller stays on the GUI thread while the device runs in a worker.
class Video_Read : public QObject
{
    Q_OBJECT
public:
    explicit Video_Read(QObject *parent = nullptr);
    ~Video_Read();
    enum funnyPic_type{ fp_tuer = 1 , fp_hat };

signals:
    void SIG_videoFrame(QImage image);
    void SIG_captureStateChanged(bool active);
public slots:
    void start();
    void pause();
    void setFunnyPic(int newFunnyPic);

private slots:
    void slot_captureStateChanged(bool active);

private:
    QThread m_captureThread;
    CameraCaptureWorker *m_worker;
    bool m_startRequested;
};

#endif // VIDEO_READ_H
