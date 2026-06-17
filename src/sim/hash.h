#pragma once
#include "gamestate.h"
#include <cstdint>

namespace parity {

// FNV-1a, 64-bit. We hash the *meaningful fields* field-by-field rather than the
// raw struct bytes on purpose: struct padding is uninitialized and would make the
// hash non-deterministic. Hashing fields keeps the checksum stable.
//
// Peers exchange this checksum each frame; the first frame whose hashes disagree
// is, by definition, the exact frame the two simulations desynced.
inline uint64_t fnv1a(uint64_t h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v & 0xFF);
        h *= 1099511628211ull;
        v >>= 8;
    }
    return h;
}

inline uint64_t hashPlayer(uint64_t h, const Player& p) {
    h = fnv1a(h, (uint64_t)(uint32_t)p.posX.raw);
    h = fnv1a(h, (uint64_t)(uint32_t)p.posY.raw);
    h = fnv1a(h, (uint64_t)(uint32_t)p.velX.raw);
    h = fnv1a(h, (uint64_t)(uint32_t)p.velY.raw);
    h = fnv1a(h, (uint64_t)(uint32_t)p.facing);
    h = fnv1a(h, (uint64_t)p.state);
    h = fnv1a(h, (uint64_t)p.stateFrame);
    h = fnv1a(h, (uint64_t)(uint32_t)p.health);
    h = fnv1a(h, (uint64_t)p.onGround);
    return h;
}

inline uint64_t hashState(const GameState& s) {
    uint64_t h = 1469598103934665603ull; // FNV offset basis
    h = hashPlayer(h, s.players[0]);
    h = hashPlayer(h, s.players[1]);
    h = fnv1a(h, (uint64_t)s.frame);
    return h;
}

} // namespace parity
