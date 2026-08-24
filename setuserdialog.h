#ifndef SETUSERDIALOG_H
#define SETUSERDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QImage>
#include <QPushButton>

namespace Ui {
class SetUserDialog;
}

class SetUserDialog : public QDialog
{
    Q_OBJECT

signals:
    void SIG_userSetCommit(int iconid, QString name, QString feeling,
                           QByteArray avatarData, bool avatarChanged);

public:
    explicit SetUserDialog(QWidget *parent = nullptr);
    ~SetUserDialog();

private slots:
    void on_pb_commit_clicked();

    void on_pb_clear_clicked();

    void on_pb_uploadAvatar_clicked();

    void on_cbx_icon_currentIndexChanged(int index);
public slots:
    void slot_setInfo(int iconid, QString name, QString feeling = "比较懒，什么也没写");
    void slot_setAvatarImage(const QImage &avatar);

private:
    QByteArray encodeAvatar(const QImage &avatar) const;
    void setCustomAvatar(const QImage &avatar, bool changed);

    Ui::SetUserDialog *ui;
    QPushButton *m_uploadAvatarButton;

    int m_iconid;
    int m_uploadedAvatarIndex;
    bool m_hasCustomAvatar;
    bool m_avatarChanged;
    bool m_blockAvatarSelection;
    QByteArray m_avatarData;
    QString m_name;
    QString m_feeling;
};

#endif // SETUSERDIALOG_H
