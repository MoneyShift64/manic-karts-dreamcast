/*
 * kart.c - the original's kart physics, translated from the decompile
 * (decomp/game; the original function and its address are noted on each part).
 *
 * Everything is the original's integer maths, unchanged: 1024 position units
 * a map cell, headings in tenths of a degree, the frame time gFT in 1024ths
 * of a second, trig tables from TRIG.DAT (scaled by 16384), the engine power
 * curves out of K.EXE (data/physics.bin, made by tools/build_assets.py).
 *
 * Fuel is used as the original's (its fuel option is on by default); power-ups,
 * the pit lane and the surfaces' grip are in powerup.c.
 *
 * Not yet: kart-against-kart bumps, pick-ups, the computer drivers' steering help.
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

int gFT = 17;                            /* 1024 / 60 */

static const s32 *sCos, *sSin, *sTan;   /* 3600 each (TRIG.DAT) */
static const s32 *sCurves;               /* 5 engine power curves of 40 (K.EXE 0xb5cf4) */
static const s32 *sUpShift;              /* 4 x 8 engine-upgrade shifts (K.EXE 0xba180) */
#define sClass (gGame.cls & 3)          /* engine class 0..3 (the original's 0x12d233) */
static s32 sDrag[8][40];                 /* surface drag tables (built as the original's 0x1c000 does) */

/* the 8 points tested against walls, for each of 225 headings (build_rotation_tables 0x40eb0:
 * 4 corners of a 1840 x 1700 box; the other 4 entries stay 0 in the original, i.e. the centre) */
static s32 sCornerX[225][8], sCornerY[225][8];

/* steering (FUN_40dbc with the default settings) */
#define STEER_MAX   250                  /* 25 * 10 */
#define STEER_RATE  0x100                /* turning the wheel */
#define STEER_BACK  0x200                /* letting it go back */

static inline s32 COS(int a) { return sCos[a < 0 ? a + 3600 : a]; }
static inline s32 SIN(int a) { return sSin[a < 0 ? a + 3600 : a]; }
static inline s32 TAN(int a) { return sTan[a < 0 ? a + 3600 : a]; }
static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }
static inline int speed_index(s32 v) { s32 t = v >> 10; t = t < 0 ? -t : t; return t > 39 ? 39 : t; }

void physics_init(void) {
    static const int k[7] = { 2000, 20000, 500, 10000, 6500, 10000, 4000 };
    u32 size;
    int i, j;
    const s32 *d = (const s32 *)plat_load_file("physics.bin", &size);
    if (d == NULL || size < 43200 + 800 + 128) return;
    sCos = d;
    sSin = d + 3600;
    sTan = d + 7200;
    sCurves = d + 10800;
    sUpShift = d + 10800 + 200;
    for (j = 0; j < 7; j++) {
        for (i = 0; i < 40; i++) sDrag[j][i] = i * i * k[j];
    }
    for (i = 0; i < 225; i++) {
        static const s32 X[4] = { -920, 920, 920, -920 }, Y[4] = { -850, -850, 850, 850 };
        int a = (i * 3600) / 225;
        s32 s = SIN(a), c = COS(a);
        for (j = 0; j < 4; j++) {
            sCornerX[i][j] = (X[j] * c - Y[j] * s) >> 14;
            sCornerY[i][j] = (X[j] * s + Y[j] * c) >> 14;
        }
    }
}

/* the drag table for a surface (kart_compute_forces 0x1a860), or -1 for none */
static int surface_drag(u8 s) {
    switch (s) {
    case 0x6c: return 0;                 /* 'l' */
    case 0x44: return 1;                 /* 'D' */
    case 0x53: case 0x64: return 2;      /* 'S' 'd' */
    case 0x4d: return 3;                 /* 'M' */
    case 0x6d: return 4;                 /* 'm' */
    case 0x70: return 5;                 /* 'p' */
    case 0x47: case 0x77: return 6;      /* 'G' 'w' */
    default: return -1;
    }
}

/* kart_reset_for_race 0x1b8c0 (after kart_init_defaults 0x1b7b8 with karts_init_all's values) */
void kart_reset(Kart *k, int slot, int gridPos, int human, const struct Entrant *e) {
    static const s32 grip[4] = { 800000, 900000, 950000, 1100000 };
    static const s32 brake[4] = { 700000, 900000, 1600000, 1800000 };
    static const s32 spin[4] = { 900000, 850000, 1200000, 1600000 };
    const u8 *g = gTrack.info.grid;
    const s32 *curve;
    memset(k, 0, sizeof *k);
    k->index = slot;
    k->mass = 0x57;
    k->rollingDrag = 200000;
    k->human = (u8)human;
    k->x = g[(gridPos & 7) * 2] * 1024;
    k->y = g[(gridPos & 7) * 2 + 1] * 1024;
    if (k->x == 0 && k->y == 0) {        /* (no grid: the viewer's start place) */
        k->x = gTrack.info.startX * 1024 + 512;
        k->y = gTrack.info.startY * 1024 + 512;
    }
    k->heading = k->headingPrev = gTrack.info.heading % 3600;
    k->cellX = k->x >> 10;
    k->cellY = k->y >> 10;
    k->damageMax = 0x400;
    k->driver = (u8)(slot * 2);          /* (karts_set_grid_order: the even drivers) */
    if (e) {                             /* the season's: upgrades, power-ups left */
        k->engineLevel = e->engine;
        k->gripLevel = e->tyres;
    }
    k->fuelMax = k->fuel = 42000 + 2000 * (e ? e->tank : 0);
    k->surface = 0x2e;
    k->checkpoint = 9;
    k->gripFactor = k->gripLevel * 2 + 0x40;
    k->power = k->engineLevel * 2 + 0x40;
    k->gripA = k->gripB = grip[sClass];
    k->brakeForce = brake[sClass];
    k->spinFriction = spin[sClass];
    k->spinDecay = 4000;
    curve = sClass == 0 ? (human ? sCurves : sCurves + 40) : sCurves + 40 * (sClass + 1);
    if (sCurves) memcpy(k->torque, curve, sizeof k->torque);
    k->bestLap = 0;
    k->fuelLapStart = k->fuel;           /* (kart_init_defaults / kart_reset_for_race) */
    k->nitroN = e ? e->nitroN : 3;
    k->gripN = e ? e->gripN : 3;
    k->nitroMax = k->gripMax = 5;
    k->awardN[0] = k->awardN[1] = k->awardN[2] = 1;
}

/* format_race_time 0x1bdac: seconds.hundredths (99.99 at most) */
static void gap_time(char *out, s32 frames) {
    int sec = frames / 60, c = (frames % 60) * 100 / 60;
    if (sec > 99) sec = 99;
    if (sec >= 10) *out++ = (char)('0' + sec / 10);
    *out++ = (char)('0' + sec % 10);
    *out++ = '.';
    *out++ = (char)('0' + c / 10);
    *out++ = (char)('0' + c % 10);
    *out = 0;
}

/* kart_lap_timing 0x1b448: crossing the line, the gap to the kart just ahead, for
 * both of them: the time since it crossed, or how many laps ahead it is. The text
 * starts with ',' (shown as the arrow for the kart ahead) or '-' (the kart behind) */
static void line_gap(Kart *k) {
    Kart *a = NULL;
    s32 dl;
    int i;
    if (k->position <= 1) return;
    for (i = 0; i < gKartN; i++) {
        if (gKarts[i].position == k->position - 1) a = &gKarts[i];
    }
    if (a == NULL) return;
    dl = kart_laps_done(a) - kart_laps_done(k);
    if (dl < 0) return;
    k->gap[a->index & 7].laps = -dl;
    a->gap[k->index & 7].laps = dl;
    if (dl == 0) {
        s32 t = k->lineStamp - a->lineStamp;
        k->gap[a->index & 7].text[0] = ',';
        gap_time(k->gap[a->index & 7].text + 1, t);
        a->gap[k->index & 7].text[0] = '-';
        gap_time(a->gap[k->index & 7].text + 1, t);
        k->gap[a->index & 7].time = -t;
        a->gap[k->index & 7].time = t;
    } else {
        char *p = k->gap[a->index & 7].text, *q = a->gap[k->index & 7].text;
        p[0] = ','; q[0] = '-';
        p[1] = q[1] = (char)(dl < 10 ? '0' + dl : '9');
        strcpy(p + 2, dl == 1 ? "LAP" : "LAPS");
        strcpy(q + 2, dl == 1 ? "LAP" : "LAPS");
    }
}

/* the original's laps_done: the first crossing of the line (off the grid) counts */
s32 kart_laps_done(const Kart *k) { return k->laps + (k->started ? 1 : 0); }

/* kart_compute_world_velocity 0x1bf28: speeds (capped at 39000) -> world velocity */
void kart_world_velocity(Kart *k) {
    s32 c = COS(k->heading), n = SIN(k->heading);
    if (k->speed > 39000) k->speed = 39000;
    if (k->speed < -39000) k->speed = -39000;
    if (k->side > 39000) k->side = 39000;
    if (k->side < -39000) k->side = -39000;
    k->vx = ((k->speed >> 6) * c - n * (k->side >> 6)) >> 8;
    k->vy = ((k->speed >> 6) * n + (k->side >> 6) * c) >> 8;
}

/* ------------------------------------------------------------------ controls */

/* FUN_3f0d8: engine upgrades give more power at some speeds */
static void engine_upgrade(Kart *k) {
    if (k->human) {
        u32 a = (u32)iabs(k->speed) >> 13;
        k->power = (k->engineLevel << (sUpShift[(sClass > 3 ? 3 : sClass) * 8 + (a > 7 ? 7 : a)] & 31)) + 0x40;
    }
}

/* kart_input_brake 0x1a010 / kart_input_brake_negforce 0x19ec4 (despite their names:
 * these drive the engine, forwards and backwards) */
void kart_engine(Kart *k, int dir) {
    int i;
    s32 f;
    engine_upgrade(k);
    k->engineOn = 1;
    k->braking = 0;
    i = speed_index(k->speed);
    f = k->torque[i];
    k->damagePct = 0;                    /* (the original clears these here) */
    k->damage = 0;
    if (i > 7 || (k->human && k->power > 63)) f = f * k->power >> 6;
    if (k->powerupTimer) f *= 2;
    if (k->fuel < 2000) {
        if (k->fuel == 0) f = k->speed < 10000 ? 600000 : 0;
        else f = k->torque[0];
    }
    k->engineForce = dir * f;
}

/* kart_input_accelerate 0x19ff4 (= brake) */
void kart_brake(Kart *k) { k->braking = 1; k->engineOn = 0; }
/* kart_input_release 0x19e80 */
void kart_release(Kart *k) { k->braking = 0; k->engineOn = 0; }
s32 kart_cos(int a) { return COS(a); }
s32 kart_sin(int a) { return SIN(a); }

/* input_keys_player1 0x22fbc, with the Dreamcast pad (the manual's keys: A accelerate,
 * Z brake, Left Alt reverse, comma / full stop steer): right trigger accelerate, left
 * trigger brake, Y reverse, stick or d-pad steer */
void kart_controls(Kart *k, const Input *in) {
    int accel = in->rtrig > 0.3f;
    int brake = in->ltrig > 0.3f;
    int back = (in->buttons & BTN_Y) != 0;
    int right = (in->buttons & BTN_RIGHT) || in->stickx > 0.3f;
    int left = (in->buttons & BTN_LEFT) || in->stickx < -0.3f;
    f32 mag = in->stickx < 0 ? -in->stickx : in->stickx;
    s32 lim = STEER_MAX, rate;
    if (!(in->buttons & (BTN_LEFT | BTN_RIGHT)) && mag < 0.95f) lim = (s32)(STEER_MAX * mag);   /* (analog: part-way) */

    k->power = 0x40;
    if (accel) kart_engine(k, 1);
    else if (brake) kart_brake(k);
    else if (back) kart_engine(k, -1);
    else kart_release(k);

    rate = k->braking ? (STEER_RATE * gFT >> 9) : (STEER_RATE * gFT >> 10);
    if (right) {
        k->steer += rate;
        if (k->steer >= lim) k->steer = lim;
    } else if (k->steer > 0) {
        k->steer -= STEER_BACK * gFT >> 10;
        if (k->steer < 0) k->steer = 0;
    }
    if (left) {
        k->steer -= rate;
        if (k->steer <= -lim) k->steer = -lim;
    } else if (k->steer < 0) {
        k->steer += STEER_BACK * gFT >> 10;
        if (k->steer > 0) k->steer = 0;
    }
}

/* ------------------------------------------------------------------ physics */

static s32 gC, gS;                       /* cos / sin of the heading (the original's 0xeb424 / 0xeb420) */

/* kart_compute_forces 0x1a860 */
static void compute_forces(Kart *k) {
    int t;
    k->fFwd = 0;
    k->fSide = 0;
    if (k->spinA != 0) {
        if (k->vx > 0) k->fX = -k->spinFriction;
        else if (k->vx < 0) k->fX = k->spinFriction;
        if (k->oil) k->fX >>= 3;
        if (k->vy > 0) k->fY = -k->spinFriction;
        else if (k->vy < 0) k->fY = k->spinFriction;
        if (k->oil) k->fY >>= 3;
        return;
    }
    if (k->z == 0) {
        if (k->braking) {
            if (k->speed > 0) k->fFwd = -k->brakeForce;
            else if (k->speed < 0) k->fFwd = k->brakeForce;
        } else {
            k->fFwd = k->engineOn ? k->engineForce : 0;
        }
        if (k->speed > 0) k->fFwd -= k->rollingDrag;
        else if (k->speed < 0) k->fFwd += k->rollingDrag;
        /* (computer drivers' wall-avoiding steering help: not for the player) */
        t = surface_drag(k->surface);
        if (t >= 0) {
            s32 d = sDrag[t][speed_index(k->speed)];
            if (k->speed > 0) k->fFwd -= d;
            else k->fFwd += d;
        }
        if (k->side > 0) {
            s32 f = -(k->gripA + k->gripB);
            if (!k->oil) {
                f = f * k->gripFactor >> 6;
                if (k->supergrip) f *= 2;
            } else {
                f >>= 3;
            }
            k->fSide = f;
            k->slide = k->side;
        } else if (k->side < 0) {
            s32 f = k->gripA + k->gripB;
            if (!k->oil) {
                f = f * k->gripFactor >> 6;
                if (k->supergrip) f *= 2;
            } else {
                f >>= 3;
            }
            k->fSide = f;
            k->slide = -k->side;
        } else {
            k->slide = 0;
        }
    }
    k->fX = (((k->fFwd >> 7) * gC) - ((k->fSide >> 7) * gS)) >> 7;
    k->fY = (((k->fFwd >> 7) * gS) + ((k->fSide >> 7) * gC)) >> 7;
}

/* kart_integrate_velocity 0x1afb8 */
static void integrate(Kart *k, int ft) {
    int sideStop = 0, fwdStop = 0;
    s32 v, d;

    d = ft * (k->fSide / k->mass) >> 10;
    if (k->side < 0) {
        k->side += d;
        if (k->side > 0) sideStop = 1;
    } else if (k->side > 0) {
        k->side += d;
        if (k->side < 0) sideStop = 1;
    }
    if (sideStop) { k->side = 0; k->fSide = 0; }

    d = ft * (k->fFwd / k->mass) >> 10;
    if (k->speed < 0) {
        k->speed += d;
        if (k->speed > 0) fwdStop = 1;
    } else if (k->speed > 0) {
        k->speed += d;
        if (k->speed < 0) fwdStop = 1;
    }
    if (fwdStop) { k->speed = 0; k->fFwd = 0; }

    if (sideStop) {
        if (k->speed == 0) { k->vx = 0; k->vy = 0; }
        else { k->vx = k->speed * gC >> 14; k->vy = k->speed * gS >> 14; }
        if (k->fFwd == 0) { k->fX = 0; k->fY = 0; }
        else { k->fX = (k->fFwd >> 7) * gC >> 7; k->fY = (k->fFwd >> 7) * gS >> 7; }
    }
    if (fwdStop) {
        if (k->side == 0) { k->vx = 0; k->vy = 0; }
        else { k->vx = -(k->side * gS) >> 14; k->vy = k->side * gC >> 14; }
        if (k->fSide == 0) { k->fX = 0; k->fY = 0; }
        else { k->fX = -((k->fSide >> 7) * gS) >> 7; k->fY = (k->fSide >> 7) * gC >> 7; }
    }

    /* up and down: gravity 18000, bounce on landing */
    if (k->vz > 0 || k->z > 0) {
        k->vz -= ft * 18000 >> 10;
        k->z += k->vz * ft >> 10;
        if (k->z < 0) {
            s32 b = (k->vz * -600) >> 11;
            k->vz = b;
            if (iabs(b) < 0x400) k->vz = 0;
            else k->soundFlag = 2;
            k->z = 0;
        }
    }

    v = k->vx;
    d = ft * (k->fX / k->mass) >> 10;
    if (v < 0) { k->vx = v + d; if (v + d > 0) k->vx = 0; }
    else if (v == 0) k->vx = d;
    else { k->vx = v + d; if (v + d < 0) k->vx = 0; }
    v = k->vy;
    d = ft * (k->fY / k->mass) >> 10;
    if (v < 0) { k->vy = v + d; if (v + d > 0) k->vy = 0; }
    else if (v == 0) k->vy = d;
    else { k->vy = v + d; if (v + d < 0) k->vy = 0; }

    if (k->vx == 0) {
        if (k->vy != 0) {
            k->speed = k->vy * gS >> 14;
            k->side = k->vy * gC >> 14;
        } else {
            k->speed = 0;
            k->side = 0;
        }
    } else if (k->vy == 0) {
        k->speed = k->vx * gC >> 14;
        k->side = -k->vx * gS >> 14;
    } else {
        k->speed = (k->vx * gC + k->vy * gS) >> 14;
        k->side = (k->vy * gC - k->vx * gS) >> 14;
    }
    if (k->side > -4 && k->side < 4) k->side = 0;
}

/* the turning part of kart_physics_step */
static void turn(Kart *k, int ft) {
    s32 s = k->steer, v = k->speed;
    if (s != 0 && v >= 0) {
        if (v > 3000) k->heading += s * 5 * ft >> 10;
        else if (v > 1000) k->heading += s * 5 * ft >> 11;
        else if (v > 10) k->heading += s * 5 * ft >> 12;
        else k->heading += s * 5 * ft >> 11;
    }
    if (k->spinA > 0) {
        k->heading += k->spinA * ft >> 10;
        k->spinA -= k->spinDecay * ft >> 10;
        if (k->spinA < 0) k->spinA = 0;
    } else if (k->spinA < 0) {
        k->heading += k->spinA * ft >> 10;
        k->spinA += k->spinDecay * ft >> 10;
        if (k->spinA > 0) k->spinA = 0;
    }
    if (k->spinB > 0) {
        k->heading += k->spinB * ft >> 10;
        k->spinB -= k->spinDecay * ft >> 10;
        if (k->spinB < 0) k->spinB = 0;
    } else if (k->spinB < 0) {
        k->heading += k->spinB * ft >> 10;
        k->spinB += k->spinDecay * ft >> 10;
        if (k->spinB > 0) k->spinB = 0;
    }
    if (k->heading >= 3600) k->heading -= 3600;
    else if (k->heading < 0) k->heading += 3600;
    gC = COS(k->heading);
    gS = SIN(k->heading);
}

/* kart_physics_step 0x1a138: two half steps when the frame is long */
void kart_physics(Kart *k) {
    int all = gFT, ft = all > 32 ? all >> 1 : all;
    if (sCos == NULL) return;
    k->headingPrev = k->heading;
    turn(k, ft);
    compute_forces(k);
    integrate(k, ft);
    k->mx = k->vx * ft >> 10;
    k->my = k->vy * ft >> 10;
    ft = all - ft;
    if (ft > 0) {
        turn(k, ft);
        compute_forces(k);
        integrate(k, ft);
        k->mx += k->vx * ft >> 10;
        k->my += k->vy * ft >> 10;
    }
    if (k->powerupTimer && (k->powerupTimer -= all) < 0) k->powerupTimer = 0;
    if (k->supergrip && (k->supergrip -= all) < 0) k->supergrip = 0;
    /* fuel: used by speed (twice as fast with nitro); reversing uses it too */
    if (k->speed < 1) k->fuel += (k->speed * all) >> (k->powerupTimer ? 15 : 16);
    else k->fuel -= (k->speed * all) >> (k->powerupTimer ? 15 : 16);
    k->damagePct = k->damage * 40 / k->damageMax;
    if (k->fuel < 0) k->fuel = 0;
}

/* ------------------------------------------------------------------ walls */

static Kart *sK;                         /* the kart being tested (0xec1d0) */
static s32 sBest;                        /* nearest wall hit so far, distance squared (0xec1e4) */
static int sHitCorner, sHitSide;         /* (0xec1e8, 0xec1e9) */
static s32 sRayX, sRayY;                 /* this frame's movement (0xec1d4, 0xec1d8) */
static s32 sTanA, sTanB;                 /* slope of the movement, and its inverse (0xec1c0, 0xec1cc) */

/* kart_check_cell_collision 0x22200: is the cell solid? Also: jumps, checkpoint lines */
static int check_cell(int c, int r) {
    Kart *k = sK;
    int i = r * 128 + c;
    u8 s, v;
    if (i < 0 || i > 0x3fff) return 0;
    if (MAP_WALLS(&gTrack)[i]) return 1;
    s = gTrack.surface[MAP_FLOOR(&gTrack)[i]];
    if (s == 0x42 && k->z == 0) k->vz = (s32)((u32)iabs(k->speed) / (u32)(rand() % 4 + 9));   /* 'B' bump */
    else if (s == 0x4a && k->z <= 99) k->vz = iabs(k->speed) / 3;                            /* 'J' jump */
    else if (s == 0x6a && k->z == 0) k->vz = iabs(k->speed) / 5;                             /* 'j' small jump */
    v = MAP_OBJECTS(&gTrack)[i];
    if (v < 0x60) {
        if (v == 9) {                    /* the start line */
            if (k->checkpoint == 9) {
                k->checkpoint++;
                if (k->started) {
                    k->laps++;
                    k->lastLap = k->lapFrames;
                    if (k->bestLap == 0 || k->lapFrames < k->bestLap) k->bestLap = k->lapFrames;
                    if (k->laps == 1) k->fuelPerLap = (k->fuelLapStart - k->fuel) * 3 >> 1;   /* (kart_lap_timing) */
                    if (k->human) game_lap_record(gTrack.index, k->lapFrames);   /* (the track records) */
                }
                k->started = 1;
                k->lapFrames = 0;
                k->lineStamp = race_clock();
                line_gap(k);
            }
        } else if (v > 9 && v < 0xe) {
            if (k->checkpoint == v) k->checkpoint++;
        } else if (v == 0xe) {
            if (k->checkpoint == 0xe) k->checkpoint = 9;
        }
        /* an object standing (or lying) here: where it is now, not where the map first
           put it (the original moves the object's code in the map as it's knocked about) */
        return objects_at(i) ? objects_hit(k, i) : 0;
    }
    if (v >= 0x6a && v < 0x6c) return 1; /* solid */
    return objects_at(i) ? objects_hit(k, i) : 0;   /* (pick-ups: not yet) */
}

/* collide_ray_x / _y / _diag 0x2181c...: from one corner along the movement, the first
 * wall cell crossed (as an x line, then as a y line); keeps the nearest. shA/shB: the
 * three originals differ only in how the slopes are scaled. */
static void ray(s32 px, s32 py, int corner, int shA, int shB) {
    int c = px >> 10, c1 = (sRayX + px) >> 10, r, r1;
    if (sRayX > 0) {
        for (; c < c1; c++) {
            int n = c + 1;
            s32 d = n * 1024 - px, yy = (s32)(((int64_t)sTanA * d) >> shA) + py;
            r = yy >> 10;
            if (r < 0 || r > 127) break;
            if (check_cell(n, r)) {
                int64_t e = (int64_t)d * d + (int64_t)(yy - py) * (yy - py);
                if (e < sBest && (n < 1 || !check_cell(c, r))) {
                    sBest = (s32)(e > 0x7ffffffe ? 0x7ffffffe : e);
                    sHitCorner = corner;
                    sHitSide = 0;
                }
                break;
            }
        }
    } else if (sRayX < 0) {
        for (; c1 < c; c--) {
            s32 d = (c * 1024 - 1) - px, yy = (s32)(((int64_t)sTanA * d) >> shA) + py;
            r = yy >> 10;
            if (r < 0 || r > 127) break;
            if (check_cell(c - 1, r)) {
                int64_t e = (int64_t)d * d + (int64_t)(yy - py) * (yy - py);
                if (e < sBest && (c - 1 > 127 || !check_cell(c, r))) {
                    sBest = (s32)(e > 0x7ffffffe ? 0x7ffffffe : e);
                    sHitCorner = corner;
                    sHitSide = 1;
                }
                break;
            }
        }
    }
    r = py >> 10;
    r1 = (sRayY + py) >> 10;
    if (sRayY > 0) {
        for (; r < r1; r++) {
            int n = r + 1;
            s32 d = n * 1024 - py, xx = (s32)(((int64_t)sTanB * d) >> shB) + px;
            c = xx >> 10;
            if (c < 0 || c > 127) return;
            if (check_cell(c, n)) {
                int64_t e = (int64_t)d * d + (int64_t)(xx - px) * (xx - px);
                if (sBest <= e) return;
                if (n < 1 || !check_cell(c, r)) {
                    sBest = (s32)e;
                    sHitCorner = corner;
                    sHitSide = 2;
                }
                return;
            }
        }
    } else if (sRayY < 0) {
        for (; r1 < r; r--) {
            s32 d = (r * 1024 - 1) - py, xx = (s32)(((int64_t)sTanB * d) >> shB) + px;
            c = xx >> 10;
            if (c < 0 || c > 127) return;
            if (check_cell(c, r - 1)) {
                int64_t e = (int64_t)d * d + (int64_t)(xx - px) * (xx - px);
                if (sBest <= e) return;
                if (r - 1 < 128 && check_cell(c, r)) return;
                sBest = (s32)e;
                sHitCorner = corner;
                sHitSide = 3;
                return;
            }
        }
    }
}

static void corners(const Kart *k, s32 *px, s32 *py) {
    int i, a = k->heading >> 4;
    for (i = 0; i < 8; i++) {
        px[i] = k->x + sCornerX[a][i];
        py[i] = k->y + sCornerY[a][i];
    }
}

static int corners_hit(const s32 *px, const s32 *py, s32 dx, s32 dy) {
    static const int order[8] = { 0, 1, 2, 3, 5, 4, 7, 6 };
    int i;
    for (i = 0; i < 8; i++) {
        int j = order[i];
        if (check_cell((px[j] + dx) >> 10, (py[j] + dy) >> 10)) return 1;
    }
    return 0;
}

/* kart_corner_wall_test 0x445c4 */
void kart_turn_test(Kart *k) {
    s32 px[8], py[8];
    sK = k;
    corners(k, px, py);
    if (corners_hit(px, py, 0, 0)) {
        k->spinA = 0;
        k->heading = k->headingPrev;
    }
}

/* kart_corner_wall_test2 0x447d4 */
int kart_boxed_in(Kart *k) {
    s32 px[8], py[8];
    sK = k;
    corners(k, px, py);
    return check_cell(k->x >> 10, k->y >> 10) || MAP_FLOOR(&gTrack)[(k->y >> 10) * 128 + (k->x >> 10)] == 0xef ||
           corners_hit(px, py, 0, 0);
}

/* the bounce off a wall (kart_collisions 0x20230): the component across the wall
 * comes back at 1/32, at least 5000 when slow */
static s32 bounce(Kart *k, s32 v) {
    s32 n = v * -0x40 >> 11;
    if (iabs(k->speed) < 4000) {
        if (v < 0) { if (n < 5000) n = 5000; }
        else if (n > -5000) n = -5000;
    }
    k->damage += iabs(n) >> 6;
    return n;
}

/* kart_collisions 0x20230, the kart-against-kart part: the first other kart (of a higher
 * number) about to overlap and still closing; along the line between them the speeds
 * mostly swap: this kart takes 7/8 of the other's plus 1/8 of its own, the other the
 * average of the two; across the line they keep their own.
 * (The original also adds damage and spins from the angle the karts meet at, using
 * angles it only keeps up to date for the kart on screen: not yet.) */
static void karts_bump(Kart *k) {
    int j;
    for (j = k->index + 1; j < gKartN; j++) {
        Kart *o = &gKarts[j];
        s32 dx, dy, d2, a, c, s, kn, kt, on, ot, rel, nk;
        if (o->z + 0x352 < k->z || k->z + 0x352 < o->z) continue;
        dx = (o->x + o->mx) - (k->x + k->mx);
        dy = (o->y + o->my) - (k->y + k->my);
        if (iabs(dx) > 0x707 || iabs(dy) > 0x707) continue;
        d2 = dx * dx + dy * dy;
        if (d2 > 0x31703f) continue;
        if ((o->x - k->x) * (o->x - k->x) + (o->y - k->y) * (o->y - k->y) <= d2) continue;   /* not closing */
        a = (int)(shz_atan2f((f32)dy, (f32)dx) * (1800.0f / PI_F));
        if (a < 0) a += 3600;
        if (a >= 3600) a -= 3600;
        c = COS(a);
        s = SIN(a);
        kn = (k->vx * c + k->vy * s) >> 14;
        kt = (k->vy * c - k->vx * s) >> 14;
        on = (o->vx * c + o->vy * s) >> 14;
        ot = (o->vy * c - o->vx * s) >> 14;
        rel = iabs(kn - on);
        nk = (on * 0x700 + kn * 0x100) >> 11;
        on = ((kn - on) * 0x300 + on * 0x700 + kn * 0x100) >> 11;
        k->vx = (c * nk - kt * s) >> 14;
        k->vy = (s * nk + kt * c) >> 14;
        o->vx = (c * on - ot * s) >> 14;
        o->vy = (s * on + ot * c) >> 14;
        k->mx = k->vx * gFT >> 10;
        k->my = k->vy * gFT >> 10;
        o->mx = o->vx * gFT >> 10;
        o->my = o->vy * gFT >> 10;
        c = COS(k->heading); s = SIN(k->heading);
        k->speed = (k->vx * c + k->vy * s) >> 14;
        k->side = (k->vy * c - k->vx * s) >> 14;
        c = COS(o->heading); s = SIN(o->heading);
        o->speed = (o->vx * c + o->vy * s) >> 14;
        o->side = (o->vy * c - o->vx * s) >> 14;
        if (rel > 1000) k->hitFlag = o->hitFlag = 0x80;
        if (rel > 3000) k->hitFlag = o->hitFlag = 0xff;
        return;
    }
}

/* kart_collisions 0x20230, the wall part */
static void collisions(Kart *k) {
    s32 px[8], py[8], hitSpeed = 0;
    int dir, i, shA = 14, shB = 14;
    static const int rayOrder[8] = { 1, 2, 0, 3, 4, 6, 5, 7 };
    sK = k;
    k->hitFlag = 0;
    karts_bump(k);
    corners(k, px, py);
    if (corners_hit(px, py, 0, 0)) {     /* turned into a wall: turn back */
        k->heading = k->headingPrev;
        k->spinA = 0;
        corners(k, px, py);
    }
    sRayX = k->mx;
    sRayY = k->my;
    if (sRayX == 0 && sRayY == 0) {
        dir = k->heading;
    } else {
        dir = (int)(shz_atan2f((f32)sRayY, (f32)sRayX) * (1800.0f / PI_F));
        if (dir < 0) dir += 3600;
        if (dir >= 3600) dir -= 3600;
    }
    k->moveDir = dir;
    if (dir == 2700 || dir == 900) sRayX = 0;
    if (dir == 0 || dir == 1800) sRayY = 0;
    sTanA = TAN(dir);
    sTanB = TAN(900 - dir);
    if (sTanA <= 400000 && sTanA >= -400000) {
        if (sTanB > 400000 || sTanB < -400000) { sTanB >>= 3; shB = 11; }
    } else {
        sTanA >>= 3;
        shA = 11;
    }
    sBest = 0x7fffffff;
    for (i = 0; i < 8; i++) ray(px[rayOrder[i]], py[rayOrder[i]], rayOrder[i], shA, shB);

    if (sBest < 0x7fffffff) {
        s32 c, s;
        k->spinA = 0;
        if (sHitSide < 2) { hitSpeed = iabs(k->vx); k->vx = bounce(k, k->vx); }
        else { hitSpeed = iabs(k->vy); k->vy = bounce(k, k->vy); }
        if (sHitCorner == 1) k->spinB = 1000;
        else if (sHitCorner == 2) k->spinB = -1000;
        c = COS(k->heading);
        s = SIN(k->heading);
        k->mx = k->vx * gFT >> 10;
        k->my = k->vy * gFT >> 10;
        k->speed = (k->vy * s + k->vx * c) >> 14;
        k->side = (k->vy * c - s * k->vx) >> 14;
        if (hitSpeed > 3000) k->hitFlag = (u8)(sHitCorner + 1);
    }
    if (corners_hit(px, py, k->mx, k->my)) {   /* still going into a wall: stop dead */
        k->mx = k->my = 0;
        k->vx = k->vy = 0;
        k->speed = k->side = 0;
        k->spinA = 0;
        k->hitFlag = 0xff;
        k->heading = k->headingPrev;
    }
    if (k->damage > k->damageMax) k->damage = k->damageMax;
}

/* kart_move 0x1a748 */
void kart_move(Kart *k) {
    int c = k->x >> 10, r = k->y >> 10;
    if (c > 0 && c < 128 && r > 0 && r < 128) {
        k->x += k->mx;
        k->y += k->my;
        k->cellX = k->x >> 10;
        k->cellY = k->y >> 10;
        if (k->cellX >= 0 && k->cellX < 128 && k->cellY >= 0 && k->cellY < 128)
            k->surface = gTrack.surface[MAP_FLOOR(&gTrack)[k->cellY * 128 + k->cellX]];
    }
}

void kart_collide(Kart *k) {
    if (sCos == NULL || gTrack.maps == NULL) return;
    collisions(k);
    k->lapFrames++;
}

/* ------------------------------------------------------------------ drawing */

/* angle of each of the 64 views, 256 = a full turn (K.EXE 0xb9dfc): many views
 * close to straight behind, where the player's own kart is usually seen */
static const u8 kViewAngle[64] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 26, 30, 38, 46, 54,
    62, 70, 78, 86, 94, 102, 110, 118, 128, 137, 145, 153, 161, 169, 177, 185, 193, 201,
    209, 217, 225, 229, 233, 235, 237, 239, 241, 242, 243, 244, 245, 246, 247, 248, 249,
    250, 251, 252, 253, 254 };

/* the nearest view for each angle (as the original's table at 0x104510) */
static int view_for(int a256) {
    static u8 lut[256];
    static int made;
    if (!made) {
        int u, j;
        for (u = 0; u < 255; u++) {
            for (j = 0;; j++) {
                if (u <= kViewAngle[j]) break;
                if (j == 63) { lut[u] = 63; break; }
                lut[u] = (u - kViewAngle[j] < kViewAngle[j + 1] - u) ? (u8)j : (u8)(j + 1);
            }
        }
        made = 1;
    }
    return lut[a256 & 255];
}

/* the kart as a sprite standing on the floor, the view picked by the angle between the
 * kart and the camera: 128 x 64 pixels, 64 pixels a cell as all the original's sprites
 * (the kart itself fills about 110 of the 128: its 1700-unit collision box) */
#define KART_HW 1.0f                     /* half the width */
#define KART_H  1.0f

void kart_draw(const Kart *k) {
    WV w;
    CV c, p[4];
    f32 cx = gCam.pos.x, cr = MAP_N - gCam.pos.z;     /* the camera in map columns / rows */
    f32 dx = k->x * (1.0f / 1024) - cx, dr = k->y * (1.0f / 1024) - cr;
    int bearing = (int)(shz_atan2f(dr, dx) * (1800.0f / PI_F));
    int rel = ((bearing - k->heading) % 3600 + 3600) % 3600;   /* (views go anticlockwise) */
    int v = view_for(rel * 256 / 3600), tex;
    f32 u0, v0, du = 0.125f, dv = 0.125f;
    int b = 255;
    u32 col;
    if (dx * dx + dr * dr > 80.0f * 80.0f) return;   /* (as far as the walls) */
    {   /* every kart full size: karts 2p and 2p+1 one above the other in texture p */
        int d = k->index & 7;            /* (the kart's picture) */
        tex = gTrack.texKarts[d >> 1];
        dv = 0.0625f;
        u0 = (v % 8) * 0.125f;
        v0 = (d & 1) * 0.5f + (v / 8) * 0.0625f;
    }
    if (tex < 0 || gTrack.light == NULL) return;
    if (k->cellX >= 0 && k->cellX < 128 && k->cellY >= 0 && k->cellY < 128)
        b = gTrack.light[k->cellY * 128 + k->cellX];
    col = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
    w = wv(v3(kart_wx(k), kart_wy(k), kart_wz(k)), 0, 0, col);
    r_xform(&w, &c);
    /* corners in camera space: upright, facing the camera */
    p[0] = c; p[0].x -= KART_HW; p[0].y += KART_H; p[0].u = u0;      p[0].v = v0;
    p[1] = c; p[1].x += KART_HW; p[1].y += KART_H; p[1].u = u0 + du; p[1].v = v0;
    p[2] = c; p[2].x += KART_HW;                   p[2].u = u0 + du; p[2].v = v0 + dv;
    p[3] = c; p[3].x -= KART_HW;                   p[3].u = u0;      p[3].v = v0 + dv;
    r_tri_cv(&p[0], &p[1], &p[2], tex, LIST_CUTOUT, 0);
    r_tri_cv(&p[0], &p[2], &p[3], tex, LIST_CUTOUT, 0);
    r_flush();
}
