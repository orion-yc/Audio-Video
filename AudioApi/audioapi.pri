QT += multimedia

!msvc {
    error("Audio-Video is built with Qt 6.11.1 MSVC 2022 x64 only.")
}

VCPKG_ROOT = $$PWD/../third_party/vcpkg/installed/x64-windows
INCLUDEPATH += $$VCPKG_ROOT/include
LIBS += $$VCPKG_ROOT/lib/speex.lib ole32.lib uuid.lib


HEADERS += \
    $$PWD/../include/AudioApi/audio_common.h \
    $$PWD/../include/AudioApi/audio_read.h \
    $$PWD/../include/AudioApi/system_audio_read.h \
    $$PWD/../include/AudioApi/audio_write.h

SOURCES += \
    $$PWD/../src/AudioApi/audio_read.cpp \
    $$PWD/../src/AudioApi/system_audio_read.cpp \
    $$PWD/../src/AudioApi/audio_write.cpp

INCLUDEPATH += $$PWD/../include/AudioApi
