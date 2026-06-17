#pragma once
#include "../sim/gamestate.h"
#include "../sim/input.h"
#include <cstdint>

namespace parity {

// GGPO-style rollback session core. Engine-agnostic: knows nothing about raylib,
// sockets, or rendering. The platform/net layer feeds it local + remote inputs
// and reads back `current` to render.
//
// The idea:
//  - We must run at 60Hz locally, but the remote player's input for the current
//    frame hasn't arrived yet. So we PREDICT it (repeat their last used input)
//    and simulate forward immediately — no input-lag for the local player.
//  - When the real remote input arrives, if our prediction was wrong, we ROLL
//    BACK to that frame (restoring a saved state) and RE-SIMULATE forward with
//    the corrected input. Because stepState is deterministic, the corrected
//    re-simulation is exactly what both peers converge to.
//
// Frame numbers grow unboundedly; all per-frame arrays are ring buffers indexed
// by (frame % RING). To stay correct across wraparound we stamp each slot with
// the frame number it currently holds, so stale data from RING frames ago is
// never mistaken for a real input.
class RollbackSession {
public:
    static const int RING = 256;          // ring buffer length (frames, ~4s @60Hz)
    static const int MAX_ROLLBACK = 16;   // deepest rollback / prediction window

    void init(int localPlayer);

    // Provide this peer's input for `frame` (the frame about to be ticked).
    void addLocalInput(uint32_t frame, Input in);

    // A remote input arrived. May trigger a rollback + re-simulation if it
    // contradicts the prediction we used. Safe to call out-of-order / duplicated.
    void addRemoteInput(uint32_t frame, Input in);

    // Advance exactly one frame using local input + (real or predicted) remote.
    void tick();

    const GameState& state() const { return current; }
    uint32_t frame() const { return currentFrame; }

    // Post-frame state hash for `f` (valid for frames already simulated). Peers
    // exchange this at confirmed frames to detect desyncs to the exact frame.
    uint64_t hashAt(uint32_t f) const;
    bool     hasHash(uint32_t f) const;

    // --- live stats for the debug overlay / résumé screenshots ---
    uint32_t lastRollbackFrames = 0; // depth of the most recent rollback
    uint32_t maxRollbackFrames  = 0; // worst case seen this session
    uint32_t mispredictions     = 0; // times a prediction was wrong
    uint32_t confirmedFrame     = 0; // highest frame with both real inputs
    uint32_t remoteHead         = 0; // highest frame we have a REAL remote input for
    bool     haveRemoteHead     = false;

private:
    int localPlayer  = 0;            // 0 or 1; remote is the other index
    int remotePlayer = 1;

    GameState current;               // simulation as of `currentFrame`
    uint32_t  currentFrame = 0;      // next frame to be simulated

    // Per-frame, per-player data (ring-indexed by frame % RING):
    Input   inUsed[2][RING];         // input actually USED to sim that frame
    int64_t realStamp[2][RING];      // frame# this slot holds a CONFIRMED input for, else -1
    GameState saved[RING];           // state snapshot BEFORE frame f was simulated
    uint64_t  frameHash[RING];       // post-frame state hash
    int64_t   hashStamp[RING];       // frame# frameHash[slot] belongs to, else -1

    bool  isReal(int player, uint32_t frame) const;
    Input predictRemote(uint32_t frame) const;
    void  simulateFrame(uint32_t frame); // snapshot, step, store hash, advance
    void  updateConfirmed();
};

} // namespace parity
