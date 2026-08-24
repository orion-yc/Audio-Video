#ifndef DESK_READ_H
#define DESK_READ_H

#include <QObject>
#include<QTimer>
#include<QApplication>
#include<QScreen>
#include<QImage>
#include <atomic>
#include"video_common.h"
#include"workthread.h"

class Desk_Read;
class DeskReadThread: public WorkThread
{
    Q_OBJECT
public:

    void setDeskRead(Desk_Read *newDeskRead);

public slots:
    void slot_threadReadMore();

private:
    Desk_Read* m_deskRead;
};


class Desk_Read : public QObject
{
    Q_OBJECT
public:
    explicit Desk_Read(QObject *parent = nullptr);
    ~Desk_Read();
signals:
    void SIG_screenFrame(QImage img);  // 因为避免影响主线程，采用多线程，参数不能是引用

public slots:
    void start();
    void pause();
    void slot_scheduleFrame();
    void slot_getScreenFrame();
private:
    QTimer* m_timer;
    QSharedPointer<DeskReadThread> m_deskReadThread;
    std::atomic_bool m_capturing;
    std::atomic_bool m_frameQueued;
};

// 需要 输出 截屏(qt) -> 信号形式输出
// 需要 开启 关闭
// 定时器 超时 --> 槽函数 执行截屏 并发送信号
#endif // DESK_READ_H
