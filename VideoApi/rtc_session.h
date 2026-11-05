#ifndef RTC_SESSION_H
#define RTC_SESSION_H
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QUrl>
#include <atomic>
#include <memory>

// LiveKit operations run on a dedicated thread. Only bounded media is queued.
class RtcSession : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Reconnecting, Failed };
    Q_ENUM(State)
    struct Config { QUrl gatewayUrl; QString roomId; QString participantIdentity; QString participantName; QString liveKitToken; };
    explicit RtcSession(QObject *parent = nullptr);
    ~RtcSession() override;
    static bool isAvailable();
    bool start(const Config &config);
    void stop();
    State state() const { return m_state; }
    bool canAcceptVideo() const;
    bool publishH264(const QByteArray &encoded, bool keyframe, int width, int height,
                     int fps, int bitrate, bool screenShare);
    void publishAudio(const QByteArray &pcm, int sampleRate = 48000, int channels = 1);
    void stopVideo();
signals:
    void SIG_stateChanged(RtcSession::State state, QString detail);
    void SIG_participantJoined(QString identity, QString name);
    void SIG_participantLeft(QString identity);
    void SIG_remoteVideoFrame(QString participantIdentity, QImage image);
    void SIG_remoteVideoStopped(QString participantIdentity);
    void SIG_remoteAudio(QString participantIdentity, QByteArray pcm);
    void SIG_keyframeRequested();
    void SIG_bitrateRequested(int bitrate);
    void SIG_videoSent();
    void SIG_statsUpdated(double fps, double bitrate, double packetLoss, double jitterMs, double rttMs);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    State m_state = State::Disconnected;
    std::atomic<quint64> m_generation {0};
    std::atomic_bool m_videoPending {false};
    std::atomic_int m_audioPending {0};
    void setState(State state, const QString &detail);
};
Q_DECLARE_METATYPE(RtcSession::State)
#endif
