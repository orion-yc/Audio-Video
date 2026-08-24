QT += multimedia

SPEEX_ROOT = E:/perject/Speex-1.2.1-MinGW64/install
INCLUDEPATH += $$SPEEX_ROOT/include
LIBS += -L$$SPEEX_ROOT/lib -lspeex


HEADERS += \
    $$PWD/audio_common.h \
    $$PWD/audio_read.h \
    $$PWD/audio_write.h

SOURCES += \
    $$PWD/audio_read.cpp \
    $$PWD/audio_write.cpp
