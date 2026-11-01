/*
 * season.c - the game around the races, translated from the decompile: game types,
 * seasons, points, prize money, the computer drivers' upgrades, standings, the
 * all-time greats table, lap records and the cheat codes.
 *
 *   game_start_new 0x386bc         new game: every kart reset, a random grid
 *   karts_prepare_for_race 0x32d24 before each race: the computer drivers' upgrades
 *   race_award_points 0x389d0      after each race: points, prize money, wins, fastest lap
 *   season_sort_standings 0x38e1c  the championship order
 *   season_next_race 0x39c00       the next track, or the end of the season
 *   hiscore_insert 0x2b120         the all-time greats table
 *   cheat_code_check 0x3ed90       the cheat codes (typed on the menu screens)
 *
 * Saved on the VMU: the season (6 slots, as the original's MANIC1..6.SAV) and the
 * all-time greats with the lap records and the ORIEL cheat ("MANICKRT.SYS").
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Game gGame;
HiScore gHi[8];

/* the track lists (K.EXE 0xba6dc, 0xba6fe, 0xba710, 0xba722; 99 = the end) */
static const u8 kList[4][17] = {
    { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15, 99 },
    { 0, 2, 4, 6, 8, 10, 12, 14, 99 },
    { 1, 3, 5, 7, 9, 11, 13, 15, 99 },
    { 0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15, 99 },
};
static const s32 kPoints[8] = { 10, 7, 5, 4, 3, 2, 1, 0 };      /* K.EXE 0xb9d58 */

static char sDrvName[16][20], sDrvHandle[16][10];

int game_list(int i) {
    if (i < 0 || i >= 17) return 99;
    return kList[gGame.mode & 3][i];
}

const char *driver_name(int kart) {
    if (kart == gGame.playerKart && strcmp(gGame.playerName, "NONE") != 0 && gGame.playerName[0]) return gGame.playerName;
    return sDrvName[(kart * 2) & 15];
}

const char *driver_handle(int kart) {
    if (kart == gGame.playerKart && strcmp(gGame.playerHandle, "NONE") != 0 && gGame.playerHandle[0]) return gGame.playerHandle;
    return sDrvHandle[(kart * 2) & 15];
}

static void hiscore_defaults(void) {
    int i;
    for (i = 0; i < 8; i++) {             /* hiscores_set_defaults 0x2b268 */
        memcpy(gHi[i].name, sDrvName[(i * 2) & 15], 20);
        gHi[i].laps = 10;
        gHi[i].earnings = (10 - i) * 1000;
        gHi[i].points = (10 - i) * 10;
    }
}

/* the settings file: all-time greats, lap records, ORIEL */
typedef struct SysSave { char magic[4]; HiScore hi[8]; s32 records[16][4]; u8 anyTrack, pad[3]; } SysSave;

void game_settings_save(void) {
    static SysSave s;
    memcpy(s.magic, "MKS1", 4);
    memcpy(s.hi, gHi, sizeof gHi);
    memcpy(s.records, gGame.records, sizeof s.records);
    s.anyTrack = gGame.anyTrack;
    plat_save_write("MANICKRT.SYS", "MANIC KARTS RECORDS", &s, sizeof s);
}

void game_init(void) {
    static SysSave s;
    u32 size;
    const u8 *d = (const u8 *)plat_load_file("drivers.bin", &size);
    int i;
    if (d && size >= 16 * 30) {
        for (i = 0; i < 16; i++) {
            memcpy(sDrvName[i], d + i * 30, 20);
            memcpy(sDrvHandle[i], d + i * 30 + 20, 10);
            sDrvName[i][19] = sDrvHandle[i][9] = 0;
        }
    }
    if (d) free((void *)d);
    memset(&gGame, 0, sizeof gGame);
    gGame.engineChoice = 1;              /* 100cc */
    gGame.laps = 4;
    strcpy(gGame.playerName, "NONE");
    strcpy(gGame.playerHandle, "NONE");
    hiscore_defaults();
    if (plat_save_read("MANICKRT.SYS", &s, sizeof s) && memcmp(s.magic, "MKS1", 4) == 0) {
        memcpy(gHi, s.hi, sizeof gHi);
        memcpy(gGame.records, s.records, sizeof gGame.records);
        gGame.anyTrack = s.anyTrack;
    }
}

/* karts_init_all + game_start_new: everyone back to nothing, a random grid */
void game_start_new(void) {
    int i, n;
    for (i = 0; i < 8; i++) {
        Entrant *e = &gGame.e[i];
        memset(e, 0, sizeof *e);
        e->nitroN = e->gripN = 3;
        e->grid = (u8)i;
    }
    n = 5 + rand() % 10;                 /* karts_set_grid_order(1): 5..14 random swaps */
    while (n--) {
        int a = rand() % 8, b = rand() % 8;
        u8 t = gGame.e[a].grid;
        gGame.e[a].grid = gGame.e[b].grid;
        gGame.e[b].grid = t;
    }
    gGame.racesDone = 0;
    gGame.cls = gGame.engineChoice;
    gGame.round = 0;
    gGame.track = (u8)game_list(gGame.startIdx);
}

/* karts_prepare_for_race: the computer drivers' upgrades before each race */
void game_prepare_race(void) {
    int i;
    for (i = 0; i < 8; i++) {
        Entrant *e = &gGame.e[i];
        int r;
        e->prizeBonus = 0;
        if (i == gGame.playerKart) continue;
        r = rand() % 100;
        if (gGame.round == 0) r = 0;     /* (none before the first race) */
        if (r >= 66) { if (e->engine < 5) e->engine++; }
        else if (r >= 21) { if (e->tyres < 5) e->tyres++; }
        else if (r >= 6 && gGame.laps >= 7) { if (e->tank < 5) e->tank++; }
        else if (r != 0 && e->bribes < 5) { e->bribes++; e->points += 5; }
        e->nitroN = e->nitroN + 2 > 5 ? 5 : e->nitroN + 2;
        e->gripN = e->gripN + 2 > 5 ? 5 : e->gripN + 2;
    }
}

/* race_award_points: order[p] = the kart that came p-th; fastest = the fastest lap's kart */
void game_award(const int *order, const s32 *bestLap, const u8 *finished) {
    int p, i, fl = -1;
    s32 c = gGame.cls + 1;
    for (p = 0; p < 8; p++) {
        Entrant *e;
        s32 prize;
        if (order[p] < 0) continue;
        e = &gGame.e[order[p]];
        e->pos = (u8)p;
        e->racePoints = kPoints[p];
        prize = c * 500 + e->racePoints * 50 * c + e->prizeBonus;
        e->money += prize;
        e->moneyTotal += prize;
        e->points += e->racePoints;
        if (p == 0) e->wins++;
    }
    for (i = 0; i < 8; i++) {            /* the fastest lap: lowest best lap, lowest kart on a tie */
        if (bestLap[i] > 0 && (fl < 0 || bestLap[i] < bestLap[fl])) fl = i;
    }
    gGame.fastestKart = (signed char)fl;
    if (fl >= 0 && finished[fl]) {
        gGame.e[fl].points++;
        gGame.e[fl].racePoints++;
        gGame.e[fl].fastest++;
    }
    for (i = 0; i < 8; i++) gGame.e[i].grid = gGame.e[i].pos;   /* the next grid: this race's order */
    if (gGame.mode != MODE_SINGLE) game_sort_standings();
    gGame.round++;
}

/* season_sort_standings: points, then total earnings */
void game_sort_standings(void) {
    int s[8], i, j;
    for (i = 0; i < 8; i++) s[i] = i;
    for (i = 0; i < 8; i++) {
        for (j = i + 1; j < 8; j++) {
            Entrant *a = &gGame.e[s[i]], *b = &gGame.e[s[j]];
            if (b->points > a->points || (b->points == a->points && b->moneyTotal > a->moneyTotal)) {
                int t = s[i]; s[i] = s[j]; s[j] = t;
            }
        }
    }
    for (i = 0; i < 8; i++) gGame.e[s[i]].standing = (u8)i;
}

/* season_next_race: 1 = another track (gGame.track), 0 = the season is over */
int game_next_race(void) {
    gGame.racesDone++;
    if (gGame.mode == MODE_SINGLE) return 0;
    gGame.track = (u8)game_list(gGame.racesDone + gGame.startIdx);
    return gGame.track != 99;
}

int game_kart_at_standing(int s) {
    int i;
    for (i = 0; i < 8; i++) {
        if (gGame.e[i].standing == s) return i;
    }
    return 0;
}

/* hiscore_insert: the player's season, if it makes the table: its row (-1: no) */
int game_hiscore_insert(void) {
    const Entrant *e = &gGame.e[gGame.playerKart];
    int i;
    for (i = 0; i < 8; i++) {
        if (gHi[i].points < e->points || (e->points == gHi[i].points && gHi[i].earnings < e->moneyTotal) ||
            (e->points == gHi[i].points && e->moneyTotal == gHi[i].earnings && gHi[i].laps < gGame.laps)) {
            memmove(&gHi[i + 1], &gHi[i], (7 - i) * sizeof gHi[0]);
            memset(&gHi[i], 0, sizeof gHi[i]);
            gHi[i].laps = (u8)gGame.laps;
            gHi[i].earnings = e->moneyTotal;
            gHi[i].points = e->points;
            return i;
        }
    }
    return -1;
}

/* a lap record (kart_lap_timing: the human's laps only, by track and engine class) */
void game_lap_record(int track, s32 frames) {
    s32 *r = &gGame.records[track & 15][gGame.cls & 3];
    if (frames > 0 && (*r == 0 || frames < *r)) *r = frames;
}

/* ---------------------------------------------------------------- saves */
typedef struct GameSave { char magic[4]; char name[16]; Game g; } GameSave;

int game_save(int slot, const char *name) {
    static GameSave s;
    char fn[24];
    memcpy(s.magic, "MKG1", 4);
    memset(s.name, 0, sizeof s.name);
    strncpy(s.name, name, 15);
    s.g = gGame;
    snprintf(fn, sizeof fn, "MANICKRT.%03d", slot + 1);
    return plat_save_write(fn, s.name, &s, sizeof s);
}

/* the slot's name ("" = empty) and, with load set, the game itself */
int game_load(int slot, char *nameOut, int load) {
    static GameSave s;
    char fn[24];
    snprintf(fn, sizeof fn, "MANICKRT.%03d", slot + 1);
    if (!plat_save_read(fn, &s, sizeof s) || memcmp(s.magic, "MKG1", 4) != 0) {
        if (nameOut) nameOut[0] = 0;
        return 0;
    }
    if (nameOut) { strncpy(nameOut, s.name, 15); nameOut[15] = 0; }
    if (load) {
        s32 rec[16][4];
        memcpy(rec, gGame.records, sizeof rec);   /* (the records stay the newest) */
        gGame = s.g;
        memcpy(gGame.records, rec, sizeof rec);
    }
    return 1;
}

/* ---------------------------------------------------------------- cheats */
static const char *const kCheat[8] = { "EXETER", "MANICMARTIN", "KEBLE", "ORIEL", "SOMERVILLE",
                                       "BRASENOSE", "NUFFIELD", "TRINITY" };

/* the effect of code i: 1 = done */
static void cheat_apply(int i) {
    switch (i) {
    case 0: gGame.cheats ^= CHEAT_GRIP; break;
    case 1: gGame.cheats ^= CHEAT_NITRO; break;
    case 2: gGame.cheats ^= CHEAT_FUEL; break;
    case 3: gGame.anyTrack = 1; game_settings_save(); break;
    case 4: gGame.cheats ^= CHEAT_NOCOLLIDE; break;
    case 5: {
        Entrant *e = &gGame.e[gGame.playerKart];
        if (e->money < 990000 && e->moneyTotal < 990000) { e->money += 10000; e->moneyTotal += 10000; }
        break;
    }
    case 6: gGame.cheats ^= CHEAT_3D; break;
    case 7: gGame.cheats ^= CHEAT_TRINITY; break;
    }
}

/* letters typed on a menu screen: the last 11, checked against the codes (1 = a code done) */
int game_cheat_key(int c) {
    static char buf[12];
    int n, i;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 'A' || c > 'Z') return 0;
    n = (int)strlen(buf);
    if (n == 11) { memmove(buf, buf + 1, 10); n = 10; }
    buf[n] = (char)c;
    buf[n + 1] = 0;
    for (i = 0; i < 8; i++) {
        int l = (int)strlen(kCheat[i]);
        if (n + 1 >= l && strcmp(buf + n + 1 - l, kCheat[i]) == 0) {
            cheat_apply(i);
            buf[0] = 0;
            return 1;
        }
    }
    return 0;
}

/* a whole word (a name typed with the pad): a cheat code? */
int game_cheat_word(const char *s) {
    int i;
    for (i = 0; i < 8; i++) {
        if (strcmp(s, kCheat[i]) == 0) { cheat_apply(i); return 1; }
    }
    return 0;
}
