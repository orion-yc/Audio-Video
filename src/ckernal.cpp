#include "ckernal.h"
#include "diagnostics.h"
#include "userprofiledialog.h"
#include "gateway_client.h"
#include "rtc_session.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QNetworkProxy>

// 通过宏定义化简
#define NetMap(x) m_netMap[x - DEF_PACK_BASE]

// 协议映射关系
void CKernal::setNetMap()
{
    memset(m_netMap, 0, sizeof(m_netMap));
    // m_netMap[_DEF_PACK_LOGIN_RS - _DEF_PROTOCOL_BASE ] = &CKernal::slot_dealLoginRs;
    NetMap(DEF_PACK_LOGIN_RS)       = &CKernal::slot_dealLoginRs;
    NetMap(DEF_PACK_REGISTER_RS)    = &CKernal::slot_dealRegisterRs;

    // 房间
    NetMap(DEF_PACK_CREATEROOM_RS) = &CKernal::slot_dealCreateRoomRs;
    NetMap(DEF_PACK_JOINROOM_RS)   = &CKernal::slot_dealJoinRoomRs;
    NetMap(DEF_PACK_ROOM_MEMBER)   = &CKernal::slot_dealRoomMemberRq;

    NetMap(DEF_PACK_USER_INFO_RQ)  = &CKernal::slot_dealUserInfoRq;
    NetMap(DEF_PACK_LEAVEROOM_RQ)  = &CKernal::slot_dealLeaveRoomRq;

    NetMap(DEF_PACK_AUDIO_FRAME)  = &CKernal::slot_dealAudioFrame;
    NetMap(DEF_PACK_VIDEO_FRAME)  = &CKernal::slot_dealVideoFrame;
    NetMap(DEF_PACK_AVATAR_UPLOAD_RS) = &CKernal::slot_dealAvatarUploadRs;
    NetMap(DEF_PACK_AVATAR_DATA_RS) = &CKernal::slot_dealAvatarDataRs;
    NetMap(DEF_PACK_ROOM_CHAT_MESSAGE) = &CKernal::slot_dealRoomChatMessage;

}

#include <QSettings>
#include <QApplication>
#include <QFileInfo>
#include <QInputDialog>

#include <cctype>

namespace {
constexpr int kMinimumRoomId = 100000;
constexpr int kMaximumRoomId = 999999;
constexpr int kRoomRequestTimeoutMs = 8000;
constexpr int kAvChannelReconnectIntervalMs = 1000;
constexpr int kLegacyAvRegistrationFallbackMs = 1500;
constexpr qint64 kDesktopReferencePixelsPerSecond =
        static_cast<qint64>(DESKTOP_IMAGE_WIDTH) * DESKTOP_IMAGE_HEIGHT * DESKTOP_FRAME_RATE;
constexpr qint64 kMinimumDesktopVideoBitRate = 6 * 1000 * 1000;
const std::chrono::milliseconds kMaximumQueuedVideoAge(250);
constexpr int kDesktopStallIntervalsBeforeRecovery = 5;

bool isValidRoomId(int roomId)
{
    return roomId >= kMinimumRoomId && roomId <= kMaximumRoomId;
}

bool hasValidSessionToken(const char *token)
{
    if (!token || token[SESSION_TOKEN_SIZE - 1] != '\0') {
        return false;
    }
    for (int i = 0; i < SESSION_TOKEN_SIZE - 1; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(token[i]))) {
            return false;
        }
    }
    return true;
}

bool hasJpegSignature(const QByteArray &data)
{
    return data.size() >= 3
            && static_cast<unsigned char>(data[0]) == 0xFF
            && static_cast<unsigned char>(data[1]) == 0xD8
            && static_cast<unsigned char>(data[2]) == 0xFF;
}
}

// 初始化配置
void CKernal::initConfig()
{

    qDebug() << "mainThread"<<QThread::currentThreadId();

    m_serverIP = _DEF_SERVER_IP;
    m_serverPort = _DEF_TCP_PORT;
    // 路径设置 exe同级目录下的 config.ini
    QString path = QApplication::applicationDirPath() + "/config.ini";

    // 判断是否存在
    QFileInfo info(path);
    QSettings settings(path, QSettings::IniFormat, NULL); // 有打开，没有创建
    if( info.exists()){
        // 加载配置文件 ip 如配置文件 设置为配置文件中的IP
        // 打开配置文件
        // 移动到Net组
        settings.beginGroup("Net");
        // 读取 ip->addr->赋值
        QString strIP = settings.value("host").toString();
        if (strIP.isEmpty()) {
            strIP = settings.value("ip").toString();
        }
        int configuredPort = settings.value("port", _DEF_TCP_PORT).toInt();
        //结束
        settings.endGroup();
        if(!strIP.isEmpty()){
            m_serverIP = strIP;
        }
        if (configuredPort > 0 && configuredPort <= 65535) {
            m_serverPort = static_cast<unsigned short>(configuredPort);
        }
    }else{
        // 没有配置文件 写入默认的IP
        settings.beginGroup("Net");
        settings.setValue("host", m_serverIP);
        settings.setValue("port", m_serverPort);
        settings.endGroup();
    }
    m_useWebRtc = settings.value("Net/transport", "webrtc").toString() != "tcp";
    m_gatewayUrl = settings.value("Net/gateway", "https://video.onnotify.top").toString();
    m_liveKitUrl = settings.value("Net/livekit", "wss://video.onnotify.top").toString();
    qDebug() << (m_useWebRtc ? m_gatewayUrl : m_serverIP);



}
CKernal::CKernal(QObject *parent) : QObject(parent)
  ,m_controlSocket(new QTcpSocket(this))
  ,m_controlReconnectTimer(new QTimer(this))
  ,m_userid(0), m_roomid(0), m_loggedIn(false), m_shuttingDown(false)
  ,m_userIconId(0), m_userProfileDialog(nullptr), m_sendVideoThread(new SendThread)
  ,m_videoEncodeThread(new VideoEncodeThread)
  ,m_videoEncodeBusy(false)
  ,m_videoStreamGeneration(0)
  ,m_activeVideoSource(VideoSource::None)
  ,m_desktopWidth(DESKTOP_IMAGE_WIDTH)
  ,m_desktopHeight(DESKTOP_IMAGE_HEIGHT)
  ,m_desktopFrameRate(DESKTOP_FRAME_RATE)
  ,m_desktopBitRate(DESKTOP_VIDEO_BITRATE)
  ,m_desktopStatsTimer(new QTimer(this))
  ,m_latencySocket(new QTcpSocket(this))
    ,m_latencyTimer(new QTimer(this))
    ,m_latencyTimeoutTimer(new QTimer(this))
    ,m_loginTimeoutTimer(new QTimer(this))
    ,m_roomRequestTimer(new QTimer(this))
    ,m_latencyProbeActive(false)
    ,m_loginPending(false)
    ,m_profileSavePending(false)
    ,m_pendingProfileAvatarChanged(false)
    ,m_registerDraftIconId(0)
    ,m_registerDraftAvatarChanged(false)
    ,m_pendingRegistrationIconId(0)
    ,m_pendingRegistrationAvatarChanged(false)
    ,m_roomRequest(RoomRequest::None)
    ,m_pendingRoomId(0)
{
    memset(m_sessionToken, 0, sizeof(m_sessionToken));
    m_videoUiClock.start();
    // 设置协议映射关系
    setNetMap();
    initConfig();

    // The meeting protocol is raw TCP. A desktop HTTP/SOCKS proxy can accept
    // the local connection without relaying this binary protocol to the server.
    m_controlSocket->setProxy(QNetworkProxy::NoProxy);
    m_latencySocket->setProxy(QNetworkProxy::NoProxy);

    m_loginDialog = new LoginDialog;
    m_demoDialog  = new demoDialog;

    connect(m_loginDialog, SIGNAL(SIG_close()),
            this, SLOT(slot_destroy()));

    connect(m_loginDialog, SIGNAL(SIG_loginCommit(QString,QString))
            , this, SLOT(slot_loginCommit(QString, QString)));
    connect(m_loginDialog,
            SIGNAL(SIG_registerCommit(QString,QString,QString,int,QByteArray,bool)),
            this,
            SLOT(slot_registerCommit(QString,QString,QString,int,QByteArray,bool)));

    connect(m_demoDialog, SIGNAL(SIG_close()),
            this, SLOT(slot_destroy()));

    connect(m_demoDialog, SIGNAL(SIG_createRoom()),
            this, SLOT(slot_createRoom()));

    connect(m_demoDialog, SIGNAL(SIG_joinRoom()),
            this, SLOT(slot_joinRoom()));

    connect(m_demoDialog, SIGNAL(SIG_setUser()),
            this, SLOT(slot_setUser()));

    connect(m_controlSocket, &QTcpSocket::connected,
            this, &CKernal::slot_controlConnected);
    connect(m_controlSocket, &QTcpSocket::readyRead,
            this, &CKernal::slot_controlReadyRead);
    connect(m_controlSocket, &QTcpSocket::disconnected,
            this, &CKernal::slot_controlDisconnected);
    connect(m_controlSocket, &QTcpSocket::errorOccurred,
            this, &CKernal::slot_controlError);
    m_controlReconnectTimer->setSingleShot(true);
    m_controlReconnectTimer->setInterval(1500);
    connect(m_controlReconnectTimer, &QTimer::timeout,
            this, &CKernal::slot_controlReconnect);
    for (int channel = 0; channel < 2; ++channel) {
        m_avReconnectTimers[channel] = new QTimer(this);
        m_avReconnectTimers[channel]->setSingleShot(true);
        m_avReconnectTimers[channel]->setInterval(kAvChannelReconnectIntervalMs);
        connect(m_avReconnectTimers[channel], &QTimer::timeout, this,
                [this, channel]() { retryAvChannel(channel); });
        m_avRegistrationFallbackTimers[channel] = new QTimer(this);
        m_avRegistrationFallbackTimers[channel]->setSingleShot(true);
        m_avRegistrationFallbackTimers[channel]->setInterval(kLegacyAvRegistrationFallbackMs);
        connect(m_avRegistrationFallbackTimers[channel], &QTimer::timeout, this,
                [this, channel]() { allowLegacyAvRegistration(channel); });
    }
    ensureControlConnection();

    m_loginDialog->show();

    m_roomDialog = new RoomDialog;
    m_userProfileDialog = new UserProfileDialog(m_roomDialog);
    connect(m_roomDialog, SIGNAL(SIG_quitRoom())
            , this, SLOT(slot_quitRoom()));
    m_setUserDialog = new SetUserDialog;
    // todo
    connect(m_setUserDialog, &SetUserDialog::SIG_userSetCommit,
            this, &CKernal::slot_userSetCommit);

    m_audioRead = new Audio_Read;
    m_systemAudioRead = new SystemAudio_Read;

    m_videoRead = new Video_Read;

    connect(m_videoRead, &Video_Read::SIG_captureStateChanged,
            this, &CKernal::slot_cameraStateChanged);
    connect(m_videoRead, &Video_Read::SIG_captureStateChanged,
            m_roomDialog, &RoomDialog::slot_setCaptureState);

    m_deskRead  = new Desk_Read;
    m_deskRead->setCaptureScreenName(m_roomDialog->desktopShareScreenName());

    connect(m_audioRead, SIGNAL(SIG_audioFrame(QByteArray&)),
            this, SLOT(slot_sendAudioFrame(QByteArray&)));
    connect(m_systemAudioRead, &SystemAudio_Read::SIG_audioFrame,
            m_audioRead, &Audio_Read::slot_systemAudioFrame, Qt::QueuedConnection);
    connect(m_systemAudioRead, &SystemAudio_Read::SIG_captureError,
            this, [this](const QString &message) {
        qWarning() << "System audio capture:" << message;
        if (m_roomDialog) {
            m_roomDialog->slot_setSystemAudioError(message);
        }
    });

    connect(m_videoRead, SIGNAL(SIG_videoFrame(QImage)),
            this, SLOT(slot_refleshCameraVideoImage(QImage)));
    connect(m_videoRead, SIGNAL(SIG_videoFrame(QImage)),
            this, SLOT(slot_sendCameraVideoFrame(QImage)));

    connect(m_deskRead, SIGNAL(SIG_screenFrame(QImage)),
            this, SLOT(slot_refleshDesktopVideoImage(QImage)), Qt::QueuedConnection);
    connect(m_deskRead, SIGNAL(SIG_screenFrame(QImage)),
            this, SLOT(slot_sendDesktopVideoFrame(QImage)), Qt::QueuedConnection);

    connect(m_roomDialog, SIGNAL(SIG_audioOpen()),
            this, SLOT(slot_audioOpen()));
    connect(m_roomDialog, SIGNAL(SIG_audioClose()),
            this, SLOT(slot_audioClose()));
    connect(m_roomDialog, &RoomDialog::SIG_systemAudioStateChanged,
            this, &CKernal::slot_systemAudioStateChanged);
    connect(m_roomDialog, &RoomDialog::SIG_systemAudioDeviceChanged,
            this, &CKernal::slot_systemAudioDeviceChanged);

    connect(m_roomDialog, SIGNAL(SIG_captureOpen()),
            this, SLOT(slot_captureOpen()));
    connect(m_roomDialog, SIGNAL(SIG_captureClose()),
            this, SLOT(slot_captureClose()));

    connect(m_roomDialog, SIGNAL(SIG_deskOpen()),
            this, SLOT(slot_deskOpen()));
    connect(m_roomDialog, SIGNAL(SIG_deskClose()),
            this, SLOT(slot_deskClose()));
    connect(m_roomDialog, SIGNAL(SIG_desktopShareSettingsChanged(int,int,int)),
            this, SLOT(slot_setDesktopShareSettings(int,int,int)));
    connect(m_roomDialog, &RoomDialog::SIG_desktopShareScreenChanged,
            this, &CKernal::slot_setDesktopShareScreen);

    connect(m_roomDialog, SIGNAL(SIG_setFunnyPic(int)),
            this, SLOT(slot_setFunnyPic(int)));
    connect(m_roomDialog, &RoomDialog::SIG_chatMessageSend,
            this, &CKernal::slot_sendRoomChatMessage);
    connect(m_roomDialog, &RoomDialog::SIG_chatUserRequested,
            this, &CKernal::slot_showRoomMemberProfile);

    connect(this, &CKernal::SIG_sendVideoFrame,
            m_sendVideoThread.get(), &SendThread::slot_queueLatestFrame,
            Qt::DirectConnection);
    connect(this, &CKernal::SIG_encodeVideoFrame,
            m_videoEncodeThread.get(), &VideoEncodeThread::slot_encodeFrame);
    connect(this, &CKernal::SIG_resetVideoEncoder,
            m_videoEncodeThread.get(), &VideoEncodeThread::slot_reset);
    connect(m_videoEncodeThread.get(), &VideoEncodeThread::SIG_videoEncoded,
            this, &CKernal::slot_sendH264Frame);
    connect(m_videoEncodeThread.get(), &VideoEncodeThread::SIG_encodeFinished,
            this, &CKernal::slot_videoEncodeFinished);

    m_videoDecodeThread = new QThread(this);
    m_videoDecodeWorker = new VideoDecodeWorker;
    m_videoDecodeWorker->moveToThread(m_videoDecodeThread);
    connect(m_videoDecodeThread, &QThread::finished,
            m_videoDecodeWorker, &QObject::deleteLater);
    connect(m_videoDecodeWorker, &VideoDecodeWorker::SIG_frameDecoded,
            this, &CKernal::slot_videoFrameDecoded, Qt::QueuedConnection);
    m_videoDecodeThread->start();

    m_desktopStatsTimer->setInterval(1000);
    connect(m_desktopStatsTimer, &QTimer::timeout,
            this, &CKernal::slot_updateDesktopShareStats);

    m_latencyTimer->setInterval(5000);
    m_latencyTimeoutTimer->setSingleShot(true);
    m_latencyTimeoutTimer->setInterval(1500);
    m_loginTimeoutTimer->setSingleShot(true);
    m_loginTimeoutTimer->setInterval(6000);
    m_loginDeadline = new QTimer(this);
    m_loginDeadline->setSingleShot(true);
    m_loginDeadline->setInterval(20000);
    connect(m_loginDeadline, &QTimer::timeout, this, &CKernal::failLogin);
    m_roomRequestTimer->setSingleShot(true);
    m_roomRequestTimer->setInterval(kRoomRequestTimeoutMs);
    connect(m_latencyTimer, &QTimer::timeout,
            this, &CKernal::slot_measureLatency);
    connect(m_latencyTimeoutTimer, &QTimer::timeout,
            this, &CKernal::slot_latencyTimeout);
    connect(m_roomRequestTimer, &QTimer::timeout,
            this, &CKernal::slot_roomRequestTimeout);
    connect(m_latencySocket, &QTcpSocket::connected,
            this, &CKernal::slot_latencyConnected);
    connect(m_latencySocket, &QTcpSocket::errorOccurred,
            this, &CKernal::slot_latencyError);
    connect(m_loginTimeoutTimer, &QTimer::timeout,
            this, &CKernal::slot_loginTimeout);
    if (m_useWebRtc) setupWebRtc();
    m_latencyTimer->start();
    QTimer::singleShot(0, this, &CKernal::slot_measureLatency);
}



CKernal::~CKernal()
{
    // slot_destroy();
}



void CKernal::slot_destroy()
{
    qDebug() <<  __func__;
    m_shuttingDown = true;
    if (m_gateway) { m_gateway->leaveRoom(); m_gateway->logout(); }
    delete m_rtc; m_rtc = nullptr;
    m_avChannelRegistered[0].store(false);
    m_avChannelRegistered[1].store(false);
    m_loginDeadline->stop();
    m_latencyTimer->stop();
    m_latencyTimeoutTimer->stop();
    m_loginTimeoutTimer->stop();
    m_roomRequestTimer->stop();
    m_controlReconnectTimer->stop();
    for (QTimer *timer : m_avReconnectTimers) {
        if (timer) {
            timer->stop();
        }
    }
    m_latencyProbeActive = false;
    m_loginPending = false;
    m_roomRequest = RoomRequest::None;
    m_pendingRoomId = 0;
    m_latencySocket->abort();
    m_controlSocket->abort();
    if (m_systemAudioRead) {
        m_systemAudioRead->stopCapture();
        delete m_systemAudioRead;
        m_systemAudioRead = nullptr;
    }
    for (TcpClientMediator*& channel : m_tcpAV) {
        if (channel) {
            channel->CloseNet();
            delete channel;
            channel = nullptr;
        }
    }
    if(m_loginDialog){
        m_loginDialog->hide();  // 不写close。不然会触发关闭时间会死循环
        delete m_loginDialog; m_loginDialog = NULL;
    }
    if(m_demoDialog){
        m_demoDialog->hide();
        delete m_demoDialog; m_demoDialog = NULL;
    }
    if(m_roomDialog){
        m_roomDialog->hide();
        delete m_roomDialog; m_roomDialog = NULL;
    }
    if(m_setUserDialog){
        m_setUserDialog->hide();
        delete m_setUserDialog; m_setUserDialog = NULL;
    }
    if(m_audioRead){
        delete m_audioRead; m_audioRead = NULL;
    }
    if(m_videoRead){
        delete m_videoRead; m_videoRead = NULL;
    }
    if(m_deskRead){
        delete m_deskRead; m_deskRead = NULL;
    }
    m_videoEncodeThread.reset();
    if (m_videoDecodeThread && m_videoDecodeThread->isRunning()) {
        if (m_videoDecodeWorker) {
            QMetaObject::invokeMethod(m_videoDecodeWorker, "slot_resetAll",
                                      Qt::BlockingQueuedConnection);
        }
        m_videoDecodeThread->quit();
        m_videoDecodeThread->wait(1000);
        m_videoDecodeWorker = nullptr;
    }
}

void CKernal::slot_measureLatency()
{
    if (m_useWebRtc) {
        if (m_gateway && !m_loginPending) m_gateway->ping();
        if (m_roomid && (!m_mediaLatencyClock.isValid() || m_mediaLatencyClock.elapsed() > 5000))
            m_roomDialog->slot_setLatency(-1, "media");
        return;
    }
    // Login owns the only server connection until the main control socket has
    // authenticated. Do not let a probe race the login connection.
    if (!m_loggedIn || m_loginPending) {
        return;
    }
    if (m_latencyProbeActive) {
        return;
    }

    if (m_latencySocket->state() != QAbstractSocket::UnconnectedState) {
        m_latencySocket->abort();
    }

    m_latencyProbeActive = true;
    m_latencyElapsed.restart();
    m_latencyTimeoutTimer->start();
    m_latencySocket->connectToHost(m_serverIP, m_serverPort);
}

void CKernal::slot_latencyConnected()
{
    if (!m_latencyProbeActive) {
        return;
    }

    const int latencyMs = static_cast<int>(m_latencyElapsed.elapsed());
    m_latencyProbeActive = false;
    m_latencyTimeoutTimer->stop();
    m_latencySocket->abort();
    updateLatencyDisplay(latencyMs);
}

void CKernal::slot_latencyError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    if (!m_latencyProbeActive) {
        return;
    }

    m_latencyProbeActive = false;
    m_latencyTimeoutTimer->stop();
    updateLatencyDisplay(-2);
}

void CKernal::slot_latencyTimeout()
{
    if (!m_latencyProbeActive) {
        return;
    }

    m_latencyProbeActive = false;
    m_latencySocket->abort();
    updateLatencyDisplay(-2);
}

void CKernal::updateLatencyDisplay(int latencyMs)
{
    if (m_loginDialog) {
        m_loginDialog->slot_setLatency(latencyMs, m_useWebRtc ? "gateway" : "");
    }
    if (m_demoDialog) {
        m_demoDialog->slot_setLatency(latencyMs, m_useWebRtc ? "gateway" : "");
    }
    if (m_roomDialog && (!m_useWebRtc || !m_roomid)) {
        m_roomDialog->slot_setLatency(latencyMs);
    }
}

void CKernal::slot_loginTimeout()
{
    if (!m_loginPending) {
        return;
    }

    // Cover both a sent login request and a connection that never reaches the
    // connected state. Aborting guarantees the recovery path gets a fresh TCP
    // socket instead of leaving the dialog pending forever.
    if (m_controlSocket->state() != QAbstractSocket::UnconnectedState) {
        m_controlSocket->abort();
    } else {
        beginControlRecovery();
    }
}

void CKernal::slot_controlConnected()
{
    if (m_shuttingDown) {
        return;
    }

    m_controlReconnectTimer->stop();
    if (m_loginPending) {
        sendPendingLogin();
    } else {
        sendPendingRegistration();
    }
}

void CKernal::slot_controlReadyRead()
{
    m_controlReceiveBuffer.append(m_controlSocket->readAll());

    while (m_controlReceiveBuffer.size() >= static_cast<int>(sizeof(int))) {
        int payloadLength = 0;
        memcpy(&payloadLength, m_controlReceiveBuffer.constData(), sizeof(payloadLength));
        if (payloadLength < static_cast<int>(sizeof(PackType)) ||
                payloadLength > _DEF_MAX_PACKET_SIZE) {
            qWarning() << "Invalid control frame length:" << payloadLength;
            m_controlReceiveBuffer.clear();
            m_controlSocket->abort();
            return;
        }

        const int frameLength = static_cast<int>(sizeof(int)) + payloadLength;
        if (m_controlReceiveBuffer.size() < frameLength) {
            return;
        }

        char* packet = new char[payloadLength];
        memcpy(packet, m_controlReceiveBuffer.constData() + sizeof(int), payloadLength);
        m_controlReceiveBuffer.remove(0, frameLength);
        slot_dealData(0, packet, payloadLength);
    }
}

void CKernal::slot_controlDisconnected()
{
    if (m_shuttingDown) {
        return;
    }
    beginControlRecovery();
}

void CKernal::slot_controlError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    if (m_shuttingDown) {
        return;
    }
    if (m_controlSocket->state() == QAbstractSocket::ConnectedState) {
        m_controlSocket->abort();
        return;
    }
    beginControlRecovery();
}

void CKernal::slot_controlReconnect()
{
    if (!m_shuttingDown && !m_loggedIn && m_loginPending) {
        ensureControlConnection();
    }
}

void CKernal::slot_dealAudioChannelData(uint socket, char *buf, int nlen)
{
    if (!buf || nlen < static_cast<int>(sizeof(PackType))) {
        delete[] buf;
        return;
    }
    int type = 0;
    memcpy(&type, buf, sizeof(type));
    if (type == DEF_PACK_AUDIO_REGISTER_RS) {
        handleMediaRegistration(0, buf, nlen);
        delete[] buf;
    } else if (type == DEF_PACK_AUDIO_FRAME && m_avChannelRegistered[0].load()) {
        slot_dealData(socket, buf, nlen);
    } else {
        delete[] buf;
    }
}

void CKernal::slot_dealVideoChannelData(uint socket, char *buf, int nlen)
{
    if (!buf || nlen < static_cast<int>(sizeof(PackType))) {
        delete[] buf;
        return;
    }
    int type = 0;
    memcpy(&type, buf, sizeof(type));
    if (type == DEF_PACK_VIDEO_REGISTER_RS) {
        handleMediaRegistration(1, buf, nlen);
        delete[] buf;
    } else if (type == DEF_PACK_VIDEO_KEYFRAME_RQ) {
        handleVideoKeyFrameRequest(buf, nlen);
        delete[] buf;
    } else if (type == DEF_PACK_VIDEO_FRAME && m_avChannelRegistered[1].load()) {
        slot_dealData(socket, buf, nlen);
    } else {
        delete[] buf;
    }
}

void CKernal::slot_dealData(uint socket, char *buf, int nlen)
{
    if (!buf || nlen < static_cast<int>(sizeof(PackType)) ||
            nlen > _DEF_MAX_PACKET_SIZE) {
        delete[] buf;
        return;
    }
    // 协议映射表
    int type = 0;
    memcpy(&type, buf, sizeof(type));
    if(type >= DEF_PACK_BASE && type < DEF_PACK_BASE + DEF_PACK_COUNT)
    {
        PFUN pf = NetMap(type);
        if( pf ){
            (this->*pf)(socket, buf, nlen);
        }
    }

    delete[] buf;
}

// 登录回复处理
void CKernal::slot_dealLoginRs(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (!m_loginPending) return;
    m_loginDeadline->stop();
    // Every 10003 response, including a failure response, ends the pending UI
    // state. The control receive buffer has already assembled the outer frame.
    m_loginTimeoutTimer->stop();
    m_loginPending = false;
    if (m_loginDialog) {
        m_loginDialog->slot_setLoginPending(false);
    }

    if (nlen != static_cast<int>(sizeof(STRU_LOGIN_RS))) {
        resetAuthenticationState();
        QMessageBox::warning(m_loginDialog, "提示",
                             "服务器返回的登录响应长度无效，请重试");
        return;
    }

    int userid = 0;
    int result = 0;
    memcpy(&userid, buf + offsetof(STRU_LOGIN_RS, userid), sizeof(userid));
    memcpy(&result, buf + offsetof(STRU_LOGIN_RS, result), sizeof(result));
    if (result != login_success) {
        resetAuthenticationState();
        switch (result) {
        case user_not_exist:
        QMessageBox::about( this->m_loginDialog, "提示" , "查无此人, 登陆失败");
        break;
        case password_error:
        QMessageBox::about( this->m_loginDialog, "提示" , "密码错误，登录失败");
        break;
        case server_error:
            QMessageBox::warning(m_loginDialog, "提示", "服务器内部错误，请稍后重试");
            break;
        default:
            QMessageBox::warning(m_loginDialog, "提示", "登录失败，请稍后重试");
            break;
        }
        return;
    }

    if (userid <= 0) {
        resetAuthenticationState();
        QMessageBox::warning(m_loginDialog, "提示", "服务器返回的登录会话无效，请重试");
        return;
    }

    char sessionToken[SESSION_TOKEN_SIZE] = {};
    // Keep the NUL byte: the server validates all 33 bytes of this field.
    memcpy(sessionToken, buf + offsetof(STRU_LOGIN_RS, sessionToken),
           sizeof(sessionToken));
    if (!hasValidSessionToken(sessionToken)) {
        resetAuthenticationState();
        QMessageBox::warning(m_loginDialog, "提示", "服务器返回的登录令牌无效，请重试");
        return;
    }

    m_userid = userid;
    memcpy(m_sessionToken, sessionToken, sizeof(m_sessionToken));
    m_loggedIn = true;
    m_loginDialog->hide();
    m_demoDialog->showNormal();

    // Audio and video have their own channels. They receive the same complete
    // token only after the control connection has authenticated this user.
    ensureAvConnections();
    // Reauthentication creates a new server session with no room membership.
    // Rejoin explicitly instead of keeping a room UI that cannot receive media.
    if (m_roomid != 0) {
        const int previousRoom = m_roomid;
        slot_quitRoom();
        STRU_JOINROOM_RQ request;
        request.m_UserID = m_userid;
        request.m_RoomID = previousRoom;
        beginRoomRequest(RoomRequest::Join, previousRoom);
        if (!slot_clientSendData(0, reinterpret_cast<char*>(&request), sizeof(request))) {
            clearRoomRequest();
            m_demoDialog->slot_setPushButton_enable(true);
        }
    }
    QTimer::singleShot(0, this, &CKernal::slot_measureLatency);
}

// 注册回复处理
void CKernal::slot_dealRegisterRs(uint socket, char *buf, int nlen)
{
    if (nlen != static_cast<int>(sizeof(STRU_REGISTER_RS))) {
        return;
    }
    // 拆包
    STRU_REGISTER_RS * rs = (STRU_REGISTER_RS*)buf;
    // 根据结果，弹窗提示
    switch (rs->result) {
    case tel_is_exist:
        m_registerDraftTel.clear();
        m_registerDraftPassword.clear();
        m_registerDraftNickname.clear();
        m_registerDraftAvatarData.clear();
        m_registerDraftAvatarChanged = false;
        QMessageBox::about( this->m_loginDialog, "提示" , "用户已经存在, 注册失败");
        break;
    case register_success: {
        const QString registeredTel = m_registerDraftTel;
        const QString registeredPassword = m_registerDraftPassword;
        m_pendingRegistrationTel = m_registerDraftTel;
        m_pendingRegistrationNickname = m_registerDraftNickname;
        m_pendingRegistrationIconId = m_registerDraftIconId;
        m_pendingRegistrationAvatarData = m_registerDraftAvatarData;
        m_pendingRegistrationAvatarChanged = m_registerDraftAvatarChanged;

        m_registerDraftTel.clear();
        m_registerDraftPassword.clear();
        m_registerDraftNickname.clear();
        m_registerDraftAvatarData.clear();
        m_registerDraftAvatarChanged = false;

        QMessageBox::about(this->m_loginDialog, "提示", "注册成功，正在登录");
        slot_loginCommit(registeredTel, registeredPassword);
        break;
    }
    case server_error:
        m_registerDraftTel.clear();
        m_registerDraftPassword.clear();
        m_registerDraftNickname.clear();
        m_registerDraftAvatarData.clear();
        m_registerDraftAvatarChanged = false;
        QMessageBox::warning(m_loginDialog, "提示", "服务器内部错误，请稍后重试");
        break;
    }
}

void CKernal::slot_dealCreateRoomRs(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (nlen != static_cast<int>(sizeof(STRU_CREATEROOM_RS))) return;
    if (m_roomRequest != RoomRequest::Create) return;
    const STRU_CREATEROOM_RS* rs = reinterpret_cast<STRU_CREATEROOM_RS*>(buf);

    if (!isValidRoomId(rs->m_RoomId)) {
        clearRoomRequest();
        m_demoDialog->slot_setPushButton_enable(true);
        QMessageBox::warning(m_demoDialog, "提示", "创建房间失败，服务器未返回有效房间号");
        return;
    }
    slot_setJoinedRoom(rs->m_RoomId);
}

void CKernal::slot_dealJoinRoomRs(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (nlen != static_cast<int>(sizeof(STRU_JOINROOM_RS))) return;
    if (m_roomRequest != RoomRequest::Join) return;
    const STRU_JOINROOM_RS* rs = reinterpret_cast<STRU_JOINROOM_RS*>(buf);

    if (rs->m_lResult != join_success ||
            rs->m_RoomID != m_pendingRoomId || !isValidRoomId(rs->m_RoomID)) {
        clearRoomRequest();
        m_demoDialog->slot_setPushButton_enable(true);
        QMessageBox::warning(m_demoDialog, "提示", "加入房间失败，请确认房间号后重试");
        return;
    }
    slot_setJoinedRoom(rs->m_RoomID);
}

void CKernal::slot_dealRoomMemberRq(uint socket, char *buf, int nlen)
{
    if (nlen != static_cast<int>(sizeof(STRU_ROOM_MEMBER_RQ))) return;
    if (m_roomid == 0 && m_roomRequest == RoomRequest::None) return;

    const STRU_ROOM_MEMBER_RQ* rq = reinterpret_cast<STRU_ROOM_MEMBER_RQ*>(buf);
    ensureRemoteParticipant(rq->m_UserID, QString::fromStdString(rq->m_szUser));
}

void CKernal::slot_dealRoomChatMessage(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (nlen != static_cast<int>(sizeof(STRU_ROOM_CHAT_MESSAGE)) || m_roomid == 0) {
        return;
    }

    const auto *message = reinterpret_cast<const STRU_ROOM_CHAT_MESSAGE *>(buf);
    if (message->m_RoomID != m_roomid || message->m_UserID <= 0 ||
            message->m_messageLength <= 0 ||
            message->m_messageLength > DEF_ROOM_CHAT_MAX_BYTES ||
            message->m_UserID == m_userid) {
        return;
    }

    const QString text = QString::fromUtf8(message->m_message, message->m_messageLength).trimmed();
    if (text.isEmpty()) {
        return;
    }

    QString senderName;
    const auto participant = m_mapIDToUserShow.find(message->m_UserID);
    if (participant != m_mapIDToUserShow.end()) {
        senderName = participant->second->m_name;
    }
    if (senderName.isEmpty()) {
        senderName = QString("用户%1").arg(message->m_UserID);
    }
    ensureRemoteParticipant(message->m_UserID, senderName);
    m_roomDialog->slot_appendChatMessage(message->m_UserID, senderName, text, false);
}

void CKernal::slot_dealUserInfoRq(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (nlen != static_cast<int>(sizeof(STRU_USER_INFO_RQ))) return;
    STRU_USER_INFO_RQ* rq = reinterpret_cast<STRU_USER_INFO_RQ*>(buf);
    if (!m_loggedIn || rq->m_UserID != m_userid) {
        return;
    }

    m_name = QString::fromStdString(rq->m_szUser);
    m_userIconId = qBound(0, rq->m_iconid, 35);
    const QString feeling = QString::fromStdString(rq->m_userFeeling);
    m_userFeeling = feeling;

    m_demoDialog->slot_setInfo(m_name, m_userIconId);
    m_setUserDialog->slot_setInfo(m_userIconId, m_name, feeling);
    updateOwnRoomAvatar(m_userAvatar.isNull() ? builtInAvatar(m_userIconId)
                                               : m_userAvatar);

    if (m_pendingRegistrationTel == m_loginTel &&
            (!m_pendingRegistrationNickname.isEmpty() ||
             m_pendingRegistrationAvatarChanged)) {
        const QString updatedName = m_pendingRegistrationNickname.isEmpty()
                ? m_name : m_pendingRegistrationNickname;
        const int updatedIconId = m_pendingRegistrationAvatarChanged
                ? m_pendingRegistrationIconId : m_userIconId;

        const QByteArray updatedAvatar = m_pendingRegistrationAvatarData;
        const bool avatarChanged = m_pendingRegistrationAvatarChanged;

        m_pendingRegistrationTel.clear();
        m_pendingRegistrationNickname.clear();
        m_pendingRegistrationAvatarData.clear();
        m_pendingRegistrationAvatarChanged = false;
        if (!queueUserInfoSave(updatedIconId, updatedName, feeling,
                               updatedAvatar, avatarChanged)) {
            QMessageBox::warning(m_loginDialog, "提示",
                                 "用户资料暂未保存，正在重新连接服务器");
        }
        return;
    }

    if (m_profileSavePending) {
        const QByteArray avatarData = m_pendingProfileAvatarData;
        const bool avatarChanged = m_pendingProfileAvatarChanged;
        clearPendingProfileSave();
        if (avatarChanged) {
            if (!sendAvatarUpload(avatarData)) {
                QMessageBox::warning(m_setUserDialog, "提示",
                                     "头像未上传，正在重新登录后重试");
            }
        } else {
            QMessageBox::information(m_setUserDialog, "提示", "用户资料已保存");
        }
    }

    requestAvatar(m_userid);
}

void CKernal::slot_dealAvatarUploadRs(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (nlen != static_cast<int>(sizeof(STRU_AVATAR_UPLOAD_RS))) {
        return;
    }

    const STRU_AVATAR_UPLOAD_RS *rs = reinterpret_cast<STRU_AVATAR_UPLOAD_RS *>(buf);
    if (rs->m_UserID != m_userid) {
        return;
    }
    if (rs->m_result == 0) {
        if (m_avatarUploadPending) {
            if (m_pendingAvatarUploadData.isEmpty()) {
                m_userAvatar = QImage();
                m_demoDialog->slot_setInfo(m_name, m_userIconId);
                updateOwnRoomAvatar(builtInAvatar(m_userIconId));
            } else {
                QImage avatar;
                if (avatar.loadFromData(m_pendingAvatarUploadData, "JPG")) {
                    // A confirmed upload should be visible immediately instead
                    // of waiting for the next avatar fetch round trip.
                    applyOwnAvatar(avatar);
                }
            }
        }
        m_avatarUploadPending = false;
        m_pendingAvatarUploadData.clear();
        requestAvatar(m_userid);
        QMessageBox::information(m_setUserDialog, "提示", "用户资料已保存，头像上传成功");
        return;
    }

    QString message;
    switch (rs->m_result) {
    case 1:
        message = "登录令牌已失效，正在重新登录";
        break;
    case 2:
        message = "控制连接与用户认证不匹配，正在重新登录";
        break;
    case 3:
        message = "图片无效，请选择 JPEG 格式且不超过 256 KiB 的头像";
        break;
    case 4:
        message = "服务器数据库保存失败，请稍后重试";
        break;
    case 5:
        message = "服务器文件保存失败，请稍后重试";
        break;
    default:
        message = QString("服务端拒绝了头像上传（错误码 %1）").arg(rs->m_result);
        break;
    }
    qWarning() << "Avatar upload rejected by server:" << rs->m_result
               << "for user" << rs->m_UserID;
    m_avatarUploadPending = false;
    m_pendingAvatarUploadData.clear();
    QMessageBox::warning(m_setUserDialog, "头像上传失败", message);
    if (rs->m_result == 1 || rs->m_result == 2) {
        beginControlRecovery();
    }
}

void CKernal::slot_dealAvatarDataRs(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    if (nlen < static_cast<int>(sizeof(STRU_AVATAR_DATA_RS))) {
        return;
    }

    const STRU_AVATAR_DATA_RS *rs = reinterpret_cast<STRU_AVATAR_DATA_RS *>(buf);
    if (rs->m_imageSize < 0 || rs->m_imageSize > DEF_AVATAR_IMAGE_MAX_SIZE ||
            nlen != static_cast<int>(sizeof(STRU_AVATAR_DATA_RS)) + rs->m_imageSize) {
        return;
    }

    if (rs->m_result != 0) {
        return;
    }

    if (rs->m_imageSize == 0) {
        if (rs->m_TargetUserID == m_userid) {
            m_avatarUploadPending = false;
            m_pendingAvatarUploadData.clear();
            m_userAvatar = QImage();
            m_demoDialog->slot_setInfo(m_name, m_userIconId);
            updateOwnRoomAvatar(builtInAvatar(m_userIconId));
        }
        return;
    }

    QImage avatar;
    const auto *imageData = reinterpret_cast<const uchar *>(
        buf + sizeof(STRU_AVATAR_DATA_RS));
    const QByteArray imageBytes(reinterpret_cast<const char *>(imageData), rs->m_imageSize);
    if (!hasJpegSignature(imageBytes)) {
        return;
    }
    if (!avatar.loadFromData(imageData, rs->m_imageSize, "JPG")) {
        return;
    }

    if (rs->m_TargetUserID == m_userid) {
        m_avatarUploadPending = false;
        m_pendingAvatarUploadData.clear();
        applyOwnAvatar(avatar);
    }
    const auto user = m_mapIDToUserShow.find(rs->m_TargetUserID);
    if (user != m_mapIDToUserShow.end()) {
        user->second->slot_setAvatarImage(avatar);
    }
    m_roomDialog->slot_setBigAvatar(rs->m_TargetUserID, avatar);
    if (m_userProfileDialog && m_userProfileDialog->userId() == rs->m_TargetUserID) {
        m_userProfileDialog->slot_setAvatar(avatar);
    }
}

void CKernal::slot_dealLeaveRoomRq(uint socket, char *buf, int nlen)
{
    if (nlen != static_cast<int>(sizeof(STRU_LEAVEROOM_RQ))) return;
    // 拆包
    STRU_LEAVEROOM_RQ * rq = (STRU_LEAVEROOM_RQ*)buf;
    // 删除特定的人
    if(m_mapIDToUserShow.count(rq->m_nUserId) == 0) return ;
    UserShow* user = m_mapIDToUserShow[rq->m_nUserId];

    m_mapIDToUserShow.erase(user->m_id);
    m_roomDialog->slot_removeUser(user);

    // todo 音频map去掉这个人
    m_mapIDToAudioWrite.erase(user->m_id);
    m_mapIDToVideoDecoder.erase(user->m_id);
}


void CKernal::slot_setJoinedRoom(int roomid)
{
    if (!m_useWebRtc && !isValidRoomId(roomid)) {
        return;
    }
    clearRoomRequest();
    m_roomid = roomid;
    m_remoteVideoGeneration.fetch_add(1);
    if (m_videoDecodeWorker) {
        QMetaObject::invokeMethod(m_videoDecodeWorker, "slot_resetAll",
                                  Qt::QueuedConnection);
    }
    // 可以写成原界面隐藏 或者按钮失效
    // m_demoDialog->hide();    按键变会代替窗口隐藏
    m_demoDialog->slot_setPushButton_enable(false);
    m_roomDialog->showNormal();

    // 初始化状态 清空等操作 可以复用
    // todo
    m_roomDialog->slot_setRoomID(roomid);
    if (m_useWebRtc) m_roomDialog->slot_setRoomName(m_webRoomName);
    const QImage ownAvatar = m_userAvatar.isNull()
            ? builtInAvatar(m_userIconId) : m_userAvatar;
    UserShow *user = nullptr;
    const auto existing = m_mapIDToUserShow.find(m_userid);
    if (existing != m_mapIDToUserShow.end()) {
        user = existing->second;
        user->slot_setInfo(m_userid, m_name);
    } else {
        user = new UserShow;
        user->slot_setInfo(m_userid, m_name);
        connect(user, SIGNAL(SIG_userClicked(int,QString)),
                m_roomDialog, SLOT(slot_setBigImageInfo(int,QString)));
        m_mapIDToUserShow[m_userid] = user;
        m_roomDialog->slot_addUser(user);
    }
    user->slot_setAvatarImage(ownAvatar);
    m_roomDialog->slot_setBigUser(m_userid, m_name, ownAvatar);
    // The server keeps audio and video subscriptions on dedicated sockets.
    // Re-register both when entering a room so a viewer never depends on a
    // connection created before the room join.
    ensureAvConnections(true);
}

// 弹出框
void CKernal::slot_setUser()
{
    m_setUserDialog->show();
}

void CKernal::slot_userSetCommit(int iconid, QString name, QString feeling,
                                 QByteArray avatarData, bool avatarChanged)
{
    if (m_profileSavePending) {
        QMessageBox::information(m_setUserDialog, "提示", "上一项用户资料正在保存，请稍候");
        return;
    }
    if (!queueUserInfoSave(iconid, name, feeling, avatarData, avatarChanged)) {
        QMessageBox::warning(m_setUserDialog, "提示",
                             "控制连接已断开，正在重新登录；请稍后重新保存资料");
    }
}

void CKernal::requestAvatar(int targetUserId)
{
    if (m_useWebRtc) { if (targetUserId > 0 && m_gateway->authenticated()) m_gateway->fetchAvatar(targetUserId); return; }
    if (targetUserId <= 0 || !hasAuthenticatedControlConnection() ||
            !hasValidSessionToken(m_sessionToken)) {
        return;
    }

    STRU_AVATAR_FETCH_RQ rq;
    rq.m_RequestUserID = m_userid;
    rq.m_TargetUserID = targetUserId;
    memcpy(rq.m_sessionToken, m_sessionToken, sizeof(rq.m_sessionToken));
    sendControlPacket(reinterpret_cast<char*>(&rq), sizeof(rq));
}

bool CKernal::sendAvatarUpload(const QByteArray &avatarData)
{
    if (m_useWebRtc) { m_gateway->uploadAvatar(avatarData); return m_gateway->authenticated(); }
    if (!hasAuthenticatedControlConnection() || !hasValidSessionToken(m_sessionToken) ||
            avatarData.size() > DEF_AVATAR_IMAGE_MAX_SIZE ||
            (!avatarData.isEmpty() && !hasJpegSignature(avatarData))) {
        return false;
    }

    STRU_AVATAR_UPLOAD_RQ header;
    header.m_UserID = m_userid;
    header.m_imageSize = avatarData.size();
    memcpy(header.m_sessionToken, m_sessionToken, sizeof(header.m_sessionToken));

    QByteArray packet(static_cast<int>(sizeof(header)) + avatarData.size(), '\0');
    memcpy(packet.data(), &header, sizeof(header));
    if (!avatarData.isEmpty()) {
        memcpy(packet.data() + sizeof(header), avatarData.constData(), avatarData.size());
    }
    if (!sendControlPacket(packet.constData(), packet.size())) {
        return false;
    }

    m_pendingAvatarUploadData = avatarData;
    m_avatarUploadPending = true;
    return true;
}

// 退出房间
void CKernal::slot_quitRoom()
{
    clearRoomRequest();
    if (m_roomid == 0) {
        m_roomDialog->hide();
        m_demoDialog->showNormal();
        return;
    }

    // Stop on the video channel while the current room id is still valid.
    // Resetting the room id first used to suppress the final stop frame and
    // could leave the next room with a stale local sharing state.
    if (m_activeVideoSource == VideoSource::Desktop) {
        m_deskRead->pause();
        stopLocalVideo();
    } else if (m_activeVideoSource == VideoSource::Camera) {
        m_videoRead->pause();
        stopLocalVideo();
    }

    // 发包
    STRU_LEAVEROOM_RQ rq;
    rq.m_nUserId = m_userid;
    rq.m_RoomId = m_roomid;

    if (m_useWebRtc) { m_gateway->leaveRoom(); m_rtc->stop(); m_webRoomName.clear(); }
    else slot_clientSendData(0, (char*)&rq, sizeof(rq));

    m_roomid = 0;
    m_remoteVideoGeneration.fetch_add(1);
    m_demoDialog->slot_setPushButton_enable(true);
    // 声音关闭 ui 后台
    m_roomDialog->slot_setRoomClear();

    // 视频回收掉
    for(auto ite = m_mapIDToUserShow.begin(); ite != m_mapIDToUserShow.end();
        /*++ite*/)
    {
        UserShow* user= ite->second;
        ite = m_mapIDToUserShow.erase(ite);    // 迭代器失效
        m_roomDialog->slot_removeUser(user);

    }

    // 音频播放的回收 map
    for(auto ite = m_mapIDToAudioWrite.begin(); ite != m_mapIDToAudioWrite.end();
        /*++ite*/)
    {
        Audio_Write* user= ite->second;
        ite = m_mapIDToAudioWrite.erase(ite);
        delete user;
    }
    m_mapIDToVideoDecoder.clear();

    m_roomDialog->hide();
    m_demoDialog->showNormal();
}

void CKernal::beginRoomRequest(RoomRequest request, int roomId)
{
    m_roomRequest = request;
    m_pendingRoomId = roomId;
    m_demoDialog->slot_setPushButton_enable(false);
    m_roomRequestTimer->start();
}

void CKernal::clearRoomRequest()
{
    m_roomRequestTimer->stop();
    m_roomRequest = RoomRequest::None;
    m_pendingRoomId = 0;
}

void CKernal::slot_roomRequestTimeout()
{
    const RoomRequest request = m_roomRequest;
    if (request == RoomRequest::None) {
        return;
    }

    clearRoomRequest();
    m_demoDialog->slot_setPushButton_enable(true);
    const QString message = request == RoomRequest::Create
            ? "创建房间超时，请重试"
            : "加入房间超时，请重试";
    QMessageBox::warning(m_demoDialog, "提示", message);
}

void CKernal::slot_audioOpen()
{
    m_audioRead->start();
}

void CKernal::slot_audioClose()
{
    m_audioRead->pause();
}

void CKernal::slot_systemAudioStateChanged(bool enabled)
{
    if (!m_audioRead || !m_systemAudioRead) {
        return;
    }
    m_audioRead->setSystemAudioEnabled(enabled);
    if (enabled) {
        m_systemAudioRead->startCapture();
    } else {
        m_systemAudioRead->stopCapture();
    }
}

void CKernal::slot_systemAudioDeviceChanged(const QString &deviceId)
{
    if (m_systemAudioRead) {
        m_systemAudioRead->setDeviceId(deviceId);
    }
}

void CKernal::slot_captureOpen()
{
    slot_systemAudioStateChanged(false);
    if (m_roomDialog) {
        m_roomDialog->slot_setSystemAudioError(QString());
    }
    if (m_activeVideoSource == VideoSource::Camera) {
        return;
    }
    if (m_activeVideoSource == VideoSource::Desktop) {
        m_deskRead->pause();
        stopLocalVideo();
    }
    Q_EMIT SIG_resetVideoEncoder();
    m_activeVideoSource = VideoSource::Camera;
    m_roomDialog->slot_clearDesktopShareStatus();
    m_videoRead->start();
}

void CKernal::slot_captureClose()
{
    m_videoRead->pause();
    if (m_activeVideoSource == VideoSource::Camera) {
        stopLocalVideo();
    }
}

void CKernal::slot_deskOpen()
{
    if (m_activeVideoSource == VideoSource::Desktop) {
        return;
    }
    if (m_roomid == 0) {
        m_roomDialog->slot_setMediaStatus("请先加入房间后再共享桌面");
        return;
    }
    if (m_activeVideoSource == VideoSource::Camera) {
        m_videoRead->pause();
        stopLocalVideo();
    }
    // Refresh the server-side video-socket association for every new share.
    // This repairs a channel which remained TCP-connected but lost its media
    // registration after a prior room leave or a server-side queue reset.
    if (!ensureAvChannelReady(1, true)) {
        m_roomDialog->slot_setMediaStatus("视频通道重连中，桌面共享将自动恢复…");
        scheduleAvChannelRecovery(1);
    }
    Q_EMIT SIG_resetVideoEncoder();
    m_activeVideoSource = VideoSource::Desktop;
    resetDesktopShareStats();
    DiagnosticLogger::instance().startDesktopShare(
                m_desktopWidth, m_desktopHeight,
                m_desktopWidth, m_desktopHeight,
                m_desktopFrameRate, m_desktopBitRate,
                m_roomDialog->desktopShareScreenName());
    m_desktopStatsTimer->start();
    m_roomDialog->slot_setDesktopSharePending(m_desktopFrameRate, m_desktopBitRate);
    m_deskRead->start();
}

void CKernal::slot_deskClose()
{
    slot_systemAudioStateChanged(false);
    m_deskRead->pause();
    if (m_activeVideoSource == VideoSource::Desktop) {
        stopLocalVideo();
    }
    m_roomDialog->slot_clearDesktopShareStatus();
}

int CKernal::desktopShareBitRate(int width, int height, int frameRate) const
{
    const qint64 pixelsPerSecond = static_cast<qint64>(width) * height * frameRate;
    const qint64 requestedBitRate = static_cast<qint64>(DESKTOP_VIDEO_BITRATE)
            * pixelsPerSecond / kDesktopReferencePixelsPerSecond;
    return static_cast<int>(qBound(kMinimumDesktopVideoBitRate, requestedBitRate,
                                   static_cast<qint64>(DESKTOP_MAX_VIDEO_BITRATE)));
}

void CKernal::slot_setDesktopShareSettings(int width, int height, int frameRate)
{
    const bool supportedResolution = (width == 1280 && height == 720)
            || (width == 1920 && height == 1080)
            || (width == 2560 && height == 1440)
            || (width == 3840 && height == 2160);
    const bool supportedFrameRate = frameRate == 15 || frameRate == 30 || frameRate == 60;
    if (!supportedResolution || !supportedFrameRate) {
        return;
    }

    const bool changed = m_desktopWidth != width || m_desktopHeight != height
            || m_desktopFrameRate != frameRate;
    m_desktopWidth = width;
    m_desktopHeight = height;
    m_desktopFrameRate = frameRate;
    m_desktopBitRate = desktopShareBitRate(width, height, frameRate);
    m_deskRead->setCaptureSettings(width, height, frameRate);

    if (changed && m_activeVideoSource == VideoSource::Desktop) {
        resetDesktopShareStats();
        m_roomDialog->slot_setDesktopSharePending(m_desktopFrameRate, m_desktopBitRate);
        Q_EMIT SIG_resetVideoEncoder();
    }
}

void CKernal::slot_setDesktopShareScreen(const QString &screenName)
{
    if (!m_deskRead || screenName.isEmpty()) {
        return;
    }
    m_deskRead->setCaptureScreenName(screenName);
    if (m_activeVideoSource == VideoSource::Desktop) {
        resetDesktopShareStats();
        m_roomDialog->slot_setDesktopSharePending(m_desktopFrameRate, m_desktopBitRate);
        Q_EMIT SIG_resetVideoEncoder();
    }
}

void CKernal::slot_sendRoomChatMessage(const QString &message)
{
    if (m_useWebRtc) { if (m_roomid) m_gateway->sendChat(message); return; }
    if (m_roomid == 0 || !hasAuthenticatedControlConnection()) {
        return;
    }

    const QString normalizedMessage = message.trimmed();
    const QByteArray messageUtf8 = normalizedMessage.toUtf8();
    if (messageUtf8.isEmpty() || messageUtf8.size() > DEF_ROOM_CHAT_MAX_BYTES) {
        QMessageBox::warning(m_roomDialog, "提示", "消息不能为空且不能超过 512 个字节");
        return;
    }

    STRU_ROOM_CHAT_MESSAGE packet;
    packet.m_RoomID = m_roomid;
    packet.m_UserID = m_userid;
    packet.m_messageLength = messageUtf8.size();
    memcpy(packet.m_message, messageUtf8.constData(), messageUtf8.size());
    if (!sendControlPacket(reinterpret_cast<const char *>(&packet), sizeof(packet))) {
        QMessageBox::warning(m_roomDialog, "提示", "消息发送失败，正在重新连接");
        return;
    }
    m_roomDialog->slot_appendChatMessage(m_userid, m_name, normalizedMessage, true);
    m_roomDialog->slot_clearChatInput();
}

void CKernal::slot_showRoomMemberProfile(int userId)
{
    if (!m_userProfileDialog || userId <= 0 || m_roomid == 0) {
        return;
    }

    const bool currentUser = userId == m_userid;
    QString name;
    QString feeling;
    QImage avatar;

    if (currentUser) {
        name = m_name;
        feeling = m_userFeeling;
        avatar = m_userAvatar.isNull() ? builtInAvatar(m_userIconId) : m_userAvatar;
    } else {
        const auto participant = m_mapIDToUserShow.find(userId);
        if (participant == m_mapIDToUserShow.end()) {
            return;
        }
        name = participant->second->m_name;
        avatar = participant->second->m_avatarImg.isNull()
                ? participant->second->m_defaultImg : participant->second->m_avatarImg;
        requestAvatar(userId);
    }

    m_userProfileDialog->setProfile(userId, name, avatar, feeling, currentUser);
    m_userProfileDialog->show();
    m_userProfileDialog->raise();
    m_userProfileDialog->activateWindow();
}

void CKernal::slot_cameraStateChanged(bool active)
{
    if (!active && m_activeVideoSource == VideoSource::Camera) {
        stopLocalVideo();
    }
}

void CKernal::ensureRemoteParticipant(int userId, const QString &name)
{
    if (userId <= 0 || userId == m_userid) {
        return;
    }

    const auto existing = m_mapIDToUserShow.find(userId);
    if (existing != m_mapIDToUserShow.end()) {
        if (!name.isEmpty()) {
            existing->second->slot_setInfo(userId, name);
        }
    } else {
        UserShow *user = new UserShow;
        user->slot_setInfo(userId, name.isEmpty() ? QString("用户 %1").arg(userId) : name);
        connect(user, SIGNAL(SIG_userClicked(int,QString)),
                m_roomDialog, SLOT(slot_setBigImageInfo(int,QString)));
        m_mapIDToUserShow[userId] = user;
        m_roomDialog->slot_addUser(user);
        requestAvatar(userId);
    }

    if (m_mapIDToAudioWrite.count(userId) == 0) {
        m_mapIDToAudioWrite[userId] = new Audio_Write(nullptr, m_useWebRtc);
    }
}

QImage CKernal::builtInAvatar(int iconid) const
{
    QImage avatar;
    avatar.load(QString(":/tx/%1.png").arg(qBound(0, iconid, 35)));
    return avatar;
}

void CKernal::applyOwnAvatar(const QImage &avatar)
{
    if (avatar.isNull()) {
        return;
    }

    m_userAvatar = avatar;
    m_demoDialog->slot_setAvatarImage(avatar);
    m_setUserDialog->slot_setAvatarImage(avatar);
    updateOwnRoomAvatar(avatar);
}

void CKernal::updateOwnRoomAvatar(const QImage &avatar)
{
    // The lobby icon is outside the room participant widgets, so update it
    // here too when the profile/avatar fetch completes.
    m_demoDialog->slot_setAvatarImage(avatar);
    const auto user = m_mapIDToUserShow.find(m_userid);
    if (user != m_mapIDToUserShow.end()) {
        user->second->slot_setAvatarImage(avatar);
    }
    m_roomDialog->slot_setBigAvatar(m_userid, avatar);
}

void CKernal::clearUserVideo(int userId)
{
    const auto user = m_mapIDToUserShow.find(userId);
    if (user != m_mapIDToUserShow.end()) {
        user->second->slot_clearVideo();
    }
    m_roomDialog->slot_clearBigImage(userId);
}

void CKernal::sendVideoStopFrame()
{
    if (m_useWebRtc) { m_rtc->stopVideo(); return; }
    if (m_roomid != 0) {
        slot_sendVideoFrameData(QByteArray(), m_videoStreamGeneration);
    }
}

void CKernal::stopLocalVideo()
{
    if (m_activeVideoSource == VideoSource::Desktop) {
        DiagnosticLogger::instance().stopDesktopShare();
    }
    ++m_videoStreamGeneration;
    {
        std::lock_guard<std::mutex> locker(m_pendingDesktopFrameMutex);
        m_pendingDesktopFrame = QImage();
    }
    m_sendVideoThread->setActiveGeneration(m_videoStreamGeneration);
    Q_EMIT SIG_resetVideoEncoder();
    clearUserVideo(m_userid);
    sendVideoStopFrame();
    m_activeVideoSource = VideoSource::None;
    m_desktopStatsTimer->stop();
}

#include "QTime"
void CKernal::slot_sendAudioFrame(QByteArray& ba)    // ba.data ba.size()
{
    // 确认协议

    // 协议头
    // 发送者id：服务器知道谁发的，不给他转发
    // 房间号：找到转发给谁
    // 时间 小时 分钟 秒 毫秒 send延迟->考虑丢弃帧 需要记录时间
    // 音频长度
    // 音频数据

    ///音频数据帧
    /// 成员描述
    /// int type;
    /// int userId;
    /// int roomId;
    /// int hour;
    /// int min;
    /// int sec;
    /// int msec;
    /// QByteArray audioFrame;  --> char frame[]; 柔性数组
    ///
    int type = DEF_PACK_AUDIO_FRAME;
    int userid = m_userid;
    int roomid = m_roomid;
    QTime tm = QTime::currentTime();
    int hour = tm.hour();
    int min = tm.minute();
    int sec = tm.second();
    int msec = tm.msec();
    char* audioData = ba.data();
    int len = ba.size();
    // 序列化数据
    char* buf = new char[sizeof(int)*7 + len];
    char* tmp = buf;
    *(int*)tmp  = type;
    tmp += sizeof(int);
    *(int*)tmp  = userid;
    tmp += sizeof(int);
    *(int*)tmp  = roomid;
    tmp += sizeof(int);
    *(int*)tmp  = hour;
    tmp += sizeof(int);
    *(int*)tmp  = min;
    tmp += sizeof(int);
    *(int*)tmp  = sec;
    tmp += sizeof(int);
    *(int*)tmp  = msec;
    tmp += sizeof(int);
    memcpy(tmp, audioData, len);
    tmp += len;

    //slot_clientSendData(0, buf, sizeof(int)*7 + len);
    slot_clientAudioSendData(0, buf, sizeof(int)*7 + len);
    delete[] buf;

}

// 刷新画面 主要使用在 采集
void CKernal::slot_refleshVideoImage(QImage img)
{
    slot_refleshUserImage(m_userid, img);
}

void CKernal::slot_refleshCameraVideoImage(QImage img)
{
    if (m_activeVideoSource == VideoSource::Camera) {
        slot_refleshVideoImage(img);
    }
}

void CKernal::slot_refleshDesktopVideoImage(QImage img)
{
    if (m_activeVideoSource == VideoSource::Desktop) {
        slot_refleshVideoImage(img);
    }
}

// 用来显示用户
void CKernal::slot_refleshUserImage(int id ,QImage &img)
{
    if (!m_roomDialog || img.isNull()) {
        return;
    }
    // 预览图 大图 显示
    m_roomDialog->slot_setBigImage(id, img);
    // 列表显示
    if(m_mapIDToUserShow.count(id) > 0){
        // The stage remains real-time, while thumbnail repaints are bounded
        // to 15 FPS so six scaled QWidget images cannot monopolize the UI.
        const qint64 now = m_videoUiClock.elapsed();
        qint64 &lastPaint = m_thumbnailLastPaintMs[id];
        if (lastPaint == 0 || now - lastPaint >= 66) {
            lastPaint = now;
            UserShow* user = m_mapIDToUserShow[id];
            user->slot_setImage(img);
        }
    }

}

void CKernal::slot_setFunnyPic(int picid)
{
    m_videoRead->setFunnyPic(picid);
}

void CKernal::slot_sendVideoFrameData(const QByteArray &ba, quint64 generation,
                                      bool desktopContent)
{
    ///视频数据帧
    /// 成员描述
    /// int type;
    /// int userId;
    /// int roomId;
    /// int hour;
    /// int min;
    /// int sec;
    /// int msec;
    /// QByteArray videoFrame;
    ///
    int type = DEF_PACK_VIDEO_FRAME;
    int userid = m_userid;
    int roomid = m_roomid;
    QTime tm = QTime::currentTime();
    int hour = tm.hour();
    int min = tm.minute();
    int sec = tm.second();
    int msec = tm.msec();
    int len = ba.size();
    // 序列化数据
    char* buf = new char[sizeof(int)*7 + len];
    char* tmp = buf;
    *(int*)tmp  = type;
    tmp += sizeof(int);
    *(int*)tmp  = userid;
    tmp += sizeof(int);
    *(int*)tmp  = roomid;
    tmp += sizeof(int);
    *(int*)tmp  = hour;
    tmp += sizeof(int);
    *(int*)tmp  = min;
    tmp += sizeof(int);
    *(int*)tmp  = sec;
    tmp += sizeof(int);
    *(int*)tmp  = msec;
    tmp += sizeof(int);
    if (len > 0) {
        memcpy(tmp, ba.constData(), len);
    }
    tmp += len;

//    slot_clientSendData(0, buf, sizeof(int)*7 + len);
//    delete buf;
    // 替换为发送信号
    Q_EMIT SIG_sendVideoFrame(0, buf, sizeof(int)*7 + len, generation, desktopContent);
}

void CKernal::queueVideoFrame(const QImage &image, int frameRate, int bitRate,
                              bool desktopContent)
{
    if (m_roomid == 0 || image.isNull() || (m_useWebRtc && !m_rtc->canAcceptVideo())) {
        return;
    }

    bool expected = false;
    if (!m_videoEncodeBusy.compare_exchange_strong(expected, true)) {
        if (desktopContent) {
            {
                std::lock_guard<std::mutex> locker(m_pendingDesktopFrameMutex);
                m_pendingDesktopFrame = image;
            }
            m_desktopEncoderDroppedFrames.fetch_add(1);
        }
        return;
    }
    // LiveKit feedback is useful for camera video, but applying every short
    // desktop feedback update here recreates NVENC and the pre-encoded track.
    // That caused visible cursor stalls and repeatedly lowered text quality.
    // Keep the selected desktop quality stable for the lifetime of the track.
    if (m_useWebRtc && m_webTargetBitrate > 0 && !desktopContent) {
        bitRate = qMin(bitRate, m_webTargetBitrate);
    }
    // QImage is implicitly shared. Passing it by value keeps the worker frame
    // alive without a full-resolution deep copy on the GUI thread.
    Q_EMIT SIG_encodeVideoFrame(image, frameRate, bitRate, desktopContent,
                                 m_videoStreamGeneration);
}

void CKernal::slot_sendCameraVideoFrame(QImage img)
{
    if (m_activeVideoSource == VideoSource::Camera) {
        queueVideoFrame(img, CAMERA_FRAME_RATE, CAMERA_VIDEO_BITRATE, false);
    }
}

void CKernal::slot_sendDesktopVideoFrame(QImage img)
{
    if (m_activeVideoSource == VideoSource::Desktop) {
        m_desktopCapturedFrames.fetch_add(1);
        queueVideoFrame(img, m_desktopFrameRate, m_desktopBitRate, true);
    }
}

void CKernal::slot_sendH264Frame(QByteArray encoded, int flags, int width, int height,
                                 quint64 generation, QString encoderName)
{
    const bool desktopContent = generation == m_videoStreamGeneration
            && m_activeVideoSource == VideoSource::Desktop;
    if (desktopContent
            && width >= 2 && height >= 2) {
        if (m_desktopEncoderName != encoderName) {
            m_desktopEncoderName = encoderName;
            m_roomDialog->slot_setDesktopShareStatus(width, height, m_desktopFrameRate,
                                                      m_desktopBitRate, encoderName);
        }
    }
    if (generation != m_videoStreamGeneration || m_roomid == 0 || encoded.isEmpty() ||
            width < 2 || height < 2) {
        return;
    }
    const int maxEncodedSize = _DEF_MAX_PACKET_SIZE - 7 * static_cast<int>(sizeof(int))
            - static_cast<int>(sizeof(H264PayloadHeader));
    if (encoded.size() > maxEncodedSize) {
        qWarning() << "Dropping oversized H.264 video frame:" << encoded.size();
        return;
    }

    if (m_useWebRtc) {
        if (desktopContent) m_desktopEncodedFrames.fetch_add(1);
        m_rtc->publishH264(encoded, flags & H264_PAYLOAD_FLAG_KEYFRAME, width, height,
                          desktopContent ? m_desktopFrameRate : CAMERA_FRAME_RATE,
                          desktopContent ? m_desktopBitRate : CAMERA_VIDEO_BITRATE, desktopContent);
        return;
    }
    H264PayloadHeader header = {};
    header.magic = H264_PAYLOAD_MAGIC;
    header.version = H264_PAYLOAD_VERSION;
    header.flags = static_cast<quint16>(flags);
    header.width = static_cast<quint16>(width);
    header.height = static_cast<quint16>(height);
    header.dataSize = static_cast<quint32>(encoded.size());

    QByteArray payload(reinterpret_cast<const char *>(&header), sizeof(header));
    payload.append(encoded);
    if (desktopContent) {
        m_desktopEncodedFrames.fetch_add(1);
    }
    slot_sendVideoFrameData(payload, generation, desktopContent);
}

void CKernal::slot_videoEncodeFinished()
{
    m_videoEncodeBusy.store(false);

    QImage pending;
    {
        std::lock_guard<std::mutex> locker(m_pendingDesktopFrameMutex);
        pending = std::move(m_pendingDesktopFrame);
    }
    if (!pending.isNull() && m_activeVideoSource == VideoSource::Desktop &&
            m_roomid != 0) {
        queueVideoFrame(pending, m_desktopFrameRate, m_desktopBitRate, true);
    }
}

void CKernal::resetDesktopShareStats()
{
    m_desktopCapturedFrames.store(0);
    m_desktopEncodedFrames.store(0);
    m_desktopEncoderDroppedFrames.store(0);
    m_desktopEncoderName.clear();
    m_desktopNoCaptureIntervals = 0;
    m_desktopNoEncodeIntervals = 0;
    m_desktopNoSendIntervals = 0;
    quint64 sent = 0;
    quint64 replaced = 0;
    quint64 late = 0;
    quint64 failed = 0;
    m_sendVideoThread->takeDesktopStats(&sent, &replaced, &late, &failed);
    if (m_useWebRtc) sent = m_webSentFrames.exchange(0);
}

void CKernal::slot_updateDesktopShareStats()
{
    if (m_activeVideoSource != VideoSource::Desktop || !m_roomDialog) {
        return;
    }

    quint64 sent = 0;
    quint64 replaced = 0;
    quint64 late = 0;
    quint64 failed = 0;
    m_sendVideoThread->takeDesktopStats(&sent, &replaced, &late, &failed);

    const quint64 captured = m_desktopCapturedFrames.exchange(0);
    const quint64 encoded = m_desktopEncodedFrames.exchange(0);
    const quint64 encoderDropped = m_desktopEncoderDroppedFrames.exchange(0);
    const quint64 dropped = encoderDropped + replaced + late + failed;
    m_roomDialog->slot_setDesktopShareRuntimeStats(
                m_desktopWidth, m_desktopHeight, m_desktopFrameRate,
                m_desktopBitRate,
                m_desktopEncoderName.isEmpty() ? QStringLiteral("等待编码器")
                                                : m_desktopEncoderName,
                static_cast<int>(captured), static_cast<int>(encoded),
                static_cast<int>(sent), static_cast<int>(dropped));

    if (captured == 0) {
        ++m_desktopNoCaptureIntervals;
    } else {
        m_desktopNoCaptureIntervals = 0;
    }
    if (captured > 0 && encoded == 0) {
        ++m_desktopNoEncodeIntervals;
    } else {
        m_desktopNoEncodeIntervals = 0;
    }
    if (encoded > 0 && sent == 0) {
        ++m_desktopNoSendIntervals;
    } else {
        m_desktopNoSendIntervals = 0;
    }

    if (m_desktopNoCaptureIntervals >= kDesktopStallIntervalsBeforeRecovery) {
        m_desktopNoCaptureIntervals = 0;
        m_roomDialog->slot_setMediaStatus("桌面采集停滞，正在自动恢复…");
        m_deskRead->pause();
        m_deskRead->start();
        Q_EMIT SIG_resetVideoEncoder();
    } else if (m_desktopNoEncodeIntervals >= kDesktopStallIntervalsBeforeRecovery) {
        m_desktopNoEncodeIntervals = 0;
        m_roomDialog->slot_setMediaStatus("桌面编码停滞，正在自动恢复…");
        Q_EMIT SIG_resetVideoEncoder();
    } else if (m_desktopNoSendIntervals >= kDesktopStallIntervalsBeforeRecovery) {
        m_desktopNoSendIntervals = 0;
        m_roomDialog->slot_setMediaStatus("视频通道停滞，正在重新连接…");
        if (m_useWebRtc) { Q_EMIT SIG_resetVideoEncoder(); return; }
        m_avChannelRegistered[1].store(false);
        if (m_tcpAV[1]) {
            m_tcpAV[1]->CloseNet();
        }
        if (!ensureAvChannelReady(1, true)) {
            scheduleAvChannelRecovery(1);
        }
    }
}

void CKernal::slot_dealAudioFrame(uint socket, char *buf, int nlen)
{
    if (nlen < 7 * static_cast<int>(sizeof(int))) return;
    // 反序列化过程
    int type;
    int userid;
    int roomid;
    int hour;
    int min;
    int sec;
    int msec;
    // 反序列化数据
    char* tmp = buf;
    type = *(int*)tmp;
    tmp += sizeof(int);
    userid = *(int*)tmp;
    tmp += sizeof(int);
    roomid = *(int*)tmp;
    tmp += sizeof(int);

    if (m_roomid == 0 || roomid != m_roomid) {
        return;
    }
    if (userid == m_userid) {
        return;
    }

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    int audioLen = nlen - 7*sizeof(int);
    // 音频数据
    QByteArray ba(tmp, audioLen);
    ensureRemoteParticipant(userid);
    if(m_mapIDToAudioWrite.count(userid) > 0){
        Audio_Write* aw = m_mapIDToAudioWrite[userid];
        aw->slot_net_rx(ba);
    }
}

void CKernal::slot_dealVideoFrame(uint socket, char *buf, int nlen)
{
	Q_UNUSED(socket);
    if (nlen < 7 * static_cast<int>(sizeof(int))) return;
    // 反序列化过程
    int type;
    int userid;
    int roomid;
    int hour;
    int min;
    int sec;
    int msec;
    // 反序列化数据
    char* tmp = buf;
    type = *(int*)tmp;
    tmp += sizeof(int);
	Q_UNUSED(type);
	Q_UNUSED(hour);
	Q_UNUSED(min);
	Q_UNUSED(sec);
	Q_UNUSED(msec);
    userid = *(int*)tmp;
    tmp += sizeof(int);
    roomid = *(int*)tmp;
    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    const int imageLen = nlen - 7*sizeof(int);
    const quint64 remoteGeneration = m_remoteVideoGeneration.load();
    if (m_roomid == 0 || roomid != m_roomid) {
        return;
    }
    if (userid != m_userid) {
        ensureRemoteParticipant(userid);
    }
    if (imageLen == 0) {
        m_mapIDToVideoDecoder.erase(userid);
        clearUserVideo(userid);
		if (m_videoDecodeWorker) {
			QMetaObject::invokeMethod(m_videoDecodeWorker, "slot_resetUser",
			                          Qt::QueuedConnection, Q_ARG(int, userid));
		}
        return;
    }
    if (imageLen >= static_cast<int>(sizeof(H264PayloadHeader))) {
        H264PayloadHeader header = {};
        memcpy(&header, tmp, sizeof(header));
        const bool isH264 = header.magic == H264_PAYLOAD_MAGIC
                && header.version == H264_PAYLOAD_VERSION;
        if (isH264) {
            const int encodedLength = imageLen - static_cast<int>(sizeof(header));
            if (header.dataSize != static_cast<quint32>(encodedLength)
                    || header.width < 2 || header.height < 2) {
                qWarning() << "Discarding malformed H.264 video frame.";
                return;
            }
            const QByteArray encoded(tmp + sizeof(header), encodedLength);
			if (m_videoDecodeWorker) {
				QMetaObject::invokeMethod(m_videoDecodeWorker, "slot_decodeH264",
				                          Qt::QueuedConnection,
				                          Q_ARG(int, userid), Q_ARG(int, roomid),
				                          Q_ARG(quint64, remoteGeneration),
				                          Q_ARG(QByteArray, encoded),
				                          Q_ARG(int, static_cast<int>(header.flags)));
			}
            return;
        }
    }

    // Clients before the H.264 upgrade send JPEG bytes directly after the frame header.
	if (m_videoDecodeWorker) {
		const QByteArray encoded(tmp, imageLen);
		QMetaObject::invokeMethod(m_videoDecodeWorker, "slot_decodeImage",
		                          Qt::QueuedConnection,
		                          Q_ARG(int, userid), Q_ARG(int, roomid),
		                          Q_ARG(quint64, remoteGeneration),
		                          Q_ARG(QByteArray, encoded));
	}
}

void CKernal::slot_videoFrameDecoded(int userId, int roomId, quint64 generation,
                                     QImage image, bool decoded)
{
	if (m_roomid == 0 || roomId != m_roomid ||
	        generation != m_remoteVideoGeneration.load()) {
		return;
	}
	if (!decoded || image.isNull()) {
		if (m_roomDialog) {
			m_roomDialog->slot_setMediaStatus("等待可解码画面…");
		}
		return;
	}
	slot_refleshUserImage(userId, image);
	if (m_roomDialog) {
		m_roomDialog->slot_setMediaStatus("正在接收画面");
	}
}

bool CKernal::hasAuthenticatedControlConnection() const
{
    if (m_useWebRtc) return m_gateway && m_gateway->authenticated() && m_loggedIn;
    return m_controlSocket &&
            m_controlSocket->state() == QAbstractSocket::ConnectedState &&
            m_loggedIn && m_userid > 0;
}

bool CKernal::writeControlFrame(const char *data, int length)
{
    if (!m_controlSocket || !data || length < static_cast<int>(sizeof(PackType)) ||
            length > _DEF_MAX_PACKET_SIZE ||
            m_controlSocket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    QByteArray frame(static_cast<int>(sizeof(int)) + length, '\0');
    memcpy(frame.data(), &length, sizeof(length));
    memcpy(frame.data() + sizeof(int), data, length);
    return m_controlSocket->write(frame) == frame.size();
}

bool CKernal::sendControlPacket(const char *data, int length, bool requiresLogin)
{
    if (!data || length < static_cast<int>(sizeof(PackType)) ||
            length > _DEF_MAX_PACKET_SIZE) {
        return false;
    }

    if (requiresLogin && (!m_controlSocket ||
            m_controlSocket->state() != QAbstractSocket::ConnectedState ||
            !m_loggedIn)) {
        beginControlRecovery();
        return false;
    }
    if (!m_controlSocket ||
            m_controlSocket->state() != QAbstractSocket::ConnectedState) {
        ensureControlConnection();
        return false;
    }

    if (!writeControlFrame(data, length)) {
        m_controlSocket->abort();
        return false;
    }
    return true;
}

void CKernal::ensureControlConnection()
{
    if (m_useWebRtc) return;
    if (!m_controlSocket || m_shuttingDown ||
            m_controlSocket->state() == QAbstractSocket::ConnectedState ||
            m_controlSocket->state() == QAbstractSocket::ConnectingState ||
            m_controlSocket->state() == QAbstractSocket::HostLookupState) {
        return;
    }
    m_controlSocket->connectToHost(m_serverIP, m_serverPort);
}

void CKernal::ensureAvConnections(bool forceRegistration)
{
    if (m_useWebRtc) return;
    for (int i = 0; i < 2; ++i) {
        if (!ensureAvChannelReady(i, forceRegistration)) {
            scheduleAvChannelRecovery(i);
        }
    }
}

void CKernal::createAvChannel(int channel)
{
    if (channel < 0 || channel > 1 || m_tcpAV[channel]) {
        return;
    }

    // TcpClient receives on a native worker thread. Keep all state changes
    // and UI work on CKernal's thread, and bind callbacks to this exact
    // connection object. A callback from a replaced socket must never revoke
    // the authenticated state of its replacement.
    TcpClientMediator *connection = new TcpClientMediator;
    m_tcpAV[channel] = connection;
    connect(connection, &INetMediator::SIG_ReadyData, this,
            [this, channel, connection](uint socket, char *buffer, int length) {
        if (m_tcpAV[channel] != connection) {
            delete[] buffer;
            return;
        }
        if (channel == 0) {
            slot_dealAudioChannelData(socket, buffer, length);
        } else {
            slot_dealVideoChannelData(socket, buffer, length);
        }
    }, Qt::QueuedConnection);
    connect(connection, &INetMediator::SIG_disConnect, this,
            [this, channel, connection]() {
        if (m_tcpAV[channel] != connection || m_shuttingDown) {
            return;
        }
        m_avChannelRegistered[channel].store(false);
        m_avRegistrationPending[channel] = false;
        if (m_avRegistrationFallbackTimers[channel]) {
            m_avRegistrationFallbackTimers[channel]->stop();
        }
        scheduleAvChannelRecovery(channel);
    }, Qt::QueuedConnection);
}

void CKernal::discardAvChannel(int channel)
{
    if (channel < 0 || channel > 1) {
        return;
    }

    m_avChannelRegistered[channel].store(false);
    m_avRegistrationPending[channel] = false;
    if (m_avReconnectTimers[channel]) {
        m_avReconnectTimers[channel]->stop();
    }
    if (m_avRegistrationFallbackTimers[channel]) {
        m_avRegistrationFallbackTimers[channel]->stop();
    }

    // Clear the active pointer before closing the socket. Any queued packet
    // or disconnect notification from this retired connection is then
    // recognized as stale by the callbacks installed in createAvChannel().
    TcpClientMediator *connection = m_tcpAV[channel];
    m_tcpAV[channel] = nullptr;
    if (connection) {
        disconnect(connection, nullptr, this, nullptr);
        connection->CloseNet();
        delete connection;
    }
}

bool CKernal::ensureAvChannelReady(int channel, bool forceRegistration)
{
    if (m_useWebRtc) return m_rtc && m_rtc->state() == RtcSession::State::Connected;
    if (channel < 0 || channel > 1) return false;
    // Connection lifecycle and Qt timers belong to the GUI thread. Video sends
    // may originate on SendThread, which only reads the atomic ready flag.
    if (QThread::currentThread() != thread()) {
        if (m_avChannelRegistered[channel].load()) return true;
        scheduleAvChannelRecovery(channel);
        return false;
    }
    if (m_shuttingDown || channel < 0 || channel > 1 || !m_loggedIn || m_userid <= 0 ||
            !hasValidSessionToken(m_sessionToken)) {
        return false;
    }

    // Reusing a TCP connection whose server-side association has expired lets
    // capture and encoding continue while every media write fails. A new
    // share (or room entry) must therefore get a clean, authenticated socket.
    if (forceRegistration && m_tcpAV[channel]) {
        discardAvChannel(channel);
    }

    bool createdConnection = false;
    if (!m_tcpAV[channel]) {
        createAvChannel(channel);
        createdConnection = true;
    }
    if (!m_tcpAV[channel]->IsConnected()) {
        // A failed/disconnected mediator may still have a native receive
        // callback in flight. Replace it instead of reconnecting through the
        // same object so that callback cannot invalidate the new registration.
        if (!createdConnection) {
            discardAvChannel(channel);
            createAvChannel(channel);
        }
        m_avChannelRegistered[channel].store(false);
        m_avRegistrationPending[channel] = false;
        if (m_avRegistrationFallbackTimers[channel]) {
            m_avRegistrationFallbackTimers[channel]->stop();
        }
        m_tcpAV[channel]->CloseNet();
        if (!m_tcpAV[channel]->OpenNet(m_serverIP.toStdString().c_str(), m_serverPort)) {
            qWarning() << "Unable to reconnect" << (channel == 0 ? "audio" : "video")
                       << "channel.";
            return false;
        }
    }

    if (m_avChannelRegistered[channel].load() && !forceRegistration) {
        return true;
    }

    if (m_avRegistrationPending[channel]) {
        return false;
    }

    m_avChannelRegistered[channel].store(false);
    // A new server confirms the media association with 10021/10022. Do not
    // send H.264 before that confirmation: the first IDR could otherwise be
    // lost, leaving a receiver waiting forever for a decodable frame.
    if (channel == 1) m_roomDialog->slot_setMediaStatus("视频通道认证中…");
    bool registered = false;
    if (channel == 0) {
        STRU_AUDIO_REGISTER request;
        request.m_userid = m_userid;
        memcpy(request.m_sessionToken, m_sessionToken, sizeof(request.m_sessionToken));
        registered = m_tcpAV[channel]->SendData(0, reinterpret_cast<char*>(&request),
                                                sizeof(request));
    } else {
        STRU_VIDEO_REGISTER request;
        request.m_userid = m_userid;
        memcpy(request.m_sessionToken, m_sessionToken, sizeof(request.m_sessionToken));
        registered = m_tcpAV[channel]->SendData(0, reinterpret_cast<char*>(&request),
                                                sizeof(request));
    }

    if (!registered) {
        m_avChannelRegistered[channel].store(false);
        qWarning() << "Unable to register" << (channel == 0 ? "audio" : "video")
                   << "channel.";
        scheduleAvChannelRecovery(channel);
        return false;
    }

    m_avRegistrationPending[channel] = true;
    // Older deployments did not acknowledge registration. Keep a bounded
    // fallback for them, but give modern servers time to authenticate first.
    if (m_avRegistrationFallbackTimers[channel]) {
        m_avRegistrationFallbackTimers[channel]->start();
    }
    return false;
}

void CKernal::scheduleAvChannelRecovery(int channel)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, channel]() {
            scheduleAvChannelRecovery(channel);
        }, Qt::QueuedConnection);
        return;
    }
    if (channel < 0 || channel > 1 || m_shuttingDown || !m_loggedIn) {
        return;
    }
    QTimer *timer = m_avReconnectTimers[channel];
    if (timer && !timer->isActive()) {
        timer->start();
    }
}

void CKernal::retryAvChannel(int channel)
{
    if (channel < 0 || channel > 1 || m_shuttingDown || !m_loggedIn) {
        return;
    }
    if (!ensureAvChannelReady(channel)) {
        scheduleAvChannelRecovery(channel);
    }
}

void CKernal::allowLegacyAvRegistration(int channel)
{
    if (channel < 0 || channel > 1 || m_shuttingDown || !m_loggedIn ||
            !m_avRegistrationPending[channel] || !m_tcpAV[channel] ||
            !m_tcpAV[channel]->IsConnected()) {
        return;
    }
    m_avRegistrationPending[channel] = false;
    m_avChannelRegistered[channel].store(true);
    m_avReconnectTimers[channel]->stop();
    if (channel == 1) {
        m_mapIDToVideoDecoder.clear();
        Q_EMIT SIG_resetVideoEncoder();
        m_roomDialog->slot_setMediaStatus("视频已连接（兼容模式）");
    }
}

void CKernal::sendPendingLogin()
{
    if (!m_loginPending || m_loginRequest.isEmpty()) {
        return;
    }
    if (!writeControlFrame(m_loginRequest.constData(), m_loginRequest.size())) {
        m_controlSocket->abort();
        return;
    }
    m_loginTimeoutTimer->start();
}

void CKernal::sendPendingRegistration()
{
    if (m_pendingRegisterRequest.isEmpty()) {
        return;
    }
    if (writeControlFrame(m_pendingRegisterRequest.constData(),
                          m_pendingRegisterRequest.size())) {
        m_pendingRegisterRequest.clear();
    }
}

void CKernal::resetAuthenticationState()
{
    m_loggedIn = false;
    m_userid = 0;
    memset(m_sessionToken, 0, sizeof(m_sessionToken));
    clearPendingProfileSave();
    m_pendingAvatarUploadData.clear();
    m_avatarUploadPending = false;
    for (int channel = 0; channel < 2; ++channel) {
        discardAvChannel(channel);
    }
}

void CKernal::scheduleControlReconnect()
{
    if (!m_shuttingDown && m_loginPending && !m_loginRequest.isEmpty() &&
            !m_controlReconnectTimer->isActive()) {
        m_controlReconnectTimer->start();
    }
}

void CKernal::beginControlRecovery()
{
    if (m_shuttingDown) {
        return;
    }

    const bool shouldRelogin = (m_loggedIn || m_loginPending) &&
            !m_loginRequest.isEmpty();
    m_controlReceiveBuffer.clear();
    resetAuthenticationState();
    if (m_roomRequest != RoomRequest::None) {
        clearRoomRequest();
        m_demoDialog->slot_setPushButton_enable(true);
    }
    if (shouldRelogin) {
        if (!m_loginDeadline->isActive()) m_loginDeadline->start();
        m_loginPending = true;
        if (m_loginDialog) {
            m_loginDialog->slot_setLoginPending(true);
        }
        if (m_controlSocket->state() == QAbstractSocket::ConnectedState) {
            m_controlSocket->abort();
        } else {
            scheduleControlReconnect();
        }
    }
}

void CKernal::failLogin()
{
    if (!m_loginPending) return;
    if (m_useWebRtc && m_gateway) m_gateway->logout();
    m_loginPending = false;
    m_loginRequest.clear();
    m_loginTimeoutTimer->stop();
    m_controlReconnectTimer->stop();
    resetAuthenticationState();
    m_controlSocket->abort();
    slot_quitRoom();
    m_demoDialog->hide();
    m_loginDialog->slot_setLoginPending(false);
    m_loginDialog->showNormal();
    QMessageBox::warning(m_loginDialog, "连接失败", "登录等待已超过20秒，请检查网络或服务器后重新登录。");
}

void CKernal::handleMediaRegistration(int channel, const char* buffer, int length)
{
    if (channel < 0 || channel > 1 || !m_loggedIn ||
            length != static_cast<int>(sizeof(STRU_MEDIA_REGISTER_RS))) return;
    STRU_MEDIA_REGISTER_RS response = {};
    memcpy(&response, buffer, sizeof(response));
    if (response.userId != m_userid ||
            memcmp(response.sessionToken, m_sessionToken, SESSION_TOKEN_SIZE) != 0) return;
    m_avRegistrationPending[channel] = false;
    if (m_avRegistrationFallbackTimers[channel]) {
        m_avRegistrationFallbackTimers[channel]->stop();
    }
    m_avChannelRegistered[channel].store(response.result == 0);
    if (response.result != 0) {
        if (m_tcpAV[channel]) {
            m_tcpAV[channel]->CloseNet();
        }
        if (channel == 1) m_roomDialog->slot_setMediaStatus("视频认证失败，正在重试…");
        scheduleAvChannelRecovery(channel);
        return;
    }
    m_avReconnectTimers[channel]->stop();
    if (channel == 1) {
        m_mapIDToVideoDecoder.clear();
        // Encode an IDR only after the server has accepted the new socket. It
        // guarantees that receivers joining this share can start decoding.
        Q_EMIT SIG_resetVideoEncoder();
        m_roomDialog->slot_setMediaStatus("视频已连接");
    }
}

void CKernal::handleVideoKeyFrameRequest(const char* buffer, int length)
{
    if (!buffer || length != static_cast<int>(sizeof(STRU_VIDEO_KEYFRAME_RQ)) ||
            m_roomid == 0 || m_activeVideoSource == VideoSource::None ||
            !m_avChannelRegistered[1].load()) {
        return;
    }

    STRU_VIDEO_KEYFRAME_RQ request = {};
    memcpy(&request, buffer, sizeof(request));
    if (request.type != DEF_PACK_VIDEO_KEYFRAME_RQ || request.roomId != m_roomid ||
            request.requesterUserId <= 0 || request.requesterUserId == m_userid) {
        return;
    }

    // A peer may have joined after this stream's first IDR was relayed. Reset
    // the encoder so its next captured frame is a self-contained starting
    // point rather than leaving the peer to decode P-frames indefinitely.
    Q_EMIT SIG_resetVideoEncoder();
    if (m_roomDialog) {
        m_roomDialog->slot_setMediaStatus("正在为新观看端同步画面…");
    }
}

void CKernal::clearPendingProfileSave()
{
    m_profileSavePending = false;
    m_pendingProfileAvatarData.clear();
    m_pendingProfileAvatarChanged = false;
}

bool CKernal::queueUserInfoSave(int iconid, const QString& name, const QString& feeling,
                                 const QByteArray& avatarData, bool avatarChanged)
{
    if (m_useWebRtc) { if (!m_gateway->authenticated()) return false; m_webAvatarChanged = avatarChanged; m_webAvatar = avatarData; m_gateway->updateProfile(name, iconid, feeling); return true; }
    if (!hasAuthenticatedControlConnection()) {
        beginControlRecovery();
        return false;
    }

    STRU_USER_INFO_RQ rq;
    rq.m_UserID = m_userid;
    rq.m_iconid = qBound(0, iconid, 35);
    const QByteArray nameUtf8 = name.toUtf8();
    const QByteArray feelingUtf8 = feeling.toUtf8();
    strncpy(rq.m_szUser, nameUtf8.constData(), sizeof(rq.m_szUser) - 1);
    strncpy(rq.m_userFeeling, feelingUtf8.constData(), sizeof(rq.m_userFeeling) - 1);

    if (!sendControlPacket(reinterpret_cast<char*>(&rq), sizeof(rq))) {
        return false;
    }

    m_profileSavePending = true;
    m_pendingProfileAvatarData = avatarData;
    m_pendingProfileAvatarChanged = avatarChanged;
    return true;
}

bool CKernal::slot_clientSendData(uint socket, char *buf, int nlen)
{
    Q_UNUSED(socket);
    return sendControlPacket(buf, nlen);
}

void CKernal::slot_clientAudioSendData(uint socket, char *buf, int nlen)
{
    if (!ensureAvChannelReady(0)) {
        return;
    }
    if (!m_tcpAV[0]->SendData(socket, buf, nlen)) {
        m_avChannelRegistered[0].store(false);
        scheduleAvChannelRecovery(0);
    }
}

bool CKernal::slot_clientVideoSendData(uint socket, char *buf, int nlen)
{
    if (!ensureAvChannelReady(1)) {
        return false;
    }
    if (!m_tcpAV[1]->SendData(socket, buf, nlen)) {
        m_avChannelRegistered[1].store(false);
        // SendThread must not manipulate the connection object directly.
        // Serialize its recovery on the GUI thread and replace the dead
        // socket, otherwise every later frame is counted as a failed send.
        QMetaObject::invokeMethod(this, [this]() {
            m_avRegistrationPending[1] = false;
            if (m_avRegistrationFallbackTimers[1]) {
                m_avRegistrationFallbackTimers[1]->stop();
            }
            if (m_tcpAV[1]) {
                m_tcpAV[1]->CloseNet();
            }
            if (m_roomDialog) {
                m_roomDialog->slot_setMediaStatus("视频连接已断开，正在重新认证…");
            }
            scheduleAvChannelRecovery(1);
        }, Qt::QueuedConnection);
        return false;
    }
    return true;
}

// 登录提交
void CKernal::slot_loginCommit(QString tel, QString passwd)
{
    if (m_useWebRtc) { webLogin(tel, passwd); return; }
    if (m_loginPending) {
        return;
    }

    // 提取手机号 密码
    m_loginTel = tel;
    const QByteArray telUtf8 = tel.toUtf8();
    const QByteArray passwordHash = QCryptographicHash::hash(
                passwd.toUtf8(), QCryptographicHash::Md5).toHex();
    QByteArray loginRequest(sizeof(STRU_LOGIN_RQ), '\0');
    const PackType type = DEF_PACK_LOGIN_RQ;
    memcpy(loginRequest.data() + offsetof(STRU_LOGIN_RQ, type), &type, sizeof(type));
    const int telLength = qMin(telUtf8.size(), _MAX_SIZE - 1);
    memcpy(loginRequest.data() + offsetof(STRU_LOGIN_RQ, tel),
           telUtf8.constData(), telLength);
    Q_ASSERT(passwordHash.size() == 32);
    memcpy(loginRequest.data() + offsetof(STRU_LOGIN_RQ, password),
           passwordHash.constData(), passwordHash.size());

    resetAuthenticationState();
    m_loginRequest = loginRequest;
    m_loginDeadline->start();
    m_loginPending = true;
    m_loginDialog->slot_setLoginPending(true);
    // Start the deadline before connecting as well as after writing. A proxy
    // or stalled host lookup otherwise never emits a login-response timeout.
    m_loginTimeoutTimer->start();
    if (m_controlSocket->state() == QAbstractSocket::ConnectedState) {
        sendPendingLogin();
    } else {
        ensureControlConnection();
    }
}

// 注册提交
void CKernal::slot_registerCommit(QString tel, QString passwd, QString nickname,
                                  int iconid, QByteArray avatarData, bool avatarChanged)
{
    if (m_useWebRtc) { webRegister(tel, passwd, nickname, iconid, avatarData, avatarChanged); return; }
    const QByteArray telUtf8 = tel.toUtf8();
    const QByteArray passwordHash = QCryptographicHash::hash(
                passwd.toUtf8(), QCryptographicHash::Md5).toHex();
    STRU_REGISTER_RQ rq;
    strncpy(rq.tel, telUtf8.constData(), sizeof(rq.tel) - 1);
    Q_ASSERT(passwordHash.size() == 32);
    memcpy(rq.password, passwordHash.constData(), passwordHash.size());

    m_registerDraftTel = tel;
    m_registerDraftPassword = passwd;
    m_registerDraftNickname = nickname;
    m_registerDraftIconId = qBound(0, iconid, 35);
    m_registerDraftAvatarData = avatarData;
    m_registerDraftAvatarChanged = avatarChanged;

    m_pendingRegisterRequest = QByteArray(reinterpret_cast<const char*>(&rq), sizeof(rq));
    if (m_controlSocket->state() == QAbstractSocket::ConnectedState) {
        sendPendingRegistration();
    } else {
        ensureControlConnection();
    }
}

// 加入房间
void CKernal::slot_joinRoom()
{
    if (m_useWebRtc) { webJoinRoom(); return; }
    if (m_roomid != 0) {
        QMessageBox::about(this->m_demoDialog, "提示", "已在房间中,加入失败");
        return;
    }
    if (m_roomRequest != RoomRequest::None) {
        return;
    }

    QInputDialog roomDialog(m_demoDialog);
    roomDialog.setWindowTitle("加入房间");
    roomDialog.setLabelText("房间号:");
    roomDialog.setInputMode(QInputDialog::IntInput);
    roomDialog.setIntRange(kMinimumRoomId, kMaximumRoomId);
    roomDialog.setIntStep(1);
    roomDialog.setIntValue(kMinimumRoomId);
    // The application is dark, while this compact numeric field intentionally
    // uses a white background.  Force a black value so every room digit stays
    // legible instead of inheriting the surrounding light text color.
    roomDialog.setStyleSheet(
            "QInputDialog QSpinBox, QInputDialog QSpinBox QLineEdit {"
            "color: #000000; background-color: #ffffff; selection-color: #ffffff;"
            "selection-background-color: #c92f3b; }");
    if (roomDialog.exec() != QDialog::Accepted) {
        return;
    }
    const int roomid = roomDialog.intValue();
    qDebug() << roomid;
    // 发送请求
    STRU_JOINROOM_RQ rq;
    rq.m_UserID = m_userid;
    rq.m_RoomID = roomid;

    beginRoomRequest(RoomRequest::Join, roomid);
    if (!slot_clientSendData(0, reinterpret_cast<char*>(&rq), sizeof(rq))) {
        clearRoomRequest();
        m_demoDialog->slot_setPushButton_enable(true);
        QMessageBox::warning(m_demoDialog, "提示", "无法连接服务器，请稍后重试");
    }
}

// 创建房间
void CKernal::slot_createRoom()
{
    if (m_useWebRtc) { if (m_roomid || m_roomRequest != RoomRequest::None) return; beginRoomRequest(RoomRequest::Create); m_gateway->createRoom(); return; }
    if (m_roomid != 0 || m_roomRequest != RoomRequest::None) {
        return;
    }
    // 发送请求
    STRU_CREATEROOM_RQ rq;
    rq.m_UserID = m_userid;

    beginRoomRequest(RoomRequest::Create);
    if (!slot_clientSendData(0, reinterpret_cast<char*>(&rq), sizeof(rq))) {
        clearRoomRequest();
        m_demoDialog->slot_setPushButton_enable(true);
        QMessageBox::warning(m_demoDialog, "提示", "无法连接服务器，请稍后重试");
    }
}


void SendThread::setActiveGeneration(quint64 generation)
{
    m_activeGeneration.store(generation);

    std::lock_guard<std::mutex> lock(m_pendingMutex);
    if (m_pendingFrame.buffer && m_pendingFrame.generation != generation) {
        if (m_pendingFrame.desktopContent) {
            m_desktopReplacedFrames.fetch_add(1);
        }
        delete[] m_pendingFrame.buffer;
        m_pendingFrame = PendingFrame();
    }
}

SendThread::~SendThread()
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    delete[] m_pendingFrame.buffer;
    m_pendingFrame = PendingFrame();
}

void SendThread::takeDesktopStats(quint64 *sentFrames, quint64 *replacedFrames,
                                  quint64 *lateFrames, quint64 *failedFrames)
{
    if (sentFrames) {
        *sentFrames = m_desktopSentFrames.exchange(0);
    }
    if (replacedFrames) {
        *replacedFrames = m_desktopReplacedFrames.exchange(0);
    }
    if (lateFrames) {
        *lateFrames = m_desktopLateFrames.exchange(0);
    }
    if (failedFrames) {
        *failedFrames = m_desktopFailedFrames.exchange(0);
    }
}

void SendThread::slot_queueLatestFrame(uint socket, char *buf, int nlen,
                                       quint64 generation, bool desktopContent)
{
    const int videoHeaderBytes = 7 * static_cast<int>(sizeof(int));
    if (!buf || nlen < videoHeaderBytes) {
        delete[] buf;
        return;
    }

    const bool stopFrame = nlen == videoHeaderBytes;
    bool keyFrame = false;
    if (!stopFrame && nlen >= videoHeaderBytes + static_cast<int>(sizeof(H264PayloadHeader))) {
        H264PayloadHeader header = {};
        memcpy(&header, buf + videoHeaderBytes, sizeof(header));
        keyFrame = header.magic == H264_PAYLOAD_MAGIC
                && header.version == H264_PAYLOAD_VERSION
                && (header.flags & H264_PAYLOAD_FLAG_KEYFRAME);
    }

    bool scheduleSend = false;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        // Delta frames can be replaced to keep latency low, but never replace
        // a waiting key frame.  Doing so leaves a newly joined viewer with an
        // endless sequence of P-frames and no point from which to decode.
        if (m_pendingFrame.buffer && m_pendingFrame.keyFrame && !keyFrame && !stopFrame) {
            if (desktopContent) {
                m_desktopReplacedFrames.fetch_add(1);
            }
            delete[] buf;
            return;
        }
        if (m_pendingFrame.buffer) {
            if (m_pendingFrame.desktopContent) {
                m_desktopReplacedFrames.fetch_add(1);
            }
            delete[] m_pendingFrame.buffer;
        }
        m_pendingFrame.socket = socket;
        m_pendingFrame.buffer = buf;
        m_pendingFrame.length = nlen;
        m_pendingFrame.generation = generation;
        m_pendingFrame.desktopContent = desktopContent;
        m_pendingFrame.keyFrame = keyFrame;
        m_pendingFrame.stopFrame = stopFrame;
        m_pendingFrame.queuedAt = std::chrono::steady_clock::now();
        if (!m_sendScheduled) {
            m_sendScheduled = true;
            scheduleSend = true;
        }
    }

    if (scheduleSend) {
        QMetaObject::invokeMethod(this, "slot_sendLatestFrame", Qt::QueuedConnection);
    }
}

void SendThread::slot_sendLatestFrame()
{
    PendingFrame frame;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (!m_pendingFrame.buffer) {
            m_sendScheduled = false;
            return;
        }
        frame = m_pendingFrame;
        m_pendingFrame = PendingFrame();
    }

    const bool isCurrentGeneration = frame.generation == m_activeGeneration.load();
    // An IDR frame (and a final stop frame) remains meaningful even after the
    // short low-latency queue window.  It must leave the client before later
    // delta frames; otherwise remote viewers can wait forever for a decodable
    // picture when a high-resolution share briefly blocks the TCP writer.
    const bool isFresh = frame.keyFrame || frame.stopFrame
            || std::chrono::steady_clock::now() - frame.queuedAt
            <= kMaximumQueuedVideoAge;
    if (!isCurrentGeneration || !isFresh) {
        if (frame.desktopContent) {
            m_desktopLateFrames.fetch_add(1);
        }
    } else if (CKernal::getInstance()->slot_clientVideoSendData(frame.socket,
                                                                  frame.buffer,
                                                                  frame.length)) {
        if (frame.desktopContent) {
            m_desktopSentFrames.fetch_add(1);
        }
    } else if (frame.desktopContent) {
        m_desktopFailedFrames.fetch_add(1);
    }
    delete[] frame.buffer;

    bool sendNext = false;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        if (m_pendingFrame.buffer) {
            sendNext = true;
        } else {
            m_sendScheduled = false;
        }
    }
    if (sendNext) {
        QMetaObject::invokeMethod(this, "slot_sendLatestFrame", Qt::QueuedConnection);
    }
}
