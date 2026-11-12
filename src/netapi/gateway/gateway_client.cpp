#include "gateway_client.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QSharedPointer>
#include <QNetworkProxy>

namespace {
QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
QJsonObject object(const QByteArray &data) { return QJsonDocument::fromJson(data).object(); }
QString roomPath(const QString &id) { return "/v1/rooms/" + QString::fromLatin1(QUrl::toPercentEncoding(id)); }
}
GatewayClient::GatewayClient(QObject *parent) : QObject(parent), m_http(this), m_healthHttp(this) {
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
                            const QByteArray &contentType, int attempt) {
    if (authorized && !authenticated()) { Q_EMIT failed(operation, QStringLiteral("请先登录")); return; }
    QUrl url(m_base); url.setPath(path, QUrl::StrictMode);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Audio-Video/2.0 Qt");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    const int timeoutMs = operation == "health" ? 4000 : operation == "login" ? 8000 : 15000;
    req.setTransferTimeout(timeoutMs);
    if (authorized) req.setRawHeader("Authorization", "Bearer " + m_token);
    const quint64 epoch = m_epoch;
    auto clock = QSharedPointer<QElapsedTimer>::create(); clock->start();
    auto &http = operation == "health" ? m_healthHttp : m_http;
    const int proxyType = static_cast<int>(http.proxy().type());
    QNetworkReply *reply = http.sendCustomRequest(req, method, body);
    m_requests.insert(reply);
    auto timings = QSharedPointer<QVariantMap>::create();
    timings->insert("phase", "connecting");
    auto mark = [clock, timings](const QString &phase) {
        timings->insert("phase", phase);
        timings->insert(phase + "_ms", clock->elapsed());
    };
    connect(reply, &QNetworkReply::socketStartedConnecting, this, [mark] { mark("tcp_connect"); });
    connect(reply, &QNetworkReply::encrypted, this, [mark] { mark("tls_ready"); });
    connect(reply, &QNetworkReply::requestSent, this, [mark] { mark("request_sent"); });
    connect(reply, &QNetworkReply::metaDataChanged, this, [mark] { mark("response_headers"); });
    // Qt's transfer timeout measures inactivity. This also bounds a slow trickle response.
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply] {
        reply->setProperty("gatewayTimedOut", true);
        reply->abort();
    });
    deadline->start(timeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, epoch, operation, done, clock,
                                                  timings, method, path, body, authorized,
                                                  contentType, attempt, deadline, proxyType]() {
        deadline->stop();
        m_requests.remove(reply);
        if (operation == "health") m_healthPending = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        const bool timedOut = reply->property("gatewayTimedOut").toBool()
                || error == QNetworkReply::TimeoutError;
        const bool success = status >= 200 && status < 300 && error == QNetworkReply::NoError;
        const bool transient = timedOut || status == 502 || status == 503 || status == 504
                || (status >= 520 && status <= 524)
                || (status == 0 && (error == QNetworkReply::TemporaryNetworkFailureError
                    || error == QNetworkReply::RemoteHostClosedError
                    || error == QNetworkReply::ConnectionRefusedError));
        QVariantMap fields = *timings;
        fields.insert("operation", operation);
        fields.insert("host", reply->url().host());
        fields.insert("port", reply->url().port(reply->url().scheme() == "https" ? 443 : 80));
        fields.insert("elapsed_ms", clock->elapsed());
        fields.insert("http_status", status);
        fields.insert("network_error", static_cast<int>(error));
        fields.insert("timed_out", timedOut);
        fields.insert("success", success);
        fields.insert("attempt", attempt + 1);
        fields.insert("cancelled", epoch != m_epoch);
        fields.insert("configured_proxy_type", proxyType);
        fields.insert("cloudflare_ray", QString::fromLatin1(reply->rawHeader("CF-Ray")).left(128));
        Q_EMIT requestDiagnostic(fields);
        reply->deleteLater();
        if (epoch != m_epoch) return;
        if (operation == "health")
            Q_EMIT latencyMeasured(success ? static_cast<int>(clock->elapsed()) : -2);
        // Only login is safe to retry here. Never replay registration or room/profile mutations.
        if (operation == "login" && attempt == 0 && transient) {
            m_http.clearConnectionCache();
            QTimer::singleShot(250, this, [=] {
                if (epoch == m_epoch)
                    request(method, path, body, operation, done, authorized, contentType, attempt + 1);
            });
            return;
        }
        if (status == 401 && authorized && authenticated()) { logout(); Q_EMIT sessionExpired(); return; }
        if (status == 404 && operation == "avatar-fetch") { done(status, {}); return; }
        if (status < 200 || status >= 300 || error != QNetworkReply::NoError) {
            QString message = object(data).value("error").toString();
            if (status == 401 && operation == "login")
                message = QStringLiteral("手机号或密码不正确，请检查后重试。");
            else if (status >= 500)
                message = QStringLiteral("网关或上游服务暂时不可用（HTTP %1，%2），请稍后重试。")
                        .arg(status).arg(reply->url().host());
            else if (timedOut)
                message = QStringLiteral("连接网关 %1 超时（%2 毫秒，第 %3 次尝试）。请检查网络，详细阶段已记入诊断日志。")
                        .arg(reply->url().host()).arg(clock->elapsed()).arg(attempt + 1);
            else if (status == 0) {
                const QString reason = error == QNetworkReply::HostNotFoundError ? QStringLiteral("域名解析失败")
                    : error == QNetworkReply::SslHandshakeFailedError ? QStringLiteral("TLS 安全连接失败")
                    : (error >= QNetworkReply::ProxyConnectionRefusedError && error <= QNetworkReply::UnknownProxyError)
                        ? QStringLiteral("代理连接失败，请检查系统代理") : QStringLiteral("网络连接失败");
                message = QStringLiteral("%1：%2（错误码 %3），请检查网络或服务器。")
                        .arg(reason, reply->url().host()).arg(static_cast<int>(error));
            }
            if (message.isEmpty()) message = QString("HTTP %1").arg(status);
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
    const quint64 generation=++m_roomGeneration;
    request("POST","/v1/rooms","{}","room",[this,generation](int,const QByteArray &data){
        if(generation!=m_roomGeneration)return;
        const auto obj=object(data);const QString id=obj.value("room_id").toString();
        const QString token=obj.value("token").toString();
        if(id.isEmpty()||token.isEmpty()){Q_EMIT failed("room","Invalid room response");return;}
        m_room=id;openChat();Q_EMIT roomJoined(id,token);
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
    const auto pending = m_requests;
    for (auto *reply : pending) reply->abort();
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

void GatewayClient::ping() {
    if (m_healthPending) return;
    m_healthPending = true;
    request("GET","/healthz",{},"health",[](int,const QByteArray &){},false);
}
