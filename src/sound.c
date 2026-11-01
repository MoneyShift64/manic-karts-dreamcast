/*
 * sound.c - the race's sounds, as the original's sound_update_channels 0x485f4 and
 * the race loop's engine notes, played on the platform's voices (on the Dreamcast,
 * the AICA's):
 *
 *   engines   the 4 karts nearest the camera, each a looping note (ENG5, or TURBO4
 *             with the turbo on) at |speed| * 3/2 + 10000 Hz (+17000 with the turbo),
 *             louder with the throttle on
 *   skids     SKID1, higher and louder the more the kart slides
 *   surfaces  BUMP2 on cobbles ('I') above 4000, PITS in the pit lane ('p')
 *   knocks    IMPACT2 on walls and karts, CONE for cones knocked flying, BOUNCE on landing
 *   ambient   the track's own (crowds, waterfall, rotors, the sea...) where map 5 puts them
 *
 * Distances and volumes are the original's formulas, in its units (1/16 of a cell
 * here): a sound at distance d plays at 127 - d / 8 (of 127), engines at 80 - d / 2.
 * Pan from where the sound is relative to the camera.
 */
#include "game.h"
#include <string.h>
#include <stdlib.h>

enum { V_ENGINE = 0, V_SKID = 4, V_SURFACE = 5, V_HIT = 6, V_AMBIENT = 8 };
enum { S_ENG, S_TURBO, S_SKID, S_BUMP, S_IMPACT, S_BOUNCE, S_GENHIT, S_FUEL, S_SELECT, S_PITS, S_CONE };

static int sSnd[32], sSndN;              /* the track's list, by the original's numbers */
static struct { u8 col, row, snd, pad; } sSrc[32];
static int sSrcN;
static int sEngineKart[4] = { -1, -1, -1, -1 }, sEngineSnd[4];
static int sSkid = -1, sSurface = -1, sHitNext, sHitWait[MAX_KARTS];
static int sAmb[4] = { -1, -1, -1, -1 };

void sound_load(int track) {
    char name[16];
    u32 size;
    const u8 *d;
    int i, n;
    for (i = 0; i < AUD_VOICES; i++) aud_stop(i);
    for (i = 0; i < 4; i++) { sEngineKart[i] = -1; sAmb[i] = -1; }
    sSkid = sSurface = -1;
    sSndN = sSrcN = 0;
    name[0] = 't'; name[1] = 'r'; name[2] = 'k';
    name[3] = (char)('0' + track / 10); name[4] = (char)('0' + track % 10);
    strcpy(name + 5, ".sfx");
    d = (const u8 *)plat_load_file(name, &size);
    if (d == NULL) return;
    n = d[0];
    for (i = 0; i < n && i < 32; i++) {
        char nm[16];
        int j;
        memcpy(nm, d + 1 + i * 13, 13);
        nm[13] = 0;
        for (j = 0; nm[j] && nm[j] != '.'; j++) {}
        nm[j] = 0;                       /* "ENG5.MSF" -> "ENG5" */
        sSnd[i] = aud_load(nm);
    }
    sSndN = i;
    d += 1 + n * 13;
    for (i = 0; i < d[0] && i < 32; i++) memcpy(&sSrc[i], d + 1 + i * 4, 4);
    sSrcN = i;
    free((void *)(d - 1 - n * 13));
    /* the music: the CD track the original plays for this track (track +0x113: 2..9) */
    {
        char mus[16] = "mus00.adp";
        int cd = track / 2 + 2;
        mus[3] = (char)('0' + cd / 10);
        mus[4] = (char)('0' + cd % 10);
        music_play(mus);
        music_volume(160);
    }
}

static int snd(int k) { return k < sSndN ? sSnd[k] : -1; }

/* the pause: every race sound off (the music plays on), started again by sound_update */
void sound_silence(void) {
    int i;
    for (i = 0; i < AUD_VOICES; i++) aud_stop(i);
    for (i = 0; i < 4; i++) { sEngineKart[i] = -1; sAmb[i] = -1; }
    sSkid = sSurface = -1;
}

/* distance from the camera in the original's sound units (16 a cell), and pan */
static int where(s32 x, s32 y, int *pan) {
    f32 wx = x * (1.0f / 1024) - gCam.pos.x, wz = (MAP_N - y * (1.0f / 1024)) - gCam.pos.z;
    f32 d = shz_sqrtf(wx * wx + wz * wz), side;
    side = d > 0.01f ? shz_divf(wx * gCam.right.x + wz * gCam.right.z, d) : 0;
    *pan = 128 + (int)(side * 100);
    return (int)(d * 16);
}

static int vol127(int dist) {
    int v = 127 - (dist >> 3);
    return v < 0 ? 0 : v;
}

void sound_update(void) {
    int i, j, order[MAX_KARTS], dist[MAX_KARTS], pan[MAX_KARTS];
    int bestSkid = -1, bestSkidVol = 0;

    for (i = 0; i < gKartN; i++) {
        dist[i] = where(gKarts[i].x, gKarts[i].y, &pan[i]);
        order[i] = i;
    }
    for (i = 1; i < gKartN; i++) {       /* nearest first */
        int t = order[i];
        for (j = i; j > 0 && dist[order[j - 1]] > dist[t]; j--) order[j] = order[j - 1];
        order[j] = t;
    }

    /* engines: the 4 nearest */
    for (i = 0; i < 4 && i < gKartN; i++) {
        const Kart *k = &gKarts[order[i]];
        int turbo = k->powerupTimer != 0, s = snd(turbo ? S_TURBO : S_ENG);
        int v = 80 - (dist[order[i]] >> 1), freq;
        s32 sp = k->speed < 0 ? -k->speed : k->speed;
        if (v < 0) v = 0;
        v += k->engineOn ? v >> 1 : -(v >> 4);
        freq = sp * 3 / 2 + (turbo ? 17000 : 10000);
        if (sEngineKart[i] != order[i] || sEngineSnd[i] != s) {
            aud_play(V_ENGINE + i, s, v * 2, pan[order[i]], freq);
            sEngineKart[i] = order[i];
            sEngineSnd[i] = s;
        } else {
            aud_set(V_ENGINE + i, v * 2, pan[order[i]], freq);
        }
    }

    for (i = 0; i < gKartN; i++) {
        Kart *k = &gKarts[i];
        int d = dist[i];
        if (sHitWait[i] > 0) sHitWait[i]--;
        /* knocks */
        if (k->hitFlag && sHitWait[i] == 0) {
            int s = k->hitFlag == 1 ? snd(S_CONE) : snd(S_IMPACT);
            int v = k->hitFlag >= 5 ? vol127(d - ((k->hitFlag * d) >> 7)) : vol127(d);
            if (s < 0) s = snd(S_GENHIT);
            if (v > 0) {
                aud_play(V_HIT + sHitNext, s, v * 2, pan[i], 0);
                sHitNext ^= 1;
            }
            sHitWait[i] = 12;
        }
        if (k->soundFlag == 2) {         /* landed from a jump */
            int v = vol127(d);
            if (v > 0) {
                aud_play(V_HIT + sHitNext, snd(S_BOUNCE), v * 2, pan[i], 0);
                sHitNext ^= 1;
            }
            k->soundFlag = 0;
        }
        /* the loudest skid */
        if (k->slide > 300) {
            int v = vol127(d * 4 - (k->slide >> 4));
            if (v > bestSkidVol) { bestSkidVol = v; bestSkid = i; }
        }
    }
    if (bestSkid >= 0) {
        const Kart *k = &gKarts[bestSkid];
        int freq = aud_rate(snd(S_SKID)) + (k->slide >> 2);
        if (freq > 30000) freq = 30000;
        if (sSkid < 0) aud_play(V_SKID, snd(S_SKID), bestSkidVol * 2, pan[bestSkid], freq);
        else aud_set(V_SKID, bestSkidVol * 2, pan[bestSkid], freq);
        sSkid = bestSkid;
    } else if (sSkid >= 0) {
        aud_stop(V_SKID);
        sSkid = -1;
    }

    /* the surface under the nearest kart (the player's, from behind) */
    {
        const Kart *k = &gKarts[order[0]];
        s32 sp = k->speed < 0 ? -k->speed : k->speed;
        u8 surf = (sp < 4000 && k->surface != 'p') || k->z > 100 ? '.' : k->surface;
        int s = surf == 'I' ? S_BUMP : surf == 'p' ? S_PITS : -1;
        if (s >= 0) {
            int v = vol127(dist[order[0]] * 4), freq = s == S_BUMP ? aud_rate(snd(s)) + (sp >> 2) : 0;
            if (sSurface != s) aud_play(V_SURFACE, snd(s), v * 2, pan[order[0]], freq);
            else aud_set(V_SURFACE, v * 2, pan[order[0]], freq);
            sSurface = s;
        } else if (sSurface >= 0) {
            aud_stop(V_SURFACE);
            sSurface = -1;
        }
    }

    /* ambient sounds: the 4 nearest within earshot */
    {
        int pick[4] = { -1, -1, -1, -1 }, pd[4] = { 1 << 30, 1 << 30, 1 << 30, 1 << 30 }, pp[4] = { 128, 128, 128, 128 };
        for (i = 0; i < sSrcN; i++) {
            int p, dd = where(sSrc[i].col * 1024 + 512, sSrc[i].row * 1024 + 512, &p);
            if (vol127(dd) == 0) continue;
            for (j = 0; j < 4; j++) {
                if (dd < pd[j]) {
                    int m;
                    for (m = 3; m > j; m--) { pick[m] = pick[m - 1]; pd[m] = pd[m - 1]; pp[m] = pp[m - 1]; }
                    pick[j] = i; pd[j] = dd; pp[j] = p;
                    break;
                }
            }
        }
        for (j = 0; j < 4; j++) {
            if (pick[j] < 0) {
                if (sAmb[j] >= 0) aud_stop(V_AMBIENT + j);
                sAmb[j] = -1;
                continue;
            }
            if (sAmb[j] != pick[j]) aud_play(V_AMBIENT + j, snd(sSrc[pick[j]].snd), vol127(pd[j]) * 2, pp[j], 0);
            else aud_set(V_AMBIENT + j, vol127(pd[j]) * 2, pp[j], 0);
            sAmb[j] = pick[j];
        }
    }
}

void sound_beep(int high) {
    int s = snd(S_FUEL);
    if (s < 0) return;
    aud_play(V_HIT + sHitNext, s, 255, 128, high ? aud_rate(s) * 2 : aud_rate(s));
    sHitNext ^= 1;
}
