QT += core gui network websockets
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = webrtc_integration
SOURCES += webrtc_integration.cpp ../netapi/gateway/gateway_client.cpp ../VideoApi/rtc_session.cpp
HEADERS += ../netapi/gateway/gateway_client.h ../VideoApi/rtc_session.h
INCLUDEPATH += ../netapi/gateway ../VideoApi
isEmpty(LIVEKIT_SDK_ROOT): error("Pass LIVEKIT_SDK_ROOT for the pinned SDK 1.13.0")
INCLUDEPATH += $$LIVEKIT_SDK_ROOT/include
win32:LIBS += $$LIVEKIT_SDK_ROOT/lib/livekit.lib
unix:LIBS += -L$$LIVEKIT_SDK_ROOT/lib -llivekit
