#include "ckernal.h"
#include <QDebug>
#include <QMessageBox>
#include "md5.h"

// 通过宏定义化简
#define NetMap(x) m_netMap[x - DEF_PACK_BASE]

// 协议映射关系
void CKernal::setNetMap()
{
    memset( m_netMap, 0, sizeof(PFUN));
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
    qDebug() << m_serverIP << m_serverPort;



}
#define MD5_KEY "1234"
/// MD5拼凑规则
/// passwd_1234

static std::string GetMD5(QString str)
{
    std::string temp = QString("%1_%2").arg(str).arg(MD5_KEY).toStdString();
    MD5 md5(temp);
    return md5.toString();
}


CKernal::CKernal(QObject *parent) : QObject(parent)
  ,m_userid(0), m_roomid(0), m_userIconId(0), m_sendVideoThread(new SendThread)
  ,m_videoEncodeThread(new VideoEncodeThread)
  ,m_videoEncodeBusy(false)
  ,m_videoStreamGeneration(0)
  ,m_activeVideoSource(VideoSource::None)
  ,m_latencySocket(new QTcpSocket(this))
    ,m_latencyTimer(new QTimer(this))
    ,m_latencyTimeoutTimer(new QTimer(this))
    ,m_loginTimeoutTimer(new QTimer(this))
    ,m_roomRequestTimer(new QTimer(this))
    ,m_latencyProbeActive(false)
    ,m_loginPending(false)
    ,m_registerDraftIconId(0)
    ,m_registerDraftAvatarChanged(false)
    ,m_pendingRegistrationIconId(0)
    ,m_pendingRegistrationAvatarChanged(false)
    ,m_roomRequest(RoomRequest::None)
    ,m_pendingRoomId(0)
{
    memset(m_sessionToken, 0, sizeof(m_sessionToken));
    // 设置协议映射关系
    setNetMap();
    initConfig();

    m_loginDialog = new LoginDialog;
    m_demoDialog  = new demoDialog;

    m_client      = new TcpClientMediator;
    connect(m_client, SIGNAL(SIG_ReadyData(uint,char*,int))
            , this, SLOT(slot_dealData(uint,char*,int)));
    connect(m_client, &INetMediator::SIG_disConnect,
            this, &CKernal::slot_mainDisconnected);


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

    m_client->OpenNet(m_serverIP.toStdString().c_str(), m_serverPort);

    // 构造里面av连接
    for(int i = 0 ; i < 2; ++i)
    {
        m_tcpAV[i]  = new TcpClientMediator;
        connect(m_tcpAV[i], SIGNAL(SIG_ReadyData(uint,char*,int))
                , this, SLOT(slot_dealData(uint,char*,int)));

        m_tcpAV[i]->OpenNet(m_serverIP.toStdString().c_str(), m_serverPort);
    }

    m_loginDialog->show();

    m_roomDialog = new RoomDialog;
    connect(m_roomDialog, SIGNAL(SIG_quitRoom())
            , this, SLOT(slot_quitRoom()));
    m_setUserDialog = new SetUserDialog;
    // todo
    connect(m_setUserDialog, &SetUserDialog::SIG_userSetCommit,
            this, &CKernal::slot_userSetCommit);



    // 测试 登录 验证与服务器的链接
    //    STRU_LOGIN_RQ rq;
    //    strcpy( rq.tel , "123");
    //    strcpy( rq.password, "123");

    //    m_client->SendData( 0 , (char*)&rq, sizeof(rq));

    m_audioRead = new Audio_Read;

    m_videoRead = new Video_Read;

    connect(m_videoRead, &Video_Read::SIG_captureStateChanged,
            this, &CKernal::slot_cameraStateChanged);
    connect(m_videoRead, &Video_Read::SIG_captureStateChanged,
            m_roomDialog, &RoomDialog::slot_setCaptureState);

    m_deskRead  = new Desk_Read;

    connect(m_audioRead, SIGNAL(SIG_audioFrame(QByteArray&)),
            this, SLOT(slot_sendAudioFrame(QByteArray&)));

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

    connect(m_roomDialog, SIGNAL(SIG_captureOpen()),
            this, SLOT(slot_captureOpen()));
    connect(m_roomDialog, SIGNAL(SIG_captureClose()),
            this, SLOT(slot_captureClose()));

    connect(m_roomDialog, SIGNAL(SIG_deskOpen()),
            this, SLOT(slot_deskOpen()));
    connect(m_roomDialog, SIGNAL(SIG_deskClose()),
            this, SLOT(slot_deskClose()));

    connect(m_roomDialog, SIGNAL(SIG_setFunnyPic(int)),
            this, SLOT(slot_setFunnyPic(int)));

    connect(this, &CKernal::SIG_sendVideoFrame,
            m_sendVideoThread.get(), &SendThread::slot_threadSendData);
    connect(this, &CKernal::SIG_encodeVideoFrame,
            m_videoEncodeThread.get(), &VideoEncodeThread::slot_encodeFrame);
    connect(this, &CKernal::SIG_resetVideoEncoder,
            m_videoEncodeThread.get(), &VideoEncodeThread::slot_reset);
    connect(m_videoEncodeThread.get(), &VideoEncodeThread::SIG_videoEncoded,
            this, &CKernal::slot_sendH264Frame);
    connect(m_videoEncodeThread.get(), &VideoEncodeThread::SIG_encodeFinished,
            this, &CKernal::slot_videoEncodeFinished);

    m_latencyTimer->setInterval(5000);
    m_latencyTimeoutTimer->setSingleShot(true);
    m_latencyTimeoutTimer->setInterval(1500);
    m_loginTimeoutTimer->setSingleShot(true);
    m_loginTimeoutTimer->setInterval(6000);
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
    m_latencyTimer->stop();
    m_latencyTimeoutTimer->stop();
    m_loginTimeoutTimer->stop();
    m_roomRequestTimer->stop();
    m_latencyProbeActive = false;
    m_loginPending = false;
    m_roomRequest = RoomRequest::None;
    m_pendingRoomId = 0;
    m_latencySocket->abort();
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
    if(m_client){
        m_client->CloseNet();
        delete m_client; m_client = NULL;
    }

}

void CKernal::slot_measureLatency()
{
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
        m_loginDialog->slot_setLatency(latencyMs);
    }
    if (m_demoDialog) {
        m_demoDialog->slot_setLatency(latencyMs);
    }
    if (m_roomDialog) {
        m_roomDialog->slot_setLatency(latencyMs);
    }
}

void CKernal::slot_loginTimeout()
{
    if (!m_loginPending) {
        return;
    }

    m_loginPending = false;
    if (m_loginDialog) {
        m_loginDialog->slot_setLoginPending(false);
        QMessageBox::warning(m_loginDialog, "提示",
                             "登录超时，服务器没有返回结果，请重试");
    }
}

void CKernal::slot_mainDisconnected()
{
    if (m_roomRequest != RoomRequest::None) {
        clearRoomRequest();
        m_demoDialog->slot_setPushButton_enable(true);
        QMessageBox::warning(m_demoDialog, "提示", "房间请求连接已断开，请重试");
    }

    if (!m_loginPending) {
        return;
    }

    m_loginTimeoutTimer->stop();
    m_loginPending = false;
    if (m_loginDialog) {
        m_loginDialog->slot_setLoginPending(false);
        QMessageBox::warning(m_loginDialog, "提示",
                             "登录连接已断开，请重试");
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
    int type = *(int*)buf;
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
    qDebug() <<  __func__;
    const int loginResponseHeaderSize = 3 * static_cast<int>(sizeof(int));
    if (nlen < loginResponseHeaderSize) {
        return;
    }

    m_loginTimeoutTimer->stop();
    m_loginPending = false;
    m_loginDialog->slot_setLoginPending(false);

    // The original server sends type/user id/result only. Newer servers append
    // a session token, so parse the common header and use the token when present.
    const int userid = *(reinterpret_cast<int*>(buf + sizeof(int)));
    const int result = *(reinterpret_cast<int*>(buf + 2 * sizeof(int)));
    const int tokenOffset = 3 * static_cast<int>(sizeof(int));
    bool hasSessionToken = nlen >= tokenOffset + SESSION_TOKEN_SIZE;
    // 根据结果，弹窗提示
    switch (result) {
    case user_not_exist:
        QMessageBox::about( this->m_loginDialog, "提示" , "查无此人, 登陆失败");
        break;
    case password_error:
        QMessageBox::about( this->m_loginDialog, "提示" , "密码错误，登录失败");
        break;
    case login_success: {
        // QMessageBox::about( this->m_loginDialog, "提示" , "注册成功");
        qDebug() << "login_success";
        // ui切换 存id
        m_userid = userid;
        memset(m_sessionToken, 0, sizeof(m_sessionToken));
        if (hasSessionToken) {
            memcpy(m_sessionToken, buf + tokenOffset, sizeof(m_sessionToken));
            hasSessionToken = hasValidSessionToken(m_sessionToken);
            if (!hasSessionToken) {
                memset(m_sessionToken, 0, sizeof(m_sessionToken));
            }
        }
        m_loginDialog->hide();
        m_demoDialog->showNormal();

        // 注册视频和音频
        if (hasSessionToken) {
            STRU_AUDIO_REGISTER audiorq;
            audiorq.m_userid = m_userid;
            memcpy(audiorq.m_sessionToken, m_sessionToken, sizeof(m_sessionToken));
            slot_clientAudioSendData(0, (char*)&audiorq, sizeof(audiorq));

            STRU_VIDEO_REGISTER videorq;
            videorq.m_userid = m_userid;
            memcpy(videorq.m_sessionToken, m_sessionToken, sizeof(m_sessionToken));
            slot_clientVideoSendData(0, (char*)&videorq, sizeof(videorq));
        } else {
            struct LegacyAvRegister {
                PackType type;
                int userid;
            };
            LegacyAvRegister audiorq = { DEF_PACK_AUDIO_REGISTER, m_userid };
            LegacyAvRegister videorq = { DEF_PACK_VIDEO_REGISTER, m_userid };
            slot_clientAudioSendData(0, reinterpret_cast<char*>(&audiorq),
                                     sizeof(audiorq));
            slot_clientVideoSendData(0, reinterpret_cast<char*>(&videorq),
                                     sizeof(videorq));
        }

        break;
    }
    case server_error:
        QMessageBox::warning(m_loginDialog, "提示", "服务器内部错误，请稍后重试");
        break;
    }
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

    const QString userName = QString::fromStdString(rq->m_szUser);
    const auto existing = m_mapIDToUserShow.find(rq->m_UserID);
    if (existing != m_mapIDToUserShow.end()) {
        existing->second->slot_setInfo(rq->m_UserID, userName);
        return;
    }

    UserShow* user = new UserShow;
    user->slot_setInfo(rq->m_UserID, userName);
    connect(user, SIGNAL(SIG_userClicked(int,QString)),
            m_roomDialog, SLOT(slot_setBigImageInfo(int,QString)));
    m_mapIDToUserShow[rq->m_UserID] = user;
    m_roomDialog->slot_addUser(user);

    m_mapIDToAudioWrite[rq->m_UserID] = new Audio_Write;
    requestAvatar(rq->m_UserID);
}

void CKernal::slot_dealUserInfoRq(uint socket, char *buf, int nlen)
{
    if (nlen != static_cast<int>(sizeof(STRU_USER_INFO_RQ))) return;
    // 拆包
    STRU_USER_INFO_RQ* rq = (STRU_USER_INFO_RQ*)buf;
    // 更新ui
    m_name = QString::fromStdString(rq->m_szUser);
    m_userIconId = qBound(0, rq->m_iconid, 35);
    const QString feeling = QString::fromStdString(rq->m_userFeeling);

    if (m_pendingRegistrationTel == m_loginTel &&
            (!m_pendingRegistrationNickname.isEmpty() ||
             m_pendingRegistrationAvatarChanged)) {
        const QString updatedName = m_pendingRegistrationNickname.isEmpty()
                ? m_name : m_pendingRegistrationNickname;
        const int updatedIconId = m_pendingRegistrationAvatarChanged
                ? m_pendingRegistrationIconId : m_userIconId;

        STRU_USER_INFO_RQ update;
        update.m_UserID = m_userid;
        update.m_iconid = updatedIconId;
        const QByteArray updatedNameUtf8 = updatedName.toUtf8();
        const QByteArray feelingUtf8 = feeling.toUtf8();
        strncpy(update.m_szUser, updatedNameUtf8.constData(), sizeof(update.m_szUser) - 1);
        strncpy(update.m_userFeeling, feelingUtf8.constData(),
                sizeof(update.m_userFeeling) - 1);
        slot_clientSendData(0, reinterpret_cast<char *>(&update), sizeof(update));

        m_name = updatedName;
        m_userIconId = qBound(0, updatedIconId, 35);
        if (m_pendingRegistrationAvatarChanged) {
            if (m_pendingRegistrationAvatarData.isEmpty()) {
                m_userAvatar = QImage();
            } else {
                QImage customAvatar;
                if (customAvatar.loadFromData(m_pendingRegistrationAvatarData, "JPG")) {
                    m_userAvatar = customAvatar;
                }
            }
            sendAvatarUpload(m_pendingRegistrationAvatarData);
        }

        m_pendingRegistrationTel.clear();
        m_pendingRegistrationNickname.clear();
        m_pendingRegistrationAvatarData.clear();
        m_pendingRegistrationAvatarChanged = false;
    }

    m_demoDialog->slot_setInfo(m_name, m_userIconId);

    // 更新设置ui
    m_setUserDialog->slot_setInfo(m_userIconId, m_name, feeling);
    updateOwnRoomAvatar(m_userAvatar.isNull() ? builtInAvatar(m_userIconId)
                                               : m_userAvatar);
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
    if (rs->m_result != 0) {
        QMessageBox::warning(m_setUserDialog, "提示", "头像上传失败，请稍后重试");
        return;
    }
    requestAvatar(m_userid);
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
            m_userAvatar = QImage();
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
        m_userAvatar = avatar;
        m_demoDialog->slot_setAvatarImage(avatar);
        m_setUserDialog->slot_setAvatarImage(avatar);
    }
    const auto user = m_mapIDToUserShow.find(rs->m_TargetUserID);
    if (user != m_mapIDToUserShow.end()) {
        user->second->slot_setAvatarImage(avatar);
    }
    m_roomDialog->slot_setBigAvatar(rs->m_TargetUserID, avatar);
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
    if (!isValidRoomId(roomid)) {
        return;
    }
    clearRoomRequest();
    m_roomid = roomid;
    // 可以写成原界面隐藏 或者按钮失效
    // m_demoDialog->hide();    按键变会代替窗口隐藏
    m_demoDialog->slot_setPushButton_enable(false);
    m_roomDialog->showNormal();

    // 初始化状态 清空等操作 可以复用
    // todo
    m_roomDialog->slot_setRoomID(roomid);
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
}

// 弹出框
void CKernal::slot_setUser()
{
    m_setUserDialog->show();
}

void CKernal::slot_userSetCommit(int iconid, QString name, QString feeling,
                                 QByteArray avatarData, bool avatarChanged)
{
    // 发包
    // 中文兼容 utf8
    std::string strName = name.toStdString();
    std::string strfeeling = feeling.toStdString();

    STRU_USER_INFO_RQ rq;
    rq.m_iconid = iconid;
    rq.m_UserID = m_userid;
    strncpy(rq.m_userFeeling, strfeeling.c_str(), sizeof(rq.m_userFeeling) - 1);
    strncpy(rq.m_szUser, strName.c_str(), sizeof(rq.m_szUser) - 1);

    const bool infoSent = slot_clientSendData(0, reinterpret_cast<char*>(&rq), sizeof(rq));
    m_name = name;
    m_userIconId = qBound(0, iconid, 35);
    m_demoDialog->slot_setInfo(m_name, iconid);

    if (avatarChanged) {
        if (avatarData.isEmpty()) {
            m_userAvatar = QImage();
        } else {
            QImage customAvatar;
            if (customAvatar.loadFromData(avatarData, "JPG")) {
                m_userAvatar = customAvatar;
            }
        }
    }
    updateOwnRoomAvatar(m_userAvatar.isNull() ? builtInAvatar(m_userIconId)
                                               : m_userAvatar);

    if (avatarChanged && !sendAvatarUpload(avatarData)) {
        QMessageBox::warning(m_setUserDialog, "提示",
                             "头像未上传。请确认服务器已升级并重新登录后再试");
    }

    if (infoSent) {
        QMessageBox::about(m_demoDialog, "提示", "用户信息设置成功");
    } else {
        QMessageBox::warning(m_demoDialog, "提示", "用户信息保存失败，请检查网络连接");
    }
}

void CKernal::requestAvatar(int targetUserId)
{
    if (targetUserId <= 0 || m_userid <= 0 || !hasValidSessionToken(m_sessionToken)) {
        return;
    }

    STRU_AVATAR_FETCH_RQ rq;
    rq.m_RequestUserID = m_userid;
    rq.m_TargetUserID = targetUserId;
    memcpy(rq.m_sessionToken, m_sessionToken, sizeof(rq.m_sessionToken));
    slot_clientSendData(0, reinterpret_cast<char*>(&rq), sizeof(rq));
}

bool CKernal::sendAvatarUpload(const QByteArray &avatarData)
{
    if (m_userid <= 0 || !hasValidSessionToken(m_sessionToken) ||
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
    return slot_clientSendData(0, packet.data(), packet.size());
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
    // 发包
    STRU_LEAVEROOM_RQ rq;
    rq.m_nUserId = m_userid;
    rq.m_RoomId = m_roomid;

    slot_clientSendData(0, (char*)&rq, sizeof(rq));

    m_roomid = 0;
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

void CKernal::slot_captureOpen()
{
    if (m_activeVideoSource == VideoSource::Camera) {
        return;
    }
    if (m_activeVideoSource == VideoSource::Desktop) {
        m_deskRead->pause();
        stopLocalVideo();
    }
    Q_EMIT SIG_resetVideoEncoder();
    m_activeVideoSource = VideoSource::Camera;
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
    if (m_activeVideoSource == VideoSource::Camera) {
        m_videoRead->pause();
        stopLocalVideo();
    }
    Q_EMIT SIG_resetVideoEncoder();
    m_activeVideoSource = VideoSource::Desktop;
    m_deskRead->start();
}

void CKernal::slot_deskClose()
{
    m_deskRead->pause();
    if (m_activeVideoSource == VideoSource::Desktop) {
        stopLocalVideo();
    }
}

void CKernal::slot_cameraStateChanged(bool active)
{
    if (!active && m_activeVideoSource == VideoSource::Camera) {
        stopLocalVideo();
    }
}

QImage CKernal::builtInAvatar(int iconid) const
{
    QImage avatar;
    avatar.load(QString(":/tx/%1.png").arg(qBound(0, iconid, 35)));
    return avatar;
}

void CKernal::updateOwnRoomAvatar(const QImage &avatar)
{
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
    if (m_roomid != 0) {
        slot_sendVideoFrameData(QByteArray(), m_videoStreamGeneration);
    }
}

void CKernal::stopLocalVideo()
{
    ++m_videoStreamGeneration;
    m_sendVideoThread->setActiveGeneration(m_videoStreamGeneration);
    Q_EMIT SIG_resetVideoEncoder();
    clearUserVideo(m_userid);
    sendVideoStopFrame();
    m_activeVideoSource = VideoSource::None;
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
    // 预览图 大图 显示
    m_roomDialog->slot_setBigImage(id, img);
    // 列表显示
    if(m_mapIDToUserShow.count(id) > 0){
        UserShow* user = m_mapIDToUserShow[id];
        user->slot_setImage(img);
    }

}

void CKernal::slot_setFunnyPic(int picid)
{
    m_videoRead->setFunnyPic(picid);
}

void CKernal::slot_sendVideoFrameData(const QByteArray &ba, quint64 generation)
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
    Q_EMIT SIG_sendVideoFrame(0, buf, sizeof(int)*7 + len, generation);
}

void CKernal::queueVideoFrame(const QImage &image, int frameRate, int bitRate)
{
    if (m_roomid == 0 || image.isNull()) {
        return;
    }

    bool expected = false;
    if (!m_videoEncodeBusy.compare_exchange_strong(expected, true)) {
        return;
    }
    Q_EMIT SIG_encodeVideoFrame(image.copy(), frameRate, bitRate,
                                 m_videoStreamGeneration);
}

void CKernal::slot_sendCameraVideoFrame(QImage img)
{
    if (m_activeVideoSource == VideoSource::Camera) {
        queueVideoFrame(img, CAMERA_FRAME_RATE, CAMERA_VIDEO_BITRATE);
    }
}

void CKernal::slot_sendDesktopVideoFrame(QImage img)
{
    if (m_activeVideoSource == VideoSource::Desktop) {
        queueVideoFrame(img, DESKTOP_FRAME_RATE, DESKTOP_VIDEO_BITRATE);
    }
}

void CKernal::slot_sendH264Frame(QByteArray encoded, int flags, int width, int height,
                                 quint64 generation)
{
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

    H264PayloadHeader header = {};
    header.magic = H264_PAYLOAD_MAGIC;
    header.version = H264_PAYLOAD_VERSION;
    header.flags = static_cast<quint16>(flags);
    header.width = static_cast<quint16>(width);
    header.height = static_cast<quint16>(height);
    header.dataSize = static_cast<quint32>(encoded.size());

    QByteArray payload(reinterpret_cast<const char *>(&header), sizeof(header));
    payload.append(encoded);
    slot_sendVideoFrameData(payload, generation);
}

void CKernal::slot_videoEncodeFinished()
{
    m_videoEncodeBusy.store(false);
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

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    int audioLen = nlen - 7*sizeof(int);
    // 音频数据
    QByteArray ba(tmp, audioLen);
    if(m_mapIDToAudioWrite.count(userid) > 0){
        Audio_Write* aw = m_mapIDToAudioWrite[userid];
        aw->slot_net_rx(ba);
    }
}

void CKernal::slot_dealVideoFrame(uint socket, char *buf, int nlen)
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

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    tmp += sizeof(int);

    const int imageLen = nlen - 7*sizeof(int);
    if (m_roomid == 0 || roomid != m_roomid) {
        return;
    }
    if (imageLen == 0) {
        m_mapIDToVideoDecoder.erase(userid);
        clearUserVideo(userid);
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
            auto &decoder = m_mapIDToVideoDecoder[userid];
            if (!decoder) {
                decoder = std::make_unique<H264Decoder>();
            }
            QImage decoded;
            const QByteArray encoded(tmp + sizeof(header), encodedLength);
            if (decoder->decode(encoded, &decoded) && !decoded.isNull()) {
                slot_refleshUserImage(userid, decoded);
            }
            return;
        }
    }

    // Clients before the H.264 upgrade send JPEG bytes directly after the frame header.
    QImage img;
    img.loadFromData((/*cosnt*/uchar*)tmp, imageLen);

    slot_refleshUserImage(userid, img);
}

bool CKernal::slot_clientSendData(uint socket, char *buf, int nlen)
{
    return m_client->SendData(socket, buf, nlen);
}

void CKernal::slot_clientAudioSendData(uint socket, char *buf, int nlen)
{
    m_tcpAV[0]->SendData(socket, buf, nlen);
}

void CKernal::slot_clientVideoSendData(uint socket, char *buf, int nlen)
{
    m_tcpAV[1]->SendData(socket, buf, nlen);
}

// 登录提交
void CKernal::slot_loginCommit(QString tel, QString passwd)
{
    if (m_loginPending) {
        return;
    }

    // 提取手机号 密码
    m_loginTel = tel;
    std::string strTel = tel.toStdString();
    std::string strPasswd = GetMD5(passwd);
    // 写入协议包，网络发送
    STRU_LOGIN_RQ rq;
    strncpy(rq.tel, strTel.c_str(), sizeof(rq.tel) - 1);
    strncpy(rq.password, strPasswd.c_str(), sizeof(rq.password) - 1);

    m_loginPending = true;
    m_loginDialog->slot_setLoginPending(true);
    m_loginTimeoutTimer->start();
    if (!slot_clientSendData(0, (char*)&rq, sizeof(rq))) {
        m_loginTimeoutTimer->stop();
        m_loginPending = false;
        m_loginDialog->slot_setLoginPending(false);
        QMessageBox::warning(m_loginDialog, "提示",
                             "无法连接服务器，请检查网络或稍后重试");
    }
    //    qDebug() << rq.tel << " " << rq.password;
}

// 注册提交
void CKernal::slot_registerCommit(QString tel, QString passwd, QString nickname,
                                  int iconid, QByteArray avatarData, bool avatarChanged)
{
    // 提取手机号 密码
    std::string strTel = tel.toStdString();
    std::string strPasswd = GetMD5(passwd);
    // 写入协议包，网络发送
    STRU_REGISTER_RQ rq;
    strncpy(rq.tel, strTel.c_str(), sizeof(rq.tel) - 1);
    strncpy(rq.password, strPasswd.c_str(), sizeof(rq.password) - 1);

    m_registerDraftTel = tel;
    m_registerDraftPassword = passwd;
    m_registerDraftNickname = nickname;
    m_registerDraftIconId = qBound(0, iconid, 35);
    m_registerDraftAvatarData = avatarData;
    m_registerDraftAvatarChanged = avatarChanged;

    if (!slot_clientSendData(0, (char*)&rq, sizeof(rq))) {
        m_registerDraftTel.clear();
        m_registerDraftPassword.clear();
        m_registerDraftNickname.clear();
        m_registerDraftAvatarData.clear();
        m_registerDraftAvatarChanged = false;
        QMessageBox::warning(m_loginDialog, "提示",
                             "无法连接服务器，请检查网络或稍后重试");
    }
    qDebug() << rq.tel << " " << rq.password;
}

// 加入房间
void CKernal::slot_joinRoom()
{
    if (m_roomid != 0) {
        QMessageBox::about(this->m_demoDialog, "提示", "已在房间中,加入失败");
        return;
    }
    if (m_roomRequest != RoomRequest::None) {
        return;
    }

    bool accepted = false;
    const int roomid = QInputDialog::getInt(m_demoDialog, "加入房间", "房间号:",
                                            kMinimumRoomId, kMinimumRoomId,
                                            kMaximumRoomId, 1, &accepted);
    if (!accepted) {
        return;
    }
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
}

void SendThread::slot_threadSendData(uint socket, char *buf, int nlen,
                                     quint64 generation)
{
    if (generation != m_activeGeneration.load()) {
        delete[] buf;
        return;
    }
    //qDebug() <<"slot_threadSendData"<<QThread::currentThreadId();
    // 判断是否有延迟 丢包
    char* tmp = buf;
// 类型
    tmp += sizeof(int);
// 用户id
    tmp += sizeof(int);
// 房间id
    tmp += sizeof(int);
    int hour = *(int*)tmp;
    tmp += sizeof(int);
    int min = *(int*)tmp;
    tmp += sizeof(int);
    int sec = *(int*)tmp;
    tmp += sizeof(int);
    int msec = *(int*)tmp;
    tmp += sizeof(int);

    QTime tm(hour, min, sec, msec);

    // 延迟超过200ms丢弃包
    if(tm.msecsTo(QTime::currentTime()) <= 200)
        CKernal::getInstance()->slot_clientVideoSendData(socket, buf, nlen);
        // CKernal::getInstance()->slot_clientSendData(socket, buf, nlen);
    delete[] buf;
}
