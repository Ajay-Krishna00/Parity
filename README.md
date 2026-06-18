# PARITY

A minimalist 1v1 platform-fighter built as a vehicle for **peer-to-peer deterministic
rollback netcode** (GGPO-style). The game is deliberately simple so the *netcode is the
star*: two fighters, a floor, move / jump / attack / parry — and underneath it, a
fully deterministic simulation that two machines can keep in lockstep over a lossy UDP
connection by predicting, rolling back, and re-simulating.

## The one rule everything follows

> The simulation is a **pure function**: `(state, input0, input1) -> state`.
> No floats. No wall-clock. No RNG. No allocation. No rendering or I/O inside it.

Rollback only works if re-simulating the same inputs from the same state *always*
produces a **byte-identical** result. So the sim uses **Q16.16 fixed-point** integer
math (`sim/fixed.h`), the whole `GameState` is plain-old-data (a save/restore is one
`memcpy`), and the simulation core (`src/sim`) compiles with zero dependencies on any
compiler.

## How the rollback works

Locally the game must run at 60Hz, but the remote player's input for the current frame
hasn't arrived yet. So PARITY:

1. **Predicts** the remote input (repeat their last input) and simulates forward
   immediately — the local player feels zero input lag.
2. When the real remote input arrives, if the prediction was wrong it **rolls back** to
   that frame (restoring a saved snapshot from a ring buffer) and **re-simulates**
   forward with the corrected input.
3. Because `stepState` is deterministic, both peers converge to exactly the same state.

Peers also exchange **per-frame state hashes** (FNV-1a, `sim/hash.h`); on a mismatch the
exact desync frame is flagged — the headline debugging feature.

## Features

- **Deterministic fixed-point sim** — 2-player fighter with action states
  (idle/walk/jump/attack/parry/hitstun), startup/active/recovery frame-data, knockback,
  hitstun, and health.
- **GGPO-style rollback session** (`net/rollback.h`) — input prediction, ring-buffered
  state snapshots (256 frames ≈ 4s), misprediction detection, and re-simulation, with
  live stats (rollback depth, mispredictions, confirmed frame).
- **P2P UDP networking** (`net/netgame.h`, `net/transport.h`) — exchanges inputs each
  frame, resends a sliding 32-input window per packet for loss tolerance, handles
  out-of-order / duplicate packets, and self-stalls when waiting on the peer.
- **Server-free LAN matchmaking** (`net/discovery.h`) — hosts broadcast a named UDP
  beacon; browsers see a live list of hosts on the same WiFi/hotspot and join with a
  click. No IP typing (with a manual-address fallback).
- **raylib view layer** (`platform/main.cpp`) — menu-driven UI, in-match netcode debug
  HUD (rtt, rollback depth, mispredicts, packet counts, sync/desync indicator), and a
  local hot-seat mode (both players on one keyboard) that shows the live frame + hash.
- **Headless determinism tests** (`tests/test_determinism.cpp`) — prove that identical
  inputs yield identical hashes, and that a rollback session with delayed/predicted
  remote inputs converges to the same state as a no-network ground-truth run.

## Architecture

```
Parity/
  src/
    sim/        PURE. The game logic. Zero deps, any compiler.
      fixed.h        Q16.16 fixed-point math (deterministic, integer-only)
      input.h        input bitmask
      gamestate.{h,cpp}   POD game state + stepState() — THE simulation
      hash.h         FNV-1a state hashing (desync detection)
    net/        PURE-ish. Rollback + networking, engine-agnostic.
      rollback.{h,cpp}    prediction, save/restore, re-simulation
      netgame.{h,cpp}     ties input + transport + session into a match
      transport.{h,cpp}   UDP socket peer
      discovery.{h,cpp}   LAN host discovery via UDP broadcast
    platform/   IMPURE. raylib lives ONLY here.
      main.cpp       window, render, input, menu, drives the session
  tests/
    test_determinism.cpp   headless proof of determinism + rollback correctness
  CMakeLists.txt
```

The dependency direction is strict: `platform` → `net` → `sim`. The sim never knows
raylib or sockets exist; the netcode never knows about rendering.

## Controls

**Hot-seat (local 2-player, one keyboard):**

| | Move | Jump | Attack | Parry |
|---|---|---|---|---|
| **P1** | `A` / `D` | `W` | `F` | `G` |
| **P2** | `←` / `→` | `↑` | `.` | `/` |

`R` reset · `P` pause · `Esc` menu

**Networked match:** `A`/`D` move, `W` jump, `F` attack, `G` parry, `Esc` to the menu.

## Building & running

The deterministic core and the headless tests build with **any** C++14 compiler and
have **no external dependencies**:

```sh
# build + run the determinism tests (no raylib needed)
./build_tests.sh
```

Or via CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build        # runs the determinism test
```

The playable game (`parity.exe`) additionally needs **raylib**. CMake builds it
automatically when raylib is found, otherwise it builds tests only:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/raylib    # point at your raylib
cmake --build build
./build/parity.exe
```

> raylib's official Windows release bundles a matching modern compiler (`w64devkit`).
> Grab `raylib-5.x_win64_mingw-w64.zip` from
> https://github.com/raysan5/raylib/releases and point CMake at it.

### Quick netplay test on one machine

Launch two windows on loopback (explicit-address path, `<0|1> <localPort> <ip> <rport>`):

```sh
./build/parity.exe 0 7000 - 7001        # host  (P0)
./build/parity.exe 1 7001 127.0.0.1 7000  # client (P1)
```

Or just run `parity.exe` twice and use **Host LAN game** / **Join LAN game** from the menu.

## Roadmap

See [`PLAN.md`](PLAN.md) for the full milestone breakdown. Implemented so far: the
deterministic core (M0), rollback session (M1), raylib view + hot-seat (M2), UDP P2P
networking (M3), and most of the portfolio features (M4) — state-hash desync detection,
the debug overlay, and LAN discovery. Remaining polish (M5): replays from the recorded
input stream, character select, and SFX. A network-condition simulator (artificial
latency / jitter / loss sliders) is the next high-value demo feature.
