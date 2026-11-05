#include "usershow.h"
#include "ui_usershow.h"

#include <QApplication>
#include <QPainterPath>

UserShow::UserShow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::UserShow), m_id(0)
{
    ui->setupUi(this);

    m_defaultImg.load(":/tx/36.jpg");

    connect(&m_timer, SIGNAL(timeout()),
            this, SLOT(slot_checkTimeOut()));

    m_timer.start(1000);

    // Initialize without treating the fallback image as a received video frame.
    m_img = m_defaultImg;

}

UserShow::~UserShow()
{
    m_timer.stop();
    delete ui;
}

void UserShow::slot_setInfo(int id, QString name)
{
    if (m_id != id) {
        m_panOffset = QPointF();
        m_dragging = false;
        m_dragMoved = false;
        setCursor(m_viewScale > 0.0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    m_id = id;
    m_name = name.trimmed();
    ui->lb_name->setText(m_name.isEmpty() ? "未设置昵称" : m_name);
}

void UserShow::slot_setImage(QImage &img)
{
    m_lastTime = QTime::currentTime();
    m_img = img;
    update();
}

void UserShow::slot_setAvatarImage(const QImage &img)
{
    if (img.isNull()) {
        return;
    }

    m_avatarImg = img;
    if (!m_lastTime.isValid() || m_lastTime.secsTo(QTime::currentTime()) > 5) {
        m_img = m_avatarImg;
        update();
    }
}

void UserShow::slot_setPreviewImage(const QImage &img)
{
    if (img.isNull()) {
        return;
    }
    m_img = img;
    update();
}

void UserShow::slot_clearVideo()
{
    m_lastTime = QTime();
    m_img = m_avatarImg.isNull() ? m_defaultImg : m_avatarImg;
    update();
}

void UserShow::slot_setViewScale(double scale)
{
    m_viewScale = scale <= 0.0 ? 0.0 : qBound(1.0, scale, 1.5);
    m_panOffset = QPointF();
    m_dragging = false;
    setCursor(m_viewScale > 0.0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void UserShow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF surface = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(surface, 6, 6);
    painter.fillPath(clip, QColor("#090A0C"));

    if (m_img.size().height() > 0) {
        painter.save();
        painter.setClipPath(clip);

        const int titleHeight = ui->lb_name->height();
        const QRectF availableRect(0, titleHeight, width(),
                                   qMax(1, height() - titleHeight));
        QSizeF targetSize = m_img.size();
        if (m_viewScale <= 0.0) {
            targetSize.scale(availableRect.size(), Qt::KeepAspectRatio);
            m_panOffset = QPointF();
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        } else {
            const double logicalScale = m_viewScale / devicePixelRatioF();
            targetSize = QSizeF(m_img.width() * logicalScale,
                                m_img.height() * logicalScale);
            clampPanOffset(targetSize, availableRect);
            painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                  !qFuzzyCompare(m_viewScale, 1.0));
        }
        QRectF targetRect(QPointF(), targetSize);
        targetRect.moveCenter(availableRect.center() + m_panOffset);
        painter.drawImage(targetRect, m_img);
        painter.restore();
    }

    painter.setPen(QPen(QColor("#3B1216"), 1));
    painter.drawPath(clip);
}

void UserShow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_viewScale > 0.0) {
        m_dragging = true;
        m_dragMoved = false;
        m_lastDragPosition = event->position();
        m_dragStartPosition = m_lastDragPosition;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    Q_EMIT SIG_userClicked(m_id, m_name);
    event->accept();
}

void UserShow::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPointF delta = event->position() - m_lastDragPosition;
    m_lastDragPosition = event->position();
    m_panOffset += delta;
    const QPointF totalDelta = event->position() - m_dragStartPosition;
    m_dragMoved = m_dragMoved
            || totalDelta.manhattanLength() >= QApplication::startDragDistance();
    update();
    event->accept();
}

void UserShow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        if (!m_dragMoved) {
            Q_EMIT SIG_userClicked(m_id, m_name);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void UserShow::clampPanOffset(const QSizeF &imageSize, const QRectF &availableRect)
{
    const double horizontalLimit = qMax(0.0,
                                        (imageSize.width() - availableRect.width()) / 2.0);
    const double verticalLimit = qMax(0.0,
                                      (imageSize.height() - availableRect.height()) / 2.0);
    m_panOffset.setX(qBound(-horizontalLimit, m_panOffset.x(), horizontalLimit));
    m_panOffset.setY(qBound(-verticalLimit, m_panOffset.y(), verticalLimit));
}

void UserShow::slot_checkTimeOut()
{
    if (!m_lastTime.isValid() || m_lastTime.secsTo(QTime::currentTime()) > 5)
    {
        m_img = m_avatarImg.isNull() ? m_defaultImg : m_avatarImg;
        update();
    }
}
