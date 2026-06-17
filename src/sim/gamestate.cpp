#include "gamestate.h"

namespace parity {

// ---- Tuning constants (all integer / fixed-point, no floats anywhere) ----
namespace {
    const Fixed STAGE_MIN   = fx(20);
    const Fixed STAGE_MAX   = fx(380);
    const Fixed FLOOR_Y     = fx(0);

    const Fixed MOVE_SPEED  = Fixed::fromFraction(5, 2);   // 2.5 units/frame
    const Fixed JUMP_VEL    = fx(8);
    const Fixed GRAVITY     = Fixed::fromFraction(1, 2);   // 0.5 units/frame^2

    // Attack frame data (in frames).
    const uint32_t ATK_STARTUP  = 3;
    const uint32_t ATK_ACTIVE   = 3;
    const uint32_t ATK_RECOVERY = 8;
    const uint32_t ATK_TOTAL    = ATK_STARTUP + ATK_ACTIVE + ATK_RECOVERY;
    const Fixed    ATK_RANGE    = fx(40);
    const int32_t  ATK_DAMAGE   = 8;
    const Fixed    ATK_KNOCKBACK= fx(6);
    const uint32_t HITSTUN_LEN  = 16;

    // Parry frame data.
    const uint32_t PARRY_ACTIVE = 3;   // first N frames of a parry are "active"
    const uint32_t PARRY_TOTAL  = 12;  // total parry animation length
    const uint32_t PARRY_PUNISH = 28;  // attacker hitstun if their attack is parried

    Fixed clampX(Fixed x) {
        if (x < STAGE_MIN) return STAGE_MIN;
        if (x > STAGE_MAX) return STAGE_MAX;
        return x;
    }

    bool canAct(const Player& p) {
        return p.state == ST_IDLE || p.state == ST_WALK || p.state == ST_JUMP;
    }

    void setState(Player& p, ActionState st) {
        p.state = st;
        p.stateFrame = 0;
    }
}

void initState(GameState& s) {
    for (int i = 0; i < 2; ++i) {
        Player& p = s.players[i];
        p.posY = FLOOR_Y;
        p.velX = Fixed(0);
        p.velY = Fixed(0);
        p.state = ST_IDLE;
        p.stateFrame = 0;
        p.health = 100;
        p.onGround = 1;
    }
    s.players[0].posX = fx(140);
    s.players[0].facing = +1;
    s.players[1].posX = fx(260);
    s.players[1].facing = -1;
    s.frame = 0;
}

namespace {

// Apply one player's input: state transitions + horizontal intent.
void applyInput(Player& p, Input in) {
    p.velX = Fixed(0);

    if (canAct(p)) {
        // Start an attack or parry (these lock out movement for their duration).
        if (held(in, IN_ATTACK)) { setState(p, ST_ATTACK); return; }
        if (held(in, IN_PARRY))  { setState(p, ST_PARRY);  return; }

        bool left  = held(in, IN_LEFT);
        bool right = held(in, IN_RIGHT);
        if (left && !right)  { p.velX = -MOVE_SPEED; p.facing = -1; }
        if (right && !left)  { p.velX =  MOVE_SPEED; p.facing = +1; }

        if (p.onGround && held(in, IN_JUMP)) {
            p.velY = JUMP_VEL;
            p.onGround = 0;
            setState(p, ST_JUMP);
        } else if (p.onGround) {
            setState(p, (p.velX == Fixed(0)) ? ST_IDLE : ST_WALK);
        }
    }
    // During ATTACK/PARRY/HITSTUN we keep facing but allow drifting velX=0.
}

// Integrate physics for one player.
void integrate(Player& p) {
    if (!p.onGround) {
        p.velY -= GRAVITY;
    }
    p.posX = clampX(p.posX + p.velX);
    p.posY += p.velY;

    if (p.posY <= FLOOR_Y) {
        p.posY = FLOOR_Y;
        p.velY = Fixed(0);
        if (!p.onGround) {
            p.onGround = 1;
            // landing returns to a neutral state if we were just airborne
            if (p.state == ST_JUMP) setState(p, ST_IDLE);
        }
    }
}

bool attackIsActive(const Player& p) {
    return p.state == ST_ATTACK &&
           p.stateFrame >= ATK_STARTUP &&
           p.stateFrame <  ATK_STARTUP + ATK_ACTIVE;
}

bool parryIsActive(const Player& p) {
    return p.state == ST_PARRY && p.stateFrame < PARRY_ACTIVE;
}

// Does attacker's active hitbox reach the defender, facing the right way?
bool attackConnects(const Player& atk, const Player& def) {
    Fixed dx = def.posX - atk.posX;
    bool inFront = (atk.facing > 0 && dx >= Fixed(0)) ||
                   (atk.facing < 0 && dx <= Fixed(0));
    return inFront && fxAbs(dx) <= ATK_RANGE && fxAbs(def.posY - atk.posY) <= fx(30);
}

void takeHit(Player& def, const Player& atk) {
    def.health -= ATK_DAMAGE;
    setState(def, ST_HITSTUN);
    def.velX = Fixed(0);
    // knock the defender away from the attacker
    def.posX = clampX(def.posX + (atk.facing > 0 ? ATK_KNOCKBACK : -ATK_KNOCKBACK));
}

// Resolve attacks against the opponent, honoring parries. Symmetric for both.
void resolveCombat(GameState& s) {
    bool parried[2] = { false, false };
    bool hit[2]     = { false, false };

    for (int a = 0; a < 2; ++a) {
        int d = 1 - a;
        Player& atk = s.players[a];
        Player& def = s.players[d];
        if (!attackIsActive(atk)) continue;
        if (!attackConnects(atk, def)) continue;

        if (parryIsActive(def)) {
            parried[a] = true;   // attacker 'a' got parried by 'd'
        } else {
            hit[d] = true;       // defender 'd' is hit
        }
    }

    // Apply results after scanning so ordering can't bias who-hits-whom.
    for (int a = 0; a < 2; ++a) {
        if (parried[a]) {
            // The attacker is punished: long hitstun, no damage dealt.
            setState(s.players[a], ST_HITSTUN);
            s.players[a].stateFrame = 0;
            // stretch the punish window
            s.players[a].state = ST_HITSTUN;
        }
    }
    for (int d = 0; d < 2; ++d) {
        if (hit[d] && !parried[1 - d]) {
            takeHit(s.players[d], s.players[1 - d]);
        }
    }
}

// Advance per-state timers and auto-exit finished actions.
void advanceState(Player& p, bool wasParried) {
    p.stateFrame++;
    switch (p.state) {
        case ST_ATTACK:
            if (p.stateFrame >= ATK_TOTAL) setState(p, ST_IDLE);
            break;
        case ST_PARRY:
            if (p.stateFrame >= PARRY_TOTAL) setState(p, ST_IDLE);
            break;
        case ST_HITSTUN: {
            uint32_t len = wasParried ? PARRY_PUNISH : HITSTUN_LEN;
            if (p.stateFrame >= len) setState(p, p.onGround ? ST_IDLE : ST_JUMP);
            break;
        }
        default: break;
    }
}

} // namespace

void stepState(GameState& s, Input in0, Input in1) {
    Input in[2] = { in0, in1 };

    // 1) inputs -> intent / state changes
    for (int i = 0; i < 2; ++i) applyInput(s.players[i], in[i]);

    // 2) physics
    for (int i = 0; i < 2; ++i) integrate(s.players[i]);

    // remember who was mid-attack before combat (used to size punish windows)
    bool wasAttacking[2] = { s.players[0].state == ST_ATTACK,
                             s.players[1].state == ST_ATTACK };

    // 3) combat resolution (parries, hits, knockback)
    resolveCombat(s);

    // 4) advance timers
    for (int i = 0; i < 2; ++i) {
        bool punished = (s.players[i].state == ST_HITSTUN && wasAttacking[i]);
        advanceState(s.players[i], punished);
    }

    s.frame++;
}

} // namespace parity
