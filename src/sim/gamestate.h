#pragma once
#include "fixed.h"
#include "input.h"
#include <cstdint>

namespace parity {

// Action states drive frame-data (startup/active/recovery) just like a real fighter.
enum ActionState {
    ST_IDLE    = 0,
    ST_WALK    = 1,
    ST_JUMP    = 2,
    ST_ATTACK  = 3,
    ST_PARRY   = 4,
    ST_HITSTUN = 5,
};

// A fighter. Plain-old-data: trivially copyable, so saving/restoring a whole
// GameState for rollback is a single memcpy / assignment.
struct Player {
    Fixed   posX, posY;     // position (posY = height above the floor)
    Fixed   velX, velY;     // velocity per frame
    int32_t facing;         // +1 faces right, -1 faces left
    uint32_t state;         // ActionState
    uint32_t stateFrame;    // frames elapsed in the current state
    int32_t health;
    uint32_t onGround;      // 1 if standing on the floor
};

struct GameState {
    Player   players[2];
    uint32_t frame;
};

// Resets to the opening position of a round.
void initState(GameState& s);

// THE simulation. Pure: depends only on its arguments, uses only integer math,
// no allocation, no IO. Re-running it on identical (state,in0,in1) always yields
// an identical result — the property rollback depends on.
void stepState(GameState& s, Input in0, Input in1);

} // namespace parity
