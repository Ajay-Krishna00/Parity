#pragma once
#include "rollback.h"
#include "transport.h"
#include <string>
#include <cstdint>

namespace parity {

// Stats surfaced to the debug HUD (and your portfolio screenshots).
struct NetStats {
    bool     connected   = false;
    int      rttMs       = 0;     // round-trip time
    uint32_t localFrame  = 0;
    uint32_t remoteFrame = 0;     // highest real remote input frame
    uint32_t confirmed   = 0;     // highest frame both inputs are real
    uint32_t lastRollback = 0;
    uint32_t maxRollback  = 0;
    uint32_t mispredicts  = 0;
    uint32_t stalls       = 0;    // frames we waited for the peer
    uint32_t packetsSent  = 0;
    uint32_t packetsRecv  = 0;
    bool     desync       = false;
    uint32_t desyncFrame  = 0;
};

// Ties local input + UDP transport + RollbackSession together into a P2P match.
// One instance per player. The same code runs on loopback (127.0.0.1, two
// windows) and across machines on a LAN / the internet — only the IPs differ.
class NetGame {
public:
    static const int INPUT_WINDOW = 32; // inputs resent per packet (loss tolerance)

    bool start(int localPlayer, uint16_t localPort,
               const std::string& remoteIp, uint16_t remotePort);

    // Advance at most one simulation frame. Returns true if a frame was stepped,
    // false if we stalled waiting for the peer (so the caller can render & retry).
    bool update(Input localInput);

    const GameState& state() const { return sess.state(); }
    const NetStats&  stats() const { return st; }
    void shutdown() { peer.close(); }

private:
    RollbackSession sess;
    UdpPeer         peer;
    int             localPlayer = 0;
    NetStats        st;

    Input    localBuf[RollbackSession::RING];
    bool     haveLocal[RollbackSession::RING];

    uint32_t lastPeerTSend = 0;   // most recent peer send-timestamp, echoed back
    uint32_t startMs       = 0;

    uint32_t nowMs() const;
    void poll();
    void sendPacket();
};

} // namespace parity
