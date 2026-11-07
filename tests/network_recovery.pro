QT += core gui widgets multimedia network

CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = network_recovery
DEFINES += AUDIO_VIDEO_TESTING

SOURCES += \
    network_recovery.cpp \
    ../src/ckernal.cpp \
    ../src/diagnostics.cpp \
    ../src/web_client.cpp \
    ../src/logindialog.cpp \
    ../src/demodialog.cpp \
    ../src/roomdialog.cpp \
    ../src/setuserdialog.cpp \
    ../src/userprofiledialog.cpp \
    ../src/usershow.cpp \
    ../src/workthread.cpp

HEADERS += \
    ../include/ckernal.h \
    ../include/demodialog.h \
    ../include/diagnostics.h \
    ../include/logindialog.h \
    ../include/roomdialog.h \
    ../include/setuserdialog.h \
    ../include/userprofiledialog.h \
    ../include/usershow.h \
    ../include/workthread.h

FORMS += \
    ../demodialog.ui \
    ../logindialog.ui \
    ../roomdialog.ui \
    ../setuserdialog.ui \
    ../usershow.ui

RESOURCES += ../resources.qrc

INCLUDEPATH += \
    ../include \
    ../include/netapi/net \
    ../include/netapi/mediator \
    ../include/netapi/gateway \
    ../include/MD5 \
    ../include/uiapi \
    ../include/AudioApi \
    ../include/VideoApi
include(../netapi/netapi.pri)
include(../MD5/MD5.pri)
include(../uiapi/uiapi.pri)
include(../AudioApi/audioapi.pri)
include(../VideoApi/videoapi.pri)
