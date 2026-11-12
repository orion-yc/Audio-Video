#ifndef LATENCYLABEL_H
#define LATENCYLABEL_H

#include <QLabel>

class QMouseEvent;

class LatencyLabel : public QLabel
{
    Q_OBJECT
public:
    explicit LatencyLabel(QWidget *parent = nullptr);

    void setLatency(int latencyMs, const QString &source = {});

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
};

#endif // LATENCYLABEL_H
