#include "discovery.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

namespace parity {

namespace {
    const uint16_t BEACON_MAGIC = 0x5042; // 'PB' (Parity Beacon)

    #pragma pack(push, 1)
    struct Beacon {
        uint16_t magic;
        uint8_t  version;
        uint8_t  _pad;
        uint16_t gamePort;
        char     name[32];   // NUL-terminated
    };
    #pragma pack(pop)

    bool wsaStart() {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
}

bool LanDiscovery::startHost(const std::string& name, uint16_t gamePort) {
    if (!wsaStart()) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;

    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);

    // Bind to an ephemeral local port; we only send from this socket.
    sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    bind(s, (sockaddr*)&local, sizeof local);

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    sock = (uintptr_t)s;
    isHost = true;
    myName = name.substr(0, 31);
    myGamePort = gamePort;
    lastBeaconMs = 0;
    return true;
}

bool LanDiscovery::startBrowse() {
    if (!wsaStart()) return false;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;

    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);

    sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(DISCOVERY_PORT);
    if (bind(s, (sockaddr*)&local, sizeof local) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return false;
    }

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    sock = (uintptr_t)s;
    isHost = false;
    hostList.clear();
    return true;
}

void LanDiscovery::broadcast() {
    if (sock == ~(uintptr_t)0) return;
    Beacon b;
    memset(&b, 0, sizeof b);
    b.magic = BEACON_MAGIC;
    b.version = 1;
    b.gamePort = myGamePort;
    strncpy(b.name, myName.c_str(), sizeof(b.name) - 1);

    sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(DISCOVERY_PORT);

    // Limited broadcast reaches every device on the LAN / hotspot...
    dst.sin_addr.s_addr = INADDR_BROADCAST; // 255.255.255.255
    sendto((SOCKET)sock, (const char*)&b, sizeof b, 0, (sockaddr*)&dst, sizeof dst);

    // ...and a loopback copy so host+browser on ONE machine still find each other.
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    sendto((SOCKET)sock, (const char*)&b, sizeof b, 0, (sockaddr*)&dst, sizeof dst);
}

void LanDiscovery::receive(uint32_t nowMs) {
    if (sock == ~(uintptr_t)0) return;
    Beacon b;
    sockaddr_in from;
    int fromLen = sizeof from;
    int n;
    while ((n = recvfrom((SOCKET)sock, (char*)&b, sizeof b, 0,
                         (sockaddr*)&from, &fromLen)) > 0) {
        if (n < (int)sizeof(Beacon) || b.magic != BEACON_MAGIC || b.version != 1)
            continue;
        b.name[sizeof(b.name) - 1] = '\0';

        char ipbuf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ipbuf, sizeof ipbuf);
        std::string ip = ipbuf;
        uint16_t port = b.gamePort;

        // One host can reach us twice on a single machine: once via the subnet
        // broadcast (source = the host's LAN IP) and once via the 127.0.0.1 copy.
        // Dedup by name+port so a host shows once, and prefer its real LAN IP
        // (that's what a second device must connect to, and it reads better).
        bool found = false;
        for (auto& h : hostList) {
            if (h.name == b.name && h.port == port) {
                h.lastSeenMs = nowMs;
                if (h.ip == "127.0.0.1" && ip != "127.0.0.1") h.ip = ip; // upgrade
                found = true;
                break;
            }
        }
        if (!found) {
            HostEntry e;
            e.name = b.name;
            e.ip = ip;
            e.port = port;
            e.lastSeenMs = nowMs;
            hostList.push_back(e);
        }
    }
}

void LanDiscovery::expire(uint32_t nowMs) {
    for (size_t i = 0; i < hostList.size();) {
        if (nowMs - hostList[i].lastSeenMs > EXPIRY_MS)
            hostList.erase(hostList.begin() + i);
        else
            ++i;
    }
}

void LanDiscovery::tick(uint32_t nowMs) {
    if (sock == ~(uintptr_t)0) return;
    if (isHost) {
        if (nowMs - lastBeaconMs >= BEACON_INTERVAL_MS) {
            broadcast();
            lastBeaconMs = nowMs;
        }
    } else {
        receive(nowMs);
        expire(nowMs);
    }
}

std::string LanDiscovery::localIpv4() {
    bool started = wsaStart();
    std::string result = "?";
    char host[256] = {0};
    if (gethostname(host, sizeof host) == 0) {
        addrinfo hints;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        addrinfo* res = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p; p = p->ai_next) {
                sockaddr_in* a = (sockaddr_in*)p->ai_addr;
                char ipbuf[INET_ADDRSTRLEN] = {0};
                inet_ntop(AF_INET, &a->sin_addr, ipbuf, sizeof ipbuf);
                std::string ip = ipbuf;
                if (ip != "127.0.0.1") { result = ip; break; }
            }
            freeaddrinfo(res);
        }
    }
    if (started) WSACleanup();
    return result;
}

void LanDiscovery::stop() {
    if (sock != ~(uintptr_t)0) {
        closesocket((SOCKET)sock);
        sock = ~(uintptr_t)0;
        WSACleanup();
    }
}

LanDiscovery::~LanDiscovery() { stop(); }

} // namespace parity
