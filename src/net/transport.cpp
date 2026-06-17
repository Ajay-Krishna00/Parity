#include "transport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace parity {

bool UdpPeer::wsaUp = false;
int  UdpPeer::wsaRefs = 0;

static_assert(sizeof(sockaddr_in) <= 32, "remoteAddr buffer too small");

bool UdpPeer::open(uint16_t localPort) {
    if (!wsaUp) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { err = "WSAStartup failed"; return false; }
        wsaUp = true;
    }
    ++wsaRefs;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { err = "socket() failed"; return false; }

    sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(localPort);
    if (bind(s, (sockaddr*)&local, sizeof local) == SOCKET_ERROR) {
        err = "bind() failed (port in use?)";
        closesocket(s);
        return false;
    }

    u_long nonblock = 1;
    ioctlsocket(s, FIONBIO, &nonblock);

    sock = (uintptr_t)s;
    return true;
}

void UdpPeer::setRemote(const std::string& ip, uint16_t port) {
    sockaddr_in* r = (sockaddr_in*)remoteAddr;
    memset(r, 0, sizeof(sockaddr_in));
    r->sin_family = AF_INET;
    r->sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &r->sin_addr);
    haveRemote = true;
}

int UdpPeer::send(const void* data, int len) {
    if (sock == ~(uintptr_t)0 || !haveRemote) return -1;
    return sendto((SOCKET)sock, (const char*)data, len, 0,
                  (sockaddr*)remoteAddr, sizeof(sockaddr_in));
}

int UdpPeer::recv(void* buf, int maxLen) {
    if (sock == ~(uintptr_t)0) return -1;
    sockaddr_in from;
    int fromLen = sizeof from;
    int n = recvfrom((SOCKET)sock, (char*)buf, maxLen, 0, (sockaddr*)&from, &fromLen);
    if (n == SOCKET_ERROR) {
        return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
    }
    return n;
}

void UdpPeer::close() {
    if (sock != ~(uintptr_t)0) { closesocket((SOCKET)sock); sock = ~(uintptr_t)0; }
    if (wsaUp && --wsaRefs <= 0) { WSACleanup(); wsaUp = false; wsaRefs = 0; }
}

UdpPeer::~UdpPeer() { close(); }

} // namespace parity
