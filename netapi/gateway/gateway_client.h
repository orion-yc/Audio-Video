#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QUrl>
#include <QTimer>
#include <QWebSocket>
#include <functional>

// HTTPS control plane. Session JWTs stay in memory and are never written to config.ini.
class GatewayClient : public QObject {
    Q_OBJECT
public:
    explicit GatewayClient(QObject *parent = nullptr);
    void setBaseUrl(const QUrl &url);
    QUrl baseUrl() const { return m_base; }
    bool authenticated() const { return !m_token.isEmpty(); }
    QString roomId() const { return m_room; }
    qint64 userId() const { return m_userId; }
    void login(const QString &telephone, const QString &password);
    void registerUser(const QString &telephone, const QString &password, const QString &name);
    void profile();
    void updateProfile(const QString &name, int icon, const QString &feeling);
    void uploadAvatar(const QByteArray &jpeg);
    void fetchAvatar(qint64 userId);
    void createRoom();
    void joinRoom(const QString &id);
    void leaveRoom();
    void sendChat(const QString &message);
    void logout();
    void ping();
signals:
    void loggedIn(qint64 id, QString name);
    void registered();
    void profileReceived(QJsonObject profile);
    void profileSaved();
    void avatarReceived(qint64 id, QByteArray jpeg);
    void avatarSaved();
    void roomJoined(QString id, QString liveKitToken);
    void chatReceived(qint64 sender, QString message);
    void chatSent(QString message);
    void failed(QString operation, QString message);
    void sessionExpired();
    void latencyMeasured(int ms);
private:
    using Result = std::function<void(int, QByteArray)>;
    void request(const QByteArray &method, const QString &path, const QByteArray &body,
                 const QString &operation, Result done, bool authorized = true,
                 const QByteArray &contentType = "application/json");
    void openChat();
    QWebSocket *m_chat = nullptr;
    QTimer m_chatRetry;
    quint64 m_roomGeneration = 0;
    quint64 m_avatarVersion = 0;
    QNetworkAccessManager m_http;
    QUrl m_base { "https://video.onnotify.top" };
    QByteArray m_token;
    QString m_room;
    qint64 m_userId = 0;
    quint64 m_epoch = 0;
};
