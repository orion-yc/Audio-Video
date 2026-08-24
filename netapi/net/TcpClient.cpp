#include "TcpClient.h"

#include "INetMediator.h"
#include "packdef.h"

#include <memory>
#include <new>
#include <vector>

namespace {

const long kConnectTimeoutMs = 1000;

bool connectWithTimeout(SOCKET socket, const sockaddr* address, int addressLength)
{
    u_long nonBlocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        return false;
    }

    int result = connect(socket, address, addressLength);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS &&
                error != WSAEINVAL) {
            return false;
        }

        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(socket, &writeSet);
        FD_SET(socket, &errorSet);

        timeval timeout;
        timeout.tv_sec = kConnectTimeoutMs / 1000;
        timeout.tv_usec = (kConnectTimeoutMs % 1000) * 1000;
        result = select(0, NULL, &writeSet, &errorSet, &timeout);
        if (result <= 0 || FD_ISSET(socket, &errorSet)) {
            return false;
        }

        int socketError = 0;
        int optionLength = sizeof(socketError);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&socketError),
                       &optionLength) == SOCKET_ERROR || socketError != 0) {
            return false;
        }
    }

    nonBlocking = 0;
    return ioctlsocket(socket, FIONBIO, &nonBlocking) != SOCKET_ERROR;
}

bool recvExact(SOCKET socket, char* buffer, int length)
{
    int received = 0;
    while (received < length) {
        int result = recv(socket, buffer + received, length - received, 0);
        if (result > 0) {
            received += result;
            continue;
        }
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool sendExact(SOCKET socket, const char* buffer, int length)
{
    int sent = 0;
    while (sent < length) {
        int result = send(socket, buffer + sent, length - sent, 0);
        if (result > 0) {
            sent += result;
            continue;
        }
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

TcpClient::TcpClient(INetMediator* mediator)
    : m_sock(INVALID_SOCKET),
      m_hThreadHandle(NULL),
      m_threadId(0),
      m_isStop(false),
      m_isConnected(false),
      m_wsaStarted(false)
{
    m_pMediator = mediator;
}

TcpClient::~TcpClient()
{
    UnInitNet();
}

bool TcpClient::InitNet(const char* host, unsigned short port)
{
    if (!host || !*host || port == 0) {
        return false;
    }

    if (!m_wsaStarted) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0 || LOBYTE(wsaData.wVersion) != 2 ||
                HIBYTE(wsaData.wVersion) != 2) {
            if (result == 0) {
                WSACleanup();
            }
            return false;
        }
        m_wsaStarted = true;
    }

    char portText[6] = {0};
    _snprintf_s(portText, sizeof(portText), _TRUNCATE, "%hu", port);
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = NULL;
    if (getaddrinfo(host, portText, &hints, &addresses) != 0) {
        UnInitNet();
        return false;
    }

    for (addrinfo* address = addresses; address; address = address->ai_next) {
        SOCKET candidate = socket(address->ai_family,
                                  address->ai_socktype,
                                  address->ai_protocol);
        if (candidate == INVALID_SOCKET) {
            continue;
        }
        if (connectWithTimeout(candidate, address->ai_addr,
                               static_cast<int>(address->ai_addrlen))) {
            m_sock = candidate;
            break;
        }
        closesocket(candidate);
    }
    freeaddrinfo(addresses);

    if (m_sock == INVALID_SOCKET) {
        UnInitNet();
        return false;
    }

    int noDelay = 1;
    setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<char*>(&noDelay), sizeof(noDelay));
    DWORD sendTimeoutMs = 15000;
    setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<char*>(&sendTimeoutMs), sizeof(sendTimeoutMs));

    m_isStop = false;
    m_isConnected = true;
    m_hThreadHandle = reinterpret_cast<HANDLE>(_beginthreadex(
            NULL, 0, &RecvThread, this, 0, &m_threadId));
    if (!m_hThreadHandle) {
        UnInitNet();
        return false;
    }
    return true;
}

unsigned int __stdcall TcpClient::RecvThread(void* context)
{
    static_cast<TcpClient*>(context)->RecvData();
    return 0;
}

void TcpClient::UnInitNet()
{
    m_isStop = true;
    m_isConnected = false;

    {
        std::lock_guard<std::mutex> guard(m_sendMutex);
        if (m_sock != INVALID_SOCKET) {
            shutdown(m_sock, SD_BOTH);
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    if (m_hThreadHandle) {
        if (GetCurrentThreadId() != m_threadId) {
            WaitForSingleObject(m_hThreadHandle, 2000);
        }
        CloseHandle(m_hThreadHandle);
        m_hThreadHandle = NULL;
        m_threadId = 0;
    }

    if (m_wsaStarted) {
        WSACleanup();
        m_wsaStarted = false;
    }
}

bool TcpClient::SendData(unsigned int, char* data, int length)
{
    if (!data || length < static_cast<int>(sizeof(PackType)) ||
            length > _DEF_MAX_PACKET_SIZE) {
        return false;
    }

    const int frameLength = length + static_cast<int>(sizeof(int));
    std::vector<char> frame(static_cast<size_t>(frameLength));
    memcpy(&frame[0], &length, sizeof(length));
    memcpy(&frame[sizeof(length)], data, static_cast<size_t>(length));

    std::lock_guard<std::mutex> guard(m_sendMutex);
    if (!m_isConnected || m_sock == INVALID_SOCKET ||
            !sendExact(m_sock, &frame[0], frameLength)) {
        m_isConnected = false;
        return false;
    }
    return true;
}

void TcpClient::RecvData()
{
    while (!m_isStop) {
        int packetSize = 0;
        if (!recvExact(m_sock, reinterpret_cast<char*>(&packetSize), sizeof(packetSize))) {
            break;
        }
        if (packetSize < static_cast<int>(sizeof(PackType)) ||
                packetSize > _DEF_MAX_PACKET_SIZE) {
            break;
        }

        std::unique_ptr<char[]> packet(new (std::nothrow) char[packetSize]);
        if (!packet || !recvExact(m_sock, packet.get(), packetSize)) {
            break;
        }
        m_pMediator->DealData(static_cast<unsigned int>(m_sock),
                              packet.release(), packetSize);
    }

    bool notify = !m_isStop.exchange(true);
    m_isConnected = false;
    {
        std::lock_guard<std::mutex> guard(m_sendMutex);
        if (m_sock != INVALID_SOCKET) {
            shutdown(m_sock, SD_BOTH);
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }
    if (notify) {
        m_pMediator->disConnect();
    }
}

bool TcpClient::IsConnected()
{
    return m_isConnected;
}
