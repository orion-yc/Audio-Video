#ifndef DEMODIALOG_H
#define DEMODIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include <QImage>
#include <QMessageBox>
#include "customwidget.h"
#include "latencylabel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class demoDialog; }
QT_END_NAMESPACE

class demoDialog : public CustomMoveDialog
{
    Q_OBJECT

public:
    demoDialog(QWidget *parent = nullptr);
    ~demoDialog();
signals:
    void SIG_close();
    void SIG_joinRoom();
    void SIG_createRoom();
    void SIG_setUser();
public:
    void closeEvent(QCloseEvent *event);
public slots:
    void on_pb_min_clicked();

    void on_pb_close_clicked();

    void on_pb_create_clicked();

    void on_pb_join_clicked();


    void on_pb_icon_clicked();

    void slot_setPushButton_enable(bool flag);

public slots:
    void slot_setInfo(QString name, int iconid);
    void slot_setAvatarImage(const QImage &avatar);
    void slot_setLatency(int latencyMs);
private:
    Ui::demoDialog *ui;
    LatencyLabel *m_latencyLabel;
    int m_iconid;
    QString m_name;
};
#endif // DEMODIALOG_H
