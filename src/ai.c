/*
 * ai.c - the computer drivers: ai_drive_kart 0x4230c and the maps it drives by,
 * translated from the decompile.
 *
 * Each track has three route maps (the .KTF's maps 2, 3 and 4). Every cell holds
 * a zone number, and somewhere in the map one "marker" cell (0x36 + the zone)
 * marks the point that zone's karts head for:
 *   map 3 (ai_build_route_b 0x41f08): where to steer for, from any cell;
 *   map 4 (ai_build_route_c 0x420a8): the racing line's points in order, which
 *         also tell how far round the lap each kart is (for race positions).
 * Map 5 adds a few special points: a choice of two ways round (0x20 / 0x22) and
 * the pit lane (0x2e).
 *
 * A computer driver steers for its cell's point, turns away from karts close in
 * front, brakes for the track's slow zones (map 7) and when about to run into
 * someone, and backs out when stuck. Any kart (the player's too) stuck for over
 * 5 seconds is turned to face the route again, or, when hemmed in by walls,
 * lifted and floated back onto it.
 *
 * A driver short of fuel for another lap heads into the pit lane (map 5's 0x2e
 * point) and slows for it; power-ups are in powerup.c.
 *
 * Not yet: the steering help near walls (kart_compute_forces' raycasts).
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

Kart gKarts[MAX_KARTS];
int gKartN;

static u8 sTarget[16384][3];             /* route b: column, row of the point to steer for; zone */
static u8 sLine[16384][3];               /* route c: the racing line's point; zone */
/* map 5's special points (track_place_objects 0x4331c; 0xfe = none) */
static u8 sAltA, sAltACol, sAltARow;     /* 0x20: zone, point */
static u8 sAltB, sAltBCol, sAltBRow, sAltOn, sAltOff;   /* 0x22: zone, point, zones that switch it on / off */
static u8 sAltZone, sAltPos;             /* 0x22: zone and position for race positions */
static u8 sPitZone = 0xfe, sPitCol, sPitRow;   /* 0x2e: the pit lane's zone and point (0xfe0d3, 0xfe0c4, 0xfe0c0) */

/* where each zone's marker is */
static void markers(const u8 *m, int mk[0x34][2], int *last) {
    int i, k;
    memset(mk, 0, sizeof(int) * 0x34 * 2);
    *last = 0;
    for (i = 0; i < 16384; i++) {
        k = m[i] - 0x36;
        if (k >= 0 && k < 0x34) {
            mk[k][0] = i % 128;
            mk[k][1] = i / 128;
            if (k > *last) *last = k;
        }
    }
}

void ai_load(void) {
    static u8 m3[16384];
    const u8 *m4 = gTrack.maps + 4 * 16384, *m5 = MAP_OBJECTS(&gTrack);
    int mk[0x34][2], last, i, row, col;
    if (gTrack.maps == NULL) return;

    /* route b: the marker cells take their left neighbour's zone */
    memcpy(m3, gTrack.maps + 3 * 16384, 16384);
    markers(m3, mk, &last);
    for (i = 1; i < 16384; i++) {
        if (m3[i] > 0x35) m3[i] = m3[i - 1];
    }
    for (i = 0; i < 16384; i++) {
        int z = m3[i];
        if (z < 0x34) {
            sTarget[i][0] = (u8)mk[z][0];
            sTarget[i][1] = (u8)mk[z][1];
            sTarget[i][2] = (u8)z;
        } else {
            memset(sTarget[i], 0, 3);
        }
    }

    /* route c: zones head for their marker; each marker for the next (the last for the first) */
    markers(m4, mk, &last);
    for (i = 0; i < 16384; i++) {
        int v = m4[i];
        memset(sLine[i], 0, 3);
        if (v >= 0x36 && v < 0x36 + 0x34) {
            int k = v - 0x36, n = k >= last ? 0 : k + 1;
            sLine[i][0] = (u8)mk[n][0];
            sLine[i][1] = (u8)mk[n][1];
            sLine[i][2] = (u8)k;
        } else if (v < 0x34) {
            sLine[i][0] = (u8)mk[v][0];
            sLine[i][1] = (u8)mk[v][1];
            sLine[i][2] = (u8)v;
        }
    }

    /* map 5's special points */
    sAltA = sAltB = sAltOn = sAltOff = sAltZone = sAltPos = sPitZone = 0xfe;
    for (row = 0; row < 128; row++) {
        for (col = 0; col < 128; col++) {
            const u8 *p = m5 + row * 128 + col;
            int left = 127 - col;
            switch (*p) {
            case 0x20:
                if (left >= 1) { sAltA = p[1]; sAltACol = (u8)(col + 1); sAltARow = (u8)row; col += 2; }
                break;
            case 0x22:
                if (left >= 5) {
                    sAltB = p[1]; sAltBCol = (u8)(col + 3); sAltOn = p[2]; sAltOff = p[3];
                    sAltBRow = (u8)row; sAltZone = p[4]; sAltPos = p[5];
                    col += 6;
                }
                break;
            case 0x2e:
                if (left >= 1) { sPitZone = p[1]; sPitCol = (u8)col; sPitRow = (u8)row; }
                col += 1;
                break;
            case 0x24:
                col += 1;
                break;
            }
        }
    }
}

static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }

/* the original's quick distance: the longer side plus half the shorter */
static inline s32 octdist(s32 dx, s32 dy) {
    dx = iabs(dx);
    dy = iabs(dy);
    return dy < dx ? (dx >> 1) + dy : (dy >> 1) + dx;
}

static int angle_to(s32 dx, s32 dy) {
    int a = (int)(shz_atan2f((f32)dy, (f32)dx) * (1800.0f / PI_F));
    if (a < 0) a += 3600;
    if (a >= 3600) a -= 3600;
    return a;
}

/* karts_update_relative_angles 0x416d0 */
void karts_relative(void) {
    int i, j;
    for (i = 0; i < gKartN; i++) {
        Kart *a = &gKarts[i];
        int cell = (a->y >> 10) * 128 + (a->x >> 10);
        s32 d;
        for (j = i + 1; j < gKartN; j++) {
            Kart *b = &gKarts[j];
            s32 dx = a->x - b->x, dy = a->y - b->y, dist = octdist(dx, dy);
            a->rel[j].angle = angle_to(dx, dy);
            b->rel[i].angle = angle_to(-dx, -dy);
            a->rel[j].closing = b->rel[i].closing = dist - a->rel[j].dist;
            a->rel[j].dist = b->rel[i].dist = dist;
        }
        if (cell < 0 || cell >= 16384) continue;
        d = octdist(sLine[cell][0] * 1024 - a->x, sLine[cell][1] * 1024 - a->y);
        if (sLine[cell][2] == sAltZone && a->altRoute) a->progress = sAltPos * 200000;
        else a->progress = sLine[cell][2] * 200000 - d + 199999;
        a->progress += a->laps * 15000000;
        if (a->finished) a->progress = 0x7f000000 - a->finished;   /* (finished: placed for good) */
        if (a->retired) a->progress = -1;                          /* (retired: last) */
    }
    /* positions: sorted by progress (ties: the later kart first), but a kart only
       takes a new place once the sort has had it there for 6 frames running, so
       the order doesn't flicker as karts cross the line or run side by side */
    {
        static int sorted[MAX_KARTS], prev[MAX_KARTS], same[MAX_KARTS], order[MAX_KARTS];
        int n = 0, p, fresh = 0;
        for (i = 0; i < gKartN; i++) {
            s32 v = gKarts[i].progress < 0 ? 0 : gKarts[i].progress;
            for (p = 0; p < n && gKarts[sorted[p]].progress > v; p++) {}
            for (j = n; j > p; j--) sorted[j] = sorted[j - 1];
            sorted[p] = i;
            n++;
            if (gKarts[i].position < 1 || gKarts[i].position > gKartN) fresh = 1;
        }
        if (fresh) {                     /* (a new race: take the sort as it is) */
            for (p = 0; p < gKartN; p++) {
                order[p] = prev[p] = sorted[p];
                same[p] = 0;
                gKarts[sorted[p]].position = p + 1;
            }
            return;
        }
        for (p = 0; p < gKartN; p++) {
            if (sorted[p] == prev[p]) same[p]++;
            else { prev[p] = sorted[p]; same[p] = 0; }
        }
        for (p = 0; p < gKartN; p++) {
            if (same[p] > 5) {
                int c = sorted[p], cur = order[p], q = gKarts[c].position - 1;
                order[q] = cur;
                order[p] = c;
                gKarts[cur].position = q + 1;
                gKarts[c].position = p + 1;
                same[p] = 0;
                same[cur] = 0;               /* (as the original: by kart number) */
            }
        }
    }
}

/* the nearest wall, out to 66/64 of a cell, in 40 directions (the original's raycasts) */
static int walls_close(const Kart *k) {
    int a, s;
    for (a = 0; a < 3600; a += 90) {
        s32 c = kart_cos(a), n = kart_sin(a);
        for (s = 1; s <= 16; s++) {
            s32 r = 66 * 16 * s / 16;    /* (1/1024 cells) */
            s32 x = k->x + (c * r >> 14), y = k->y + (n * r >> 14);
            if (track_is_wall(x >> 10, y >> 10)) return 1;
        }
    }
    return 0;
}

/* corner speeds for the track's slow zones (kart_reset_for_race, by engine class: the
 * original's 0x1296fc, 0x12977c, 0x129780, 0x129768 for zones 1/6, 2/7, 3/8, 4/9) */
static const s32 kSlowTab[4][4] = {
    { 20000, 20000, 20000, 20000 }, { 0x47e0, 0x35e8, 0x23f0, 0x11f8 },
    { 0x5460, 0x3f48, 0x2a30, 0x1518 }, { 27000, 0x5000, 0x34bc, 7000 },
};
#define kSlow kSlowTab[gGame.cls & 3]

void ai_drive(Kart *k, int computer) {
    int cell = k->cellY * 128 + k->cellX, zone, slow, tang, diff, act = 0, i, inPit;
    s32 tx, ty, dx, dy, st = 0, distSum;
    if (cell < 0 || cell >= 16384 || gTrack.maps == NULL) return;
    if (kart_laps_done(k) == gRaceLaps && k->fuel > 5000) k->wantPit = 0;   /* (the last lap: carry on) */
    zone = sTarget[cell][2];
    inPit = zone == sPitZone;
    tx = sTarget[cell][0] << 10;
    ty = sTarget[cell][1] << 10;
    slow = MAP_SURFACE(&gTrack)[cell];
    if (slow > 0x35) slow -= 0x36;
    if (k->wantPit && inPit) {           /* in for fuel: head for the pit point */
        tx = sPitCol << 10;
        ty = sPitRow << 10;
    } else if (sAltOff != 0xfe) {        /* the two ways round */
        if (zone == sAltOn) k->altRoute = 1;
        if (zone == sAltOff) k->altRoute = 0;
        if (zone == sAltB && k->altRoute) { tx = sAltBCol << 10; ty = sAltBRow << 10; }
        else if (zone == sAltA && !k->altRoute) { tx = sAltACol << 10; ty = sAltARow << 10; }
    }
    dx = tx - k->x;
    dy = ty - k->y;
    distSum = octdist(dx, dy);
    tang = angle_to(dx, dy);
    diff = tang - k->heading;
    if (diff < -1799) diff += 3600;
    if (diff > 1799) diff = -(3600 - diff);

    if (computer) {
        int avoidSurf = k->surface == 'e' || k->surface == 'i', slowZone = slow >= 5 && slow <= 9;
        st = diff >> 1;
        if (st > 300) st = 300;
        if (st < -300) st = -300;
        for (i = 0; i < gKartN; i++) {
            s32 d, b, cl;
            if (i == k->index || avoidSurf) continue;
            d = k->rel[i].dist;
            if (d >= 0x1900) continue;
            b = k->heading - k->rel[i].angle - 1800;          /* where the other kart is, from our heading */
            if (b < 0) b += 3600;
            if (b < 0) b += 3600;
            if (b > 3599) b -= 3600;
            if (b > 3599) b -= 3600;
            cl = k->rel[i].closing;
            if (slowZone) {              /* only brake for it */
                if (d > 50 && d < 1200 && (b < 700 || b > 2899) && cl < -5) act = 2;
                continue;
            }
            if (d <= 50) continue;
            if (d < 1200) {
                if (b < 550) { st += 480; if (cl < -5) act = 2; }
                else if (b < 3050) {
                    if (b < 700) { if (cl < -5) act = 2; st += 120; }
                    else if (b > 2899) { if (cl < -5) act = 2; st -= 120; }
                } else { st -= 480; if (cl < -5) act = 2; }
            } else {
                if (b < 400) st += 80;
                else if (b < 3200) {
                    if (b < 600) st += 40;
                    else if (b > 2999) st -= 40;
                } else st -= 80;
            }
        }
    }

    /* stuck: back out, then brake, then (after 5 s) turn to the route or be lifted onto it */
    if (k->speed < 1000 && k->speed > -6001 && k->stuck < 6000) {
        k->stuck += gFT;
        if (computer) {
            if (k->stuck > 2000 && k->stuck < 3250) { st = -st; act = 3; }
            if (k->stuck > 4000 && k->stuck < 4250) act = 2;
        }
        if (k->stuck > 5000) {
            if (k->stuckX == 0) { k->stuckX = tx; k->stuckY = ty; }
            if (kart_boxed_in(k) || walls_close(k)) {
                s32 ex, ey, d;
                k->z = k->z < 0x400 ? k->z + 0x100 : 0x400;
                k->vz += gFT * 18000 >> 10;
                ex = k->stuckX - k->x;
                ey = k->stuckY - k->y;
                if (iabs(ex + ey) <= 0x200) {
                    k->stuckX = tx; k->stuckY = ty;
                    ex = k->stuckX - k->x;
                    ey = k->stuckY - k->y;
                    k->stuck = 5001;
                }
                if (iabs(ex + ey) <= 0x200) {
                    k->stuck = 0; k->stuckX = k->stuckY = 0; k->rescue = 0;
                    goto done;
                }
                k->stuck = 5001;
                d = octdist(ex, ey);         /* (float a cell a frame towards it) */
                if (d > 1000) { ex = ex * 1000 / d; ey = ey * 1000 / d; }
                k->x += ex;
                k->y += ey;
                k->cellX = k->x >> 10;
                k->cellY = k->y >> 10;
                k->vx = k->vy = 0;
                k->rescue = 1;
            } else {
                k->rescue = 0;
                k->headingPrev = k->heading;
                if (iabs(diff) < 41) {
                    k->stuck = 0; k->stuckX = k->stuckY = 0;
                    k->heading = tang;
                } else {
                    k->heading += diff > 0 ? 0x27 : -0x27;
                    if (k->heading >= 3600) k->heading -= 3600;
                    if (k->heading < 0) k->heading += 3600;
                }
                kart_turn_test(k);
            }
        }
    } else {
        k->stuck = 0;
        k->rescue = 0;
    }
done:
    if (k->spinA != 0) { k->stuck = 0; k->rescue = 0; }
    if (k->slide > 2000) act = 4;
    if (!computer || k->stuck > 4999) return;

    /* slow zones (map 7); with super grip on, only the two slowest */
    if (slow >= 1 && slow <= 9 && slow != 5) {
        int z = (slow - 1) % 5;          /* 1/6, 2/7, 3/8, 4/9 */
        if (k->supergrip) {
            if (z == 2 && k->speed > kSlow[0]) act = 2;
            if (z == 3 && k->speed > kSlow[1]) act = 2;
        } else if (k->speed > kSlow[z]) act = 2;
    }
    /* going in for fuel: slow down in the pit lane */
    if (k->wantPit && inPit && !powerups_pit_no_brake() && k->speed > kSlow[1]) act = 2;

    switch (act) {
    case 0:
        if (k->speed < 9000) kart_engine(k, 1);
        else if (st < -219 || st > 219) {
            if (st == -300 || st == 300) kart_brake(k);
            else kart_release(k);
        } else kart_engine(k, 1);
        break;
    case 1: kart_engine(k, 1); break;
    case 2: kart_brake(k); break;
    case 3: kart_engine(k, -1); break;
    case 4: kart_release(k); break;
    }
    if (st > 300) st = 300;
    if (st < -300) st = -300;
    {
        s32 rate = gFT * 600 >> 10;
        (void)distSum;
        if (st + 40 < k->steer) k->steer -= rate;
        else if (k->steer < st - 40) k->steer += rate;
        else k->steer = st;
    }
    if (!race_frozen()) ai_powerups(k, inPit);
}
