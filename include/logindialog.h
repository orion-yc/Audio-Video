#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QByteArray>
#include <QCloseEvent>
#include <QImage>
#include <QMessageBox>
#include "latencylabel.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

signals:
    void SIG_close();
    // 定义登录 注册提交
    void  SIG_loginCommit(QString tel, QString passwd);
    void SIG_registerCommit(QString tel, QString passwd, QString nickname,
                            int iconid, QByteArray avatarData, bool avatarChanged);



public:
    void closeEvent(QCloseEvent *event);
    void slot_setLatency(int latencyMs, const QString &source = {});
    void slot_setLoginPending(bool pending);
    void slot_showLoginFor(const QString &tel);

private slots:
    void on_pb_clear_clicked();

    void on_pb_commit_clicked();

    void on_pb_clear_register_clicked();

    void on_pb_commit_register_clicked();

    void on_pb_uploadRegisterAvatar_clicked();

    void on_cbx_registerAvatar_currentIndexChanged(int index);

private:
    QByteArray encodeRegisterAvatar(const QImage &avatar) const;
    void setRegisterCustomAvatar(const QImage &avatar);
    void resetRegisterProfile();

    Ui::LoginDialog *ui;
    LatencyLabel *m_latencyLabel;
    QLabel *m_registerNicknameLabel;
    QLabel *m_registerAvatarLabel;
    QLineEdit *m_registerNicknameEdit;
    QComboBox *m_registerAvatarCombo;
    QPushButton *m_registerAvatarUploadButton;
    int m_registerCustomAvatarIndex;
    bool m_registerAvatarChanged;
    bool m_blockRegisterAvatarSelection;
    QByteArray m_registerAvatarData;
};

#endif // LOGINDIALOG_H
