#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <numeric>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi.h>
#include <wrl/client.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {
constexpr qint64 kMaxLogBytes = 10LL * 1024LL * 1024LL;
constexpr int kMaxLogFiles = 5;
constexpr int kMaxSamplesPerSecond = 4096;

void appendSample(QVector<qint64> *values, qint64 value)
{
    if (!values || value < 0 || values->size() >= kMaxSamplesPerSecond) {
        return;
    }
    values->append(value);
}

QStringList gpuNames()
{
    QStringList names;
#ifdef Q_OS_WIN
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for (UINT index = 0;; ++index) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (!adapter) {
                continue;
            }
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.Description[0] != L'\0') {
                names << QString::fromWCharArray(desc.Description);
            }
        }
    }
#endif
    if (names.isEmpty()) {
        names << QStringLiteral("unknown");
    }
    return names;
}

QVariantMap startupFields()
{
    QVariantMap fields;
    fields.insert(QStringLiteral("version"),
                  QCoreApplication::applicationVersion().isEmpty()
                      ? QStringLiteral("2.0.0")
                      : QCoreApplication::applicationVersion());
    fields.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    fields.insert(QStringLiteral("kernel"), QSysInfo::kernelVersion());
    fields.insert(QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture());

#ifdef Q_OS_WIN
    SYSTEM_INFO systemInfo = {};
    GetNativeSystemInfo(&systemInfo);
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    fields.insert(QStringLiteral("logical_cpu_count"),
                  static_cast<int>(systemInfo.dwNumberOfProcessors));
    fields.insert(QStringLiteral("memory_total_bytes"),
                  static_cast<qulonglong>(memory.ullTotalPhys));
    fields.insert(QStringLiteral("memory_available_bytes"),
                  static_cast<qulonglong>(memory.ullAvailPhys));
#endif
    fields.insert(QStringLiteral("gpu"), gpuNames());

    QVariantMap encoders;
    for (const char *name : {"h264_nvenc", "h264_mf", "libopenh264"}) {
        encoders.insert(QString::fromLatin1(name), avcodec_find_encoder_by_name(name) != nullptr);
    }
    fields.insert(QStringLiteral("h264_encoder_candidates"), encoders);
    fields.insert(QStringLiteral("log_directory"), DiagnosticLogger::logDirectory());
    return fields;
}
} // namespace

DiagnosticLogger &DiagnosticLogger::instance()
{
    static DiagnosticLogger logger;
    return logger;
}

DiagnosticLogger::DiagnosticLogger() = default;

DiagnosticLogger::~DiagnosticLogger()
{
    if (!m_writerThread) {
        return;
    }
    QMetaObject::invokeMethod(m_writer, [this]() { flushMetricsOnWriterThread(); },
                              Qt::BlockingQueuedConnection);
    m_writerThread->quit();
    m_writerThread->wait();
    delete m_writerThread;
    m_writerThread = nullptr;
    m_writer = nullptr;
}

QString DiagnosticLogger::logDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("diagnostics"));
}

void DiagnosticLogger::openLogDirectory()
{
    const QString path = logDirectory();
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void DiagnosticLogger::initialize()
{
    if (m_initialized.exchange(true)) {
        return;
    }
    m_appVersion = QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("2.0.0") : QCoreApplication::applicationVersion();
    m_startupFields = startupFields();
    m_writerThread = new QThread;
    m_writer = new QObject;
    m_writer->moveToThread(m_writerThread);
    QObject::connect(m_writerThread, &QThread::finished, m_writer, &QObject::deleteLater);
    m_writerThread->start();
    QMetaObject::invokeMethod(m_writer, [this]() {
        openNextFileOnWriterThread();
        m_timer = new QTimer(m_writer);
        m_timer->setInterval(1000);
        QObject::connect(m_timer, &QTimer::timeout, m_writer,
                         [this]() { flushMetricsOnWriterThread(); });
        m_timer->start();
        writeStartupOnWriterThread();
    }, Qt::BlockingQueuedConnection);
}

QString DiagnosticLogger::currentLogPath() const
{
    std::lock_guard<std::mutex> lock(m_pathMutex);
    return m_currentPath;
}

void DiagnosticLogger::logEvent(const QString &event, const QVariantMap &fields)
{
    if (!m_initialized.load() || !m_writer) {
        return;
    }
    QJsonObject object;
    object.insert(QStringLiteral("timestamp"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("event"), event);
    object.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(fields));
    const QString line = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    QMetaObject::invokeMethod(m_writer, [this, line]() { writeLineOnWriterThread(line); },
                              Qt::QueuedConnection);
}

void DiagnosticLogger::startDesktopShare(int captureWidth, int captureHeight,
                                         int targetWidth, int targetHeight, int targetFps,
                                         int targetBitrate, const QString &screenName)
{
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        m_metrics = Metrics();
        m_metrics.active = true;
        m_metrics.captureWidth = captureWidth;
        m_metrics.captureHeight = captureHeight;
        m_metrics.targetWidth = targetWidth;
        m_metrics.targetHeight = targetHeight;
        m_metrics.targetFps = targetFps;
        m_metrics.targetBitrate = targetBitrate;
        m_metrics.screenName = screenName;
    }
    logEvent(QStringLiteral("desktop_share_started"),
             {{QStringLiteral("capture_width"), captureWidth},
              {QStringLiteral("capture_height"), captureHeight},
              {QStringLiteral("target_width"), targetWidth},
              {QStringLiteral("target_height"), targetHeight},
              {QStringLiteral("target_fps"), targetFps},
              {QStringLiteral("target_bitrate_bps"), targetBitrate},
              {QStringLiteral("screen"), screenName}});
}

void DiagnosticLogger::stopDesktopShare()
{
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        m_metrics.active = false;
    }
    if (m_writer) {
        QMetaObject::invokeMethod(m_writer, [this]() {
            flushMetricsOnWriterThread();
            writeLineOnWriterThread(QStringLiteral("{\"event\":\"desktop_share_stopped\"}"));
        }, Qt::QueuedConnection);
    }
}

void DiagnosticLogger::recordCaptureFrame(const DesktopCaptureTiming &timing,
                                          qint64 scaleUs, qint64 mouseUs,
                                          int width, int height)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    if (!m_metrics.active) return;
    ++m_metrics.captured;
    m_metrics.captureWidth = width;
    m_metrics.captureHeight = height;
    appendSample(&m_metrics.captureUs, timing.captureUs);
    appendSample(&m_metrics.gpuToCpuUs, timing.gpuToCpuUs);
    appendSample(&m_metrics.scaleUs, scaleUs);
    appendSample(&m_metrics.mouseUs, mouseUs);
}

void DiagnosticLogger::recordEncodedFrame(qint64 encodeUs, int width, int height,
                                          int bytes, const QString &encoderName)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    if (!m_metrics.active) return;
    ++m_metrics.encoded;
    m_metrics.encodedWidth = width;
    m_metrics.encodedHeight = height;
    m_metrics.encoder = encoderName;
    appendSample(&m_metrics.encodeUs, encodeUs);
    Q_UNUSED(bytes);
}

void DiagnosticLogger::recordLiveKitQueued(qint64 queueUs, int bytes, int queueDepth)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    if (!m_metrics.active) return;
    ++m_metrics.queued;
    m_metrics.queuedBytes += static_cast<quint64>(qMax(0, bytes));
    m_metrics.queueDepth = queueDepth;
    appendSample(&m_metrics.liveKitQueueUs, queueUs);
}

void DiagnosticLogger::recordDroppedFrame(const QString &reason)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    if (!m_metrics.active) return;
    ++m_metrics.dropped;
    if (m_metrics.dropReasons.size() < kMaxSamplesPerSecond) {
        m_metrics.dropReasons.append(reason);
    }
}

void DiagnosticLogger::recordLiveKitStats(double rttMs, double packetLossPercent,
                                          double jitterMs, double sendBitrateBps,
                                          quint64 networkFramesSent,
                                          quint64 networkBytesSent, int queueDepth)
{
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    if (!m_metrics.active) return;
    m_metrics.rttMs = rttMs;
    m_metrics.packetLossPercent = packetLossPercent;
    m_metrics.jitterMs = jitterMs;
    m_metrics.sendBitrateBps = sendBitrateBps;
    m_metrics.networkFramesSent = networkFramesSent;
    m_metrics.networkBytesSent = networkBytesSent;
    m_metrics.queueDepth = queueDepth;
}

qint64 DiagnosticLogger::percentile95(QVector<qint64> values)
{
    if (values.isEmpty()) return -1;
    std::sort(values.begin(), values.end());
    const int index = qBound(0, static_cast<int>(values.size() * 0.95) - 1,
                             values.size() - 1);
    return values.at(index);
}

void DiagnosticLogger::flushMetricsOnWriterThread()
{
    Metrics metrics;
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        const bool wasActive = m_metrics.active;
        if (m_metrics.captured == 0 && m_metrics.encoded == 0
            && m_metrics.queued == 0 && m_metrics.dropped == 0) {
            return;
        }
        metrics = std::move(m_metrics);
        m_metrics = Metrics();
        m_metrics.active = wasActive;
        if (wasActive) {
            m_metrics.targetWidth = metrics.targetWidth;
            m_metrics.targetHeight = metrics.targetHeight;
            m_metrics.targetFps = metrics.targetFps;
            m_metrics.targetBitrate = metrics.targetBitrate;
            m_metrics.screenName = metrics.screenName;
            m_metrics.encoder = metrics.encoder;
        }
    }
    QVariantMap fields;
    fields.insert(QStringLiteral("captured_frames"), static_cast<qulonglong>(metrics.captured));
    fields.insert(QStringLiteral("encoded_frames"), static_cast<qulonglong>(metrics.encoded));
    fields.insert(QStringLiteral("livekit_queue_frames"), static_cast<qulonglong>(metrics.queued));
    fields.insert(QStringLiteral("network_sent_frames"), static_cast<qulonglong>(metrics.networkFramesSent));
    fields.insert(QStringLiteral("dropped_frames"), static_cast<qulonglong>(metrics.dropped));
    fields.insert(QStringLiteral("queued_bytes"), static_cast<qulonglong>(metrics.queuedBytes));
    fields.insert(QStringLiteral("capture_resolution"),
                  QStringLiteral("%1x%2").arg(metrics.captureWidth).arg(metrics.captureHeight));
    fields.insert(QStringLiteral("encoded_resolution"),
                  QStringLiteral("%1x%2").arg(metrics.encodedWidth).arg(metrics.encodedHeight));
    fields.insert(QStringLiteral("target_resolution"),
                  QStringLiteral("%1x%2").arg(metrics.targetWidth).arg(metrics.targetHeight));
    fields.insert(QStringLiteral("target_fps"), metrics.targetFps);
    fields.insert(QStringLiteral("target_bitrate_bps"), metrics.targetBitrate);
    fields.insert(QStringLiteral("encoder"), metrics.encoder);
    fields.insert(QStringLiteral("queue_depth"), metrics.queueDepth);
    fields.insert(QStringLiteral("rtt_ms"), metrics.rttMs);
    fields.insert(QStringLiteral("packet_loss_percent"), metrics.packetLossPercent);
    fields.insert(QStringLiteral("jitter_ms"), metrics.jitterMs);
    fields.insert(QStringLiteral("actual_send_bitrate_bps"), metrics.sendBitrateBps);
    fields.insert(QStringLiteral("capture_avg_us"), metrics.captureUs.isEmpty() ? -1 : std::accumulate(metrics.captureUs.cbegin(), metrics.captureUs.cend(), qint64(0)) / metrics.captureUs.size());
    fields.insert(QStringLiteral("capture_p95_us"), percentile95(metrics.captureUs));
    fields.insert(QStringLiteral("gpu_to_cpu_avg_us"), metrics.gpuToCpuUs.isEmpty() ? -1 : std::accumulate(metrics.gpuToCpuUs.cbegin(), metrics.gpuToCpuUs.cend(), qint64(0)) / metrics.gpuToCpuUs.size());
    fields.insert(QStringLiteral("gpu_to_cpu_p95_us"), percentile95(metrics.gpuToCpuUs));
    fields.insert(QStringLiteral("scale_avg_us"), metrics.scaleUs.isEmpty() ? -1 : std::accumulate(metrics.scaleUs.cbegin(), metrics.scaleUs.cend(), qint64(0)) / metrics.scaleUs.size());
    fields.insert(QStringLiteral("scale_p95_us"), percentile95(metrics.scaleUs));
    fields.insert(QStringLiteral("mouse_avg_us"), metrics.mouseUs.isEmpty() ? -1 : std::accumulate(metrics.mouseUs.cbegin(), metrics.mouseUs.cend(), qint64(0)) / metrics.mouseUs.size());
    fields.insert(QStringLiteral("mouse_p95_us"), percentile95(metrics.mouseUs));
    fields.insert(QStringLiteral("encode_avg_us"), metrics.encodeUs.isEmpty() ? -1 : std::accumulate(metrics.encodeUs.cbegin(), metrics.encodeUs.cend(), qint64(0)) / metrics.encodeUs.size());
    fields.insert(QStringLiteral("encode_p95_us"), percentile95(metrics.encodeUs));
    fields.insert(QStringLiteral("livekit_queue_avg_us"), metrics.liveKitQueueUs.isEmpty() ? -1 : std::accumulate(metrics.liveKitQueueUs.cbegin(), metrics.liveKitQueueUs.cend(), qint64(0)) / metrics.liveKitQueueUs.size());
    fields.insert(QStringLiteral("livekit_queue_p95_us"), percentile95(metrics.liveKitQueueUs));
    fields.insert(QStringLiteral("drop_reasons"), metrics.dropReasons);
    QJsonObject object;
    object.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    object.insert(QStringLiteral("event"), QStringLiteral("desktop_share_second"));
    object.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(fields));
    writeLineOnWriterThread(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void DiagnosticLogger::writeLineOnWriterThread(const QString &line)
{
    static QFile file;
    static qint64 bytes = 0;
    if (!file.isOpen() || bytes + line.toUtf8().size() + 1 > kMaxLogBytes) {
        if (file.isOpen()) file.close();
        const QString dir = logDirectory();
        QDir().mkpath(dir);
        QString path;
        for (int suffix = 0; suffix < 100; ++suffix) {
            path = QDir(dir).filePath(QStringLiteral("diagnostics-%1%2.log")
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))
                .arg(suffix == 0 ? QString() : QStringLiteral("-%1").arg(suffix)));
            if (!QFile::exists(path)) break;
        }
        file.setFileName(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            return;
        }
        bytes = file.size();
        {
            std::lock_guard<std::mutex> lock(m_pathMutex);
            m_currentPath = path;
        }
        QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("diagnostics-*.log")},
                                                       QDir::Files, QDir::Time);
        for (int i = kMaxLogFiles; i < files.size(); ++i) {
            QFile::remove(files.at(i).absoluteFilePath());
        }
    }
    if (!file.isOpen()) return;
    const QByteArray data = line.toUtf8() + '\n';
    file.write(data);
    file.flush();
    bytes += data.size();
}

void DiagnosticLogger::openNextFileOnWriterThread()
{
    writeLineOnWriterThread(QStringLiteral("{\"event\":\"diagnostics_file_opened\"}"));
}

void DiagnosticLogger::writeStartupOnWriterThread()
{
    logEvent(QStringLiteral("startup"), m_startupFields);
}
