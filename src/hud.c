/*
 * hud.c - the on-screen display, laid out as the original's (hud_draw_lap_times
 * 0x45330) on its 320x200 screen, scaled to 640x480, with its own fonts and
 * icons (data/XX_hud.tex, from the theme's nFONT0..4 and nSMLNUM .AZ files):
 *
 *   top left    lap (LAP 1 OF 4), current or last lap time, best lap
 *   top middle  position (1ST..8TH) and the track map with every kart on it
 *   top right   the karts just ahead and just behind: name, the gap (as timed at
 *               the line) and helmet
 *   bottom left the N / S / F bars (nitro, super grip, fuel: flashing when low) and
 *               the picked power-up with how many are left
 *   bottom right speed in km/h (the original's |speed| * 9 / 2500)
 *
 * and, after the race, the results: every kart's place, driver, race time and best
 * lap, in the same fonts (the big one for the places), the player's row marked by
 * their helmet.
 */
#include "game.h"
#include <stdio.h>
#include <string.h>

enum { F_SMALL = 0, F_YELLOW = 1, F_DOTS = 2, F_BIG = 3, F_POS = 4, F_MAP = 5 };

#define SX 2.0f                          /* the original's 320 x 200 -> 640 x 480 */
#define SY 2.4f
#define HUD_Z 100.0f                     /* (in front of everything) */
#define MAXQ 800                        /* (the results table's bars take ~330; at most 50 nearer than HUD_Z) */

static Vtx sV[MAXQ * 6];
static int sN;

static const u16 *rect_of(int font, int k) {
    if (gTrack.hudRect == NULL) return NULL;
    return gTrack.hudRect + (font * 60 + k) * 4;
}

/* columns c0..c0+n-1 of a glyph, their top left at (x, y) in the original's screen
 * (the original builds its bar frames like this: draw_sprite_columns_2x) */
static int glyph_cols(int font, int k, f32 x, f32 y, int c0, int n) {
    const u16 *r = rect_of(font, k);
    f32 x0, y0, x1, y1, u0, v0, u1, v1, z;
    Vtx *v;
    if (r == NULL || r[2] == 0) return 0;
    if (c0 + n > r[2]) n = r[2] - c0;
    if (n <= 0 || sN + 6 > MAXQ * 6) return n;
    x0 = x * SX; y0 = y * SY; x1 = (x + n) * SX; y1 = (y + r[3]) * SY;
    u0 = (r[0] + c0) * (1.0f / 512); v0 = r[1] * (1.0f / 256);
    u1 = (r[0] + c0 + n) * (1.0f / 512); v1 = (r[1] + r[3]) * (1.0f / 256);
    v = &sV[sN];
    /* each piece nearer than the one before, so the later ones (the big lap digits,
       the gap arrows, over their bars) are on top for certain: at the same depth the
       real PowerVR doesn't keep to the drawing order (Flycast does). The steps are
       1/16 (out of 100 or so): the real hardware compares depths with less precision
       than Flycast, and tiny steps (1/16384 before) came out as the same depth there */
    z = HUD_Z + (f32)(sN / 6) * (1.0f / 16);
    v[0] = (Vtx){ x0, y0, z, u0, v0, 0xffffffff };
    v[1] = (Vtx){ x1, y0, z, u1, v0, 0xffffffff };
    v[2] = (Vtx){ x1, y1, z, u1, v1, 0xffffffff };
    v[3] = v[0];
    v[4] = v[2];
    v[5] = (Vtx){ x0, y1, z, u0, v1, 0xffffffff };
    sN += 6;
    return n;
}

/* one glyph with its top left at (x, y); returns its width */
static int glyph(int font, int k, f32 x, f32 y) {
    return glyph_cols(font, k, x, y, 0, 64);
}

/* the fonts' letters: glyph = character - '!' ('0' is 15, 'A' is 32); in the small
 * font '-' and ':' are at 58 and 59 */
static int glyph_of(int font, char c) {
    if (font == F_SMALL && c == '-') return 58;
    if (font == F_SMALL && c == ':') return 59;
    return c - 0x21;
}

/* the small font steps 6 pixels a letter (its letters are 5 wide plus a gap); the
 * others by each letter's width plus 1 */
static int advance(int font, char c) {
    const u16 *r;
    if (font == F_SMALL) return 6;
    if (c == ' ') return 8;
    r = rect_of(font, glyph_of(font, c));
    return r && r[2] ? r[2] + 1 : 0;
}

static int text_width(int font, const char *s) {
    int w = 0;
    for (; *s; s++) w += advance(font, *s);
    return w;
}

static void text(int font, const char *s, f32 x, f32 y) {
    for (; *s; s++) {
        if (*s != ' ') glyph(font, glyph_of(font, *s), x, y);
        x += advance(font, *s);
    }
}

/* the dark bar the texts sit on: the 6-wide tile (small font glyph 2) repeated */
static void bar(f32 x, f32 y, int len) {
    int i;
    for (i = 0; i < len / 6; i++) glyph(F_SMALL, 2, x + i * 6, y);
    glyph(F_SMALL, 2, x + len - 6, y);
}

/* a solid column (the N / S / F bars), bottom at y */
static void column(f32 x, f32 bottom, int h, u32 argb) {
    Vtx v[6];
    f32 x0 = x * SX, x1 = (x + 3) * SX, y0 = (bottom - h) * SY, y1 = bottom * SY;
    if (h <= 0) return;
    v[0] = (Vtx){ x0, y0, HUD_Z + 60, 0, 0, argb };
    v[1] = (Vtx){ x1, y0, HUD_Z + 60, 0, 0, argb };
    v[2] = (Vtx){ x1, y1, HUD_Z + 60, 0, 0, argb };
    v[3] = v[0];
    v[4] = v[2];
    v[5] = (Vtx){ x0, y1, HUD_Z + 60, 0, 0, argb };
    gfx_tris(v, 6, -1, LIST_BLEND);      /* (drawn after the glyphs: on top of the frame) */
}

/* lap time as the original prints it: seconds.hundredths */
static void time_text(char *out, int n, s32 frames) {
    int s = frames / 60, c = (frames % 60) * 100 / 60;
    if (s > 99) s = 99;
    snprintf(out, (size_t)n, c < 10 ? "%d.0%d" : "%d.%d", s, c);
}

static const Kart *kart_at(int position) {
    int i;
    for (i = 0; i < gKartN; i++) {
        if (gKarts[i].position == position) return &gKarts[i];
    }
    return NULL;
}

/* a rival's bar: name right-aligned up to 0x10a, then the gap as last timed at the
 * line (an arrow, then the time or the laps), helmet at the right end. A gap that's
 * out of date (the rival has since swapped places with the player) is cleared. */
static void rival(const Kart *k, const Kart *o, f32 y, f32 helmetY, int ahead) {
    char name[16];
    int w, i;
    const char *g;
    if (o == NULL) return;
    strncpy(name, driver_handle(o->index), 15);   /* (the driver's handle: +0x82) */
    name[15] = 0;
    w = text_width(F_SMALL, name);
    glyph(F_SMALL, 25, 0xd4, y);
    if (0x30 - w > 5) bar(0xda, y, 0x30 - w);
    text(F_SMALL, name, 0x10a - w, y);
    glyph(F_SMALL, 2, 0x10a, y);
    {
        Kart *m = &gKarts[k->index & 7];
        int j = o->index & 7;
        if (ahead ? (m->gap[j].time > 0 || m->gap[j].laps > 0) : (m->gap[j].time < 0 || m->gap[j].laps < 0)) {
            memset(&m->gap[j], 0, sizeof m->gap[j]);
        }
        g = m->gap[j].text;
    }
    if (g[0] == 0) {
        bar(0x10c, y, 0x28);
    } else {
        int n = (int)strlen(g) * 6;
        if (0x133 - (0x10c + n) > 5) bar(0x10c + n, y, 0x133 - (0x10c + n));
        for (i = 0; g[i]; i++) {         /* (',' and '-' here are the arrows, glyphs 11 and 12) */
            int c = g[i] == ',' || g[i] == '-' ? g[i] - 0x21 : glyph_of(F_SMALL, g[i]);
            glyph(F_SMALL, c, 0x10c + i * 6, y);
        }
    }
    glyph(F_SMALL, 3 + (o->index & 7), 0x131, helmetY);
}

void hud_draw(const Kart *k, int lapsTotal) {
    char s[24];
    int i, w, lap;
    if (gTrack.texHud < 0 || gTrack.hudRect == NULL) return;
    sN = 0;

    /* track map and every kart on it */
    glyph(F_MAP, gTrack.index & 1, 0x8c, 0x1c);
    for (i = 0; i < gKartN; i++) {
        const Kart *o = &gKarts[i];
        if (o == k) continue;
        glyph(F_DOTS, 8 + i, (o->x * 10 >> 15) + 0x8b, (o->y * 10 >> 15) + 0x1b);
    }
    glyph(F_DOTS, k->index & 7, (k->x * 10 >> 15) + 0x8a, (k->y * 10 >> 15) + 0x1a);

    /* position */
    if (k->position >= 1 && k->position <= 8) glyph(F_POS, k->position - 1, 0x90, 3);

    /* the karts just ahead and behind */
    if (k->position > 1) rival(k, kart_at(k->position - 1), 5, 2, 1);
    if (k->position < gKartN) rival(k, kart_at(k->position + 1), 0x13, 0x10, 0);

    /* lap and times */
    glyph(F_SMALL, 26, 0x5e, 5);
    bar(0xc, 5, 0x50);
    glyph(F_SMALL, 26, 0x5e, 0x13);
    bar(0xc, 0x13, 0x18);
    glyph(F_SMALL, 26, 0x5e, 0x21);
    bar(0xe, 0x21, 0x16);
    glyph(F_SMALL, 0, 1, 1);
    glyph(F_SMALL, 1, 1, 0xf);
    glyph(F_SMALL, 29, 1, 0x1d);
    bar(0x3c, 0x13, 0xc);
    bar(0x3c, 0x21, 0xc);
    if (k->lapFrames < 300 && k->laps >= 1 && k->lastLap) {    /* (just after the line: the lap just done) */
        text(F_SMALL, "LAST", 0x24, 0x13);
        time_text(s, sizeof s, k->lastLap);
    } else {
        text(F_SMALL, "CURRENT", 0x12, 0x13);
        time_text(s, sizeof s, k->lapFrames);
    }
    text(F_SMALL, ":", 0x3c, 0x13);
    text(F_SMALL, s, k->lapFrames / 60 < 10 && !(k->lapFrames < 300 && k->lastLap && k->laps >= 1) ? 0x48 : 0x42, 0x13);
    text(F_SMALL, "BEST", 0x24, 0x21);
    text(F_SMALL, ":", 0x3c, 0x21);
    if (k->bestLap) time_text(s, sizeof s, k->bestLap);
    else strcpy(s, "00.00");
    text(F_SMALL, s, 0x42, 0x21);
    lap = k->laps + 1;
    if (lap > lapsTotal) lap = lapsTotal;
    text(F_SMALL, "LAP", 0x2d, 5);
    text(F_SMALL, "OF", 0x4b, 5);
    snprintf(s, sizeof s, "%d", lap);
    text(F_YELLOW, s, 0x41, 2);
    snprintf(s, sizeof s, "%d", lapsTotal);
    text(F_YELLOW, s, 0x57, 2);

    /* speed */
    text(F_SMALL, "KMH", 0x128, 0xbe);
    snprintf(s, sizeof s, "%d", (int)((k->speed < 0 ? -k->speed : k->speed) * 9 / 2500));
    w = text_width(F_BIG, s);
    text(F_BIG, s, 0x126 - w, 0xb4);

    /* N / S / F: a frame built from glyphs 30 (top) and 31 (bottom): their left 6 columns,
       then columns 1-5 twice, then columns 8-9; and the power-ups left */
    {
        int g;
        for (g = 0; g < 2; g++) {
            f32 y = g ? 0x9e : 0x8a;
            glyph_cols(F_SMALL, 30 + g, 8, y, 0, 6);
            glyph_cols(F_SMALL, 30 + g, 0xe, y, 1, 5);
            glyph_cols(F_SMALL, 30 + g, 0x13, y, 1, 5);
            glyph_cols(F_SMALL, 30 + g, 0x18, y, 8, 2);
        }
    }
    text(F_SMALL, "NSF", 8, 0xaa);
    if (k->selected == 0 || k->selected == 2) {   /* the picked power-up and how many are left */
        glyph(F_SMALL, k->selected ? 28 : 27, 4, 0xb1);
        snprintf(s, sizeof s, "X%d", (int)(k->selected ? k->gripN : k->nitroN));
        text(F_SMALL, s, 0x1a, 0xbe);
    }

    if (sN) gfx_tris(sV, sN, gTrack.texHud, LIST_CUTOUT);

    /* (the bars, after the glyphs: solid colour) */
    column(0xb, 0xa7, k->powerupTimer * 27 / 4000, 0xff2060ff);
    column(0x10, 0xa7, k->supergrip * 27 / 0x1194, 0xffffd020);
    {                                    /* fuel: flashing when nearly out */
        static unsigned blink;
        int f = k->fuelMax ? k->fuel * 27 / k->fuelMax : 0;
        if (f >= 7) column(0x15, 0xa7, f, 0xff20d040);
        else if (++blink & 4) column(0x15, 0xa7, f, (blink & 3) == 0 ? 0xffffffff : 0xffe02020);
    }
}

/* a race time: minutes:seconds.hundredths */
static void race_time_text(char *out, int n, s32 frames) {
    int m = frames / 3600, sec = frames / 60 % 60, c = (frames % 60) * 100 / 60;
    if (m > 9) m = 9;
    snprintf(out, (size_t)n, "%d:%d%d.%d%d", m, sec / 10, sec % 10, c / 10, c % 10);
}

void hud_results(const Kart *player) {
    char s[24];
    int i, w;
    if (gTrack.texHud < 0 || gTrack.hudRect == NULL) return;
    sN = 0;
    {                                    /* (the race dimmed behind the table) */
        Vtx d[6];
        d[0] = (Vtx){ 0, 0, HUD_Z - 1, 0, 0, 0x90000000 };
        d[1] = (Vtx){ SCREEN_W, 0, HUD_Z - 1, 0, 0, 0x90000000 };
        d[2] = (Vtx){ SCREEN_W, SCREEN_H, HUD_Z - 1, 0, 0, 0x90000000 };
        d[3] = d[0];
        d[4] = d[2];
        d[5] = (Vtx){ 0, SCREEN_H, HUD_Z - 1, 0, 0, 0x90000000 };
        gfx_tris(d, 6, -1, LIST_BLEND);
    }
    w = text_width(F_BIG, "RESULTS");
    text(F_BIG, "RESULTS", 160 - w / 2, 4);
    text(F_SMALL, "TIME", 0xb4, 0x1b);
    text(F_SMALL, "BEST LAP", 0xe0, 0x1b);
    for (i = 0; i < gKartN; i++) {
        int n = race_order(i);
        f32 y = 0x24 + i * 19;
        const Kart *k;
        char name[16];
        if (n < 0) continue;
        k = &gKarts[n];
        snprintf(s, sizeof s, "%d", i + 1);
        text(F_BIG, s, 0x30 - text_width(F_BIG, s), y);
        bar(0x38, y + 5, 0xe4);
        strncpy(name, gTrack.hudNames ? gTrack.hudNames + 16 * (k->driver & 15) : "", 15);
        name[15] = 0;
        text(F_SMALL, name, 0x3e, y + 5);
        if (k->finished) race_time_text(s, sizeof s, k->raceFrames);
        else strcpy(s, "-:--.--");
        text(F_SMALL, s, 0xb4, y + 5);
        if (k->bestLap) time_text(s, sizeof s, k->bestLap);
        else strcpy(s, "--.--");
        text(F_SMALL, s, 0xe8, y + 5);
        if (k == player) glyph(F_SMALL, 3 + (k->index & 7), 0x120, y + 2);
    }
    text(F_SMALL, "A RACE AGAIN", 0x30, 0xbe);
    w = text_width(F_SMALL, "START NEXT TRACK");
    text(F_SMALL, "START NEXT TRACK", 0x110 - w, 0xbe);
    if (sN) gfx_tris(sV, sN, gTrack.texHud, LIST_CUTOUT);
}

/* the pause menu (race_main_loop 0x3f1d0): CONTINUE, RETIRE, QUIT centred at
 * y 0x39, 0x57, 0x75; the picked one in the bigger font, 5 higher */
void hud_pause(int sel) {
    static const char *const kItem[3] = { "CONTINUE", "RETIRE", "QUIT" };
    static const int kY[3] = { 0x39, 0x57, 0x75 };
    int i;
    if (gTrack.texHud < 0 || gTrack.hudRect == NULL) return;
    sN = 0;
    for (i = 0; i < 3; i++) {
        int f = i == sel ? F_BIG : F_YELLOW;
        text(f, kItem[i], 160 - text_width(f, kItem[i]) / 2, kY[i] - (i == sel ? 5 : 0));
    }
    if (sN) gfx_tris(sV, sN, gTrack.texHud, LIST_CUTOUT);
}
