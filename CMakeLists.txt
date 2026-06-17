cmake_minimum_required(VERSION 3.15)
project(parity CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Pure simulation + netcode (no external deps) ---
add_library(parity_core STATIC
    src/sim/gamestate.cpp
    src/net/rollback.cpp
)
target_include_directories(parity_core PUBLIC src)

# --- Headless tests (always buildable) ---
add_executable(parity_tests tests/test_determinism.cpp)
target_link_libraries(parity_tests PRIVATE parity_core)
enable_testing()
add_test(NAME determinism COMMAND parity_tests)

# --- Game executable (only if raylib is available) ---
# Set -DRAYLIB_DIR=C:/raylib/... or install raylib so find_package works.
find_package(raylib QUIET)
if(raylib_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/src/platform/main.cpp")
    add_executable(parity src/platform/main.cpp)
    target_link_libraries(parity PRIVATE parity_core raylib)
    message(STATUS "raylib found -> building 'parity' game executable")
else()
    message(STATUS "raylib not found -> building tests only (see PLAN.md SETUP)")
endif()
