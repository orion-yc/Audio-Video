QT += multimedia

msvc {
    VCPKG_ROOT = $$PWD/../third_party/vcpkg/installed/x64-windows
    INCLUDEPATH += $$VCPKG_ROOT/include
    LIBS += $$VCPKG_ROOT/lib/speex.lib ole32.lib uuid.lib
} else {
    SPEEX_ROOT = E:/perject/Speex-1.2.1-MinGW64/install
    INCLUDEPATH += $$SPEEX_ROOT/include
    LIBS += -L$$SPEEX_ROOT/lib -lspeex -lole32 -luuid
}


HEADERS += \
    $$PWD/audio_common.h \
    $$PWD/audio_read.h \
    $$PWD/system_audio_read.h \
    $$PWD/audio_write.h

SOURCES += \
    $$PWD/audio_read.cpp \
    $$PWD/system_audio_read.cpp \
    $$PWD/audio_write.cpp
