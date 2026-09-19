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
    $$PWD/../VideoApi/h264codec.cpp

HEADERS += $$PWD/../VideoApi/h264codec.h

FFMPEG_ROOT = $$PWD/../third_party/ffmpeg-master-latest-win64-lgpl-shared
INCLUDEPATH += $$PWD/../VideoApi $$FFMPEG_ROOT/include
LIBS += $$FFMPEG_ROOT/lib/avcodec.lib \
    $$FFMPEG_ROOT/lib/avutil.lib \
    $$FFMPEG_ROOT/lib/swscale.lib
