#!/usr/bin/env bash
# Build the raylib game executable.
# Requires a 64-bit g++ (w64devkit) and raylib 6.0 (win64).
# Run from a shell where C:\w64devkit\bin is on PATH, or it falls back to it below.
set -e
cd "$(dirname "$0")"
mkdir -p build

GPP="${GPP:-/d/toolchain/mingw64/bin/g++.exe}"
RAYLIB="${RAYLIB:-/c/raylib-6.0_win64_mingw-w64}"

"$GPP" -std=c++17 -O2 -Wall \
    src/sim/gamestate.cpp \
    src/net/rollback.cpp \
    src/net/transport.cpp \
    src/net/netgame.cpp \
    src/platform/main.cpp \
    -I"$RAYLIB/include" \
    -L"$RAYLIB/lib" \
    -o build/parity.exe \
    -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 \
    -static -static-libgcc -static-libstdc++

echo "Built build/parity.exe"
