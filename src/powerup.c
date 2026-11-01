/*
 * powerup.c - power-ups, fuel and the pit lane, translated from the decompile:
 *
 *   karts_surface      FUN_43978 (every frame, every kart, in race order): each
 *                      kart's grip and power from its driver (the computer's) or
 *                      its upgrades (the player's), changed by the surface under
 *                      it; the pit lane ('p'): fuel back in, damage off, and a
 *                      power-up for every second or so spent there; boost ('s') and
 *                      slow ('L') strips; cobbles ('I') shake the kart; oil ('o').
 *                      The computer drivers also get the track's catch-up table:
 *                      the leaders less grip and power, the karts behind the player
 *                      more.
 *   karts_powerups     karts_update_powerups 0x3ec80: with the button held, the
 *                      picked power-up fires when the last one has (nearly) run out:
 *                      nitro (twice the engine force, and fuel, for 4 s) or super
 *                      grip (twice the grip for 4.5 s).
 *   ai_powerups        the end of ai_drive_kart: map 5's power-up zones (0x30 nitro
 *                      and grip, 0x31 nitro, 0x33 grip), more likely to be used the
 *                      further into the race; and the decision to pit when the fuel
 *                      won't last another lap.
 *
 * Every kart starts with 3 nitros and 3 super grips (at most 5) and 42000 of fuel,
 * used at speed x frame time / 65536 (twice that with nitro): about 10 laps.
 *
 * Not here: oil. The original's oiling option is off unless started with /O1, and
 * no track's map has the computer drivers' oil zones (0x32), so no oil is dropped.
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

#define NITRO_TIME 4000                  /* xRam0012a950 */
#define GRIP_TIME  0x1194                /* xRam0012a940 */
#define FUEL_TANK  42000                 /* xRam0012a95c */

/* each driver's grip and power (the driver table, +0xa and +0xc, set up in
 * init_default_settings): the computer drivers' karts are tuned differently */
static const u8 kDriverGrip[16]  = { 64, 66, 74, 66, 66, 64, 66, 74, 66, 62, 74, 74, 64, 64, 62, 74 };
static const u8 kDriverPower[16] = { 66, 56, 66, 72, 56, 66, 64, 72, 56, 72, 64, 66, 64, 64, 56, 64 };

static struct {
    s32 pitAward[4];
    struct { u8 pitNoBrake, pad[3]; s16 ai[4][16]; } track[16];
} *sRace;
static int sTrack;
#define sClass (gGame.cls & 3)

void powerups_load(int track) {
    u32 size;
    if (sRace == NULL) {
        const u8 *d = (const u8 *)plat_load_file("race.bin", &size);
        if (d && size >= 8 + sizeof *sRace && memcmp(d, "MKRC", 4) == 0) sRace = (void *)(d + 8);
    }
    sTrack = track & 15;
}

int powerups_pit_no_brake(void) { return sRace ? sRace->track[sTrack].pitNoBrake : 0; }

static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }

/* the race order: kart numbers by position (the original's 0x12a824) */
static void by_position(int ord[MAX_KARTS]) {
    int i, n = 0, p;
    for (p = 1; p <= gKartN; p++) {
        for (i = 0; i < gKartN; i++) {
            if (gKarts[i].position == p) { ord[n++] = i; break; }
        }
    }
    for (i = n; i < gKartN; i++) ord[i] = i;    /* (no positions yet) */
}

void karts_surface(void) {
    int ord[MAX_KARTS], p, bonus = 0;
    by_position(ord);
    for (p = 0; p < gKartN; p++) {
        Kart *k = &gKarts[ord[p]];
        int grip, power, decay = 1, cell = k->cellY * 128 + k->cellX;
        s32 pit = k->pitTime < 0 ? 0 : k->pitTime;
        if (k->human) {
            grip = k->gripLevel * 2 + 0x40;
            power = k->engineLevel * 2 + 0x40;
        } else {
            grip = kDriverGrip[k->driver & 15] + k->gripLevel * 2;   /* (driver = 2 x the kart) */
            power = kDriverPower[k->driver & 15] + k->engineLevel * 2;
        }
        /* oil on the floor: 1.6 s of sliding, and a spin */
        if (cell >= 0 && cell < 16384 && gTrack.surface[MAP_FLOOR(&gTrack)[cell]] == 'o' && k->z == 0) {
            k->oil = 0x640;
            k->spinA = k->speed * k->steer / 500;
        }
        if (k->oil && (k->oil -= gFT) < 0) k->oil = 0;

        switch (k->surface) {
        case 'C': case 'D': grip += 30; break;
        case 'G': grip -= 30; break;
        case 'I':                        /* cobbles: shaken */
            if (k->z == 0) k->vz = iabs(k->speed) / 20;
            grip += 15;
            break;
        case 'L':                        /* slow strip */
            decay = 0;
            if (k->z == 0) { k->speed -= 1000; kart_world_velocity(k); }
            break;
        case 'M': grip -= 10; break;
        case 'N': grip -= 15; break;
        case 'S': case 'd': grip -= 8; break;
        case 'T': grip += 5; break;
        case 'W': grip += 8; break;
        case 'b': grip -= 4; break;
        case 'c': case 'e': case 'l': grip += 10; break;
        case 'g': case 'm': grip -= 10; break;
        case 'i': grip -= 40; break;
        case 'r': grip += 20; break;
        case 's':                        /* boost strip */
            decay = 0;
            if (k->z == 0) { k->speed += 1000; kart_world_velocity(k); }
            break;
        case 't': grip += 3; break;
        case 'w': grip -= 30; break;
        case '.': break;
        case 'p': {                      /* the pit lane */
            s32 a = iabs(k->speed + 6000);
            decay = 0;
            k->fuel += a * gFT >> 11;
            if (k->fuel > FUEL_TANK) k->fuel = FUEL_TANK;
            k->damage -= a * gFT >> 16;
            if (k->damage < 0) k->damage = 0;
            k->wantPit = 0;
            if (!k->finished) {
                pit += gFT >> 1;
                if (sRace && pit > sRace->pitAward[sClass]) pit = -100;
            }
            break;
        }
        default: decay = 0; break;
        }
        if (decay && pit) pit -= gFT * 2;
        if (grip < 10) grip = 10;
        if (grip > 0xb4) grip = 0xb4;
        if (power < 10) power = 10;
        if (power > 0xb4) power = 0xb4;
        if (!k->human) {                 /* the catch-up table, by race position */
            const s16 *t = sRace ? sRace->track[sTrack].ai[sClass] : NULL;
            k->power = power + (t ? t[8 + p] : 0) + (bonus >> 1);
            k->gripFactor = grip + (t ? t[p] : 0) + bonus;
            bonus -= 2;
        } else {
            k->power = power;
            k->gripFactor = grip;
            bonus = 16;
        }

        /* a power-up earned in the pit lane: one more of the picked kind (more if
           earned again soon after), up to 5; when that kind is full, the other */
        if (pit == -100) {
            if (k->selected == 0) {
                if (k->awardT[0]) k->awardN[0]++;
                if (k->nitroN + k->awardN[0] > k->nitroMax) k->awardN[0] = k->nitroMax - k->nitroN;
                if (k->nitroN == k->nitroMax || k->awardN[0] == 0) { k->awardT[0] = 0; k->selected = 2; }
                else k->awardT[0] = 0x6e;
                k->nitroN += k->awardN[0];
            } else if (k->selected == 2) {
                if (k->awardT[2]) k->awardN[2]++;
                if (k->gripN + k->awardN[2] > k->gripMax) k->awardN[2] = k->gripMax - k->gripN;
                if (k->gripN == k->gripMax || k->awardN[2] == 0) { k->awardT[2] = 0; k->selected = 0; }
                else k->awardT[2] = 0x6e;
                k->gripN += k->awardN[2];
            }
            pit = 0;
        }
        k->pitTime = pit;
        {
            int j;
            for (j = 0; j < 3; j++) {
                if (k->awardT[j] == 0) k->awardN[j] = 1;
                else k->awardT[j]--;
            }
        }
    }
}

void karts_powerups(void) {
    int i;
    for (i = 0; i < gKartN; i++) {
        Kart *k = &gKarts[i];
        if (!k->fire) continue;
        if (k->selected == 0) {
            if (k->powerupTimer < 0xfa && k->nitroN) {
                k->powerupTimer = NITRO_TIME;
                k->nitroN--;
            }
        } else if (k->selected == 2) {
            if (k->supergrip < 0xfa && k->gripN) {
                k->gripN--;
                k->supergrip = GRIP_TIME;
            }
        }
    }
}

/* input_keys: one button picks (the manual's Tab: nitro, oil, super grip in turn;
 * oil is skipped with the oiling option off), the other, held, fires (Space).
 * On the pad: X picks, A fires */
void kart_powerup_buttons(Kart *k, const Input *in) {
    static u32 last;
    u32 pressed = in->buttons & ~last;
    last = in->buttons;
    if (pressed & BTN_X) k->selected = k->selected == 2 ? 0 : k->selected + 1;
    if (k->selected == 1) k->selected = 2;
    k->fire = (in->buttons & BTN_A) != 0;
}

/* the end of ai_drive_kart (0x42f13): computer drivers only, while racing */
void ai_powerups(Kart *k, int pitZone) {
    int cell = k->cellY * 128 + k->cellX, code;
    u32 prog;
    if (cell < 0 || cell >= 16384 || k->finished) return;
    code = MAP_OBJECTS(&gTrack)[cell];
    prog = (u32)(kart_laps_done(k) * 100) / (u32)gRaceLaps;
    if ((k->fuel < k->fuelPerLap || k->damagePct > 0x19) && !pitZone) k->wantPit = 1;
    switch (code) {
    case 0x30:                           /* nitro (and a super grip) */
        if (k->wantPit) break;
        if (!(k->nitroN == k->nitroMax && k->powerupTimer < 0xfa && k->gripN == k->gripMax && k->supergrip < 0xfa)) {
            if (prog <= (u32)(rand() % 0x186)) return;
            if (k->powerupTimer > 0xf9 || k->nitroN == 0 || k->supergrip > 0xf9 || k->gripN == 0) return;
        }
        /* (the original then stores the super grip time in the grip factor rather
           than the super grip timer, so only the nitro takes effect; the grip
           factor is set again next frame. Left out: it overflows the grip force) */
        k->selected = 2;
        k->powerupTimer = NITRO_TIME;
        k->nitroN--;
        k->gripN--;
        break;
    case 0x31:                           /* nitro */
        if (k->wantPit) break;
        if (k->nitroN == k->nitroMax && k->powerupTimer < 0xfa) {
            k->selected = 0;
            k->nitroN--;
            k->powerupTimer = NITRO_TIME;
            return;
        }
        if ((u32)(rand() % 0x186) < prog && k->powerupTimer < 0xfa && k->nitroN) {
            k->selected = 0;
            k->powerupTimer = NITRO_TIME;
            k->nitroN--;
        }
        break;
    case 0x33:                           /* super grip */
        if (k->gripN == k->gripMax && k->supergrip < 0xfa) {
            k->selected = 2;
            k->gripN--;
            k->supergrip = GRIP_TIME;
            return;
        }
        if ((u32)(rand() % 0x186) < prog && k->supergrip < 0xfa && k->gripN) {
            k->selected = 2;
            k->supergrip = GRIP_TIME;
            k->gripN--;
        }
        break;
    }
}
