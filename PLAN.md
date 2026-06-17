# PARITY — Rollback Netcode Fighter · Implementation Plan

A minimalist 1v1 platform-fighter built as a vehicle to demonstrate **peer-to-peer
deterministic rollback netcode** (GGPO-style). The game is deliberately simple so the
*netcode is the star*.

## Core architectural rule

> The simulation is a **pure function**: `(state, input0, input1) -> state`.
> No floats. No wall-clock. No RNG. No allocation. No rendering/IO inside it.

This is non-negotiable — rollback only works if re-simulating the same inputs from the
same state *always* produces a byte-identical result. Everything below follows from it.

```
Parity/
  src/
    sim/        <- PURE. Compiles with any compiler, zero deps. The game logic.
      fixed.h        Q16.16 fixed-point math (deterministic, integer-only)
      input.h        Input bitmask
      gamestate.h    POD game state + stepState()
      gamestate.cpp  the simulation
      hash.h         FNV-1a state hashing (desync detection)
    net/        <- PURE. Rollback session core (engine-agnostic).
      rollback.h     RollbackSession: prediction, save/restore, re-simulation
      rollback.cpp
      transport.h    abstract packet transport (UDP / loopback / lossy-sim)
    platform/   <- IMPURE. raylib lives ONLY here. Added once raylib is set up.
      main.cpp       window, render, gather input, drive RollbackSession
  tests/
    test_determinism.cpp   headless proof: determinism + rollback == ground truth
  CMakeLists.txt
  build_tests.sh           one-liner to build+run tests with plain g++ (no cmake/deps)
```

## Milestones

### M0 — Deterministic core (DONE in this session)
- Fixed-point math, POD game state, `stepState`, state hashing.
- A 2-player platform-fighter sim: move, jump, attack, parry, hitstun, knockback, health.
- **Headless test** proving (a) re-running identical inputs yields identical hashes, and
  (b) a rollback session with delayed/predicted remote inputs converges to the exact same
  state as a no-network ground-truth run.

### M1 — Rollback session core (DONE in this session)
- `RollbackSession`: input history, ring buffer of saved states, prediction
  (repeat-last-input), misprediction detection, rollback + re-simulation.
- Tracks rollback depth, mispredictions — the numbers your debug overlay will show.

### M2 — raylib view layer (needs raylib installed — see SETUP below)
- Window, 60Hz fixed render, draw two fighters + stage from `GameState`.
- Gather local keyboard input -> `Input` bitmask.
- Local 2-player (same keyboard) hot-seat first — no networking yet. Proves feel.

### M3 — Networking (UDP, P2P)
- `transport.h` impl over UDP sockets. Exchange inputs each frame.
- Drive `RollbackSession` from received packets. Handle out-of-order / loss
  (inputs are tiny + idempotent; resend recent input window every packet).

### M4 — The portfolio features (the "wow")
- **State-hash exchange**: peers swap per-frame state checksums; on mismatch, log the
  exact desync frame. This is the headline résumé feature.
- **Debug overlay**: live ping, input delay, rollback depth, predicted-vs-confirmed
  frames, desync indicator.
- **Network condition simulator**: sliders for artificial latency / jitter / packet loss
  so you can demo "frame-perfect at 150ms + 10% loss" on one machine.

### M5 — Polish
- Replays (just record the input stream — determinism gives you replays for free).
- Title/menu, character select, win/lose, simple SFX.

## SETUP needed from you (for M2+)

The bundled GCC 6.3.0 is too old for modern raylib. Easiest fix — download raylib's
official Windows release which **bundles a matching compiler**:

1. Get `raylib-5.x_win64_mingw-w64.zip` from https://github.com/raysan5/raylib/releases
2. Unzip it (e.g. to `C:\raylib`). It contains `w64devkit` (modern gcc) + prebuilt raylib.
3. Tell me where you put it and I'll wire up CMake + the platform layer.

Until then, M0/M1 build and run with your **existing** g++ — no install required.
