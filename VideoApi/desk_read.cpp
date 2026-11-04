#include "desk_read.h"
#include "../diagnostics.h"

#include <QCursor>
#include <QElapsedTimer>
#include <QPainter>
#include <QPolygonF>

namespace {

void overlayCursor(QImage *image, QScreen *screen, const QPoint &globalCursor)
{
    if (!image || image->isNull() || !screen) {
        return;
    }

    const QRect screenGeometry = screen->geometry();
    if (screenGeometry.isEmpty() || !screenGeometry.contains(globalCursor)) {
        return;
    }

    const qreal xScale = static_cast<qreal>(image->width()) / screenGeometry.width();
    const qreal yScale = static_cast<qreal>(image->height()) / screenGeometry.height();
    const QPointF cursorPosition((globalCursor.x() - screenGeometry.x()) * xScale,
                                 (globalCursor.y() - screenGeometry.y()) * yScale);
    const qreal scale = qBound<qreal>(0.75, image->width() / 1920.0, 1.4);

    QPainter painter(image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(cursorPosition);
    painter.setPen(QPen(Qt::white, qMax<qreal>(1.0, 1.8 * scale), Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(QColor(16, 20, 24));
    QPolygonF pointer;
    pointer << QPointF(0, 0)
            << QPointF(0, 18 * scale)
            << QPointF(4.5 * scale, 14 * scale)
            << QPointF(8.5 * scale, 23 * scale)
            << QPointF(12.5 * scale, 21 * scale)
            << QPointF(8.5 * scale, 12 * scale)
            << QPointF(15 * scale, 12 * scale);
    painter.drawPolygon(pointer);
}

} // namespace

Desk_Read::Desk_Read(QObject *parent) : QObject(parent)
  ,m_deskReadThread(new DeskReadThread)
  ,m_capturing(false)
  ,m_frameQueued(false)
  ,m_maxWidth(DESKTOP_IMAGE_WIDTH)
  ,m_maxHeight(DESKTOP_IMAGE_HEIGHT)
  ,m_frameRate(DESKTOP_FRAME_RATE)
  ,m_dxgiCapture(std::make_unique<DxgiDesktopCapture>())
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
    // A queued capture task from a disconnected or recreated display must
    // not leave the producer permanently marked as busy.
    m_frameQueued.store(false);
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

void Desk_Read::setCaptureScreenName(const QString &screenName)
{
    QMutexLocker locker(&m_captureScreenMutex);
    m_captureScreenName = screenName;
}

QScreen *Desk_Read::captureScreen() const
{
    QString screenName;
    {
        QMutexLocker locker(&m_captureScreenMutex);
        screenName = m_captureScreenName;
    }

    if (!screenName.isEmpty()) {
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (QScreen *screen : screens) {
            if (screen && screen->name() == screenName) {
                return screen;
            }
        }
    }
    return QGuiApplication::primaryScreen();
}

void Desk_Read::slot_scheduleFrame()
{
    if (!m_capturing.load()) {
        return;
    }

    // Query the cursor from the GUI thread.  The actual screenshot work runs
    // in a worker, while the position is atomically carried with that frame.
    const QPoint cursorPosition = QCursor::pos();
    m_cursorX.store(cursorPosition.x());
    m_cursorY.store(cursorPosition.y());

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
    // Capture the selected physical screen. Falling back to the primary screen
    // keeps sharing available if a selected monitor is disconnected mid-call.
    QScreen *src = captureScreen();

    if (!src) {
        m_frameQueued.store(false);
        return;
    }
    QImage image;
    QElapsedTimer captureTimer;
    captureTimer.start();
    DesktopCaptureTiming captureTiming;
    // DXGI duplication avoids the compositor round-trip and the QPixmap
    // conversion used by the legacy fallback. It also lets us reuse the last
    // desktop image when only the cursor moves.
    const bool dxgiCaptured = m_dxgiCapture->capture(src->name(), &image, &captureTiming);
    const QString backend = dxgiCaptured ? QStringLiteral("dxgi") : QStringLiteral("qscreen");
    if (backend != m_lastCaptureBackend) {
        m_lastCaptureBackend = backend;
        DiagnosticLogger::instance().logEvent(QStringLiteral("desktop_capture_backend"),
                                              {{QStringLiteral("backend"), backend},
                                               {QStringLiteral("screen"), src->name()}});
    }
    if (!dxgiCaptured) {
        image = src->grabWindow(0).toImage();
    }
    captureTiming.captureUs = captureTimer.nsecsElapsed() / 1000;
    const int maxWidth = m_maxWidth.load();
    const int maxHeight = m_maxHeight.load();
    QElapsedTimer scaleTimer;
    scaleTimer.start();
    if (image.width() > maxWidth || image.height() > maxHeight) {
        image = image.scaled(maxWidth, maxHeight,
                             Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    const qint64 scaleUs = scaleTimer.nsecsElapsed() / 1000;
    // QScreen::grabWindow does not include the hardware cursor on Windows.
    // Composite the current pointer into the shared pixels automatically so
    // viewers see mouse operations without any extra UI or notification.
    QElapsedTimer mouseTimer;
    mouseTimer.start();
    overlayCursor(&image, src, QPoint(m_cursorX.load(), m_cursorY.load()));
    const qint64 mouseUs = mouseTimer.nsecsElapsed() / 1000;
    if (m_capturing.load() && !image.isNull()) {
        DiagnosticLogger::instance().recordCaptureFrame(captureTiming, scaleUs, mouseUs,
                                                         image.width(), image.height());
        Q_EMIT SIG_screenFrame(image);
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
