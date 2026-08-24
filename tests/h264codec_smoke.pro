QT += core gui
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = h264codec_smoke

SOURCES += \
    $$PWD/h264codec_smoke.cpp \
    $$PWD/../VideoApi/h264codec.cpp

HEADERS += $$PWD/../VideoApi/h264codec.h

FFMPEG_ROOT = $$PWD/../third_party/ffmpeg-master-latest-win64-lgpl-shared
INCLUDEPATH += $$PWD/../VideoApi $$FFMPEG_ROOT/include
LIBS += -L$$FFMPEG_ROOT/lib \
    -lavcodec \
    -lavutil \
    -lswscale
