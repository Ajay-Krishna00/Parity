#include "rollback.h"
#include "../sim/hash.h"

namespace parity {

void RollbackSession::init(int local) {
    localPlayer  = local;
    remotePlayer = 1 - local;
    currentFrame = 0;
    initState(current);

    for (int p = 0; p < 2; ++p)
        for (int f = 0; f < RING; ++f) {
            inUsed[p][f]    = 0;
            realStamp[p][f] = -1;
        }
    for (int f = 0; f < RING; ++f) hashStamp[f] = -1;

    lastRollbackFrames = maxRollbackFrames = mispredictions = 0;
    confirmedFrame = remoteHead = 0;
    haveRemoteHead = false;
}

bool RollbackSession::isReal(int player, uint32_t frame) const {
    return realStamp[player][frame % RING] == (int64_t)frame;
}

void RollbackSession::addLocalInput(uint32_t frame, Input in) {
    int r = frame % RING;
    inUsed[localPlayer][r]    = in;
    realStamp[localPlayer][r] = (int64_t)frame; // local input is always confirmed
}

// Prediction policy: assume the remote keeps doing whatever they did on the
// previous frame. We read the previous frame's USED input (real or predicted),
// so predictions chain deterministically through a re-simulation.
Input RollbackSession::predictRemote(uint32_t frame) const {
    if (frame == 0) return 0;
    return inUsed[remotePlayer][(frame - 1) % RING];
}

void RollbackSession::simulateFrame(uint32_t frame) {
    int r = frame % RING;
    saved[r] = current; // snapshot BEFORE stepping, so we can roll back to here

    Input localIn  = inUsed[localPlayer][r];
    Input remoteIn = isReal(remotePlayer, frame) ? inUsed[remotePlayer][r]
                                                 : predictRemote(frame);
    inUsed[remotePlayer][r] = remoteIn; // store what we USED (real or predicted)

    Input in0 = (localPlayer == 0) ? localIn : remoteIn;
    Input in1 = (localPlayer == 0) ? remoteIn : localIn;

    stepState(current, in0, in1);
    currentFrame = frame + 1;

    frameHash[r] = hashState(current);
    hashStamp[r] = (int64_t)frame;
}

void RollbackSession::tick() {
    simulateFrame(currentFrame);
    updateConfirmed();
}

void RollbackSession::addRemoteInput(uint32_t frame, Input in) {
    int r = frame % RING;

    if (!haveRemoteHead || frame > remoteHead) { remoteHead = frame; haveRemoteHead = true; }

    Input usedBefore = inUsed[remotePlayer][r]; // what we simulated this frame with

    // Record the confirmed remote input.
    inUsed[remotePlayer][r]    = in;
    realStamp[remotePlayer][r] = (int64_t)frame;

    // Not yet simulated -> it'll simply be used when we reach that frame.
    if (frame >= currentFrame) { updateConfirmed(); return; }

    // Already simulated and our prediction matched -> nothing to redo.
    if (usedBefore == in) { updateConfirmed(); return; }

    // Misprediction: roll back to `frame` and re-simulate forward to where we were.
    uint32_t resimTarget = currentFrame;
    uint32_t depth = currentFrame - frame;

    mispredictions++;
    lastRollbackFrames = depth;
    if (depth > maxRollbackFrames) maxRollbackFrames = depth;

    current = saved[r];           // restore snapshot taken before `frame`
    currentFrame = frame;
    while (currentFrame < resimTarget) simulateFrame(currentFrame);

    updateConfirmed();
}

void RollbackSession::updateConfirmed() {
    uint32_t f = confirmedFrame;
    while (f < currentFrame && isReal(0, f) && isReal(1, f)) ++f;
    confirmedFrame = f;
}

uint64_t RollbackSession::hashAt(uint32_t f) const {
    return frameHash[f % RING];
}

bool RollbackSession::hasHash(uint32_t f) const {
    return hashStamp[f % RING] == (int64_t)f;
}

} // namespace parity
