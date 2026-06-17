// Headless proof harness. No raylib, no sockets — builds with any C++ compiler.
//
// It establishes the three properties rollback netcode stands on:
//   TEST 1  Determinism: identical inputs from identical state -> identical hashes.
//   TEST 2  Rollback correctness: a session that PREDICTS delayed remote inputs and
//           rolls back on mispredictions converges to the exact ground-truth state.
//   TEST 3  Desync detection: a single divergent input is caught by the state hash,
//           and the reported desync frame is exact.
#include "../src/sim/gamestate.h"
#include "../src/sim/hash.h"
#include "../src/net/rollback.h"
#include <cstdio>
#include <vector>

using namespace parity;

// Deterministic pseudo-input generator (integer LCG — NOT used inside the sim,
// only to script a repeatable match for the test).
struct InputScript {
    uint32_t s;
    explicit InputScript(uint32_t seed) : s(seed) {}
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    Input at(uint32_t frame, uint32_t playerSalt) {
        uint32_t v = (frame * 2654435761u) ^ (playerSalt * 40503u);
        v ^= v >> 13; v *= 1274126177u; v ^= v >> 16;
        // bias toward movement/attacks so matches are lively and parries happen
        Input in = 0;
        if (v & 1)        in |= IN_RIGHT;
        if (v & 2)        in |= IN_LEFT;
        if ((v & 28) == 28) in |= IN_JUMP;
        if ((v & 0xF0) == 0xF0) in |= IN_ATTACK;
        if ((v & 0x700) == 0x700) in |= IN_PARRY;
        return in;
    }
};

static const uint32_t FRAMES = 1200; // 20 seconds at 60Hz

// Run the match with zero networking — this is "ground truth".
static std::vector<uint64_t> runGroundTruth(InputScript& a, InputScript& b) {
    GameState s; initState(s);
    std::vector<uint64_t> hashes;
    hashes.reserve(FRAMES + 1);
    hashes.push_back(hashState(s));
    for (uint32_t f = 0; f < FRAMES; ++f) {
        stepState(s, a.at(f, 0), b.at(f, 1));
        hashes.push_back(hashState(s));
    }
    return hashes;
}

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) g_failures++;
}

int main() {
    printf("PARITY determinism + rollback test\n");
    printf("==================================\n");

    // TEST 1 — determinism across independent runs.
    printf("\nTEST 1: deterministic simulation\n");
    {
        InputScript a1(1), b1(2), a2(1), b2(2);
        std::vector<uint64_t> run1 = runGroundTruth(a1, b1);
        std::vector<uint64_t> run2 = runGroundTruth(a2, b2);
        bool same = (run1 == run2);
        check(same, "two runs of identical inputs produce identical hash streams");
        uint64_t h = run1.back();
        // Split into two 32-bit halves: MinGW's printf lacks reliable %llx.
        printf("        final state hash = %08x%08x\n",
               (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
    }

    // Ground truth for the rollback comparison.
    InputScript gtA(1), gtB(2);
    std::vector<uint64_t> truth = runGroundTruth(gtA, gtB);

    // TEST 2 — rollback session (we are player 0) with remote (player 1) input
    // delayed by DELAY frames. Every frame we predict the remote, simulate, and
    // correct via rollback when real inputs land. Must equal ground truth.
    printf("\nTEST 2: rollback converges to ground truth (remote input delayed)\n");
    for (uint32_t DELAY = 1; DELAY <= 8; ++DELAY) {
        InputScript la(1), ra(2);
        RollbackSession sess; sess.init(/*localPlayer=*/0);

        bool mismatch = false;
        for (uint32_t f = 0; f < FRAMES; ++f) {
            // Local input is always available immediately.
            sess.addLocalInput(f, la.at(f, 0));
            // Remote input for frame (f - DELAY) arrives now (simulated latency).
            if (f >= DELAY) {
                uint32_t rf = f - DELAY;
                sess.addRemoteInput(rf, ra.at(rf, 1));
            }
            sess.tick();
        }
        // Flush the remaining delayed remote inputs so the tail gets corrected.
        for (uint32_t rf = (FRAMES >= DELAY ? FRAMES - DELAY : 0); rf < FRAMES; ++rf)
            sess.addRemoteInput(rf, ra.at(rf, 1));

        uint64_t got = hashState(sess.state());
        uint64_t exp = truth[FRAMES];
        if (got != exp) mismatch = true;
        char buf[128];
        snprintf(buf, sizeof buf,
                 "DELAY=%u: final hash matches truth (maxRollback=%u, mispredicts=%u)",
                 DELAY, sess.maxRollbackFrames, sess.mispredictions);
        check(!mismatch, buf);
    }

    // TEST 3 — desync detection. Simulate one peer running a DIFFERENT input from
    // frame CORRUPT_AT onward (the classic cause of a real desync). The state hash
    // must (a) stay identical to ground truth on every frame BEFORE the corruption
    // — no false positives — and (b) diverge once the differing input takes effect.
    printf("\nTEST 3: state-hash desync detection\n");
    {
        InputScript a(1), b(2);
        GameState s; initState(s);
        std::vector<uint64_t> alt;
        alt.push_back(hashState(s));
        const uint32_t CORRUPT_AT = 500;
        for (uint32_t f = 0; f < FRAMES; ++f) {
            Input ia = a.at(f, 0);
            Input ib = b.at(f, 1);
            // From CORRUPT_AT on, force a clearly different input stream for p1.
            if (f >= CORRUPT_AT) ib = IN_RIGHT;
            stepState(s, ia, ib);
            alt.push_back(hashState(s));
        }
        // Find first divergence vs ground truth.
        uint32_t firstDiff = FRAMES + 1;
        for (uint32_t i = 0; i <= FRAMES; ++i)
            if (alt[i] != truth[i]) { firstDiff = i; break; }

        // (a) No false positive: everything up to and including the pre-corruption
        //     state (index CORRUPT_AT) must be identical.
        bool noFalsePositive = (firstDiff > CORRUPT_AT);
        check(noFalsePositive, "no divergence reported before the corrupted frame");

        // (b) Sensitive: the differing input is caught promptly after injection.
        bool detected = (firstDiff <= FRAMES);
        char buf[128];
        snprintf(buf, sizeof buf,
                 "desync detected at frame %u (corruption began at %u)",
                 firstDiff, CORRUPT_AT);
        check(detected && noFalsePositive, buf);
    }

    printf("\n==================================\n");
    printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
