QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

VERSION = 2.0.2.0
TARGET = Audio-Video
QMAKE_TARGET_PRODUCT = Audio-Video
QMAKE_TARGET_DESCRIPTION = Audio-Video conferencing client
win32:RC_ICONS = images/app-icon.ico

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    src/ckernal.cpp \
    src/diagnostics.cpp \
    src/web_client.cpp \
    src/logindialog.cpp \
    src/main.cpp \
    src/demodialog.cpp \
    src/roomdialog.cpp \
    src/setuserdialog.cpp \
    src/userprofiledialog.cpp \
    src/usershow.cpp \
    src/workthread.cpp

HEADERS += \
    include/ckernal.h \
    include/diagnostics.h \
    include/demodialog.h \
    include/logindialog.h \
    include/roomdialog.h \
    include/setuserdialog.h \
    include/userprofiledialog.h \
    include/usershow.h \
    include/workthread.h

FORMS += \
    demodialog.ui \
    logindialog.ui \
    roomdialog.ui \
    setuserdialog.ui \
    usershow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target


include(./netapi/netapi.pri)
include(./MD5/MD5.pri)
include(./uiapi/uiapi.pri)
include(./AudioApi/audioapi.pri)
include(./VideoApi/videoapi.pri)
INCLUDEPATH += $$PWD/include \
            $$PWD/include/netapi/net \
            $$PWD/include/netapi/mediator \
            $$PWD/include/netapi/gateway \
            $$PWD/include/MD5 \
            $$PWD/include/uiapi \
            $$PWD/include/AudioApi \
            $$PWD/include/VideoApi


RESOURCES += \
    resources.qrc


