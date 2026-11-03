#include "dxgi_desktop_capture.h"

#ifdef Q_OS_WIN

#include <QRect>

#include <algorithm>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

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

bool DxgiDesktopCapture::initialize(const QString &outputName)
{
    if (m_duplication && (outputName.isEmpty() || outputName == m_outputName)) {
        return true;
    }
    reset();

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceResult = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &m_device, &featureLevel, &m_context);
    if (FAILED(deviceResult)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(m_device.As(&dxgiDevice))) {
        reset();
        return false;
    }
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
        reset();
        return false;
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIOutput> output;
        if (adapter->EnumOutputs(index, &output) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!output) {
            continue;
        }
        DXGI_OUTPUT_DESC description = {};
        if (FAILED(output->GetDesc(&description))) {
            continue;
        }
        const QString deviceName = QString::fromWCharArray(description.DeviceName);
        if (!outputName.isEmpty() && outputName != deviceName &&
            !outputName.contains(deviceName) && !deviceName.contains(outputName)) {
            continue;
        }

        ComPtr<IDXGIOutput1> output1;
        if (FAILED(output.As(&output1)) ||
            FAILED(output1->DuplicateOutput(m_device.Get(), &m_duplication))) {
            reset();
            return false;
        }
        m_outputName = deviceName;
        return true;
    }
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
                                         const QSize &size)
{
    if (m_lastImage.isNull()) {
        return false;
    }

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
        dirtyRects.push_back(RECT{0, 0, size.width(), size.height()});
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

bool DxgiDesktopCapture::capture(const QString &outputName, QImage *image)
{
    if (!image || !initialize(outputName)) {
        return false;
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    const HRESULT acquireResult = m_duplication->AcquireNextFrame(0, &frameInfo, &resource);
    if (acquireResult == DXGI_ERROR_WAIT_TIMEOUT) {
        if (!m_lastImage.isNull()) {
            *image = m_lastImage;
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

    // Desktop Duplication can wake for a pointer-only update. The application
    // composites the pointer itself after capture, so keep the cached desktop
    // pixels and avoid a full GPU-to-CPU frame readback in that case.
    if (frameInfo.LastPresentTime.QuadPart == 0 && !m_lastImage.isNull()) {
        m_duplication->ReleaseFrame();
        *image = m_lastImage;
        return true;
    }

    ComPtr<ID3D11Texture2D> source;
    D3D11_TEXTURE2D_DESC sourceDescription = {};
    bool copied = false;
    if (SUCCEEDED(resource.As(&source))) {
        source->GetDesc(&sourceDescription);
        if (ensureStagingTexture(sourceDescription)) {
            m_context->CopyResource(m_staging.Get(), source.Get());
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                copied = copyMappedFrame(frameInfo, mapped,
                                         QSize(static_cast<int>(sourceDescription.Width),
                                               static_cast<int>(sourceDescription.Height)));
                m_context->Unmap(m_staging.Get(), 0);
            }
        }
    }
    m_duplication->ReleaseFrame();
    if (!copied) {
        return false;
    }
    *image = m_lastImage;
    return true;
}

#else

DxgiDesktopCapture::DxgiDesktopCapture() = default;
DxgiDesktopCapture::~DxgiDesktopCapture() = default;
bool DxgiDesktopCapture::capture(const QString &, QImage *) { return false; }
void DxgiDesktopCapture::reset() {}

#endif
