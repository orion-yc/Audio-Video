#include "latencylabel.h"

#include <QMouseEvent>

LatencyLabel::LatencyLabel(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setFixedSize(112, 24);
    setCursor(Qt::PointingHandCursor);
    setLatency(-1);
}

void LatencyLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton) {
        Q_EMIT clicked();
    }
    QLabel::mouseReleaseEvent(event);
}

void LatencyLabel::setLatency(int latencyMs, const QString &source)
{
    const QString label = source == "gateway" ? QStringLiteral("网关")
                        : source == "media" ? QStringLiteral("媒体") : QStringLiteral("延迟");
    setToolTip((source == "gateway"
        ? QStringLiteral("网关 HTTP 请求总耗时，包含连接、TLS 和服务响应，不代表音视频 RTT。")
        : source == "media" ? QStringLiteral("LiveKit 音视频连接的往返时间 RTT，取发布与接收连接较高值。")
        : QStringLiteral("每 5 秒测量到会议服务器的 TCP 连接耗时。"))
        + QStringLiteral(" 点击显示或隐藏共享状态；失败或无样本时不显示旧数值。"));
    QString foreground;
    QString background;
    QString border;

    if (latencyMs < -1) {
        setText(label + QStringLiteral(" 不可用"));
        foreground = "#B42318";
        background = "#FEF3F2";
        border = "#FDA29B";
    } else if (latencyMs < 0) {
        setText(label + " --");
        foreground = "#475467";
        background = "#F8FAFC";
        border = "#CBD5E1";
    } else if (latencyMs <= 80) {
        setText(QString("%1 %2 ms").arg(label).arg(latencyMs));
        foreground = "#067647";
        background = "#ECFDF3";
        border = "#6CE9A6";
    } else if (latencyMs <= 150) {
        setText(QString("%1 %2 ms").arg(label).arg(latencyMs));
        foreground = "#93370D";
        background = "#FFFAEB";
        border = "#FEC84B";
    } else {
        setText(QString("%1 %2 ms").arg(label).arg(latencyMs));
        foreground = "#B42318";
        background = "#FEF3F2";
        border = "#FDA29B";
    }

    setStyleSheet(QString(
        "QLabel { color: %1; background: %2; border: 1px solid %3; "
        "border-radius: 4px; font-size: 12px; }")
        .arg(foreground, background, border));
}
