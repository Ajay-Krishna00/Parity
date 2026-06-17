#include "netgame.h"
#include <chrono>
#include <cstring>

namespace parity {

#pragma pack(push, 1)
struct Packet {
    uint16_t magic;       // 'PA'
    uint8_t  version;     // 1
    uint8_t  sender;      // sending player index (0/1)
    uint32_t startFrame;  // frame of inputs[0]
    uint8_t  count;       // valid entries in inputs[]
    uint8_t  _pad[3];
    uint32_t csFrame;     // frame the checksum is for
    uint64_t csHash;      // sender's state hash at csFrame
    uint32_t tSend;       // sender clock (ms) at send
    uint32_t tEcho;       // most recent tSend the sender received from us
    Input    inputs[NetGame::INPUT_WINDOW];
};
#pragma pack(pop)

static const uint16_t MAGIC = 0x5041; // 'PA'

uint32_t NetGame::nowMs() const {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool NetGame::start(int local, uint16_t localPort,
                    const std::string& remoteIp, uint16_t remotePort) {
    localPlayer = local;
    sess.init(local);
    for (int i = 0; i < RollbackSession::RING; ++i) haveLocal[i] = false;
    st = NetStats();
    startMs = nowMs();

    if (!peer.open(localPort)) return false;
    peer.setRemote(remoteIp, remotePort);
    return true;
}

void NetGame::poll() {
    Packet p;
    int n;
    while ((n = peer.recv(&p, sizeof p)) > 0) {
        if (n < (int)sizeof(Packet) || p.magic != MAGIC || p.version != 1) continue;
        if (p.sender == localPlayer) continue; // ignore our own (loopback echoes)

        st.connected = true;
        st.packetsRecv++;

        // Feed every input in the redundant window; addRemoteInput dedups and
        // rolls back on mispredictions automatically.
        uint32_t cnt = p.count > INPUT_WINDOW ? INPUT_WINDOW : p.count;
        for (uint32_t i = 0; i < cnt; ++i)
            sess.addRemoteInput(p.startFrame + i, p.inputs[i]);

        // Round-trip time: the peer echoed back one of our own send-timestamps.
        if (p.tEcho != 0) {
            uint32_t now = nowMs();
            st.rttMs = (int)(now - p.tEcho);
        }
        lastPeerTSend = p.tSend; // echo this in our next packet

        // Desync detection: compare hashes once we both have the same confirmed frame.
        if (p.csFrame != 0 && sess.hasHash(p.csFrame) && sess.confirmedFrame >= p.csFrame) {
            if (sess.hashAt(p.csFrame) != p.csHash && !st.desync) {
                st.desync = true;
                st.desyncFrame = p.csFrame;
            }
        }
    }
}

void NetGame::sendPacket() {
    Packet p;
    memset(&p, 0, sizeof p);
    p.magic = MAGIC;
    p.version = 1;
    p.sender = (uint8_t)localPlayer;

    // Send the most recent INPUT_WINDOW local inputs (redundancy vs packet loss).
    uint32_t latest = sess.frame();           // we have local inputs for [0, latest)
    uint32_t count = latest < INPUT_WINDOW ? latest : INPUT_WINDOW;
    uint32_t start = latest - count;
    p.startFrame = start;
    p.count = (uint8_t)count;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t f = start + i;
        p.inputs[i] = haveLocal[f % RollbackSession::RING] ? localBuf[f % RollbackSession::RING] : 0;
    }

    p.csFrame = sess.confirmedFrame;
    p.csHash  = sess.hasHash(sess.confirmedFrame) ? sess.hashAt(sess.confirmedFrame) : 0;
    p.tSend   = nowMs();
    p.tEcho   = lastPeerTSend;

    peer.send(&p, sizeof p);
    st.packetsSent++;
}

bool NetGame::update(Input localInput) {
    poll();

    uint32_t localFrame = sess.frame();

    // Prediction window: never run more than (MAX_ROLLBACK-2) frames ahead of the
    // newest confirmed remote input, or a late packet couldn't be rolled back in.
    const uint32_t margin = RollbackSession::MAX_ROLLBACK - 2;
    uint32_t allowed = (sess.haveRemoteHead ? sess.remoteHead : 0) + margin;

    bool stepped = false;
    if (localFrame <= allowed) {
        int r = localFrame % RollbackSession::RING;
        localBuf[r]   = localInput;
        haveLocal[r]  = true;
        sess.addLocalInput(localFrame, localInput);
        sess.tick();
        stepped = true;
    } else {
        st.stalls++;
    }

    sendPacket(); // always send (carries inputs, acks, ping echo) even when stalled

    // refresh stats
    st.localFrame   = sess.frame();
    st.remoteFrame  = sess.haveRemoteHead ? sess.remoteHead : 0;
    st.confirmed    = sess.confirmedFrame;
    st.lastRollback = sess.lastRollbackFrames;
    st.maxRollback  = sess.maxRollbackFrames;
    st.mispredicts  = sess.mispredictions;
    return stepped;
}

} // namespace parity
