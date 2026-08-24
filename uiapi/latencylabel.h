#ifndef LATENCYLABEL_H
#define LATENCYLABEL_H

#include <QLabel>

class LatencyLabel : public QLabel
{
public:
    explicit LatencyLabel(QWidget *parent = nullptr);

    void setLatency(int latencyMs);
};

#endif // LATENCYLABEL_H
