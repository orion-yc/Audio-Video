#include "usershow.h"
#include "ui_usershow.h"

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
        const QSize availableSize(width(), qMax(1, height() - titleHeight));
        const QPixmap pixmap = QPixmap::fromImage(
            m_img.scaled(availableSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        const int x = (width() - pixmap.width()) / 2;
        const int y = titleHeight + (availableSize.height() - pixmap.height()) / 2;
        painter.drawPixmap(QPoint(x, y), pixmap);
        painter.restore();
    }

    painter.setPen(QPen(QColor("#3B1216"), 1));
    painter.drawPath(clip);
}

void UserShow::mousePressEvent(QMouseEvent *event)
{
    Q_EMIT SIG_userClicked(m_id, m_name);
    event->accept();
}

void UserShow::slot_checkTimeOut()
{
    if (!m_lastTime.isValid() || m_lastTime.secsTo(QTime::currentTime()) > 5)
    {
        m_img = m_avatarImg.isNull() ? m_defaultImg : m_avatarImg;
        update();
    }
}
