#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class QThread;
class QTimer;

struct DesktopCaptureTiming
{
    qint64 captureUs = 0;
    qint64 gpuToCpuUs = 0;
};

class DiagnosticLogger final : public QObject
{
public:
    static DiagnosticLogger &instance();

    void initialize();
    static QString logDirectory();
    static void openLogDirectory();

    void logEvent(const QString &event, const QVariantMap &fields = {});

    void startDesktopShare(int captureWidth, int captureHeight, int targetWidth,
                           int targetHeight, int targetFps, int targetBitrate,
                           const QString &screenName);
    void stopDesktopShare();
    void recordCaptureFrame(const DesktopCaptureTiming &timing, qint64 scaleUs,
                            qint64 mouseUs, int width, int height);
    void recordEncodedFrame(qint64 encodeUs, int width, int height, int bytes,
                            const QString &encoderName);
    void recordLiveKitQueued(qint64 queueUs, int bytes, int queueDepth);
    void recordDroppedFrame(const QString &reason);
    void recordLiveKitStats(double rttMs, double packetLossPercent, double jitterMs,
                            double sendBitrateBps, quint64 networkFramesSent,
                            quint64 networkBytesSent, int queueDepth);

    QString currentLogPath() const;

private:
    DiagnosticLogger();
    ~DiagnosticLogger() override;
    DiagnosticLogger(const DiagnosticLogger &) = delete;
    DiagnosticLogger &operator=(const DiagnosticLogger &) = delete;

    struct Metrics
    {
        bool active = false;
        quint64 captured = 0;
        quint64 encoded = 0;
        quint64 queued = 0;
        quint64 dropped = 0;
        quint64 queuedBytes = 0;
        int captureWidth = 0;
        int captureHeight = 0;
        int encodedWidth = 0;
        int encodedHeight = 0;
        int targetWidth = 0;
        int targetHeight = 0;
        int targetFps = 0;
        int targetBitrate = 0;
        QString encoder;
        QVector<qint64> captureUs;
        QVector<qint64> gpuToCpuUs;
        QVector<qint64> scaleUs;
        QVector<qint64> mouseUs;
        QVector<qint64> encodeUs;
        QVector<qint64> liveKitQueueUs;
        QStringList dropReasons;
        double rttMs = -1.0;
        double packetLossPercent = -1.0;
        double jitterMs = -1.0;
        double sendBitrateBps = -1.0;
        quint64 networkFramesSent = 0;
        quint64 networkBytesSent = 0;
        int queueDepth = 0;
        QString screenName;
    };

    void flushMetricsOnWriterThread();
    void writeLineOnWriterThread(const QString &line);
    void openNextFileOnWriterThread();
    void writeStartupOnWriterThread();
    static qint64 percentile95(QVector<qint64> values);

    QObject *m_writer = nullptr;
    QTimer *m_timer = nullptr;
    QThread *m_writerThread = nullptr;
    mutable std::mutex m_metricsMutex;
    Metrics m_metrics;
    std::atomic_bool m_initialized {false};
    mutable std::mutex m_pathMutex;
    QString m_currentPath;
    QString m_appVersion;
    QVariantMap m_startupFields;
};

#endif // DIAGNOSTICS_H
