#pragma once
#include <cstdint>
#include <string>

namespace parity {

// Thin non-blocking UDP socket over Winsock. One local port, one fixed remote
// peer (this is a 1v1 game). Windows-only — lives in the net layer, never the sim.
class UdpPeer {
public:
    bool open(uint16_t localPort);                 // bind; returns false on failure
    void setRemote(const std::string& ip, uint16_t port);
    void close();

    // Returns bytes sent, or <0 on error.
    int send(const void* data, int len);
    // Non-blocking: returns bytes received, 0 if nothing waiting, <0 on error.
    int recv(void* buf, int maxLen);

    const std::string& lastError() const { return err; }
    ~UdpPeer();

private:
    uintptr_t sock = ~(uintptr_t)0; // SOCKET (avoid leaking winsock headers here)
    bool haveRemote = false;
    // sockaddr_in stored opaquely to keep this header clean of <winsock2.h>.
    unsigned char remoteAddr[32] = {0};
    std::string err;
    static bool wsaUp;
    static int  wsaRefs;
};

} // namespace parity
