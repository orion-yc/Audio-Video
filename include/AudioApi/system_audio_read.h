#ifndef SYSTEM_AUDIO_READ_H
#define SYSTEM_AUDIO_READ_H

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QTimer>

#include <atomic>

struct SystemAudioDevice
{
    QString id;
    QString name;
    bool isDefault = false;
};

// Captures a Windows render endpoint with WASAPI loopback and normalizes it to
// the 8 kHz mono PCM format used by the existing Speex transport.
class SystemAudio_Read : public QThread
{
    Q_OBJECT
public:
    explicit SystemAudio_Read(QObject *parent = nullptr);
    ~SystemAudio_Read() override;

    static QList<SystemAudioDevice> availableOutputDevices();
    static QString defaultOutputDeviceId();

    QString selectedDeviceId() const;
    bool followsDefaultDevice() const;

public slots:
    void startCapture();
    void setSampleRate(int rate) { if (!isRunning() && (rate == 48000 || rate == 8000)) m_sampleRate.store(rate); }
    void stopCapture();
    void setDeviceId(const QString &deviceId);

signals:
    void SIG_audioFrame(const QByteArray &pcm);
    void SIG_captureError(const QString &message);

protected:
    void run() override;

private slots:
    void slot_checkDefaultDevice();

private:
    void captureLoop(const QString &deviceId);

    mutable QMutex m_deviceMutex;
    QString m_selectedDeviceId;
    QString m_activeDeviceId;
    QTimer m_defaultDeviceTimer;
    QTimer m_retryTimer;
    std::atomic_int m_sampleRate {8000};
    std::atomic_bool m_captureRequested { false };
};

#endif // SYSTEM_AUDIO_READ_H
