QT       += network
LIBS += -lws2_32 -lmswsock


INCLUDEPATH += $$PWD/../include/netapi/net
INCLUDEPATH += $$PWD/../include/netapi/mediator
INCLUDEPATH += $$PWD/../include/netapi/gateway

HEADERS += \
    $$PWD/../include/netapi/mediator/INetMediator.h \
    $$PWD/../include/netapi/mediator/TcpClientMediator.h \
    $$PWD/../include/netapi/mediator/TcpServerMediator.h \
    $$PWD/../include/netapi/mediator/UdpMediator.h \
    $$PWD/../include/netapi/net/INet.h \
    $$PWD/../include/netapi/net/TcpClient.h \
    $$PWD/../include/netapi/net/TcpServer.h \
    $$PWD/../include/netapi/net/UdpNet.h \
    $$PWD/../include/netapi/net/packdef.h \
    $$PWD/../include/netapi/gateway/gateway_client.h


SOURCES += \
    $$PWD/../src/netapi/mediator/INetMediator.cpp \
    $$PWD/../src/netapi/mediator/TcpClientMediator.cpp \
    $$PWD/../src/netapi/mediator/TcpServerMediator.cpp \
    $$PWD/../src/netapi/mediator/UdpMediator.cpp \
    $$PWD/../src/netapi/net/INet.cpp \
    $$PWD/../src/netapi/net/TcpClient.cpp \
    $$PWD/../src/netapi/net/TcpServer.cpp \
    $$PWD/../src/netapi/net/UdpNet.cpp \
    $$PWD/../src/netapi/gateway/gateway_client.cpp


QT += websockets
