#ifndef ROOMDIALOG_H
#define ROOMDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include "customwidget.h"
#include "latencylabel.h"
#include "usershow.h"
namespace Ui {
class RoomDialog;
}
class QComboBox;
class QCheckBox;
class QEvent;
class QFrame;
class QLabel;
class QPushButton;
class QScreen;
class QTextBrowser;
class QTextEdit;

// 无边框设计
class RoomDialog : public CustomMoveDialog
{
    Q_OBJECT
signals:
    void SIG_quitRoom();
    // 摄像头 音频 桌面
    void SIG_audioOpen();
    void SIG_audioClose();
    void SIG_systemAudioStateChanged(bool enabled);
    void SIG_systemAudioDeviceChanged(const QString &deviceId);
    void SIG_captureOpen();
    void SIG_captureClose();
    void SIG_deskOpen();
    void SIG_deskClose();
    void SIG_desktopShareSettingsChanged(int width, int height, int frameRate);
    void SIG_desktopShareScreenChanged(const QString &screenName);
    void SIG_setFunnyPic(int picid);
    void SIG_chatMessageSend(const QString &message);
    void SIG_chatUserRequested(int userId);
public:
    explicit RoomDialog(QWidget *parent = nullptr);
    ~RoomDialog();
public slots:
    void slot_setRoomID(int roomid);
    void slot_setMediaStatus(const QString &text);
private:
    QLabel* m_mediaStatus = nullptr;
    bool m_topStatusVisible = false;
public slots:

    void slot_addUser(QWidget* user);
    void slot_removeUser(QWidget* user);
    void slot_setRoomClear();
    void slot_setBigImage(int userid, QImage &img);
    void slot_setBigImageInfo(int userid, QString name);
    void slot_setBigUser(int userid, const QString &name, const QImage &avatar);
    void slot_setBigAvatar(int userid, const QImage &avatar);
    void slot_clearBigImage(int userid);
    void slot_setCaptureState(bool active);
    void slot_setSystemAudioError(const QString &message);
    void slot_setLatency(int latencyMs);
    void slot_setDesktopSharePending(int frameRate, int bitRate);
    void slot_setDesktopShareStatus(int width, int height, int frameRate, int bitRate,
                                    const QString &encoderName);
    void slot_setDesktopShareRuntimeStats(int width, int height, int targetFrameRate,
                                          int bitRate, const QString &encoderName,
                                          int capturedFrames, int encodedFrames,
                                          int sentFrames, int droppedFrames);
    void slot_clearDesktopShareStatus();
    void slot_appendChatMessage(int userId, const QString &name, const QString &message,
                                bool ownMessage);
    void slot_clearChatInput();
    QString desktopShareScreenName() const;
private slots:
    void on_pb_close_clicked();

    void on_pb_max_clicked();

    void on_pb_min_clicked();

    void on_pb_hide_clicked();

    void on_pb_quit_clicked();

    void on_cb_audio_clicked();

    void on_cb_capture_clicked();

    void on_cb_desk_clicked();

    void on_cbx_moji_currentIndexChanged(int index);

private:
    void emitDesktopShareSettings();
    void emitDesktopShareScreen();
    void refreshDesktopScreens();
    void setDesktopShareStatusText(const QString &text, const QString &toolTip);
    void setTopStatusVisible(bool visible);
    void setChatPanelVisible(bool visible);
    void sendChatMessage();
    void updateChatButton();
    void setSystemAudioChecked(bool checked);
    void setSystemAudioControlsEnabled(bool enabled);
    void refreshSystemAudioDevices();
    UserShow *userShowForId(int userId) const;
    void setBigUserFromUserShow(UserShow *user, const QImage &preview);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

    Ui::RoomDialog *ui;
    LatencyLabel *m_latencyLabel;
    QLabel *m_deskStatusLabel = nullptr;
    QComboBox *m_viewScaleCombo = nullptr;
    QComboBox *m_deskResolutionCombo = nullptr;
    QComboBox *m_deskFrameRateCombo = nullptr;
    QComboBox *m_deskScreenCombo = nullptr;
    QCheckBox *m_systemAudioCheck = nullptr;
    QComboBox *m_systemAudioCombo = nullptr;
    QFrame *m_chatPanel = nullptr;
    QTextBrowser *m_chatHistory = nullptr;
    QTextEdit *m_chatInput = nullptr;
    QPushButton *m_chatButton = nullptr;
    int m_unreadChatCount = 0;
    int m_localUserId = 0;
    bool m_bigUserPinnedByUser = false;

    // 用于用户列表的垂直布局
    QVBoxLayout* m_userLayout;
};

// 预览大图 点击User 发信号 id name 根据这个设置大图
// 刷新图片会根据id 刷新图片



#endif // ROOMDIALOG_H
