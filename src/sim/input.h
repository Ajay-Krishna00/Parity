#pragma once
#include <cstdint>

namespace parity {

// One frame of input for one player, packed into a single byte so it is trivial
// and cheap to send over the wire and to store in long input histories.
typedef uint8_t Input;

enum InputBits {
    IN_LEFT   = 1 << 0,
    IN_RIGHT  = 1 << 1,
    IN_JUMP   = 1 << 2,
    IN_ATTACK = 1 << 3,
    IN_PARRY  = 1 << 4,
};

inline bool held(Input in, InputBits b) { return (in & b) != 0; }

} // namespace parity
