QT += core gui
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = h264codec_smoke

!msvc {
    error("Run this smoke test with Qt 6.11.1 MSVC 2022 x64.")
}

SOURCES += \
    $$PWD/h264codec_smoke.cpp \
    $$PWD/../src/VideoApi/h264codec.cpp \
    $$PWD/../src/diagnostics.cpp

HEADERS += $$PWD/../include/VideoApi/h264codec.h $$PWD/../include/diagnostics.h

FFMPEG_ROOT = $$PWD/../third_party/ffmpeg-master-latest-win64-lgpl-shared
INCLUDEPATH += $$PWD/../include $$PWD/../include/VideoApi $$FFMPEG_ROOT/include
LIBS += $$FFMPEG_ROOT/lib/avcodec.lib \
    $$FFMPEG_ROOT/lib/avutil.lib \
    $$FFMPEG_ROOT/lib/swscale.lib
