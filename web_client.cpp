#include "ckernal.h"
#include "gateway_client.h"
#include "rtc_session.h"
#include "userprofiledialog.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonObject>

namespace {
int userId(const QString &identity) {
    if(!identity.startsWith("user-"))return 0;
    bool ok=false;const int id=identity.mid(5).toInt(&ok);return ok&&id>0?id:0;
}
}
void CKernal::setupWebRtc() {
    m_gateway=new GatewayClient(this);m_gateway->setBaseUrl(QUrl(m_gatewayUrl));
    m_rtc=new RtcSession(this);
    m_audioRead->setPcmMode(true);m_systemAudioRead->setSampleRate(48000);
    connect(m_audioRead,&Audio_Read::SIG_pcmFrame,this,[this](const QByteArray &pcm){if(m_roomid)m_rtc->publishAudio(pcm);});
    connect(m_gateway,&GatewayClient::latencyMeasured,this,&CKernal::updateLatencyDisplay);
    connect(m_gateway,&GatewayClient::loggedIn,this,[this](qint64 id,const QString &name){
        m_loginDeadline->stop();m_loginTimeoutTimer->stop();m_loginPending=false;
        m_loginDialog->slot_setLoginPending(false);m_userid=static_cast<int>(id);m_name=name;m_loggedIn=true;
        m_webRegisterPassword.fill(QChar('\0'));m_webRegisterPassword.clear();
        m_loginDialog->hide();m_demoDialog->showNormal();
    });
    connect(m_gateway,&GatewayClient::registered,this,[this]{webLogin(m_webRegisterPhone,m_webRegisterPassword);});
    connect(m_gateway,&GatewayClient::profileReceived,this,[this](const QJsonObject &p){
        if(!m_loggedIn||p.value("id").toInteger()!=m_userid)return;
        m_name=p.value("name").toString();m_userIconId=p.value("icon").toInt();m_userFeeling=p.value("feeling").toString();
        m_demoDialog->slot_setInfo(m_name,m_userIconId);m_setUserDialog->slot_setInfo(m_userIconId,m_name,m_userFeeling);
        updateOwnRoomAvatar(m_userAvatar.isNull()?builtInAvatar(m_userIconId):m_userAvatar);
        if(m_webRegistrationPending){
            m_webRegistrationPending=false;
            m_gateway->updateProfile(m_webRegisterName.isEmpty()?m_name:m_webRegisterName,m_webRegisterIcon,m_userFeeling);
        }else requestAvatar(m_userid);
    });
    connect(m_gateway,&GatewayClient::profileSaved,this,[this]{
        if(m_webAvatarChanged){m_webAvatarChanged=false;m_gateway->uploadAvatar(m_webAvatar);m_webAvatar.clear();}
        else QMessageBox::information(m_setUserDialog,QStringLiteral("提示"),QStringLiteral("用户资料已保存"));
    });
    connect(m_gateway,&GatewayClient::avatarSaved,this,[this]{QMessageBox::information(m_setUserDialog,QStringLiteral("提示"),QStringLiteral("头像已保存"));});
    connect(m_gateway,&GatewayClient::avatarReceived,this,[this](qint64 id,const QByteArray &bytes){
        QImage avatar;if(!bytes.isEmpty()&&!avatar.loadFromData(bytes,"JPG"))return;
        if(id==m_userid){m_userAvatar=avatar;updateOwnRoomAvatar(avatar.isNull()?builtInAvatar(m_userIconId):avatar);}
        auto p=m_mapIDToUserShow.find(static_cast<int>(id));
        if(p!=m_mapIDToUserShow.end())p->second->slot_setAvatarImage(avatar.isNull()?builtInAvatar(5):avatar);
        m_roomDialog->slot_setBigAvatar(static_cast<int>(id),avatar);
        if(m_userProfileDialog&&m_userProfileDialog->userId()==id)m_userProfileDialog->slot_setAvatar(avatar);
    });
    connect(m_gateway,&GatewayClient::roomJoined,this,[this](const QString &id,const QString &token){
        m_roomRequestTimer->stop();m_webRoomName=id;
        RtcSession::Config config;config.gatewayUrl=QUrl(m_liveKitUrl);config.roomId=id;
        config.participantIdentity=QString("user-%1").arg(m_userid);config.participantName=m_name;config.liveKitToken=token;
        if(!m_rtc->start(config)){m_gateway->leaveRoom();clearRoomRequest();m_demoDialog->slot_setPushButton_enable(true);}
    });
    connect(m_gateway,&GatewayClient::chatSent,this,[this](const QString &text){
        m_roomDialog->slot_appendChatMessage(m_userid,m_name,text,true);m_roomDialog->slot_clearChatInput();
    });
    connect(m_gateway,&GatewayClient::chatReceived,this,[this](qint64 sender,const QString &text){
        if(!m_roomid)return;const int id=static_cast<int>(sender);ensureRemoteParticipant(id);
        const auto p=m_mapIDToUserShow.find(id);
        m_roomDialog->slot_appendChatMessage(id,p==m_mapIDToUserShow.end()?QString::number(id):p->second->m_name,text,false);
    });
    connect(m_gateway,&GatewayClient::failed,this,[this](const QString &op,const QString &detail){
        if(op=="avatar-fetch"||op=="health"||op=="leave")return;
        if(op=="login"||op=="register"){
            m_loginPending=false;m_loginDeadline->stop();m_loginTimeoutTimer->stop();m_loginDialog->slot_setLoginPending(false);
            m_webRegisterPassword.fill(QChar('\0'));m_webRegisterPassword.clear();
        }
        if(op=="register"){m_webRegistrationPending=false;m_webAvatarChanged=false;m_webAvatar.clear();}
        if(op=="room"){clearRoomRequest();m_demoDialog->slot_setPushButton_enable(true);}
        QMessageBox::warning(m_roomid?static_cast<QWidget*>(m_roomDialog):static_cast<QWidget*>(m_loginDialog),QStringLiteral("连接提示"),detail);
    });
    connect(m_gateway,&GatewayClient::sessionExpired,this,[this]{
        slot_quitRoom();m_rtc->stop();m_loggedIn=false;m_userid=0;
        m_demoDialog->hide();m_loginDialog->showNormal();
        QMessageBox::information(m_loginDialog,QStringLiteral("提示"),QStringLiteral("登录已过期，请重新登录"));
    });
    connect(m_rtc,&RtcSession::SIG_stateChanged,this,[this](RtcSession::State state,const QString &detail){
        m_roomDialog->slot_setMediaStatus(detail);
        if(state==RtcSession::State::Connected){
            if(!m_roomid)slot_setJoinedRoom(1); // A local active-room marker; the actual room ID remains a QString.
            Q_EMIT SIG_resetVideoEncoder();
        }else if(state==RtcSession::State::Failed){
            if(m_roomid)slot_quitRoom();else {m_gateway->leaveRoom();m_webRoomName.clear();m_rtc->stop();clearRoomRequest();m_demoDialog->slot_setPushButton_enable(true);}
            QMessageBox::warning(m_demoDialog,QStringLiteral("音视频连接"),detail);
        }
    });
    connect(m_rtc,&RtcSession::SIG_participantJoined,this,[this](const QString &identity,const QString &name){if(const int id=userId(identity))ensureRemoteParticipant(id,name);});
    connect(m_rtc,&RtcSession::SIG_participantLeft,this,[this](const QString &identity){removeWebParticipant(userId(identity));});
    connect(m_rtc,&RtcSession::SIG_remoteVideoFrame,this,[this](const QString &identity,QImage image){
        if(!m_roomid)return;const int id=userId(identity);if(!id)return;ensureRemoteParticipant(id);slot_refleshUserImage(id,image);m_roomDialog->slot_setMediaStatus(QStringLiteral("正在接收画面"));
    });
    connect(m_rtc,&RtcSession::SIG_remoteVideoStopped,this,[this](const QString &identity){clearUserVideo(userId(identity));});
    connect(m_rtc,&RtcSession::SIG_remoteAudio,this,[this](const QString &identity,const QByteArray &pcm){
        if(!m_roomid)return;const int id=userId(identity);if(!id)return;ensureRemoteParticipant(id);
        const auto it=m_mapIDToAudioWrite.find(id);if(it!=m_mapIDToAudioWrite.end())it->second->slot_pcm_rx(pcm);
    });
    connect(m_rtc,&RtcSession::SIG_keyframeRequested,this,[this]{if(m_activeVideoSource!=VideoSource::None)Q_EMIT SIG_resetVideoEncoder();});
    connect(m_rtc,&RtcSession::SIG_bitrateRequested,this,[this](int rate){
        if(m_webBitrateClock.isValid()&&m_webBitrateClock.elapsed()<1000)return;
        const int capped=qBound(250000,rate,72000000);
        if(m_webTargetBitrate==0||qAbs(capped-m_webTargetBitrate)>m_webTargetBitrate/5){m_webTargetBitrate=capped;m_webBitrateClock.restart();}
    });
    connect(m_rtc,&RtcSession::SIG_videoSent,this,[this]{if(m_activeVideoSource==VideoSource::Desktop)++m_webSentFrames;});
}
void CKernal::webLogin(const QString &phone,const QString &password){
    if(m_loginPending)return;
    if(m_roomid)slot_quitRoom();
    m_userAvatar=QImage();m_userFeeling.clear();m_userIconId=5;
    m_loggedIn=false;m_userid=0;m_loginPending=true;m_loginDeadline->start();m_loginDialog->slot_setLoginPending(true);
    m_gateway->login(phone,password);
}
void CKernal::webRegister(const QString &phone,const QString &password,const QString &name,int icon,const QByteArray &avatar,bool changed){
    m_webRegisterPhone=phone;m_webRegisterPassword=password;m_webRegisterName=name;m_webRegisterIcon=icon;
    m_webRegistrationPending=true;m_webAvatarChanged=changed;m_webAvatar=avatar;
    m_gateway->registerUser(phone,password,name);
}
void CKernal::webJoinRoom(){
    if(m_roomid||m_roomRequest!=RoomRequest::None)return;
    bool ok=false;
    const QString id=QInputDialog::getText(m_demoDialog,QStringLiteral("加入房间"),QStringLiteral("房间号（例如 room-a1b2c3d4）"),QLineEdit::Normal,QString(),&ok).trimmed();
    if(!ok||id.isEmpty())return;
    beginRoomRequest(RoomRequest::Join);m_gateway->joinRoom(id);
}
void CKernal::removeWebParticipant(int id){
    if(id<=0||id==m_userid)return;
    const auto p=m_mapIDToUserShow.find(id);if(p!=m_mapIDToUserShow.end()){auto *widget=p->second;m_mapIDToUserShow.erase(p);m_roomDialog->slot_removeUser(widget);}
    const auto a=m_mapIDToAudioWrite.find(id);if(a!=m_mapIDToAudioWrite.end()){delete a->second;m_mapIDToAudioWrite.erase(a);}
    m_thumbnailLastPaintMs.erase(id);m_mapIDToVideoDecoder.erase(id);
}
