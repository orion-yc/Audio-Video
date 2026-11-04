#ifndef DXGI_DESKTOP_CAPTURE_H
#define DXGI_DESKTOP_CAPTURE_H

#include <QImage>
#include <QString>
#include <vector>

#ifdef Q_OS_WIN
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

class DxgiDesktopCapture
{
public:
    DxgiDesktopCapture();
    ~DxgiDesktopCapture();

    bool capture(const QString &outputName, QImage *image);
    void reset();

private:
#ifdef Q_OS_WIN
    bool initialize(const QString &outputName);
    bool ensureStagingTexture(const D3D11_TEXTURE2D_DESC &sourceDescription);
    bool copyMappedFrame(const DXGI_OUTDUPL_FRAME_INFO &frameInfo,
                         const D3D11_MAPPED_SUBRESOURCE &mapped,
                         const QSize &size, const std::vector<RECT> &dirtyRects);

    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_duplication;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_staging;
    QString m_outputName;
    QSize m_size;
    QImage m_lastImage;
#endif
};

#endif // DXGI_DESKTOP_CAPTURE_H
