#include "dxgi_desktop_capture.h"

#ifdef Q_OS_WIN

#include <QElapsedTimer>
#include <QRect>

#include <windows.h>

#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
QString hresultString(HRESULT result)
{
    return QStringLiteral("0x%1").arg(static_cast<qulonglong>(static_cast<unsigned long>(result)), 8, 16, QLatin1Char('0'));
}

void logDxgi(const QString &event, const QVariantMap &fields)
{
    DiagnosticLogger::instance().logEvent(event, fields);
}

bool outputMatchesRequestedName(const QString &requested, const QRect &requestedGeometry,
                                const DXGI_OUTPUT_DESC &description)
{
    const QRect desktopGeometry(description.DesktopCoordinates.left,
                                description.DesktopCoordinates.top,
                                description.DesktopCoordinates.right - description.DesktopCoordinates.left,
                                description.DesktopCoordinates.bottom - description.DesktopCoordinates.top);
    if (!requestedGeometry.isEmpty() && desktopGeometry == requestedGeometry) {
        return true;
    }
    if (requested.isEmpty()) {
        return true;
    }
    const QString dxgiName = QString::fromWCharArray(description.DeviceName);
    if (requested.compare(dxgiName, Qt::CaseInsensitive) == 0 ||
        requested.contains(dxgiName, Qt::CaseInsensitive) ||
        dxgiName.contains(requested, Qt::CaseInsensitive)) {
        return true;
    }

    // QScreen::name() normally returns the monitor model (for example
    // "H27T22X"), while DXGI exposes the logical display name
    // ("\\\\.\\DISPLAY1"). Resolve the model through the Win32 monitor
    // device so physical screen selection maps to Desktop Duplication.
    if (!description.Monitor) {
        return false;
    }
    MONITORINFOEXW monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(description.Monitor, &monitorInfo)) {
        return false;
    }
    const QString monitorDeviceName = QString::fromWCharArray(monitorInfo.szDevice);
    DISPLAY_DEVICEW adapterDisplay = {};
    adapterDisplay.cb = sizeof(adapterDisplay);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &adapterDisplay, 0); ++index) {
        if (monitorDeviceName.compare(QString::fromWCharArray(adapterDisplay.DeviceName),
                                      Qt::CaseInsensitive) != 0) {
            adapterDisplay = {};
            adapterDisplay.cb = sizeof(adapterDisplay);
            continue;
        }
        DISPLAY_DEVICEW monitorDisplay = {};
        monitorDisplay.cb = sizeof(monitorDisplay);
        if (EnumDisplayDevicesW(adapterDisplay.DeviceName, 0, &monitorDisplay, 0)) {
            const QString model = QString::fromWCharArray(monitorDisplay.DeviceString);
            if (requested.compare(model, Qt::CaseInsensitive) == 0 ||
                requested.contains(model, Qt::CaseInsensitive) ||
                model.contains(requested, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }
    return false;
}
} // namespace

DxgiDesktopCapture::DxgiDesktopCapture() = default;

DxgiDesktopCapture::~DxgiDesktopCapture()
{
    reset();
}

void DxgiDesktopCapture::reset()
{
    m_staging.Reset();
    m_duplication.Reset();
    m_context.Reset();
    m_device.Reset();
    m_outputName.clear();
    m_size = QSize();
    m_lastImage = QImage();
}

bool DxgiDesktopCapture::initialize(const QString &outputName, const QRect &screenGeometry)
{
    if (m_duplication && (outputName.isEmpty() || outputName == m_requestedOutputName)
            && (screenGeometry.isEmpty() || screenGeometry == m_requestedGeometry)) {
        return true;
    }
    if (!m_duplication && outputName == m_lastInitFailureName) {
        return false;
    }
    reset();
    m_requestedOutputName = outputName;
    m_requestedGeometry = screenGeometry;

    // Enumerate adapters and create the device on the adapter that owns the
    // selected monitor. This avoids falling back to slow QScreen capture on
    // hybrid-GPU laptops.
    ComPtr<IDXGIFactory1> factory;
    const HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(factoryResult)) {
        m_lastInitFailureName = outputName;
        logDxgi(QStringLiteral("dxgi_factory_failed"),
                {{QStringLiteral("screen"), outputName},
                 {QStringLiteral("hresult"), hresultString(factoryResult)}});
        return false;
    }
    bool sawOutput = false;
    bool sawMatchingOutput = false;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!adapter) {
            continue;
        }
        DXGI_ADAPTER_DESC1 adapterDescription = {};
        const HRESULT adapterResult = adapter->GetDesc1(&adapterDescription);
        if (FAILED(adapterResult)) {
            logDxgi(QStringLiteral("dxgi_adapter_query_failed"),
                    {{QStringLiteral("adapter_index"), static_cast<int>(adapterIndex)},
                     {QStringLiteral("hresult"), hresultString(adapterResult)}});
            continue;
        }
        if (adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }
        const QString adapterName = QString::fromWCharArray(adapterDescription.Description);

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (!output) {
                continue;
            }
            DXGI_OUTPUT_DESC description = {};
            const HRESULT outputResult = output->GetDesc(&description);
            if (FAILED(outputResult)) {
                logDxgi(QStringLiteral("dxgi_output_query_failed"),
                        {{QStringLiteral("adapter"), adapterName},
                         {QStringLiteral("output_index"), static_cast<int>(outputIndex)},
                         {QStringLiteral("hresult"), hresultString(outputResult)}});
                continue;
            }
            sawOutput = true;
            const QString dxgiOutputName = QString::fromWCharArray(description.DeviceName);
            logDxgi(QStringLiteral("dxgi_output_enumerated"),
                    {{QStringLiteral("adapter"), adapterName},
                     {QStringLiteral("output"), dxgiOutputName},
                     {QStringLiteral("requested_screen"), outputName},
                     {QStringLiteral("desktop_geometry"), QStringLiteral("%1,%2 %3x%4").arg(description.DesktopCoordinates.left).arg(description.DesktopCoordinates.top).arg(description.DesktopCoordinates.right - description.DesktopCoordinates.left).arg(description.DesktopCoordinates.bottom - description.DesktopCoordinates.top)},
                     {QStringLiteral("requested_geometry"), QStringLiteral("%1,%2 %3x%4").arg(screenGeometry.x()).arg(screenGeometry.y()).arg(screenGeometry.width()).arg(screenGeometry.height())},
                     {QStringLiteral("matching"), outputMatchesRequestedName(outputName, screenGeometry, description)}});
            if (!outputMatchesRequestedName(outputName, screenGeometry, description)) {
                continue;
            }
            sawMatchingOutput = true;

            D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
            const HRESULT deviceResult = D3D11CreateDevice(
                adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &m_device, &featureLevel, &m_context);
            if (FAILED(deviceResult)) {
                logDxgi(QStringLiteral("dxgi_device_create_failed"),
                        {{QStringLiteral("adapter"), adapterName},
                         {QStringLiteral("output"), dxgiOutputName},
                         {QStringLiteral("hresult"), hresultString(deviceResult)}});
                reset();
                continue;
            }

            ComPtr<IDXGIOutput1> output1;
            const HRESULT output1Result = output.As(&output1);
            const HRESULT duplicateOutputResult = SUCCEEDED(output1Result)
                    ? output1->DuplicateOutput(m_device.Get(), &m_duplication)
                    : output1Result;
            if (FAILED(duplicateOutputResult)) {
                logDxgi(QStringLiteral("dxgi_duplicate_output_failed"),
                        {{QStringLiteral("adapter"), adapterName},
                         {QStringLiteral("output"), dxgiOutputName},
                         {QStringLiteral("hresult"), hresultString(duplicateOutputResult)}});
                reset();
                continue;
            }
            m_outputName = QString::fromWCharArray(description.DeviceName);
            m_lastInitFailureName.clear();
            logDxgi(QStringLiteral("dxgi_initialized"),
                    {{QStringLiteral("adapter"), adapterName},
                     {QStringLiteral("output"), m_outputName},
                     {QStringLiteral("screen"), outputName}});
            return true;
        }
    }
    m_lastInitFailureName = outputName;
    logDxgi(QStringLiteral("dxgi_output_not_found"),
            {{QStringLiteral("screen"), outputName},
             {QStringLiteral("saw_output"), sawOutput},
             {QStringLiteral("saw_matching_output"), sawMatchingOutput}});
    reset();
    return false;
}

bool DxgiDesktopCapture::ensureStagingTexture(const D3D11_TEXTURE2D_DESC &sourceDescription)
{
    if (m_staging && m_size == QSize(static_cast<int>(sourceDescription.Width),
                                     static_cast<int>(sourceDescription.Height))) {
        return true;
    }
    D3D11_TEXTURE2D_DESC stagingDescription = sourceDescription;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDescription.MiscFlags = 0;
    stagingDescription.ArraySize = 1;
    stagingDescription.MipLevels = 1;
    if (FAILED(m_device->CreateTexture2D(&stagingDescription, nullptr, &m_staging))) {
        m_staging.Reset();
        return false;
    }
    m_size = QSize(static_cast<int>(sourceDescription.Width),
                   static_cast<int>(sourceDescription.Height));
    m_lastImage = QImage(m_size, QImage::Format_ARGB32);
    m_lastImage.fill(Qt::black);
    return true;
}

bool DxgiDesktopCapture::copyMappedFrame(const DXGI_OUTDUPL_FRAME_INFO &frameInfo,
                                         const D3D11_MAPPED_SUBRESOURCE &mapped,
                                         const QSize &size,
                                         const std::vector<RECT> &dirtyRects)
{
    Q_UNUSED(frameInfo);
    if (m_lastImage.isNull()) {
        return false;
    }

    const auto *source = static_cast<const uchar *>(mapped.pData);
    for (const RECT &dirty : dirtyRects) {
        const QRect bounds = QRect(dirty.left, dirty.top,
                                   dirty.right - dirty.left, dirty.bottom - dirty.top)
                                 .intersected(QRect(QPoint(), size));
        for (int y = bounds.top(); y < bounds.bottom(); ++y) {
            auto *destination = m_lastImage.scanLine(y) + bounds.left() * 4;
            const auto *row = source + static_cast<size_t>(y) * mapped.RowPitch
                    + static_cast<size_t>(bounds.left()) * 4;
            memcpy(destination, row, static_cast<size_t>(bounds.width()) * 4);
        }
    }
    return true;
}

bool DxgiDesktopCapture::capture(const QString &outputName, const QRect &screenGeometry, QImage *image,
                                 DesktopCaptureTiming *timing)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (timing) {
        *timing = DesktopCaptureTiming();
    }
    if (!image || !initialize(outputName, screenGeometry)) {
        return false;
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    const HRESULT acquireResult = m_duplication->AcquireNextFrame(0, &frameInfo, &resource);
    if (acquireResult == DXGI_ERROR_WAIT_TIMEOUT) {
        if (!m_lastImage.isNull()) {
            *image = m_lastImage;
            if (timing) timing->captureUs = totalTimer.nsecsElapsed() / 1000;
            return true;
        }
        return false;
    }
    if (acquireResult == DXGI_ERROR_ACCESS_LOST) {
        reset();
        return false;
    }
    if (FAILED(acquireResult) || !resource) {
        return false;
    }

    if (frameInfo.LastPresentTime.QuadPart == 0 && !m_lastImage.isNull()) {
        m_duplication->ReleaseFrame();
        *image = m_lastImage;
        if (timing) timing->captureUs = totalTimer.nsecsElapsed() / 1000;
        return true;
    }

    ComPtr<ID3D11Texture2D> source;
    D3D11_TEXTURE2D_DESC sourceDescription = {};
    bool copied = false;
    if (SUCCEEDED(resource.As(&source))) {
        source->GetDesc(&sourceDescription);
        if (ensureStagingTexture(sourceDescription)) {
            std::vector<RECT> dirtyRects;
            if (frameInfo.TotalMetadataBufferSize > 0) {
                dirtyRects.resize(frameInfo.TotalMetadataBufferSize / sizeof(RECT) + 1);
                UINT bytes = frameInfo.TotalMetadataBufferSize;
                if (FAILED(m_duplication->GetFrameDirtyRects(
                               static_cast<UINT>(dirtyRects.size() * sizeof(RECT)),
                               dirtyRects.data(), &bytes))) {
                    dirtyRects.clear();
                } else {
                    dirtyRects.resize(bytes / sizeof(RECT));
                }
            }
            if (dirtyRects.empty()) {
                dirtyRects.push_back(RECT{0, 0, static_cast<LONG>(sourceDescription.Width),
                                          static_cast<LONG>(sourceDescription.Height)});
            }
            for (const RECT &dirty : dirtyRects) {
                D3D11_BOX box{static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
                              static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1};
                m_context->CopySubresourceRegion(m_staging.Get(), 0,
                    static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
                    source.Get(), 0, &box);
            }
            QElapsedTimer gpuTimer;
            gpuTimer.start();
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                copied = copyMappedFrame(frameInfo, mapped,
                                         QSize(static_cast<int>(sourceDescription.Width),
                                               static_cast<int>(sourceDescription.Height)),
                                         dirtyRects);
                m_context->Unmap(m_staging.Get(), 0);
                if (timing) timing->gpuToCpuUs = gpuTimer.nsecsElapsed() / 1000;
            }
        }
    }
    m_duplication->ReleaseFrame();
    if (!copied) {
        return false;
    }
    *image = m_lastImage;
    if (timing) timing->captureUs = totalTimer.nsecsElapsed() / 1000;
    return true;
}

#else

DxgiDesktopCapture::DxgiDesktopCapture() = default;
DxgiDesktopCapture::~DxgiDesktopCapture() = default;
bool DxgiDesktopCapture::capture(const QString &, const QRect &, QImage *, DesktopCaptureTiming *) { return false; }
void DxgiDesktopCapture::reset() {}

#endif
