#include "desk_read.h"

Desk_Read::Desk_Read(QObject *parent) : QObject(parent)
  ,m_deskReadThread(new DeskReadThread)
  ,m_capturing(false)
  ,m_frameQueued(false)
  ,m_maxWidth(DESKTOP_IMAGE_WIDTH)
  ,m_maxHeight(DESKTOP_IMAGE_HEIGHT)
  ,m_frameRate(DESKTOP_FRAME_RATE)
{
    m_deskReadThread->setDeskRead( this);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout,
            this, &Desk_Read::slot_scheduleFrame);

}

Desk_Read::~Desk_Read()
{
    pause();
    m_deskReadThread.reset();
}

void Desk_Read::start()
{
    if (m_capturing.exchange(true)) {
        return;
    }
    m_timer->start(qMax(1, 1000 / m_frameRate.load()));
}

void Desk_Read::pause()
{
    m_capturing.store(false);
    m_timer->stop();
}

void Desk_Read::setCaptureSettings(int maxWidth, int maxHeight, int frameRate)
{
    if (maxWidth <= 0 || maxWidth > DESKTOP_MAX_IMAGE_WIDTH ||
            maxHeight <= 0 || maxHeight > DESKTOP_MAX_IMAGE_HEIGHT ||
            frameRate <= 0 || frameRate > DESKTOP_MAX_FRAME_RATE) {
        return;
    }

    m_maxWidth.store(maxWidth);
    m_maxHeight.store(maxHeight);
    m_frameRate.store(frameRate);
    if (m_capturing.load()) {
        m_timer->start(qMax(1, 1000 / frameRate));
    }
}

void Desk_Read::slot_scheduleFrame()
{
    if (!m_capturing.load()) {
        return;
    }

    bool expected = false;
    if (!m_frameQueued.compare_exchange_strong(expected, true)) {
        return;
    }
    if (!QMetaObject::invokeMethod(m_deskReadThread.get(), "slot_threadReadMore",
                                   Qt::QueuedConnection)) {
        m_frameQueued.store(false);
    }
}

void Desk_Read::slot_getScreenFrame()
{
    if (!m_capturing.load()) {
        m_frameQueued.store(false);
        return;
    }
    // 获取桌面
    // 1.获取主屏
    QScreen *src = QApplication::primaryScreen();
    // QRect deskRect = QApplication::desktop()->screenGeometry();

    // 2.获取主屏的图片，可以选择截取的大小
    if (!src) {
        m_frameQueued.store(false);
        return;
    }
    QPixmap map = src->grabWindow(0);

    QImage image = map.toImage();
    const int maxWidth = m_maxWidth.load();
    const int maxHeight = m_maxHeight.load();
    if (image.width() > maxWidth || image.height() > maxHeight) {
        image = image.scaled(maxWidth, maxHeight,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (m_capturing.load() && !image.isNull()) {
        Q_EMIT SIG_screenFrame(image.copy());
    }
    m_frameQueued.store(false);
}

void DeskReadThread::setDeskRead(Desk_Read *newDeskRead)
{
    m_deskRead = newDeskRead;
}

void DeskReadThread::slot_threadReadMore()
{
    if (m_deskRead) {
        m_deskRead->slot_getScreenFrame();
    }
}
