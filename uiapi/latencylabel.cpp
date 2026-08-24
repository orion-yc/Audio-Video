#include "latencylabel.h"

LatencyLabel::LatencyLabel(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setFixedSize(88, 24);
    setToolTip("每 5 秒自动测量到会议服务器的连接延迟");
    setLatency(-1);
}

void LatencyLabel::setLatency(int latencyMs)
{
    QString foreground;
    QString background;
    QString border;

    if (latencyMs < -1) {
        setText("连接失败");
        foreground = "#B42318";
        background = "#FEF3F2";
        border = "#FDA29B";
    } else if (latencyMs < 0) {
        setText("延迟 --");
        foreground = "#475467";
        background = "#F8FAFC";
        border = "#CBD5E1";
    } else if (latencyMs <= 80) {
        setText(QString("延迟 %1 ms").arg(latencyMs));
        foreground = "#067647";
        background = "#ECFDF3";
        border = "#6CE9A6";
    } else if (latencyMs <= 150) {
        setText(QString("延迟 %1 ms").arg(latencyMs));
        foreground = "#93370D";
        background = "#FFFAEB";
        border = "#FEC84B";
    } else {
        setText(QString("延迟 %1 ms").arg(latencyMs));
        foreground = "#B42318";
        background = "#FEF3F2";
        border = "#FDA29B";
    }

    setStyleSheet(QString(
        "QLabel { color: %1; background: %2; border: 1px solid %3; "
        "border-radius: 4px; font-size: 12px; }")
        .arg(foreground, background, border));
}
