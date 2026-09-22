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

win32:LIBS += d3d11.lib dxgi.lib

# The LiveKit C++ SDK is optional during the migration. A root directory alone
# never enables the client: LIVEKIT_SDK_ADAPTER must identify a compiled,
# ABI-matched adapter so an arbitrary library cannot be mistaken for WebRTC.
isEmpty(LIVEKIT_SDK_ROOT) {
    message("LiveKit C++ SDK not configured; legacy TCP transport remains available.")
} else {
    isEmpty(LIVEKIT_SDK_ADAPTER) {
        warning("LIVEKIT_SDK_ROOT supplied without LIVEKIT_SDK_ADAPTER; native WebRTC remains disabled.")
    } else {
        DEFINES += AUDIO_VIDEO_LIVEKIT_SDK AUDIO_VIDEO_LIVEKIT_NATIVE_ADAPTER
        INCLUDEPATH += $$LIVEKIT_SDK_ROOT/include
        INCLUDEPATH += $$LIVEKIT_SDK_ADAPTER/include
        SOURCES += $$LIVEKIT_SDK_ADAPTER/livekit_session_adapter.cpp
        HEADERS += $$LIVEKIT_SDK_ADAPTER/livekit_session_adapter.h
        LIBS += -L$$LIVEKIT_SDK_ROOT/lib -llivekit
    }
}
