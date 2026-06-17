#!/usr/bin/env bash
# Build + run the headless determinism/rollback tests with plain g++.
# No raylib, no cmake required.
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++14 -O2 -Wall -Wextra \
    src/sim/gamestate.cpp \
    src/net/rollback.cpp \
    tests/test_determinism.cpp \
    -o build/parity_tests
./build/parity_tests
