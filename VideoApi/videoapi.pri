HEADERS += \
    $$PWD/../include/VideoApi/desk_read.h \
    $$PWD/../include/VideoApi/dxgi_desktop_capture.h \
    $$PWD/../include/VideoApi/h264codec.h \
    $$PWD/../include/VideoApi/myfacedetect.h \
    $$PWD/../include/VideoApi/video_common.h \
    $$PWD/../include/VideoApi/video_decode_worker.h \
    $$PWD/../include/VideoApi/rtc_session.h \
    $$PWD/../include/VideoApi/track_publication_state.h \
    $$PWD/../include/VideoApi/video_encode_thread.h \
    $$PWD/../include/VideoApi/video_read.h

SOURCES += \
    $$PWD/../src/VideoApi/desk_read.cpp \
    $$PWD/../src/VideoApi/dxgi_desktop_capture.cpp \
    $$PWD/../src/VideoApi/h264codec.cpp \
    $$PWD/../src/VideoApi/myfacedetect.cpp \
    $$PWD/../src/VideoApi/video_decode_worker.cpp \
    $$PWD/../src/VideoApi/rtc_session.cpp \
    $$PWD/../src/VideoApi/track_publication_state.cpp \
    $$PWD/../src/VideoApi/video_encode_thread.cpp \
    $$PWD/../src/VideoApi/video_read.cpp

INCLUDEPATH += $$PWD/../include/VideoApi

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
