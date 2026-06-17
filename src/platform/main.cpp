// PARITY — raylib view layer.
//
// This is the ONLY file allowed to touch raylib, the keyboard, or the screen.
// It gathers input, drives the PURE simulation at a fixed 60Hz, and renders the
// resulting GameState. The sim (src/sim) and netcode (src/net) never see raylib.
//
// Normal use: just run parity.exe and use the on-screen menu —
//   Host LAN game  -> broadcast a beacon; opponents see your name and join.
//   Join LAN game  -> live list of hosts on your WiFi/hotspot; click to play.
//   Local hot-seat -> both players on one keyboard.
//
// Power-user / scripted path (explicit addresses, used by the loopback test):
//   parity.exe <0|1> <localPort> <ip> <rport>
#include "raylib.h"
#include "../sim/gamestate.h"
#include "../sim/hash.h"
#include "../net/netgame.h"
#include "../net/discovery.h"
#include <cstdlib>
#include <cstring>
#include <string>

using namespace parity;

// ---- screen mapping (sim units -> pixels) -------------------------------
static const int   SCREEN_W = 900;
static const int   SCREEN_H = 506;
static const float SCALE    = 2.0f;
static const float OX        = 90.0f;
static const float FLOOR_PX = 400.0f;
static const float FIGHTER_W = 34.0f;
static const float FIGHTER_H = 70.0f;
static const Color BG        = { 24, 26, 34, 255 };
static const uint16_t GAME_PORT = 7000;   // host's fixed game port (advertised in beacon)

static float sx(Fixed v) { return OX + v.toInt() * SCALE; }
static float syTop(const Player& p) {
    return FLOOR_PX - p.posY.toInt() * SCALE - FIGHTER_H;
}

// ---- crisp UI text -------------------------------------------------------
// raylib's built-in font is a tiny pixel bitmap that looks rough scaled up.
// We load a real TrueType font once (Windows ships Segoe UI) and render with
// bilinear filtering. Falls back to the default font if the file is missing.
static Font g_font;
static bool g_fontReady = false;

static void loadUiFont() {
    g_font = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 64, nullptr, 0);
    if (g_font.texture.id != 0 && g_font.glyphCount > 0) {
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
        g_fontReady = true;
    } else {
        g_font = GetFontDefault();
        g_fontReady = false;
    }
}

// Drop-in replacement for DrawText with the same (text,x,y,size,color) shape.
static void DT(const char* t, int x, int y, int size, Color c) {
    if (g_fontReady)
        DrawTextEx(g_font, t, (Vector2){ (float)x, (float)y }, (float)size, 1.0f, c);
    else
        DrawText(t, x, y, size, c);
}

// Width of a string in the active font, so we can center text precisely.
static float textW(const char* t, int size) {
    if (g_fontReady)
        return MeasureTextEx(g_font, t, (float)size, 1.0f).x;
    return (float)MeasureText(t, size);
}

// Horizontally-centered text around cx.
static void DTc(const char* t, int cx, int y, int size, Color c) {
    DT(t, (int)(cx - textW(t, size) * 0.5f), y, size, c);
}

// A clickable button. Must be called between Begin/EndDrawing. Returns true on click.
static bool button(Rectangle r, const char* label, int fontSize = 22) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    DrawRectangleRounded(r, 0.2f, 8, hover ? (Color){ 64, 84, 130, 255 }
                                           : (Color){ 40, 46, 66, 255 });
    DrawRectangleLinesEx(r, hover ? 2.0f : 1.0f, (Color){ 120, 150, 220, 255 });
    DTc(label, (int)(r.x + r.width * 0.5f),
        (int)(r.y + r.height * 0.5f - fontSize * 0.5f), fontSize, RAYWHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
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

// ---- netcode debug HUD ---------------------------------------------------
static void drawNetHud(const NetStats& st, int localPlayer) {
    int x = 20, y = 50;
    Color ok = (Color){ 120, 230, 140, 255 };
    DT(TextFormat("NET  you are P%d", localPlayer), x, y, 18,
       (Color){ 200, 200, 255, 255 });
    DT(st.connected ? "CONNECTED" : "waiting for peer...", x, y + 22, 16,
       st.connected ? ok : (Color){ 230, 200, 120, 255 });

    int yy = y + 50;
    DT(TextFormat("rtt        %d ms", st.rttMs), x, yy, 16, RAYWHITE);          yy += 20;
    DT(TextFormat("local f    %u", st.localFrame), x, yy, 16, RAYWHITE);        yy += 20;
    DT(TextFormat("remote f   %u", st.remoteFrame), x, yy, 16, RAYWHITE);       yy += 20;
    DT(TextFormat("confirmed  %u", st.confirmed), x, yy, 16, RAYWHITE);         yy += 20;
    DT(TextFormat("rollback   %u (max %u)", st.lastRollback, st.maxRollback),
       x, yy, 16, st.lastRollback ? (Color){ 255, 210, 120, 255 } : RAYWHITE);  yy += 20;
    DT(TextFormat("mispredict %u", st.mispredicts), x, yy, 16, RAYWHITE);       yy += 20;
    DT(TextFormat("stalls     %u", st.stalls), x, yy, 16, RAYWHITE);            yy += 20;
    DT(TextFormat("pkt s/r    %u / %u", st.packetsSent, st.packetsRecv),
       x, yy, 16, Fade(RAYWHITE, 0.7f));                                        yy += 24;

    if (st.desync)
        DT(TextFormat("!! DESYNC at frame %u", st.desyncFrame), x, yy, 18, RED);
    else if (st.connected)
        DT("sync OK", x, yy, 16, ok);
}

// One networked frame: gather local input, advance the session, render.
static void netMatchFrame(NetGame& ng, int localPlayer) {
    Input local = gatherInput(KEY_A, KEY_D, KEY_W, KEY_F, KEY_G);
    ng.update(local);

    BeginDrawing();
    ClearBackground(BG);
    drawStageAndFighters(ng.state());
    drawNetHud(ng.stats(), localPlayer);
    DT("move A/D   jump W   attack F   parry G      [Esc] menu",
       20, SCREEN_H - 22, 14, Fade(RAYWHITE, 0.55f));

    const GameState& s = ng.state();
    if (s.players[0].health <= 0 || s.players[1].health <= 0) {
        int win = s.players[0].health <= 0 ? 1 : 0;
        DTc(win == localPlayer ? "YOU WIN" : "YOU LOSE", SCREEN_W / 2,
            SCREEN_H / 2 - 20, 40, GOLD);
    }
    EndDrawing();
}

// One hot-seat frame.
static void hotSeatFrame(GameState& s, bool& paused) {
    if (IsKeyPressed(KEY_R)) initState(s);
    if (IsKeyPressed(KEY_P)) paused = !paused;
    Input in0 = gatherInput(KEY_A, KEY_D, KEY_W, KEY_F, KEY_G);
    Input in1 = gatherInput(KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_PERIOD, KEY_SLASH);
    bool over = (s.players[0].health <= 0 || s.players[1].health <= 0);
    if (!paused && !over) stepState(s, in0, in1);

    BeginDrawing();
    ClearBackground(BG);
    drawStageAndFighters(s);
    uint64_t h = hashState(s);
    DT(TextFormat("frame %u", s.frame), 20, 50, 18, RAYWHITE);
    DT(TextFormat("hash %08x%08x", (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu)),
       20, 72, 16, Fade(RAYWHITE, 0.7f));
    DT("P1: A/D W F G    P2: <- -> Up . /    [R] reset  [P] pause  [Esc] menu",
       20, SCREEN_H - 22, 14, Fade(RAYWHITE, 0.55f));
    if (paused) DTc("PAUSED", SCREEN_W / 2, SCREEN_H / 2, 30, YELLOW);
    if (over) {
        const char* who = s.players[0].health <= 0 ? "P2 WINS" : "P1 WINS";
        DTc(who, SCREEN_W / 2, SCREEN_H / 2 - 20, 40, GOLD);
    }
    EndDrawing();
}

// ============================ menu-driven app ============================
enum Screen { SCR_MENU, SCR_HOSTNAME, SCR_HOSTWAIT, SCR_BROWSE, SCR_NET, SCR_HOTSEAT };

static int runApp() {
    InitWindow(SCREEN_W, SCREEN_H, "PARITY");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // we handle Esc ourselves (back navigation, not quit)
    loadUiFont();

    Screen scr = SCR_MENU;
    NetGame ng;
    LanDiscovery disc;
    GameState hot; initState(hot);
    bool hotPaused = false;
    int  localPlayer = 0;
    std::string nameStr = TextFormat("Player-%d", GetRandomValue(100, 999));
    std::string status;
    const std::string localIp = LanDiscovery::localIpv4();
    bool quit = false;

    while (!WindowShouldClose() && !quit) {
        uint32_t nowMs = (uint32_t)(GetTime() * 1000.0);

        switch (scr) {
        case SCR_MENU: {
            BeginDrawing();
            ClearBackground(BG);
            DTc("PARITY", SCREEN_W / 2, 60, 60, RAYWHITE);
            DTc("rollback netcode 1v1", SCREEN_W / 2, 130, 20, Fade(RAYWHITE, 0.55f));
            Rectangle bHost = { SCREEN_W / 2 - 150, 190, 300, 50 };
            Rectangle bJoin = { SCREEN_W / 2 - 150, 255, 300, 50 };
            Rectangle bHot  = { SCREEN_W / 2 - 150, 320, 300, 50 };
            Rectangle bQuit = { SCREEN_W / 2 - 150, 385, 300, 50 };
            bool cHost = button(bHost, "Host LAN game");
            bool cJoin = button(bJoin, "Join LAN game");
            bool cHot  = button(bHot,  "Local hot-seat");
            bool cQuit = button(bQuit, "Quit");
            DTc("keys:  [H] host   [J] join   [L] hot-seat   [Q] quit",
                SCREEN_W / 2, SCREEN_H - 54, 14, Fade(RAYWHITE, 0.4f));
            if (!status.empty())
                DTc(status.c_str(), SCREEN_W / 2, SCREEN_H - 30, 16, (Color){ 230, 150, 150, 255 });
            EndDrawing();

            cHost = cHost || IsKeyPressed(KEY_H);
            cJoin = cJoin || IsKeyPressed(KEY_J);
            cHot  = cHot  || IsKeyPressed(KEY_L);
            cQuit = cQuit || IsKeyPressed(KEY_Q);

            if (cHost) {
                status.clear();
                while (GetCharPressed() != 0) {}   // drop the 'H' that triggered this
                scr = SCR_HOSTNAME;
            }
            else if (cJoin) {
                status.clear();
                if (disc.startBrowse()) scr = SCR_BROWSE;
                else status = "Could not open discovery socket";
            }
            else if (cHot) { initState(hot); hotPaused = false; scr = SCR_HOTSEAT; }
            else if (cQuit) quit = true;
            break;
        }

        case SCR_HOSTNAME: {
            int c = GetCharPressed();
            while (c > 0) {
                if (nameStr.size() < 18 && c >= 32 && c < 127) nameStr += (char)c;
                c = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !nameStr.empty()) nameStr.pop_back();

            BeginDrawing();
            ClearBackground(BG);
            DTc("Host a LAN game", SCREEN_W / 2, 80, 36, RAYWHITE);
            DTc("Your name (opponents see this in their list):",
                SCREEN_W / 2, 165, 18, Fade(RAYWHITE, 0.7f));
            Rectangle fld = { SCREEN_W / 2 - 200, 195, 400, 46 };
            DrawRectangleRec(fld, (Color){ 18, 22, 32, 255 });
            DrawRectangleLinesEx(fld, 2.0f, SKYBLUE);
            DT(nameStr.c_str(), (int)fld.x + 12, (int)fld.y + 11, 24, RAYWHITE);
            Rectangle bStart = { SCREEN_W / 2 - 150, 280, 300, 50 };
            Rectangle bBack  = { SCREEN_W / 2 - 150, 345, 300, 44 };
            bool cStart = button(bStart, "Start hosting");
            bool cBack  = button(bBack, "Back");
            EndDrawing();

            bool go = (cStart || IsKeyPressed(KEY_ENTER)) && !nameStr.empty();
            if (go) {
                if (ng.start(0, GAME_PORT, "", 0) && disc.startHost(nameStr, GAME_PORT)) {
                    localPlayer = 0;
                    scr = SCR_HOSTWAIT;
                } else {
                    status = "Could not host (is port 7000 in use?)";
                    ng.shutdown(); disc.stop();
                    scr = SCR_MENU;
                }
            }
            if (cBack || IsKeyPressed(KEY_ESCAPE)) scr = SCR_MENU;
            break;
        }

        case SCR_HOSTWAIT: {
            disc.tick(nowMs);   // broadcast beacon
            ng.update(0);       // poll for the client; self-caps until it connects

            BeginDrawing();
            ClearBackground(BG);
            DTc("Hosting", SCREEN_W / 2, 80, 40, RAYWHITE);
            DTc(TextFormat("Name:  %s", nameStr.c_str()), SCREEN_W / 2, 150, 24, SKYBLUE);
            DTc("Waiting for a player to join...", SCREEN_W / 2, 200, 20, Fade(RAYWHITE, 0.75f));
            DTc("(both devices must be on the same WiFi / hotspot)",
                SCREEN_W / 2, 232, 16, Fade(RAYWHITE, 0.5f));
            DTc(TextFormat("manual connect fallback  ->  IP %s   port %u", localIp.c_str(), GAME_PORT),
                SCREEN_W / 2, 285, 15, Fade(RAYWHITE, 0.45f));
            Rectangle bCancel = { SCREEN_W / 2 - 150, 350, 300, 44 };
            bool cCancel = button(bCancel, "Cancel");
            EndDrawing();

            if (ng.stats().connected) { disc.stop(); scr = SCR_NET; }
            else if (cCancel || IsKeyPressed(KEY_ESCAPE)) {
                disc.stop(); ng.shutdown(); scr = SCR_MENU;
            }
            break;
        }

        case SCR_BROWSE: {
            disc.tick(nowMs);   // receive beacons, expire stale hosts

            BeginDrawing();
            ClearBackground(BG);
            DTc("Join a LAN game", SCREEN_W / 2, 50, 36, RAYWHITE);
            const std::vector<HostEntry>& hs = disc.hosts();
            if (hs.empty())
                DTc("searching your network for hosts...", SCREEN_W / 2, 150, 20,
                    Fade(RAYWHITE, 0.6f));
            int chosen = -1;
            int rowY = 120;
            for (size_t i = 0; i < hs.size(); ++i) {
                Rectangle row = { SCREEN_W / 2 - 260.0f, (float)rowY, 520, 50 };
                if (button(row, TextFormat("%d.  %s    (%s)", (int)i + 1,
                                           hs[i].name.c_str(), hs[i].ip.c_str())))
                    chosen = (int)i;
                rowY += 60;
            }
            if (!hs.empty())
                DTc("keys:  [Enter] join first   [1-9] pick   [Esc] back",
                    SCREEN_W / 2, SCREEN_H - 96, 14, Fade(RAYWHITE, 0.4f));
            Rectangle bBack = { SCREEN_W / 2 - 150.0f, (float)(SCREEN_H - 64), 300, 44 };
            bool cBack = button(bBack, "Back");
            EndDrawing();

            // Keyboard: Enter joins the first host, number keys pick by index.
            if (!hs.empty() && IsKeyPressed(KEY_ENTER)) chosen = 0;
            for (int k = 0; k < 9 && k < (int)hs.size(); ++k)
                if (IsKeyPressed(KEY_ONE + k)) chosen = k;

            if (chosen >= 0) {
                HostEntry h = hs[chosen];                 // copy before we tear down disc
                if (ng.start(1, 0, h.ip, h.port)) {       // ephemeral local port
                    localPlayer = 1;
                    disc.stop();
                    scr = SCR_NET;
                } else {
                    status = "Could not join host";
                    disc.stop(); scr = SCR_MENU;
                }
            } else if (cBack || IsKeyPressed(KEY_ESCAPE)) {
                disc.stop(); scr = SCR_MENU;
            }
            break;
        }

        case SCR_NET:
            netMatchFrame(ng, localPlayer);
            if (IsKeyPressed(KEY_ESCAPE)) { ng.shutdown(); scr = SCR_MENU; }
            break;

        case SCR_HOTSEAT:
            if (IsKeyPressed(KEY_ESCAPE)) { scr = SCR_MENU; break; }
            hotSeatFrame(hot, hotPaused);
            break;
        }
    }

    disc.stop();
    ng.shutdown();
    CloseWindow();
    return 0;
}

// ---- explicit-address CLI path (scripted loopback test, power users) ----
static int runNetCli(int localPlayer, uint16_t localPort, const char* ip, uint16_t rport) {
    InitWindow(SCREEN_W, SCREEN_H, TextFormat("PARITY — netplay (P%d)", localPlayer));
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    loadUiFont();

    NetGame ng;
    if (!ng.start(localPlayer, localPort, ip, rport)) {
        for (int i = 0; i < 180 && !WindowShouldClose(); ++i) {
            BeginDrawing();
            ClearBackground((Color){ 30, 16, 16, 255 });
            DTc("Failed to open UDP socket (port in use?)", SCREEN_W / 2, 200, 22, RED);
            EndDrawing();
        }
        CloseWindow();
        return 1;
    }

    while (!WindowShouldClose()) {
        netMatchFrame(ng, localPlayer);
        if (IsKeyPressed(KEY_ESCAPE)) break;
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
        // "-" as the remote IP means host mode (auto-adopt the first peer).
        const char* ip = (strcmp(argv[3], "-") == 0) ? "" : argv[3];
        return runNetCli(local, lport, ip, rport);
    }
    return runApp();
}
