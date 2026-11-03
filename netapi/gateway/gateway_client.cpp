#include "gateway_client.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QSharedPointer>

namespace {
QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
QJsonObject object(const QByteArray &data) { return QJsonDocument::fromJson(data).object(); }
QString roomPath(const QString &id) { return "/v1/rooms/" + QString::fromLatin1(QUrl::toPercentEncoding(id)); }
}
GatewayClient::GatewayClient(QObject *parent) : QObject(parent), m_http(this) {
    m_chatRetry.setSingleShot(true);m_chatRetry.setInterval(1500);
    connect(&m_chatRetry,&QTimer::timeout,this,&GatewayClient::openChat);
}
void GatewayClient::setBaseUrl(const QUrl &url) {
    if ((url.scheme() == "https" || (url.scheme() == "http" &&
          (url.host() == "127.0.0.1" || url.host() == "localhost"))) &&
            !url.host().isEmpty() && url.userInfo().isEmpty() && url.query().isEmpty() && url.fragment().isEmpty()) {
        m_base = url; m_base.setPath("");
    }
}
void GatewayClient::request(const QByteArray &method, const QString &path, const QByteArray &body,
                            const QString &operation, Result done, bool authorized,
                            const QByteArray &contentType) {
    if (authorized && !authenticated()) { Q_EMIT failed(operation, QStringLiteral("请先登录")); return; }
    QUrl url(m_base); url.setPath(path, QUrl::StrictMode);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Audio-Video/2.0 Qt");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(15000);
    if (authorized) req.setRawHeader("Authorization", "Bearer " + m_token);
    const quint64 epoch = m_epoch;
    auto clock = QSharedPointer<QElapsedTimer>::create(); clock->start();
    QNetworkReply *reply = m_http.sendCustomRequest(req, method, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, epoch, operation, done, clock]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const auto error = reply->error(); const QString detail = reply->errorString();
        reply->deleteLater();
        if (epoch != m_epoch) return;
        Q_EMIT latencyMeasured(static_cast<int>(clock->elapsed()));
        if (status == 401 && authenticated()) { logout(); Q_EMIT sessionExpired(); return; }
        if (status == 404 && operation == "avatar-fetch") { done(status, {}); return; }
        if (status < 200 || status >= 300 || error != QNetworkReply::NoError) {
            QString message = object(data).value("error").toString();
            if (message.isEmpty()) message = status ? QString("HTTP %1").arg(status) : detail;
            Q_EMIT failed(operation, message); return;
        }
        done(status, data);
    });
}
void GatewayClient::login(const QString &phone, const QString &password) {
    logout();
    request("POST", "/v1/auth/login", json({{"telephone",phone},{"password",password}}), "login",
            [this](int,const QByteArray &data) {
        const auto obj=object(data); const auto user=obj.value("user").toObject();
        const auto token=obj.value("token").toString().toUtf8(); const qint64 id=user.value("id").toInteger();
        if (token.isEmpty() || id<=0 || id>2147483647) { Q_EMIT failed("login", "Invalid login response"); return; }
        m_token=token; m_userId=id; Q_EMIT loggedIn(id,user.value("name").toString()); profile();
    },false);
}
void GatewayClient::registerUser(const QString &phone,const QString &password,const QString &name) {
    request("POST","/v1/auth/register",json({{"telephone",phone},{"password",password},{"name",name.isEmpty()?phone:name}}),"register",
            [this](int,const QByteArray &){Q_EMIT registered();},false);
}
void GatewayClient::profile() { request("GET","/v1/me/profile",{},"profile",[this](int,const QByteArray &data){Q_EMIT profileReceived(object(data));}); }
void GatewayClient::updateProfile(const QString &name,int icon,const QString &feeling) {
    request("PATCH","/v1/me/profile",json({{"name",name},{"icon",icon},{"feeling",feeling}}),"profile",
            [this](int,const QByteArray &data){Q_EMIT profileReceived(object(data)); Q_EMIT profileSaved();});
}
void GatewayClient::uploadAvatar(const QByteArray &jpeg) {
    if (jpeg.size()>256*1024) {Q_EMIT failed("avatar","Avatar exceeds 256 KiB");return;}
    ++m_avatarVersion;
    request("PUT","/v1/me/avatar",jpeg,"avatar",[this,jpeg](int,const QByteArray &){Q_EMIT avatarReceived(m_userId,jpeg);Q_EMIT avatarSaved();},true,"image/jpeg");
}
void GatewayClient::fetchAvatar(qint64 id) {
    if (id<=0) return;
    // Missing custom avatars are an ordinary built-in-avatar case.
    const quint64 version=m_avatarVersion;
    request("GET",QString("/v1/users/%1/avatar").arg(id),{},"avatar-fetch",[this,id,version](int,const QByteArray &data){if(version==m_avatarVersion)Q_EMIT avatarReceived(id,data);});
}
void GatewayClient::createRoom() {
    const quint64 generation=m_roomGeneration;
    request("POST","/v1/rooms","{}","room",[this,generation](int,const QByteArray &data){
        if(generation!=m_roomGeneration)return;
        const QString id=object(data).value("room_id").toString();
        if(id.isEmpty()){Q_EMIT failed("room","Invalid room response");return;} joinRoom(id);
    });
}
void GatewayClient::joinRoom(const QString &id) {
    if (id.isEmpty() || id.contains('/') || id.size()>128) {Q_EMIT failed("room","Invalid room ID");return;}
    const quint64 generation=++m_roomGeneration;
    request("POST",roomPath(id)+"/join","{}","room",[this,id,generation](int,const QByteArray &data){
        if(generation!=m_roomGeneration)return;
        const auto obj=object(data); const QString token=obj.value("token").toString();
        if(token.isEmpty()||obj.value("room_id").toString()!=id){Q_EMIT failed("room","Invalid room token");return;}
        m_room=id; openChat(); Q_EMIT roomJoined(id,token);
    });
}
void GatewayClient::leaveRoom() {
    const QString id=m_room; m_room.clear();
    ++m_roomGeneration;m_chatRetry.stop();if(m_chat){m_chat->disconnect(this);m_chat->close();m_chat->deleteLater();m_chat=nullptr;} if(id.isEmpty()||!authenticated())return;
    request("POST",roomPath(id)+"/leave","{}","leave",[](int,const QByteArray &){});
}
void GatewayClient::logout() {
    ++m_epoch;++m_roomGeneration;m_chatRetry.stop();
    if(m_chat){m_chat->disconnect(this);m_chat->close();m_chat->deleteLater();m_chat=nullptr;}
    m_token.clear();m_room.clear();m_userId=0;
}

void GatewayClient::openChat() {
    if(m_room.isEmpty()||!authenticated())return;
    if(m_chat){m_chat->disconnect(this);m_chat->close();m_chat->deleteLater();}
    m_chat=new QWebSocket(QString(),QWebSocketProtocol::VersionLatest,this);
    auto *socket=m_chat;const QString id=m_room;
    QUrl url(m_base);url.setScheme(url.scheme()=="https"?"wss":"ws");url.setPath(roomPath(id)+"/chat");
    QNetworkRequest req(url);req.setRawHeader("Authorization","Bearer "+m_token);
    req.setHeader(QNetworkRequest::UserAgentHeader,"Audio-Video/2.0 Qt");
    connect(socket,&QWebSocket::textMessageReceived,this,[this,socket,id](const QString &text){
        if(socket!=m_chat||id!=m_room)return;
        const auto msg=object(text.toUtf8());
        if(msg.contains("error")){Q_EMIT failed("chat",msg.value("error").toString());return;}
        const qint64 sender=msg.value("user_id").toInteger();const QString message=msg.value("message").toString();
        const auto bytes=message.toUtf8();
        if(sender>0&&sender!=m_userId&&bytes.size()>0&&bytes.size()<=512)Q_EMIT chatReceived(sender,message);
    });
    connect(socket,&QWebSocket::disconnected,this,[this,socket,id]{
        if(socket==m_chat&&id==m_room&&authenticated())m_chatRetry.start();
    });
    socket->open(req);
}
void GatewayClient::sendChat(const QString &message) {
    const auto bytes=message.toUtf8();
    if(bytes.isEmpty()||bytes.size()>512){Q_EMIT failed("chat",QStringLiteral("消息必须是 1～512 字节"));return;}
    if(!m_chat||m_chat->state()!=QAbstractSocket::ConnectedState){Q_EMIT failed("chat",QStringLiteral("聊天正在重新连接，请稍后重试"));return;}
    if(m_chat->sendTextMessage(QString::fromUtf8(json({{"message",message}})))<0){Q_EMIT failed("chat",QStringLiteral("发送失败"));return;}
    Q_EMIT chatSent(message);
}

void GatewayClient::ping() { request("GET","/healthz",{},"health",[](int,const QByteArray &){},false); }
