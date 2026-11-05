HEADERS += \
    $$PWD/desk_read.h \
    $$PWD/dxgi_desktop_capture.h \
    $$PWD/h264codec.h \
    $$PWD/myfacedetect.h \
    $$PWD/video_common.h \
    $$PWD/video_decode_worker.h \
    $$PWD/rtc_session.h \
    $$PWD/video_encode_thread.h \
    $$PWD/video_read.h

SOURCES += \
    $$PWD/desk_read.cpp \
    $$PWD/dxgi_desktop_capture.cpp \
    $$PWD/h264codec.cpp \
    $$PWD/myfacedetect.cpp \
    $$PWD/video_decode_worker.cpp \
    $$PWD/rtc_session.cpp \
    $$PWD/video_encode_thread.cpp \
    $$PWD/video_read.cpp

!msvc {
    error("Audio-Video is built with Qt 6.11.1 MSVC 2022 x64 only.")
}

VCPKG_ROOT = $$PWD/../third_party/vcpkg/installed/x64-windows
INCLUDEPATH += $$VCPKG_ROOT/include/opencv4

OPENCV_WORLD_LIB = $$files($$VCPKG_ROOT/lib/opencv_world*.lib)
isEmpty(OPENCV_WORLD_LIB) {
    error("Bundled MSVC OpenCV library was not found. Restore third_party dependencies.")
}
LIBS += $$OPENCV_WORLD_LIB
FFMPEG_ROOT = $$PWD/../third_party/ffmpeg-master-latest-win64-lgpl-shared
INCLUDEPATH += $$FFMPEG_ROOT/include
LIBS += $$FFMPEG_ROOT/lib/avcodec.lib \
    $$FFMPEG_ROOT/lib/avutil.lib \
    $$FFMPEG_ROOT/lib/swscale.lib

win32:LIBS += d3d11.lib dxgi.lib user32.lib

# Pin the native SDK ABI. The release script installs this verified bundle.
isEmpty(LIVEKIT_SDK_ROOT) {
    LIVEKIT_SDK_ROOT = $$PWD/../third_party/livekit-1.13.0/livekit-sdk-windows-x64-1.13.0
}
!exists($$LIVEKIT_SDK_ROOT/include/livekit/encoded_video_source.h) {
    error("Run packaging/install-livekit-sdk.ps1 before building.")
}
INCLUDEPATH += $$LIVEKIT_SDK_ROOT/include
LIBS += $$LIVEKIT_SDK_ROOT/lib/livekit.lib
