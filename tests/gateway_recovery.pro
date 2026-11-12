QT += core network websockets
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = gateway_recovery
SOURCES += gateway_recovery.cpp ../src/netapi/gateway/gateway_client.cpp
HEADERS += ../include/netapi/gateway/gateway_client.h
INCLUDEPATH += ../include/netapi/gateway
