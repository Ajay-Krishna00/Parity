// PARITY — raylib view layer.
//
// This is the ONLY file allowed to touch raylib, the keyboard, or the screen.
// It gathers input, drives the PURE simulation at a fixed 60Hz, and renders the
// resulting GameState. The sim (src/sim) and netcode (src/net) never see raylib.
//
// Two modes:
//   parity.exe                                  -> local hot-seat (both on one keyboard)
//   parity.exe <0|1> <localPort> <ip> <rport>   -> P2P netplay vs a remote peer
//
// Loopback test on one PC (two terminals):
//   parity.exe 0 7000 127.0.0.1 7001
//   parity.exe 1 7001 127.0.0.1 7000
// Across machines: same thing, with the peer's real LAN/Internet IP.
#include "raylib.h"
#include "../sim/gamestate.h"
#include "../sim/hash.h"
#include "../net/netgame.h"
#include <cstdlib>
#include <cstring>

using namespace parity;

// ---- screen mapping (sim units -> pixels) -------------------------------
static const int   SCREEN_W = 900;
static const int   SCREEN_H = 506;
static const float SCALE    = 2.0f;
static const float OX        = 90.0f;
static const float FLOOR_PX = 400.0f;
static const float FIGHTER_W = 34.0f;
static const float FIGHTER_H = 70.0f;

static float sx(Fixed v) { return OX + v.toInt() * SCALE; }
static float syTop(const Player& p) {
    return FLOOR_PX - p.posY.toInt() * SCALE - FIGHTER_H;
}

static Input gatherInput(int kLeft, int kRight, int kJump, int kAtk, int kParry) {
    Input in = 0;
    if (IsKeyDown(kLeft))  in |= IN_LEFT;
    if (IsKeyDown(kRight)) in |= IN_RIGHT;
    if (IsKeyDown(kJump))  in |= IN_JUMP;
    if (IsKeyDown(kAtk))   in |= IN_ATTACK;
    if (IsKeyDown(kParry)) in |= IN_PARRY;
    return in;
}

static const char* stateName(uint32_t s) {
    switch (s) {
        case ST_IDLE:    return "IDLE";
        case ST_WALK:    return "WALK";
        case ST_JUMP:    return "JUMP";
        case ST_ATTACK:  return "ATTACK";
        case ST_PARRY:   return "PARRY";
        case ST_HITSTUN: return "HITSTUN";
    }
    return "?";
}

static Color stateColor(const Player& p, Color base) {
    switch (p.state) {
        case ST_ATTACK:  return ORANGE;
        case ST_PARRY:   return (p.stateFrame < 3) ? SKYBLUE : DARKBLUE;
        case ST_HITSTUN: return RED;
        default:         return base;
    }
}

static void drawFighter(const Player& p, Color base) {
    float x = sx(p.posX) - FIGHTER_W * 0.5f;
    float y = syTop(p);
    DrawRectangleRounded((Rectangle){ x, y, FIGHTER_W, FIGHTER_H }, 0.25f, 6,
                         stateColor(p, base));
    float eye = p.facing > 0 ? x + FIGHTER_W - 8 : x + 2;
    DrawRectangle((int)eye, (int)y + 10, 6, 6, RAYWHITE);

    if (p.state == ST_ATTACK && p.stateFrame >= 3 && p.stateFrame < 6) {
        const float w = 80;
        float hbX = p.facing > 0 ? x + FIGHTER_W : x - w;
        DrawRectangle((int)hbX, (int)y + 20, (int)w, 24, Fade(YELLOW, 0.55f));
    }
}

static void drawHealthBar(const Player& p, float x, bool rightAligned) {
    const float W = 320, H = 22;
    int hp = p.health < 0 ? 0 : p.health;
    float frac = hp / 100.0f;
    float fillW = W * frac;
    float fx2 = rightAligned ? x + (W - fillW) : x;
    DrawRectangle((int)x, 20, (int)W, (int)H, Fade(BLACK, 0.5f));
    DrawRectangle((int)fx2, 20, (int)fillW, (int)H, hp > 30 ? GREEN : RED);
    DrawRectangleLines((int)x, 20, (int)W, (int)H, RAYWHITE);
}

static void drawStageAndFighters(const GameState& s) {
    DrawRectangle((int)(sx(Fixed::fromInt(20)) - FIGHTER_W), (int)FLOOR_PX,
                  (int)((380 - 20) * SCALE + FIGHTER_W * 2), 6, GRAY);
    DrawLine((int)sx(Fixed::fromInt(20)), 0, (int)sx(Fixed::fromInt(20)), SCREEN_H, Fade(GRAY, 0.2f));
    DrawLine((int)sx(Fixed::fromInt(380)), 0, (int)sx(Fixed::fromInt(380)), SCREEN_H, Fade(GRAY, 0.2f));
    drawFighter(s.players[0], (Color){ 90, 160, 255, 255 });
    drawFighter(s.players[1], (Color){ 255, 120, 120, 255 });
    drawHealthBar(s.players[0], 20, false);
    drawHealthBar(s.players[1], SCREEN_W - 340, true);
}

// ============================ hot-seat mode ============================
static int runHotSeat() {
    InitWindow(SCREEN_W, SCREEN_H, "PARITY — hot-seat");
    SetTargetFPS(60);
    GameState s; initState(s);
    bool paused = false;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) initState(s);
        if (IsKeyPressed(KEY_P)) paused = !paused;
        Input in0 = gatherInput(KEY_A, KEY_D, KEY_W, KEY_F, KEY_G);
        Input in1 = gatherInput(KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_PERIOD, KEY_SLASH);
        bool over = (s.players[0].health <= 0 || s.players[1].health <= 0);
        if (!paused && !over) stepState(s, in0, in1);

        BeginDrawing();
        ClearBackground((Color){ 24, 26, 34, 255 });
        drawStageAndFighters(s);
        uint64_t h = hashState(s);
        DrawText(TextFormat("frame %u", s.frame), 20, 50, 18, RAYWHITE);
        DrawText(TextFormat("hash %08x%08x", (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu)),
                 20, 72, 16, Fade(RAYWHITE, 0.7f));
        DrawText("P1: A/D W F G    P2: <- -> Up . /", 20, SCREEN_H - 22, 14, Fade(RAYWHITE, 0.55f));
        DrawText("[R] reset  [P] pause", SCREEN_W / 2 - 90, 52, 16, Fade(RAYWHITE, 0.6f));
        if (paused) DrawText("PAUSED", SCREEN_W / 2 - 40, SCREEN_H / 2, 30, YELLOW);
        if (over) {
            const char* who = s.players[0].health <= 0 ? "P2 WINS" : "P1 WINS";
            DrawText(who, SCREEN_W / 2 - 70, SCREEN_H / 2 - 20, 40, GOLD);
        }
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

// ============================ netplay mode ============================
static void drawNetHud(const NetStats& st, int localPlayer) {
    int x = 20, y = 50;
    Color ok = (Color){ 120, 230, 140, 255 };
    DrawText(TextFormat("NET  you are P%d", localPlayer), x, y, 18,
             (Color){ 200, 200, 255, 255 });
    DrawText(st.connected ? "CONNECTED" : "waiting for peer...", x, y + 22, 16,
             st.connected ? ok : (Color){ 230, 200, 120, 255 });

    int yy = y + 50;
    DrawText(TextFormat("rtt        %d ms", st.rttMs), x, yy, 16, RAYWHITE);          yy += 20;
    DrawText(TextFormat("local f    %u", st.localFrame), x, yy, 16, RAYWHITE);        yy += 20;
    DrawText(TextFormat("remote f   %u", st.remoteFrame), x, yy, 16, RAYWHITE);       yy += 20;
    DrawText(TextFormat("confirmed  %u", st.confirmed), x, yy, 16, RAYWHITE);         yy += 20;
    DrawText(TextFormat("rollback   %u (max %u)", st.lastRollback, st.maxRollback),
             x, yy, 16, st.lastRollback ? (Color){ 255, 210, 120, 255 } : RAYWHITE);  yy += 20;
    DrawText(TextFormat("mispredict %u", st.mispredicts), x, yy, 16, RAYWHITE);       yy += 20;
    DrawText(TextFormat("stalls     %u", st.stalls), x, yy, 16, RAYWHITE);            yy += 20;
    DrawText(TextFormat("pkt s/r    %u / %u", st.packetsSent, st.packetsRecv),
             x, yy, 16, Fade(RAYWHITE, 0.7f));                                        yy += 24;

    if (st.desync)
        DrawText(TextFormat("!! DESYNC at frame %u", st.desyncFrame), x, yy, 18, RED);
    else if (st.connected)
        DrawText("sync OK", x, yy, 16, ok);
}

static int runNet(int localPlayer, uint16_t localPort, const char* ip, uint16_t rport) {
    InitWindow(SCREEN_W, SCREEN_H, TextFormat("PARITY — netplay (P%d)", localPlayer));
    SetTargetFPS(60);

    NetGame ng;
    if (!ng.start(localPlayer, localPort, ip, rport)) {
        // Show the failure on screen for a moment instead of silently dying.
        for (int i = 0; i < 180 && !WindowShouldClose(); ++i) {
            BeginDrawing();
            ClearBackground((Color){ 30, 16, 16, 255 });
            DrawText("Failed to open UDP socket (port in use?)", 40, 200, 22, RED);
            EndDrawing();
        }
        CloseWindow();
        return 1;
    }

    while (!WindowShouldClose()) {
        // In netplay each instance controls its own fighter with the same keys.
        Input local = gatherInput(KEY_A, KEY_D, KEY_W, KEY_F, KEY_G);
        ng.update(local);

        BeginDrawing();
        ClearBackground((Color){ 24, 26, 34, 255 });
        drawStageAndFighters(ng.state());
        drawNetHud(ng.stats(), localPlayer);
        DrawText("move A/D  jump W  attack F  parry G", 20, SCREEN_H - 22, 14,
                 Fade(RAYWHITE, 0.55f));

        const GameState& s = ng.state();
        if (s.players[0].health <= 0 || s.players[1].health <= 0) {
            int win = s.players[0].health <= 0 ? 1 : 0;
            DrawText(win == localPlayer ? "YOU WIN" : "YOU LOSE",
                     SCREEN_W / 2 - 90, SCREEN_H / 2 - 20, 40, GOLD);
        }
        EndDrawing();
    }
    ng.shutdown();
    CloseWindow();
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 5) {
        int      local = atoi(argv[1]) ? 1 : 0;
        uint16_t lport = (uint16_t)atoi(argv[2]);
        uint16_t rport = (uint16_t)atoi(argv[4]);
        return runNet(local, lport, argv[3], rport);
    }
    return runHotSeat();
}
