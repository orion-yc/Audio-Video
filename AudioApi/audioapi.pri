QT += multimedia

!msvc {
    error("Audio-Video is built with Qt 6.11.1 MSVC 2022 x64 only.")
}

VCPKG_ROOT = $$PWD/../third_party/vcpkg/installed/x64-windows
INCLUDEPATH += $$VCPKG_ROOT/include
LIBS += $$VCPKG_ROOT/lib/speex.lib ole32.lib uuid.lib


HEADERS += \
    $$PWD/audio_common.h \
    $$PWD/audio_read.h \
    $$PWD/system_audio_read.h \
    $$PWD/audio_write.h

SOURCES += \
    $$PWD/audio_read.cpp \
    $$PWD/system_audio_read.cpp \
    $$PWD/audio_write.cpp
