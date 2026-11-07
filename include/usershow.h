#ifndef USERSHOW_H
#define USERSHOW_H

#include <QWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <QTime>
namespace Ui {
class UserShow;
}

class CKernal;
class RoomDialog;
class UserShow : public QWidget
{
    Q_OBJECT
signals:
    void SIG_userClicked(int id, QString name);
public:
    explicit UserShow(QWidget *parent = nullptr);
    ~UserShow();
public slots:
    void slot_setInfo(int id, QString name);
    void slot_setImage(QImage &img);
    void slot_setAvatarImage(const QImage &img);
    void slot_setPreviewImage(const QImage &img);
    void slot_clearVideo();
    void slot_setViewScale(double scale);
    // 重写重绘事件
    void paintEvent(QPaintEvent* event);
    void mousePressEvent(QMouseEvent * event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);

    void slot_checkTimeOut();
private:
    void clampPanOffset(const QSizeF &imageSize, const QRectF &availableRect);

    Ui::UserShow *ui;

    int m_id;
    QString m_name;
    friend class CKernal;
    friend class RoomDialog;

    QImage m_img;
    QImage m_defaultImg;
    QImage m_avatarImg;
    QTime  m_lastTime;
    QTimer  m_timer;
    double m_viewScale = 0.0;
    QPointF m_panOffset;
    QPointF m_lastDragPosition;
    QPointF m_dragStartPosition;
    bool m_dragging = false;
    bool m_dragMoved = false;
};

// 添加点击事件 点击发送信号 发送内容 id， name
// 一段时间收不到图片，就将图片设置为默认图片
// 定时器检测， 最后一次收到画面的时间，与现在当前的时间进行比较超过5秒 就设置成默认
#endif // USERSHOW_H
