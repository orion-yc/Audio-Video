QT += core gui widgets multimedia network

CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = network_recovery
DEFINES += AUDIO_VIDEO_TESTING

SOURCES += \
    network_recovery.cpp \
    ../ckernal.cpp \
    ../web_client.cpp \
    ../logindialog.cpp \
    ../demodialog.cpp \
    ../roomdialog.cpp \
    ../setuserdialog.cpp \
    ../userprofiledialog.cpp \
    ../usershow.cpp \
    ../workthread.cpp

HEADERS += \
    ../ckernal.h \
    ../demodialog.h \
    ../logindialog.h \
    ../roomdialog.h \
    ../setuserdialog.h \
    ../userprofiledialog.h \
    ../usershow.h \
    ../workthread.h

FORMS += \
    ../demodialog.ui \
    ../logindialog.ui \
    ../roomdialog.ui \
    ../setuserdialog.ui \
    ../usershow.ui

RESOURCES += ../resources.qrc

INCLUDEPATH += \
    .. \
    ../netapi \
    ../MD5 \
    ../uiapi \
    ../AudioApi \
    ../VideoApi
include(../netapi/netapi.pri)
include(../MD5/MD5.pri)
include(../uiapi/uiapi.pri)
include(../AudioApi/audioapi.pri)
include(../VideoApi/videoapi.pri)
