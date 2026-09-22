#ifndef RTC_SESSION_H
#define RTC_SESSION_H

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>

class RtcSession : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        Failed
    };
    Q_ENUM(State)

    struct Config {
        QUrl gatewayUrl;
        QString roomId;
        QString participantIdentity;
        QString participantName;
        QString liveKitToken;
    };

    explicit RtcSession(QObject *parent = nullptr);
    ~RtcSession() override;

    static bool isAvailable();
    bool start(const Config &config);
    void stop();
    State state() const;

signals:
    void SIG_stateChanged(RtcSession::State state, QString detail);
    void SIG_remoteVideoFrame(QString participantIdentity, QImage image);
    void SIG_remoteAudio(QString participantIdentity, QByteArray pcm);
    void SIG_statsUpdated(double fps, double bitrate, double packetLoss,
                          double jitterMs, double rttMs);

private:
    State m_state = State::Disconnected;
};

Q_DECLARE_METATYPE(RtcSession::State)

#endif // RTC_SESSION_H
