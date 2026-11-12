#ifndef CKERNAL_H
#define CKERNAL_H

#include <QObject>
#include <QAbstractSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QTimer>
#include <QThread>
#include <logindialog.h>
#include <demodialog.h>
#include "TcpClientMediator.h"
#include "packdef.h"
#include "roomdialog.h"
#include "setuserdialog.h"
#include <map>

#include "audio_read.h"
#include "system_audio_read.h"
#include "audio_write.h"
#include "video_read.h"
#include "desk_read.h"
#include "h264codec.h"
#include "video_encode_thread.h"
#include "video_decode_worker.h"
#include "workthread.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
class CKernal;
class GatewayClient;
class RtcSession;
class UserProfileDialog;

typedef void (CKernal::*PFUN)(uint socket,char* buf,int nlen);

// 为了优化发送导致的卡顿，可以采用多线程，发送在工作者线程执行
class SendThread: public WorkThread
{
    Q_OBJECT
public:
    ~SendThread() override;
    void setActiveGeneration(quint64 generation);
    void takeDesktopStats(quint64 *sentFrames, quint64 *replacedFrames,
                          quint64 *lateFrames, quint64 *failedFrames);

public slots:
    // This slot is deliberately invoked directly by the GUI thread.  It only
    // swaps a pending pointer under a mutex; network I/O stays on this worker.
    void slot_queueLatestFrame(uint socket, char* buf, int nlen, quint64 generation,
                               bool desktopContent);
    void slot_sendLatestFrame();
private:
    struct PendingFrame {
        uint socket = 0;
        char *buffer = nullptr;
        int length = 0;
        quint64 generation = 0;
        bool desktopContent = false;
        bool keyFrame = false;
        bool stopFrame = false;
        std::chrono::steady_clock::time_point queuedAt;
    };

    std::atomic<quint64> m_activeGeneration { 0 };
    std::mutex m_pendingMutex;
    PendingFrame m_pendingFrame;
    bool m_sendScheduled = false;
    std::atomic<quint64> m_desktopSentFrames { 0 };
    std::atomic<quint64> m_desktopReplacedFrames { 0 };
    std::atomic<quint64> m_desktopLateFrames { 0 };
    std::atomic<quint64> m_desktopFailedFrames { 0 };
};


// 单例
class CKernal : public QObject
{
    Q_OBJECT
private:
    explicit CKernal(QObject *parent = nullptr);
    ~CKernal();
    CKernal(const CKernal&) = delete;
    CKernal& operator=(const CKernal&) = delete;
signals:
    void SIG_sendVideoFrame(uint socket, char* buf, int nlen, quint64 generation,
                            bool desktopContent);
    void SIG_encodeVideoFrame(QImage image, int frameRate, int bitRate,
                              bool desktopContent, quint64 generation);
    void SIG_resetVideoEncoder();
    void SIG_requestVideoKeyframe();
public:
    static CKernal* getInstance()
    {
        static CKernal kernal;
        return &kernal;
    }

public slots:
    // 设置协议映射
    void setNetMap();
    // 初始化配置
    void initConfig();
    // 回收
    void slot_destroy();
    // 网络处理
    void slot_dealData(uint socket,char* buf,int nlen);
    // 网络处理函数
    void slot_dealLoginRs(uint socket,char* buf,int nlen);
    void slot_dealRegisterRs(uint socket,char* buf,int nlen);
    void slot_dealCreateRoomRs(uint socket,char* buf,int nlen);
    void slot_dealJoinRoomRs(uint socket,char* buf,int nlen);
    void slot_dealRoomMemberRq(uint socket,char* buf,int nlen);
    void slot_dealUserInfoRq(uint socket,char* buf,int nlen);
    void slot_dealLeaveRoomRq( uint socket, char* buf, int nlen);
    void slot_dealAudioFrame( uint socket, char* buf, int nlen);
    void slot_dealVideoFrame( uint socket, char* buf, int nlen);
    void slot_dealAvatarUploadRs(uint socket, char* buf, int nlen);
    void slot_dealAvatarDataRs(uint socket, char* buf, int nlen);
    void slot_dealRoomChatMessage(uint socket, char* buf, int nlen);


    // 客户端发送数据
    bool slot_clientSendData( uint socket, char* buf, int nlen);
    // 音频发送接口
    void slot_clientAudioSendData( uint socket, char* buf, int nlen);
    // 视频发送接口
    bool slot_clientVideoSendData( uint socket, char* buf, int nlen);


    // ui槽函数
    // 登录 注册提交
    void slot_loginCommit(QString tel, QString passwd);
    void slot_registerCommit(QString tel, QString passwd, QString nickname,
                             int iconid, QByteArray avatarData, bool avatarChanged);
    void slot_joinRoom();
    void slot_createRoom();
    void slot_setJoinedRoom(int roomid);
    void slot_setUser();
    void slot_userSetCommit(int iconid, QString name, QString feeling,
                            QByteArray avatarData, bool avatarChanged);
    void slot_quitRoom();

    // 音视频控制 开关
    void slot_audioOpen();
    void slot_audioClose();
    void slot_systemAudioStateChanged(bool enabled);
    void slot_systemAudioDeviceChanged(const QString &deviceId);
    void slot_captureOpen();
    void slot_captureClose();
    void slot_deskOpen();
    void slot_deskClose();
    void slot_setDesktopShareSettings(int width, int height, int frameRate);
    void slot_setDesktopShareScreen(const QString &screenName);
    void slot_sendRoomChatMessage(const QString &message);
    void slot_showRoomMemberProfile(int userId);

    void slot_sendAudioFrame(QByteArray &ba );
    void slot_refleshVideoImage(QImage img);
    void slot_refleshCameraVideoImage(QImage img);
    void slot_refleshDesktopVideoImage(QImage img);
    void slot_sendVideoFrameData(const QByteArray &ba, quint64 generation,
                                 bool desktopContent = false);
    void slot_sendCameraVideoFrame(QImage img);
    void slot_sendDesktopVideoFrame(QImage img);
    void slot_sendH264Frame(QByteArray encoded, int flags, int width, int height,
                            quint64 generation, QString encoderName);
    void slot_videoEncodeFinished();
    void slot_videoFrameDecoded(int userId, int roomId, quint64 generation,
                                QImage image, bool decoded);
    void slot_updateDesktopShareStats();
    void slot_refleshUserImage(int id, QImage &img);

    void slot_setFunnyPic(int picid);
    void slot_cameraStateChanged(bool active);

    void slot_measureLatency();
    void slot_latencyConnected();
    void slot_latencyError(QAbstractSocket::SocketError error);
    void slot_latencyTimeout();
    void slot_loginTimeout();
    void slot_controlConnected();
    void slot_controlReadyRead();
    void slot_controlDisconnected();
    void slot_controlError(QAbstractSocket::SocketError error);
    void slot_controlReconnect();
    void slot_dealAudioChannelData(uint socket, char* buf, int nlen);
    void slot_dealVideoChannelData(uint socket, char* buf, int nlen);


#ifdef AUDIO_VIDEO_TESTING
public:
#else
private:
#endif
    enum class RoomRequest
    {
        None,
        Create,
        Join
    };

    enum class VideoSource
    {
        None,
        Camera,
        Desktop
    };

    void requestAvatar(int targetUserId);
    bool sendAvatarUpload(const QByteArray &avatarData);
    void sendVideoStopFrame();
    void stopLocalVideo();
    void clearUserVideo(int userId);
    void ensureRemoteParticipant(int userId, const QString &name = QString());
    QImage builtInAvatar(int iconid) const;
    void applyOwnAvatar(const QImage &avatar);
    void updateOwnRoomAvatar(const QImage &avatar);
    void beginRoomRequest(RoomRequest request, int roomId = 0);
    void clearRoomRequest();
    void slot_roomRequestTimeout();
    bool hasAuthenticatedControlConnection() const;
    bool sendControlPacket(const char* data, int length, bool requiresLogin = true);
    bool writeControlFrame(const char* data, int length);
    bool queueUserInfoSave(int iconid, const QString& name, const QString& feeling,
                           const QByteArray& avatarData, bool avatarChanged);
    void ensureControlConnection();
    void ensureAvConnections(bool forceRegistration = false);
    bool ensureAvChannelReady(int channel, bool forceRegistration = false);
    void createAvChannel(int channel);
    void discardAvChannel(int channel);
    void scheduleAvChannelRecovery(int channel);
    void retryAvChannel(int channel);
    void allowLegacyAvRegistration(int channel);
    void handleMediaRegistration(int channel, const char* buffer, int length);
    void handleVideoKeyFrameRequest(const char* buffer, int length);
    void failLogin();
    QTimer* m_loginDeadline = nullptr;
    void sendPendingLogin();
    void sendPendingRegistration();
    void resetAuthenticationState();
    void beginControlRecovery();
    void scheduleControlReconnect();
    void clearPendingProfileSave();
    int desktopShareBitRate(int width, int height, int frameRate) const;

    void setupWebRtc();
    void webLogin(const QString &phone, const QString &password);
    void webRegister(const QString &phone, const QString &password, const QString &name,
                     int icon, const QByteArray &avatar, bool avatarChanged);
    void webJoinRoom();
    void removeWebParticipant(int id);
    bool m_useWebRtc = true;
    QString m_gatewayUrl = QStringLiteral("https://video.onnotify.top");
    QString m_liveKitUrl = QStringLiteral("wss://video.onnotify.top");
    GatewayClient *m_gateway = nullptr;
    RtcSession *m_rtc = nullptr;
    QString m_webRoomName;
    QString m_webRegisterPhone, m_webRegisterPassword, m_webRegisterName;
    int m_webRegisterIcon = 5;
    bool m_webRegistrationPending = false;
    bool m_webAvatarChanged = false;
    QByteArray m_webAvatar;
    std::atomic<quint64> m_webQueuedFrames {0};
    QElapsedTimer m_webKeyframeClock;
    int m_webTargetBitrate = 0;
    QElapsedTimer m_webBitrateClock;
    PFUN                            m_netMap[DEF_PACK_COUNT];
    LoginDialog*                    m_loginDialog;
    demoDialog*                     m_demoDialog;
    QTcpSocket*                     m_controlSocket;
    QByteArray                      m_controlReceiveBuffer;
    QTimer*                         m_controlReconnectTimer;
    QString                         m_serverIP;
    unsigned short                  m_serverPort;
    int                             m_userid;
    int                             m_roomid;
    char                            m_sessionToken[SESSION_TOKEN_SIZE];
    bool                            m_loggedIn;
    bool                            m_shuttingDown;
    RoomDialog*                     m_roomDialog;
    QString                         m_name;
    int                             m_userIconId;
    QImage                          m_userAvatar;
    QString                         m_userFeeling;
    SetUserDialog*                  m_setUserDialog;
    UserProfileDialog*              m_userProfileDialog;
    // 每个用户视频的显示
    std::map<int, UserShow*>        m_mapIDToUserShow;
    QElapsedTimer                   m_videoUiClock;
    std::map<int, qint64>           m_thumbnailLastPaintMs;

    // 音频
    Audio_Read*                     m_audioRead = nullptr;
    SystemAudio_Read*               m_systemAudioRead = nullptr;
    // Audio_Write 每个用户都创建
    std::map<int, Audio_Write*>     m_mapIDToAudioWrite;
    Video_Read*                     m_videoRead;
    Desk_Read*                      m_deskRead;

    QSharedPointer<SendThread>      m_sendVideoThread;
    QSharedPointer<VideoEncodeThread> m_videoEncodeThread;
    QThread*                         m_videoDecodeThread = nullptr;
    VideoDecodeWorker*               m_videoDecodeWorker = nullptr;
    std::atomic<quint64>              m_remoteVideoGeneration { 0 };
    std::map<int, std::unique_ptr<H264Decoder>> m_mapIDToVideoDecoder;
    std::atomic_bool                 m_videoEncodeBusy;
    std::mutex                       m_pendingDesktopFrameMutex;
    QImage                           m_pendingDesktopFrame;
    quint64                          m_videoStreamGeneration;
    VideoSource                      m_activeVideoSource;
    int                              m_desktopWidth;
    int                              m_desktopHeight;
    int                              m_desktopFrameRate;
    int                              m_desktopBitRate;
    QString                          m_desktopEncoderName;
    QTimer*                          m_desktopStatsTimer;
    std::atomic<quint64>             m_desktopCapturedFrames { 0 };
    std::atomic<quint64>             m_desktopEncodedFrames { 0 };
    std::atomic<quint64>             m_desktopEncoderDroppedFrames { 0 };
    int                              m_desktopNoCaptureIntervals = 0;
    int                              m_desktopNoEncodeIntervals = 0;
    int                              m_desktopNoSendIntervals = 0;

    // 视频和音频单独的 tcp，只有主控制连接认证成功后才创建。
    TcpClientMediator*              m_tcpAV[2] = { nullptr, nullptr }; // 0 音频 1 视频
    std::atomic_bool                m_avChannelRegistered[2] = { false, false };
    bool                            m_avRegistrationPending[2] = { false, false };
    QTimer*                         m_avReconnectTimers[2] = { nullptr, nullptr };
    QTimer*                         m_avRegistrationFallbackTimers[2] = { nullptr, nullptr };
    QTcpSocket*                     m_latencySocket;
    QTimer*                         m_latencyTimer;
    QTimer*                         m_latencyTimeoutTimer;
    QTimer*                         m_loginTimeoutTimer;
    QTimer*                         m_roomRequestTimer;
    QElapsedTimer                   m_latencyElapsed;
    bool                            m_latencyProbeActive;
    bool                            m_loginPending;
    QString                         m_loginTel;
    QByteArray                      m_loginRequest;
    QByteArray                      m_pendingRegisterRequest;
    bool                            m_profileSavePending;
    QByteArray                      m_pendingProfileAvatarData;
    bool                            m_pendingProfileAvatarChanged;
    QByteArray                      m_pendingAvatarUploadData;
    bool                            m_avatarUploadPending = false;
    QString                         m_registerDraftTel;
    QString                         m_registerDraftPassword;
    QString                         m_registerDraftNickname;
    int                             m_registerDraftIconId;
    QByteArray                      m_registerDraftAvatarData;
    bool                            m_registerDraftAvatarChanged;
    QString                         m_pendingRegistrationTel;
    QString                         m_pendingRegistrationNickname;
    int                             m_pendingRegistrationIconId;
    QByteArray                      m_pendingRegistrationAvatarData;
    bool                            m_pendingRegistrationAvatarChanged;
    RoomRequest                     m_roomRequest;
    int                             m_pendingRoomId;

    void updateLatencyDisplay(int latencyMs);
    QElapsedTimer m_mediaLatencyClock;
    void resetDesktopShareStats();
    void queueVideoFrame(const QImage &image, int frameRate, int bitRate,
                         bool desktopContent);
};

//  当前视频和音频同时传输时，音频会卡顿，视频会有较大延迟
//  优化方案
//  视频帧的协议包里面有时间戳与当前时间(绝对时间)进行比较，如果发送发现延迟(大于200ms)比较大，就丢弃
//  延迟大的原因：发送是在单独线程里面用槽函数写的，是排队执行的
//  前一个包有一定延迟，会影响到下一个包，让其延迟执行槽函数
//  这样音频和视频都和当前的绝对时间偏差小于200ms，那么就间接同步了

// 因为视频流量较大 音频就卡断，考虑视频和音频用两个不同tcp传输
// 什么时候与服务器建立视频和音频的tcp连接
#endif // CKERNAL_H
