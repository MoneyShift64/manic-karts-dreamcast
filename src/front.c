/*
 * front.c - the menus, as the original's frontend_loop 0x35158 runs them: one screen at
 * a time, 20 ticks a second, each drawn from its own picture set (fe.c) as the
 * original lays it out (positions in its 640 x 480).
 *
 *   Virgin logo -> MAIN MENU (NEW GAME / LOAD GAME / HIGH SCORES; CONFIGURE and QUIT
 *   TO DOS greyed out on the Dreamcast)
 *   NEW GAME -> RACE MENU (GO KARTING, game type, CONFIGURE RACE, EXIT)
 *     GO KARTING: single race -> TRACK SELECT -> race -> RESULTS -> race menu
 *                 season -> NEXT CIRCUIT -> (SHOP from race 2) -> race -> RESULTS
 *                 (race and championship) -> ... -> CHAMPION or LOSER -> HI-SCORES
 *   LOAD GAME (VMU) -> SHOP;  HIGH SCORES -> the all-time season greats
 *
 * Pad: d-pad (or stick) moves, A or START selects, B goes back. A Dreamcast keyboard
 * types names and the cheat codes (any menu screen, as the original); without one,
 * names are edited letter by letter with the pad (up / down change the letter).
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { S_LOGO, S_MAIN, S_RACEMENU, S_CONFRACE, S_TRACKSEL, S_NEXT, S_SHOP, S_RESULTS, S_CHAMPION,
       S_PLACED, S_HISCORES, S_LOAD, S_SAVE };
enum { E_UP = 1, E_DOWN = 2, E_LEFT = 4, E_RIGHT = 8, E_SELECT = 16, E_BACK = 32 };

static int sScreen = -1, sTick, sEv, sBackCount, sTicked, sWantRace;
static const char *const kCC[4] = { "50cc", "100cc", "150cc", "250cc" };
static char sTrackCountry[16][12], sTrackName[16][20], sTrackDesc[16][11][32];
static const u8 kLetterOf[16] = { 0, 0, 2, 2, 4, 4, 1, 1, 6, 6, 3, 3, 1, 1, 7, 7 };   /* K.EXE 0xba168 */

/* ---------------------------------------------------------------- input */
static Input sIn;
static u32 sPressed;
static int sRep[4];

/* key repeat (pause_key_up 0x375d8): at once, then after 0.75 s every 0.2 s */
static int repeat(int held, int *c) {
    if (!held) { *c = 0; return 0; }
    if (*c == 0) { *c = 1; return 1; }
    if (*c < 32) { *c += 2; return *c >= 32; }
    *c += 8;
    if (*c >= 64) { *c = 32; return 1; }
    return 0;
}

static void read_events(void) {
    int up = (sIn.buttons & BTN_UP) || sIn.sticky < -0.5f, down = (sIn.buttons & BTN_DOWN) || sIn.sticky > 0.5f;
    int left = (sIn.buttons & BTN_LEFT) || sIn.stickx < -0.5f, right = (sIn.buttons & BTN_RIGHT) || sIn.stickx > 0.5f;
    sEv = 0;
    if (repeat(up, &sRep[0])) sEv |= E_UP;
    if (repeat(down, &sRep[1])) sEv |= E_DOWN;
    if (repeat(left, &sRep[2])) sEv |= E_LEFT;
    if (repeat(right, &sRep[3])) sEv |= E_RIGHT;
    if (sPressed & (BTN_A | BTN_START)) sEv |= E_SELECT;
    if (sPressed & BTN_B) sEv |= E_BACK;
    sPressed = 0;
    if (sEv & (E_UP | E_DOWN | E_LEFT | E_RIGHT)) sBackCount = 0;
}

/* ---------------------------------------------------------------- shared pieces */
static void go(int screen);

/* the screen title: each letter flies in from the right (menu_text_anim_setup 0x26b74) */
static void title_line(int font, const char *s, int x, int y) {
    int i, shift = 368, step = 70;
    for (i = 0; s[i]; i++) {
        char one[2] = { s[i], 0 };
        int off = shift - 60 * sTick;
        if (off < 0) off = 0;
        x += fe_text(font, one, x + off, y, 0);
        shift += step;
        step -= 8;
    }
}

static void title(int font, const char *a, int ax, int ay, const char *b, int bx, int by) {
    title_line(font, a, ax, ay);
    if (b) title_line(font, b, bx, by);
}

static void fmt_time(char *out, int n, s32 frames) {
    int s = frames / 60, c = (frames % 60) * 100 / 60;
    if (s > 99) s = 99;
    snprintf(out, (size_t)n, "%d.%02d", s, c);
}

/* the typed-out description: 2.5 letters a tick (50 a second), counted from 'from' */
static void typewriter(int font, int track, f32 x, f32 y0, int from) {
    int shown = (sTick - from) * 5 / 2, i;
    for (i = 0; i < 11 && shown > 0; i++) {
        char line[32];
        int n = (int)strlen(sTrackDesc[track][i]);
        if (n == 0) break;
        strncpy(line, sTrackDesc[track][i], 31);
        line[31] = 0;
        if (shown < n) line[shown] = 0;
        fe_text(font, line, x, y0 + i * 20, 0);
        shown -= n;
    }
}

/* the spinning track map (track_select_draw_rotating_preview 0x16d94): tilted back 40
 * degrees, turning 10 degrees a tick, projected with focal 160 at distance 200 */
static void spinning_map(int sprite, f32 cx, f32 cy) {
    /* (cut into a grid of small pieces, each projected at its own corners: one flat
       quad would bend the picture along its diagonal) */
    enum { N = 8 };
    f32 w = fe_sprite_w(sprite) * 0.5f, h = fe_sprite_h(sprite) * 0.5f, gx[N + 1][N + 1], gy[N + 1][N + 1];
    f32 yaw = (sTick % 36) * (PI_F / 18), cyw = shz_cosf(yaw), syw = shz_sinf(yaw);
    f32 cp = shz_cosf(40 * PI_F / 180), sp = shz_sinf(40 * PI_F / 180), inv = shz_invf((f32)N);
    int i, j;
    for (j = 0; j <= N; j++) {
        for (i = 0; i <= N; i++) {
            f32 px = (2 * i * inv - 1) * w, py = (2 * j * inv - 1) * h;
            f32 X = px * cyw, Y = px * syw * sp + py * cp, Z = -px * syw * cp + py * sp + 200;
            Z = shz_divf(160, Z);
            gx[j][i] = cx + X * Z;
            gy[j][i] = cy + Y * Z;
        }
    }
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            f32 x[4] = { gx[j][i], gx[j][i + 1], gx[j + 1][i + 1], gx[j + 1][i] };
            f32 y[4] = { gy[j][i], gy[j][i + 1], gy[j + 1][i + 1], gy[j + 1][i] };
            fe_sprite_pts_part(sprite, x, y, i * inv, j * inv, (i + 1) * inv, (j + 1) * inv, 0xffffffff);
        }
    }
}

/* ---------------------------------------------------------------- text entry
 * (menu_text_input 0x2712c): a keyboard types; the pad edits letter by letter */
static struct { char buf[16]; int max, pos, active, pad; } sEd;
static const char kPadChars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";

static void edit_start(const char *cur, int max) {
    memset(&sEd, 0, sizeof sEd);
    if (strcmp(cur, "NONE") != 0) strncpy(sEd.buf, cur, 15);
    sEd.max = max;
    sEd.pos = (int)strlen(sEd.buf);
    if (sEd.pos >= max) sEd.pos = max - 1;
    sEd.active = 1;
}

/* one tick of editing: 1 = accepted, -1 = cancelled, 0 = still going */
static int edit_tick(void) {
    int c, n = (int)strlen(sEd.buf);
    while ((c = plat_key()) != 0) {
        sEd.pad = 0;
        if (c == '\n') { fe_sound(1); return 1; }
        if (c == 27) { fe_sound(1); return -1; }
        if (c == 8) { if (n > 0) sEd.buf[--n] = 0; sEd.pos = n; fe_sound(0); continue; }
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 32 && c < 127 && n < sEd.max) { sEd.buf[n++] = (char)c; sEd.buf[n] = 0; sEd.pos = n; fe_sound(0); }
    }
    if (sEv & (E_UP | E_DOWN)) {         /* the pad: change the letter under the cursor */
        const char *p;
        int k;
        sEd.pad = 1;
        while ((int)strlen(sEd.buf) <= sEd.pos) strcat(sEd.buf, " ");
        p = strchr(kPadChars, sEd.buf[sEd.pos]);
        k = p ? (int)(p - kPadChars) : 0;
        k += (sEv & E_UP) ? 1 : -1;
        if (k < 0) k = (int)sizeof kPadChars - 2;
        if (k > (int)sizeof kPadChars - 2) k = 0;
        sEd.buf[sEd.pos] = kPadChars[k];
        fe_sound(0);
    }
    if ((sEv & E_RIGHT) && sEd.pos < sEd.max - 1) { sEd.pad = 1; sEd.pos++; fe_sound(0); }
    if ((sEv & E_LEFT) && sEd.pos > 0) { sEd.pad = 1; sEd.pos--; fe_sound(0); }
    if (sEv & E_SELECT) { fe_sound(1); return 1; }
    if (sEv & E_BACK) { fe_sound(1); return -1; }
    return 0;
}

/* the text being edited, with its cursor (blinking '_' every 0.2 s) */
static void edit_text(char *out, int n) {
    int blink = (sTick / 4) & 1, len = (int)strlen(sEd.buf);
    strncpy(out, sEd.buf, (size_t)n - 2);
    out[n - 2] = 0;
    while (len > 0 && out[len - 1] == ' ' && len - 1 > sEd.pos) out[--len] = 0;   /* (trailing pad spaces) */
    if (sEd.pad) {
        if (blink && sEd.pos < len) out[sEd.pos] = '_';
        else if (blink) { out[sEd.pos] = '_'; out[sEd.pos + 1] = 0; }
    } else if (len < sEd.max) {
        out[len] = blink ? '_' : '~';
        out[len + 1] = 0;
    }
}

/* the accepted text: trimmed, upper case; a cheat code instead does the cheat */
static int edit_result(char *dst, int size) {
    char t[16];
    int n;
    strncpy(t, sEd.buf, 15);
    t[15] = 0;
    n = (int)strlen(t);
    while (n > 0 && t[n - 1] == ' ') t[--n] = 0;
    sEd.active = 0;
    if (game_cheat_word(t)) return 0;
    if (n == 0) strcpy(t, "NONE");
    strncpy(dst, t, (size_t)size - 1);
    dst[size - 1] = 0;
    return 1;
}

/* ---------------------------------------------------------------- the race */
static void do_race(void) { sWantRace = 1; }   /* (after this frame) */

static void run_race(void) {
    int r;
    game_prepare_race();
    music_stop();
    r = race_play();
    fe_forget();
    music_play("mus02.adp");
    music_volume(160);
    if (r == 2) { go(S_LOGO); return; }  /* soft reset (A+B+X+Y+START): the title */
    if (r == 1) { go(S_MAIN); return; }  /* QUIT: no points, the season abandoned */
    game_award(gRaceOrder, gRaceBest, gRaceFinished);
    game_settings_save();                /* (the lap records) */
    go(S_RESULTS);
}

/* ---------------------------------------------------------------- the screens */

/* the Virgin logo (play_intro 0x343b0): faded in, held up to 4 s (or a button), faded out */
static int sLogoSkip;
static void logo(void) {
    int a, done = 0, i;
    fe_load("VIRGIN");
    /* meanwhile, every menu screen's pictures are read into memory (a few files a frame),
       so that later the screens change at once: the logo stays up until that's done */
    for (i = 0; i < 3 && !done; i++) done = fe_preload();
    if (sEv & (E_SELECT | E_BACK)) sLogoSkip = 1;
    if (done && (sTick > 100 || sLogoSkip)) { go(S_MAIN); return; }
    fe_bg();
    a = sTick < 10 ? 255 - sTick * 25 : sTick > 90 ? (sTick - 90) * 25 : 0;
    fe_fade(a);
}

/* the main menu: five icons on a turning ring (menu_main 0x26cb8, menu_circle_setup) */
static int sMainSel, sMainPos;           /* the front item; the ring's turn (100 an item) */
static const char *const kMainLabel[5] = { "NEW GAME", "CONFIGURE", "LOAD GAME", "HIGH SCORES", "QUIT TO DOS" };
static int main_greyed(int i) { return i == 1 || i == 4; }   /* (no configuration or DOS on the Dreamcast) */

static void main_menu(void) {
    int i, order[5], n = 5, T = 500;
    f32 d;
    if (sEv & (E_LEFT | E_RIGHT)) {
        int dir = (sEv & E_LEFT) ? -1 : 1;
        do sMainSel = (sMainSel + dir + 5) % 5; while (main_greyed(sMainSel));
        fe_sound(0);
    }
    if (sEv & E_SELECT) {
        fe_sound(1);
        if (sMainSel == 0) go(S_RACEMENU);
        else if (sMainSel == 2) go(S_LOAD);
        else if (sMainSel == 3) go(S_HISCORES);
        return;
    }
    if (sTicked) {   /* the ring turns 20 a tick towards the front item, the short way round */
        int want = sMainSel * 100, dd = ((want - sMainPos) % T + T) % T;
        if (dd > T / 2) dd -= T;
        if (dd > 20) dd = 20;
        if (dd < -20) dd = -20;
        sMainPos = ((sMainPos + dd) % T + T) % T;
    }
    fe_load("HM1");
    fe_bg();
    for (i = 0; i < n; i++) order[i] = i;
    for (i = 0; i < n; i++) {            /* the far ones first */
        int j;
        for (j = i + 1; j < n; j++) {
            f32 xa, ya, sa, xb, yb, sb;
            fe_ring(sMainPos - order[i] * 100, &xa, &ya, &sa);
            fe_ring(sMainPos - order[j] * 100, &xb, &yb, &sb);
            if (sb < sa) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        }
    }
    for (i = 0; i < n; i++) {
        f32 x, y, s;
        fe_ring(sMainPos - order[i] * 100, &x, &y, &s);
        fe_sprite(order[i], x, y, s, main_greyed(order[i]) ? 0xff606060 : 0xffffffff);
    }
    d = (f32)(((sMainPos - sMainSel * 100) % T + T + T / 2) % T - T / 2);
    fe_text(FA, kMainLabel[sMainSel], 320 + d * 5, 434, 1);
    title(FD, "MAIN", 400, 42, "MENU", 450, 69);
}

/* the race menu (menu_game_type 0x284cc) */
static int sRaceSel;
static const char *const kMode[4] = { "SINGLE RACE", "EASY SEASON", "HARD SEASON", "MANIC CUP" };

static void race_menu(void) {
    static const int ys[4] = { 175, 235, 295, 355 };
    int i;
    if (sEv & E_UP) { sRaceSel = (sRaceSel + 3) % 4; fe_sound(0); }
    if (sEv & E_DOWN) { sRaceSel = (sRaceSel + 1) % 4; fe_sound(0); }
    if (sRaceSel == 1 && (sEv & (E_LEFT | E_RIGHT | E_SELECT))) {
        gGame.mode = (u8)((gGame.mode + ((sEv & E_LEFT) ? 3 : 1)) % 4);
        fe_sound(0);
    } else if (sEv & E_SELECT) {
        fe_sound(1);
        if (sRaceSel == 0) {             /* GO KARTING */
            if (gGame.mode == MODE_SINGLE || gGame.anyTrack) { go(S_TRACKSEL); return; }
            gGame.startIdx = 0;
            game_start_new();
            go(S_NEXT);
            return;
        }
        if (sRaceSel == 2) { go(S_CONFRACE); return; }
        if (sRaceSel == 3) { go(S_MAIN); return; }
    }
    if (sEv & E_BACK) { fe_sound(1); go(S_MAIN); return; }
    fe_load("HM3");
    fe_bg();
    for (i = 0; i < 4; i++) {
        const char *s = i == 0 ? (gGame.anyTrack && gGame.mode != MODE_SINGLE ? "GO KARTING - ANY TRACK" : "GO KARTING")
                      : i == 1 ? kMode[gGame.mode & 3] : i == 2 ? "CONFIGURE RACE" : "EXIT";
        fe_text(i == sRaceSel ? FC : FF, s, 320, ys[i], 1);
    }
    title(FD, "RACE", 400, 42, "MENU", 450, 69);
}

/* configure race (menu_engine_size_and_laps 0x286dc): engine, laps, kart, name, handle */
static int sConfSel;

static void configure_race(void) {
    static const int ys[6] = { 160, 210, 260, 310, 360, 410 };
    char s[40], e[20];
    int i, dir = (sEv & E_LEFT) ? -1 : (sEv & (E_RIGHT | E_SELECT)) ? 1 : 0;
    if (sEd.active) {
        int r = edit_tick();
        if (r > 0) {
            if (!edit_result(sConfSel == 3 ? gGame.playerName : gGame.playerHandle, sConfSel == 3 ? 15 : 9)) fe_sound(1);
        } else if (r < 0) sEd.active = 0;
    } else {
        if (sEv & E_UP) { sConfSel = (sConfSel + 5) % 6; fe_sound(0); }
        if (sEv & E_DOWN) { sConfSel = (sConfSel + 1) % 6; fe_sound(0); }
        if (dir && sConfSel == 0) { gGame.engineChoice = (u8)((gGame.engineChoice + dir + 4) % 4); fe_sound(0); }
        if (dir && sConfSel == 1) {
            int l = gGame.laps + dir;
            gGame.laps = (u16)(l < 2 ? 40 : l > 40 ? 2 : l);
            fe_sound(0);
        }
        if (dir && sConfSel == 2) { gGame.playerKart = (u8)((gGame.playerKart + dir) & 7); fe_sound(0); }
        if ((sEv & E_SELECT) && (sConfSel == 3 || sConfSel == 4)) {
            fe_sound(1);
            edit_start(sConfSel == 3 ? gGame.playerName : gGame.playerHandle, sConfSel == 3 ? 14 : 8);
        }
        if (((sEv & E_SELECT) && sConfSel == 5) || (sEv & E_BACK)) { fe_sound(1); go(S_RACEMENU); return; }
    }
    fe_load("HM3B");
    fe_bg();
    for (i = 0; i < 6; i++) {
        switch (i) {
        case 0: snprintf(s, sizeof s, "ENGINE SIZE: %d CC", (const int[]){ 50, 100, 150, 250 }[gGame.engineChoice & 3]); break;
        case 1: snprintf(s, sizeof s, "RACE LAPS: %d", gGame.laps); break;
        case 2: strcpy(s, "SELECT KART"); break;
        case 3:
        case 4:
            if (sEd.active && sConfSel == i) edit_text(e, sizeof e);
            else strcpy(e, i == 3 ? gGame.playerName : gGame.playerHandle);
            snprintf(s, sizeof s, "%s%s", i == 3 ? "NAME: " : "HANDLE: ", e);
            break;
        default: strcpy(s, "EXIT");
        }
        fe_text(i == sConfSel ? FC : FF, s, 320, ys[i], 1);
    }
    {   /* the chosen kart, either side (the 150 / 250cc karts are the bottom two rows) */
        int k = (gGame.playerKart & 7) + (gGame.engineChoice >= 2 ? 8 : 0);
        fe_sprite(k, 110, 245, 1, 0xffffffff);
        fe_sprite(k, 530, 245, 1, 0xffffffff);
    }
    title(FD, "CONFIGURE", 193, 42, "RACE", 450, 69);
}

/* the track details (track_select_draw_info 0x3b968): country, name, number, best lap */
static void track_details(int t, int x, int bx) {
    char s[48], tm[16];
    s32 rec = gGame.records[t & 15][gGame.engineChoice & 3];
    fe_text(FC, sTrackCountry[t], x, 115, 0);
    fe_text(FC, sTrackName[t], x, 140, 0);
    snprintf(s, sizeof s, "Track %d", (t & 1) * 8 + t / 2 + 1);
    fe_text(FB, s, x, 165, 0);
    if (rec) fmt_time(tm, sizeof tm, rec);
    else strcpy(tm, "00.00");
    snprintf(s, sizeof s, "%s Best Lap: %s", kCC[gGame.engineChoice & 3], tm);
    fe_text(FB, s, bx, 165, 0);
}

/* track select (screen 0x1b): the flags on a curve, the map spinning, the details typed out */
static int sTrkCur, sTrkAnim, sTrkMove, sTrkTyped;

static int trk_n(void) {
    int n = 0;
    while (n < 16 && game_list(n) != 99) n++;
    return n;
}

static void track_select(void) {
    int n = trk_n(), j, t;
    if (n == 0) n = 1;
    if (sTrkMove == 0) {
        if (sEv & (E_UP | E_LEFT)) { sTrkMove = -1; fe_sound(0); }
        else if (sEv & (E_DOWN | E_RIGHT)) { sTrkMove = 1; fe_sound(0); }
        else if (sEv & E_SELECT) {
            fe_sound(1);
            gGame.startIdx = (u16)sTrkCur;
            game_start_new();
            if (gGame.mode == MODE_SINGLE) { do_race(); return; }
            go(S_NEXT);
            return;
        } else if (sEv & E_BACK) { fe_sound(1); go(S_RACEMENU); return; }
    }
    if (sTrkMove && sTicked) {           /* 6 a tick along the curve (20 an item) */
        sTrkAnim += 6 * sTrkMove;
        if (sTrkAnim >= 20 || sTrkAnim <= -20) {
            sTrkCur = (sTrkCur + sTrkMove + n) % n;
            sTrkAnim = 0;
            sTrkMove = 0;
            sTrkTyped = sTick;
        }
    }
    t = game_list(sTrkCur);
    fe_load("HTR");
    fe_bg();
    for (j = 7; j >= -1; j--) {          /* the flags, the furthest first */
        int k = (sTrkCur + j + n * 2) % n, idx = (j + 1) * 20 + sTrkAnim;
        f32 x, y, s;
        if (idx < 0 || idx >= fe_ring_n()) continue;
        fe_ring(idx, &x, &y, &s);
        if (s <= 0.02f) continue;
        fe_sprite(kLetterOf[game_list(k) & 15], x, y, s, 0xffffffff);
    }
    if (sTrkMove == 0) spinning_map(9 + (t & 15), 325, 310);
    track_details(t & 15, 370, 467);
    if (sTrkMove == 0) typewriter(FB, t & 15, 423, 200, sTrkTyped);
    title(FD, "TRACK", 295, 42, "SELECT", 365, 69);
}

/* next circuit (screen 0x22, track_select_screen 0x3c4a0) */
static void next_circuit(void) {
    static const char kLetters[] = "EUFMG BA";
    char set[8];
    int t = gGame.track & 15;
    if (sEv & E_SELECT) {
        fe_sound(1);
        if (gGame.racesDone == 0) do_race();
        else go(S_SHOP);
        return;
    }
    if (sEv & E_BACK) {                  /* three times: the season is abandoned */
        fe_sound(1);
        if (++sBackCount >= 3) { go(S_RACEMENU); return; }
    }
    snprintf(set, sizeof set, "HTR%c", kLetters[kLetterOf[t]]);
    fe_load(set);
    fe_bg();
    spinning_map(t, 400, 310);
    track_details(t, 30, 127);
    typewriter(FB, t, 75, 200, 0);
    title(FD, "NEXT", 310, 42, "CIRCUIT", 360, 69);
}

/* the shop (menu_shop_upgrades 0x3d618): eight items on a turning ring */
static int sShopSel, sShopPos, sShopPending, sShopTyped;
static const char *const kShopName[8] = { "ENGINE TUNING", "TYRE COMPOUND", "BRIBE OFFICIAL", "FUEL TANK",
                                          "NITRO", "SUPER-GRIP", "SAVE GAME", "EXIT" };
static const char *const kShopText[8][6] = {
    { "Spend money on a", "good tune-up for", "increased power", "and torque across", "the full engine", "operating range." },
    { "Higher quality", "wheels will improve", "your traction and", "enable you to take", "corners at higher", "speed." },
    { "It may be illegal,", "but who's looking?", "Slip a backhander", "to a sympathetic", "party and points", "will be yours." },
    { "Buy a larger fuel", "tank to reduce the", "number of pit-stops", "needed during a", "longer race.", "" },
    { "The nitrous addition", "to your kart gives", "you five second", "bursts of extra power.", "", "" },
    { "This experimental", "polymer glue is", "sprayed onto the", "wheels for temporary", "extra grip.", "" },
    { "Save your current", "game, money, and", "shop settings.", "", "", "" },
    { "Start the next race.", "", "", "", "", "" },
};
static const int kShopIcon[8] = { 12, 11, 15, 8, 9, 10, 13, 14 };   /* SHPIC5 frames */
#define SHOP5(f) (36 + (f))              /* (sprite ids: SHPIC2/3/4 frames 0..35, then SHPIC5's) */

static s32 shop_price(int item) {
    static s32 table[4][6];
    static int loaded;
    if (!loaded) {
        u32 size;
        const u8 *d = (const u8 *)plat_load_file("race.bin", &size);
        if (d && size >= 96) memcpy(table, d + size - 96, 96);
        if (d) free((void *)d);
        loaded = 1;
    }
    return item < 6 ? table[gGame.cls & 3][item] : 0;
}

static int *shop_level(Entrant *e, int item, int *max) {
    static int dummy;
    static int v[6];
    *max = 5;
    v[0] = e->engine; v[1] = e->tyres; v[2] = e->bribes; v[3] = e->tank; v[4] = e->nitroN; v[5] = e->gripN;
    if (item < 6) return &v[item];
    return &dummy;
}

static void shop_buy(Entrant *e, int item) {
    int max, lvl = *shop_level(e, item, &max);
    s32 price = shop_price(item);
    if (lvl >= max || price > e->money - sShopPending) return;
    sShopPending += price;
    switch (item) {
    case 0: e->engine++; break;
    case 1: e->tyres++; break;
    case 2: e->bribes++; e->points += 5; break;   /* (+5 championship points at once) */
    case 3: e->tank++; break;
    case 4: e->nitroN++; break;
    case 5: e->gripN++; break;
    }
    fe_sound(1);
}

static void shop_pay(Entrant *e) {
    e->money -= sShopPending;
    sShopPending = 0;
}

static void shop(void) {
    Entrant *e = &gGame.e[gGame.playerKart & 7];
    int T = 8 * 38, i, order[8], max, lvl;
    char s[40];
    if (sEv & (E_LEFT | E_RIGHT)) {
        sShopSel = (sShopSel + ((sEv & E_LEFT) ? 7 : 1)) % 8;
        sShopTyped = sTick;
        fe_sound(0);
    }
    if (sEv & E_SELECT) {
        if (sShopSel < 6) shop_buy(e, sShopSel);
        else {
            fe_sound(1);
            shop_pay(e);
            if (sShopSel == 6) { go(S_SAVE); return; }
            do_race();
            return;
        }
    }
    if (sEv & E_BACK) {
        fe_sound(1);
        if (++sBackCount >= 3) { shop_pay(e); go(S_RACEMENU); return; }
    }
    if (sShopPending > 0 && sTicked) {   /* the cash counts down ($200 every 4 units) */
        int d = sShopPending < 250 ? sShopPending : 250;
        e->money -= d;
        sShopPending -= d;
    }
    if (sTicked) {
        int want = sShopSel * 38, dd = ((want - sShopPos) % T + T) % T;
        if (dd > T / 2) dd -= T;
        if (dd > 8) dd = 8;
        if (dd < -8) dd = -8;
        sShopPos = ((sShopPos + dd) % T + T) % T;
    }
    fe_load("HSH");
    fe_bg();
    {   /* the item's big picture (shop_get_item_icon 0x3d4a0) */
        int it = sShopSel, spr;
        lvl = *shop_level(e, it, &max);
        spr = it == 0 ? lvl : it == 1 ? 12 + lvl : it == 2 ? 6 + lvl : it == 3 ? 24 + lvl :
              it == 4 ? 24 + 6 + (lvl > 5 ? 5 : lvl) : it == 5 ? 12 + 6 + (lvl > 5 ? 5 : lvl) : SHOP5(it == 6 ? 6 : 7);
        fe_sprite(spr, 102, 150, 1.0f, 0xffffffff);
    }
    for (i = 0; i < 8; i++) order[i] = i;
    for (i = 0; i < 8; i++) {
        int j;
        for (j = i + 1; j < 8; j++) {
            f32 xa, ya, sa, xb, yb, sb;
            fe_ring(sShopPos - order[i] * 38, &xa, &ya, &sa);
            fe_ring(sShopPos - order[j] * 38, &xb, &yb, &sb);
            if (sb < sa) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        }
    }
    for (i = 0; i < 8; i++) {
        int it = order[i];
        f32 x, y, sc, hh;
        fe_ring(sShopPos - it * 38, &x, &y, &sc);
        fe_sprite(SHOP5(kShopIcon[it]), x, y, sc, 0xffffffff);
        if (it < 6) {                    /* the level (or how many) and the price */
            hh = fe_sprite_h(SHOP5(kShopIcon[it])) * sc * 0.5f;
            lvl = *shop_level(e, it, &max);
            fe_sprite(SHOP5(lvl > 5 ? 5 : lvl), x, y + hh + 8 * sc, sc, 0xffffffff);
            snprintf(s, sizeof s, "$%d", (int)shop_price(it));
            fe_text_scaled(sc > 0.6f ? FE : FB, s, x, y + hh + 26 * sc, 1, 0xffffffff, 1.0f);
        }
    }
    snprintf(s, sizeof s, "CASH: $%d", (int)(e->money - 0));
    fe_text(FF, s, 100, 230, 1);
    fe_text(FC, kShopName[sShopSel], 420, 434, 1);
    {   /* the description, typed out */
        int shown = (sTick - sShopTyped) * 5 / 2;
        for (i = 0; i < 6 && shown > 0; i++) {
            char line[32];
            int n = (int)strlen(kShopText[sShopSel][i]);
            strcpy(line, kShopText[sShopSel][i]);
            if (shown < n) line[shown] = 0;
            fe_text(FB, line, 20, 275 + i * 20, 0);
            shown -= n;
        }
    }
    snprintf(s, sizeof s, "- %s -", sTrackName[gGame.track & 15]);
    fe_text(FA, s, 320, 42, 1);
    fe_text(FA, "RACE PREPARATION", 320, 69, 1);
}

/* the results (results_screen_update 0x398b0): race results, and in a season the
 * championship too, on the two faces of a turning panel; the player's row blinks */
static int sFace, sFaceTurn, sFaceSeen;

static void results(void) {
    char s[48], tm[16];
    int season = gGame.mode != MODE_SINGLE, i, show;
    f32 w;
    if (sFaceTurn == 0) {
        if (season && (sEv & (E_LEFT | E_RIGHT))) { sFaceTurn = 1; fe_sound(0); }
        else if (sEv & E_SELECT) {
            fe_sound(1);
            if (season && !sFaceSeen) sFaceTurn = 1;
            else {
                if (!game_next_race()) {
                    if (gGame.mode == MODE_SINGLE) { go(S_RACEMENU); return; }
                    go(gGame.e[gGame.playerKart & 7].standing == 0 ? S_CHAMPION : S_PLACED);
                    return;
                }
                go(S_NEXT);
                return;
            }
        }
    } else if (sTicked && ++sFaceTurn > 8) {
        sFaceTurn = 0;
    } else if (sTicked && sFaceTurn == 5) {
        sFace ^= 1;
        if (sFace) sFaceSeen = 1;
    }
    fe_load("HRES");
    fe_bg();
    w = sFaceTurn ? shz_cosf(sFaceTurn * PI_F / 8) : 1.0f;
    if (w < 0) w = -w;
    fe_sprite_rect(1 + sFace, 321 - 275 * w, 81, 321 + 275 * w, 441, 0xffffffff);
    title(FA, sFace ? "CHAMPIONSHIP" : "RACE RESULTS", sFace ? 267 : 245, 36, NULL, 0, 0);
    if (sFaceTurn) return;
    snprintf(s, sizeof s, " CIRCUIT: %s", sTrackName[gGame.track & 15]);
    fe_text(FC, s, 320, 120, 1);
    fe_text(FF, "POS", 130, 180, 2);
    fe_text(FF, "DRIVER", 150, 180, 0);
    fe_text(FF, sFace ? "PRIZE MONEY" : "BEST LAP", 490, 180, 2);
    fe_text(FF, "PTS.", 565, 180, 2);
    show = (sTick % 10) >= 4;            /* (the player's rows: 20 units off, 30 on) */
    for (i = 0; i < 8; i++) {
        int k = sFace ? game_kart_at_standing(i) : gRaceOrder[i];
        f32 y = 212 + 25 * i;
        const Entrant *e;
        if (k < 0) continue;
        if (k == gGame.playerKart && !show) continue;
        e = &gGame.e[k];
        snprintf(s, sizeof s, "%d.", i + 1);
        fe_text(FF, s, 130, y, 2);
        fe_text(FF, driver_name(k), 150, y, 0);
        if (sFace) {
            snprintf(s, sizeof s, "$%d", (int)e->money);
            snprintf(tm, sizeof tm, "%d", (int)e->points);
        } else {
            if (gRaceBest[k] > 0) fmt_time(s, sizeof s, gRaceBest[k]);
            else strcpy(s, "--.--");
            snprintf(tm, sizeof tm, "%d", (int)e->racePoints);
            if (k == gGame.fastestKart && gRaceFinished[k]) fe_sprite(0, 390 + 13, y - 5, 1, 0xffffffff);
        }
        fe_text(FF, s, 490, y, 2);
        fe_text(FF, tm, 560, y, 2);
    }
}

/* the end of a season: the champion (season_draw_champion 0x3b1a4), or the player's
 * place (season_draw_place 0x3b5cc) */
static void season_figures(int k, int x, int y0, const int *dy) {
    char s[48];
    const Entrant *e = &gGame.e[k];
    snprintf(s, sizeof s, "CHAMPIONSHIP POINTS: %d", (int)e->points); fe_text(FB, s, x, y0 + dy[0], 1);
    snprintf(s, sizeof s, "TOTAL EARNINGS: $%d", (int)e->moneyTotal); fe_text(FB, s, x, y0 + dy[1], 1);
    snprintf(s, sizeof s, "RACE WINS: %d", e->wins); fe_text(FB, s, x, y0 + dy[2], 1);
    snprintf(s, sizeof s, "FASTEST LAPS: %d", e->fastest); fe_text(FB, s, x, y0 + dy[3], 1);
    snprintf(s, sizeof s, "LAPS PER RACE: %d", gGame.laps); fe_text(FB, s, x, y0 + dy[4], 1);
}

static int sHiRow = -1;

static void season_end_next(void) {
    sHiRow = game_hiscore_insert();
    if (sHiRow >= 0) {
        edit_start(gGame.playerName, 14);
        go(S_HISCORES);
    } else {
        game_settings_save();
        go(S_MAIN);
    }
}

static void champion(void) {
    static const char *const kTitle[4] = { "", "EASY SEASON CHAMPION", "HARD SEASON CHAMPION", "MANIC CUP CHAMPION" };
    static const int dy[5] = { 0, 25, 75, 100, 150 };
    int k = game_kart_at_standing(0), x;
    char set[8], s[48];
    if (sTick > 60 && (sEv & E_SELECT)) {
        fe_sound(1);
        if (k == gGame.playerKart) season_end_next();
        else go(S_PLACED);
        return;
    }
    snprintf(set, sizeof set, "HW%d", k & 7);
    fe_load(set);
    fe_bg();
    fe_text(FF, kTitle[gGame.mode & 3], 183, 55, 1);
    snprintf(s, sizeof s, "- %s -", driver_name(k));
    fe_text(FF, s, 183, 80, 1);
    season_figures(k, 183, 135, dy);
    if (k == gGame.playerKart) fe_text(FF, "- CONGRATULATIONS -", 183, 335, 1);
    x = 640 - (sTick * 10) % 640;        /* the ticker */
    fe_text(FA, "CHAMPIONSHIP WINNER", x - 320, 425, 1);
    fe_text(FA, "CHAMPIONSHIP WINNER", x + 320, 425, 1);
}

static void placed(void) {
    static const char *const kMode2[4] = { "", "EASY SEASON", "HARD SEASON", "MANIC CUP" };
    static const char *const kPlace[8] = { "1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH" };
    static const int dy[5] = { 0, 25, 75, 100, 150 };
    int k = gGame.playerKart & 7;
    char set[8], s[48];
    if (sTick > 60 && (sEv & E_SELECT)) { fe_sound(1); season_end_next(); return; }
    snprintf(set, sizeof set, "HL%d", k);
    fe_load(set);
    fe_bg();
    fe_text(FF, kMode2[gGame.mode & 3], 188, 145, 1);
    snprintf(s, sizeof s, "%s PLACE", kPlace[gGame.e[k].standing & 7]);
    fe_text(FF, s, 188, 170, 1);
    snprintf(s, sizeof s, "- %s -", driver_name(k));
    fe_text(FF, s, 188, 195, 1);
    season_figures(k, 188, 245, dy);
    title(FA, "CHAMPIONSHIP", 245, 42, "LOSER", 245, 69);
}

/* the all-time season greats (draw_all_time_greats 0x2b3ac): after a season that makes
 * the table, the player's name is typed into its row */
static void hiscores(void) {
    char s[32];
    int i;
    if (sHiRow >= 0 && sEd.active) {
        int r = edit_tick();
        if (r != 0) {
            char t[16];
            strcpy(t, "NONE");
            if (r > 0) edit_result(t, sizeof t);
            else sEd.active = 0;
            if (strcmp(t, "NONE") == 0) strncpy(t, driver_name(gGame.playerKart), 15);
            strncpy(gHi[sHiRow].name, t, 19);
            sHiRow = -1;
            game_settings_save();
        }
    } else if ((sEv & (E_SELECT | E_BACK)) || sTick > 400) {
        fe_sound(1);
        go(S_MAIN);
        return;
    }
    fe_load("HHI");
    fe_bg();
    fe_text(FC, "ALL TIME SEASON GREATS", 320, 120, 1);
    fe_text(FF, "DRIVER", 80, 180, 0);
    fe_text(FF, "LAPS", 350, 180, 2);
    fe_text(FF, "EARNINGS", 490, 180, 2);
    fe_text(FF, "PTS.", 565, 180, 2);
    for (i = 0; i < 8; i++) {
        f32 y = 212 + 25 * i;
        if (i == sHiRow && sEd.active) edit_text(s, sizeof s);
        else strncpy(s, gHi[i].name, 31), s[31] = 0;
        fe_text(FF, s, 80, y, 0);
        snprintf(s, sizeof s, "%d", gHi[i].laps); fe_text(FF, s, 350, y, 2);
        snprintf(s, sizeof s, "$%d", (int)gHi[i].earnings); fe_text(FF, s, 490, y, 2);
        snprintf(s, sizeof s, "%d", (int)gHi[i].points); fe_text(FF, s, 560, y, 2);
    }
    title(FA, "HI-SCORES", 350, 36, NULL, 0, 0);
}

/* load / save (menu_load_game_draw 0x477c0, menu_save_game 0x474c8): six slots on the VMU */
static int sSlotSel;
static char sSlotName[6][16];
static int sSlotsRead = -1;

static int sSlotErr;                     /* a save there that couldn't be read (shown, with its code) */
static void read_slots(void) {
    int i;
    sSlotErr = 0;
    for (i = 0; i < 6; i++) {
        game_load(i, sSlotName[i], 0);
        if (!sSlotName[i][0] && plat_save_error() && !sSlotErr) sSlotErr = plat_save_error();
    }
    sSlotsRead = sTick;
}

static void load_save(int save) {
    char s[32];
    int i;
    if (sSlotsRead < 0) read_slots();
    if (save && sEd.active) {
        int r = edit_tick();
        if (r > 0) {
            char t[16];
            edit_result(t, sizeof t);
            if (strcmp(t, "NONE") == 0) snprintf(t, sizeof t, "SAVE %d", (sSlotSel + 1) % 10);
            game_save(sSlotSel, t);
            strcpy(sSlotName[sSlotSel], t);
            go(S_SHOP);
            return;
        } else if (r < 0) sEd.active = 0;
    } else {
        if (sEv & E_UP) { sSlotSel = (sSlotSel + 6) % 7; fe_sound(0); }
        if (sEv & E_DOWN) { sSlotSel = (sSlotSel + 1) % 7; fe_sound(0); }
        if (sEv & E_SELECT) {
            fe_sound(1);
            if (sSlotSel == 6) { go(save ? S_SHOP : S_MAIN); return; }
            if (save) {
                char d[16];
                snprintf(d, sizeof d, "SAVE %d", (sSlotSel + 1) % 10);
                edit_start(sSlotName[sSlotSel][0] ? sSlotName[sSlotSel] : d, 14);
            } else if (sSlotName[sSlotSel][0] && game_load(sSlotSel, NULL, 1)) {
                sShopPending = 0;
                go(S_SHOP);
                return;
            }
        }
        if (sEv & E_BACK) { fe_sound(1); go(save ? S_SHOP : S_MAIN); return; }
    }
    fe_load("HSAV");
    fe_bg();
    for (i = 0; i < 6; i++) {
        if (save && sEd.active && i == sSlotSel) edit_text(s, sizeof s);
        else strcpy(s, sSlotName[i][0] ? sSlotName[i] : "EMPTY SLOT");
        fe_text(i == sSlotSel ? FC : FF, s, 320, 160 + 40 * i, 1);
    }
    fe_text(sSlotSel == 6 ? FC : FF, "EXIT", 320, 420, 1);
    if (!plat_save_present()) fe_text(FF, "NO VMU FOUND", 320, 455, 1);
    else if (sSlotErr) {
        snprintf(s, sizeof s, "VMU READ ERROR %d", sSlotErr);
        fe_text(FF, s, 320, 455, 1);
    }
    title(FD, save ? "SAVE" : "LOAD", 400, 42, "GAME", 450, 69);
}

/* ---------------------------------------------------------------- the loop */
static void go(int screen) {
    sScreen = screen;
    sTick = 0;
    sBackCount = 0;
    if (screen == S_LOGO) sLogoSkip = 0;
    if (screen == S_TRACKSEL) { sTrkCur = 0; sTrkAnim = sTrkMove = 0; sTrkTyped = 0; }
    if (screen == S_SHOP) { sShopSel = 7; sShopPos = 7 * 38; sShopTyped = 0; }
    if (screen == S_RESULTS) { sFace = sFaceTurn = sFaceSeen = 0; }
    if (screen == S_LOAD || screen == S_SAVE) { sSlotsRead = -1; sSlotSel = 0; sEd.active = 0; }
    if (screen == S_MAIN) { sMainPos = sMainSel * 100; sHiRow = -1; }
    if (screen == S_HISCORES && sHiRow < 0) sEd.active = 0;
}

static void track_texts(void) {
    u32 size;
    const u8 *d = (const u8 *)plat_load_file("trackinfo.bin", &size);
    int t, i;
    if (d && size >= 16 * (32 + 11 * 32)) {
        for (t = 0; t < 16; t++) {
            const u8 *p = d + t * (32 + 11 * 32);
            memcpy(sTrackCountry[t], p, 12);
            memcpy(sTrackName[t], p + 12, 20);
            sTrackCountry[t][11] = sTrackName[t][19] = 0;
            for (i = 0; i < 11; i++) {
                memcpy(sTrackDesc[t][i], p + 32 + i * 32, 32);
                sTrackDesc[t][i][31] = 0;
            }
        }
    }
    if (d) free((void *)d);
}

void front_run(void) {
    u32 t0 = plat_time_us(), acc = 0, last = 0;
    track_texts();
    music_play("mus02.adp");
    music_volume(160);
    go(S_LOGO);
    while (plat_running()) {
        u32 now = plat_time_us();
        int ticked = 0, c;
        acc += now - t0;
        t0 = now;
        if (acc > 200000) acc = 200000;
        plat_input(&sIn);
        sPressed |= sIn.buttons & ~last;
        last = sIn.buttons;
        {   /* soft reset (A+B+X+Y+START held): back to the title, once until let go */
            static int held;
            if ((sIn.buttons & BTN_RESET) == BTN_RESET) {
                if (!held && sScreen != S_LOGO) {
                    sEd.active = 0;
                    sPressed = 0;
                    go(S_LOGO);
                }
                held = 1;
                sPressed = 0;
            } else held = 0;
        }
        fe_begin();
        while (acc >= 50000) {           /* (20 ticks a second, as frontend_wait_tick) */
            acc -= 50000;
            read_events();
            if (!sEd.active) {           /* the cheat codes, typed on a keyboard */
                while ((c = plat_key()) != 0) {
                    if (game_cheat_key(c)) fe_sound(1);
                }
            }
            ticked = 1;
            sTick++;
            break;                       /* (one tick a frame at most) */
        }
        if (!ticked) sEv = 0;
        sTicked = ticked;
        switch (sScreen) {
        case S_LOGO: logo(); break;
        case S_MAIN: main_menu(); break;
        case S_RACEMENU: race_menu(); break;
        case S_CONFRACE: configure_race(); break;
        case S_TRACKSEL: track_select(); break;
        case S_NEXT: next_circuit(); break;
        case S_SHOP: shop(); break;
        case S_RESULTS: results(); break;
        case S_CHAMPION: champion(); break;
        case S_PLACED: placed(); break;
        case S_HISCORES: hiscores(); break;
        case S_LOAD: load_save(0); break;
        case S_SAVE: load_save(1); break;
        }
        fe_end();
        sEv = 0;
        if (sWantRace) {
            sWantRace = 0;
            run_race();
            t0 = plat_time_us();
            acc = 0;
            plat_input(&sIn);
            last = sIn.buttons;
            sPressed = 0;
        }
    }
}
