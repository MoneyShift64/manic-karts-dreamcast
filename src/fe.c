/*
 * fe.c - drawing the menu screens: one picture set at a time (data/fe_S.*, made by
 * tools/build_front.py from the original's hi-res H*.PCX pictures): the background,
 * the six fonts cut from PIC0 and the screen's icons, all in the screen's palette
 * (palette banks 2 = the background, 3 = sprites with colour 0 see-through).
 *
 * Coordinates are the original's 640 x 480. As the original (font_draw_glyph
 * 0x10728): text is drawn from its left edge with y its vertical centre; sprites
 * are centred on (x, y); scaling is nearest-neighbour (no smoothing).
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { u16 x, y, w, h, adv; } FeGlyph;

static char sSet[8];
static int sTex = -1, sTex2 = -1, sBg0 = -1, sBg1 = -1;
static f32 sTexW, sTexH, sTex2W, sTex2H;
static FeGlyph sGlyph[6][96];
static u16 (*sSpr)[4];
static int sSprN;
static struct { s16 x, y; u16 s; } *sRing;
static int sRingN;
static void *sBin;
static f32 sZ;

#define MAXQ 1500
static Vtx sQ[2][MAXQ * 6];              /* sprites in each texture, drawn at the end */
static int sQN[2];

/* ---------------------------------------------------------------- the menu file cache
 * Every menu screen's pictures, kept in memory once read: changing screens then needs
 * no disc reads, only the upload to video memory (a few milliseconds). The main sets
 * are read in behind the Virgin logo (fe_preload); the rest (the next-circuit and
 * results faces) the first time they're shown, and dropped first when space runs out. */
#define CACHE_N   96
#define CACHE_MAX (5u << 20)
static struct { char name[24]; u8 *data; u32 size; u8 core, used; } sCache[CACHE_N];
static u32 sCacheBytes;
static const char *const kCore[] = { "VIRGIN", "HM1", "HM3", "HM3B", "HTR", "HSH", "HRES", "HHI", "HSAV" };
static const char *const kPart[] = { ".pal", "_bg0.tex", "_bg1.tex", ".tex", "_2.tex", ".bin" };
#define CORE_N ((int)(sizeof kCore / sizeof kCore[0]))
#define PART_N ((int)(sizeof kPart / sizeof kPart[0]))

static int is_core(const char *name) {
    int i;
    for (i = 0; i < CORE_N; i++) {
        size_t n = strlen(kCore[i]);
        if (strncmp(name + 3, kCore[i], n) == 0 && (name[3 + n] == '.' || name[3 + n] == '_')) return 1;
    }
    return 0;
}

static void cache_drop(int i) {
    if (sCache[i].data) free(sCache[i].data);
    sCacheBytes -= sCache[i].size;
    memset(&sCache[i], 0, sizeof sCache[i]);
}

/* a menu file, from memory if it's been read before (NULL: no such file) */
static const u8 *fe_file(const char *name, u32 *size) {
    int i, slot = -1, core;
    u8 *d;
    u32 n = 0;
    for (i = 0; i < CACHE_N; i++) {
        if (sCache[i].used && strcmp(sCache[i].name, name) == 0) { *size = sCache[i].size; return sCache[i].data; }
    }
    d = (u8 *)plat_load_file(name, &n);
    if (d == NULL) n = 0;
    core = is_core(name);
    /* room: the other screens' files go first, oldest first (never the one on show) */
    for (i = 0; i < CACHE_N && (sCacheBytes + n > CACHE_MAX || slot < 0); i++) {
        if (!sCache[i].used) { if (slot < 0) slot = i; continue; }
        if (!sCache[i].core && strncmp(sCache[i].name + 3, sSet, strlen(sSet)) != 0) {
            cache_drop(i);
            if (slot < 0) slot = i;
        }
    }
    if (slot < 0) { *size = n; return d; }   /* (shouldn't happen: not kept, and leaks) */
    strncpy(sCache[slot].name, name, sizeof sCache[slot].name - 1);
    sCache[slot].data = d;                   /* (NULL kept too: no looking for it again) */
    sCache[slot].size = n;
    sCache[slot].core = (u8)core;
    sCache[slot].used = 1;
    sCacheBytes += n;
    *size = n;
    return d;
}

/* one more of the main menu files read in (1: all of them are) */
int fe_preload(void) {
    static int next;
    char fn[32];
    u32 size;
    if (next >= CORE_N * PART_N) return 1;
    snprintf(fn, sizeof fn, "fe_%s%s", kCore[next / PART_N], kPart[next % PART_N]);
    next++;
    fe_file(fn, &size);
    return next >= CORE_N * PART_N;
}

static int load_tex(const char *name, f32 *w, f32 *h) {
    int tw = 0, th = 0, id = -1;
    u32 size;
    const u8 *d = fe_file(name, &size);
    if (d) id = tex_from_mem(d, size, -1, &tw, &th);
    if (w) *w = (f32)tw;
    if (h) *h = (f32)th;
    return id;
}

/* every texture out (the race's too: track_forget), then this set's */
void fe_load(const char *set) {
    char fn[32];
    u32 size;
    u16 pal[256];
    const u8 *d;
    if (strcmp(set, sSet) == 0 && sTex >= 0) return;
    gfx_free_all();
    track_forget();
    strncpy(sSet, set, 7);
    snprintf(fn, sizeof fn, "fe_%s.pal", set);
    d = fe_file(fn, &size);
    if (d && size >= 512) {
        memcpy(pal, d, 512);
        gfx_palette(2, pal);
        pal[0] = 0;
        gfx_palette(3, pal);
    }
    snprintf(fn, sizeof fn, "fe_%s_bg0.tex", set);
    sBg0 = load_tex(fn, NULL, NULL);
    snprintf(fn, sizeof fn, "fe_%s_bg1.tex", set);
    sBg1 = load_tex(fn, NULL, NULL);
    snprintf(fn, sizeof fn, "fe_%s.tex", set);
    sTex = load_tex(fn, &sTexW, &sTexH);
    snprintf(fn, sizeof fn, "fe_%s_2.tex", set);
    sTex2 = load_tex(fn, &sTex2W, &sTex2H);
    snprintf(fn, sizeof fn, "fe_%s.bin", set);
    sBin = (void *)fe_file(fn, &size);       /* (it stays in the cache) */
    sSprN = sRingN = 0;
    memset(sGlyph, 0, sizeof sGlyph);
    if (sBin && size >= 4 + 6 * 96 * 10 + 2 && memcmp(sBin, "MKFE", 4) == 0) {
        const u8 *p = (const u8 *)sBin + 4;
        memcpy(sGlyph, p, sizeof sGlyph);
        p += sizeof sGlyph;
        sSprN = p[0] | p[1] << 8;
        sSpr = (u16 (*)[4])(p + 2);
        p += 2 + sSprN * 8;
        sRingN = p[0] | p[1] << 8;
        sRing = (void *)(p + 2);
    }
}

void fe_begin(void) {
    sQN[0] = sQN[1] = 0;
    sZ = 10;
    gfx_no_fog();
    gfx_set_bg(0, 0, 0);
    gfx_begin();
}

/* the background (opaque) */
void fe_bg(void) {
    Vtx v[6];
    f32 vv = 480.0f / 512;
    if (sBg0 >= 0) {
        v[0] = (Vtx){ 0, 0, 1, 0, 0, 0xffffffff };
        v[1] = (Vtx){ 512, 0, 1, 1, 0, 0xffffffff };
        v[2] = (Vtx){ 512, 480, 1, 1, vv, 0xffffffff };
        v[3] = v[0]; v[4] = v[2];
        v[5] = (Vtx){ 0, 480, 1, 0, vv, 0xffffffff };
        gfx_tris(v, 6, sBg0, LIST_OPAQUE);
    }
    if (sBg1 >= 0) {
        v[0] = (Vtx){ 512, 0, 1, 0, 0, 0xffffffff };
        v[1] = (Vtx){ 640, 0, 1, 1, 0, 0xffffffff };
        v[2] = (Vtx){ 640, 480, 1, 1, vv, 0xffffffff };
        v[3] = v[0]; v[4] = v[2];
        v[5] = (Vtx){ 512, 480, 1, 0, vv, 0xffffffff };
        gfx_tris(v, 6, sBg1, LIST_OPAQUE);
    }
}

/* a rectangle of the sprite texture (page from x's 0x8000) at (x0,y0)-(x1,y1); later = on top */
static void quad(const u16 *r, f32 x0, f32 y0, f32 x1, f32 y1, u32 argb, int flipX) {
    int pg = (r[0] & 0x8000) ? 1 : 0;
    f32 tw = pg ? sTex2W : sTexW, th = pg ? sTex2H : sTexH;
    f32 itw = shz_invf(tw), ith = shz_invf(th);
    f32 u0 = (r[0] & 0x7fff) * itw, v0 = r[1] * ith, u1 = ((r[0] & 0x7fff) + r[2]) * itw, v1 = (r[1] + r[3]) * ith;
    Vtx *v;
    if (sQN[pg] >= MAXQ || r[2] == 0) return;
    if (flipX) { f32 t = u0; u0 = u1; u1 = t; }
    v = &sQ[pg][sQN[pg]++ * 6];
    sZ += 0.01f;
    v[0] = (Vtx){ x0, y0, sZ, u0, v0, argb };
    v[1] = (Vtx){ x1, y0, sZ, u1, v0, argb };
    v[2] = (Vtx){ x1, y1, sZ, u1, v1, argb };
    v[3] = v[0]; v[4] = v[2];
    v[5] = (Vtx){ x0, y1, sZ, u0, v1, argb };
}

int fe_sprite_w(int id) { return id >= 0 && id < sSprN ? sSpr[id][2] : 0; }
int fe_sprite_h(int id) { return id >= 0 && id < sSprN ? sSpr[id][3] : 0; }

/* sprite id centred on (x, y), scaled */
void fe_sprite(int id, f32 x, f32 y, f32 scale, u32 argb) {
    f32 w, h;
    if (id < 0 || id >= sSprN) return;
    w = sSpr[id][2] * scale * 0.5f;
    h = sSpr[id][3] * scale * 0.5f;
    quad(sSpr[id], x - w, y - h, x + w, y + h, argb, 0);
}

/* sprite id stretched across a rectangle (the spinning panels: x1 < x0 shows it mirrored) */
void fe_sprite_rect(int id, f32 x0, f32 y0, f32 x1, f32 y1, u32 argb) {
    if (id < 0 || id >= sSprN) return;
    if (x1 < x0) { f32 t = x0; x0 = x1; x1 = t; quad(sSpr[id], x0, y0, x1, y1, argb, 1); return; }
    quad(sSpr[id], x0, y0, x1, y1, argb, 0);
}

/* sprite id on any four corners (top left, top right, bottom right, bottom left): the
 * spinning cards */
void fe_sprite_pts(int id, const f32 *x, const f32 *y, u32 argb) {
    fe_sprite_pts_part(id, x, y, 0, 0, 1, 1, argb);
}

/* part of a sprite (s0..s1 across, t0..t1 down, as fractions) on four points */
void fe_sprite_pts_part(int id, const f32 *x, const f32 *y, f32 s0, f32 t0, f32 s1, f32 t1, u32 argb) {
    const u16 *r;
    int pg;
    f32 tw, th, u0, v0, u1, v1;
    Vtx *v;
    if (id < 0 || id >= sSprN || sSpr[id][2] == 0) return;
    r = sSpr[id];
    pg = (r[0] & 0x8000) ? 1 : 0;
    tw = pg ? sTex2W : sTexW;
    th = pg ? sTex2H : sTexH;
    {
        f32 itw = shz_invf(tw), ith = shz_invf(th);
        f32 x0 = (f32)(r[0] & 0x7fff), y0 = (f32)r[1];
        u0 = (x0 + r[2] * s0) * itw; v0 = (y0 + r[3] * t0) * ith;
        u1 = (x0 + r[2] * s1) * itw; v1 = (y0 + r[3] * t1) * ith;
    }
    if (sQN[pg] >= MAXQ) return;
    v = &sQ[pg][sQN[pg]++ * 6];
    sZ += 0.01f;
    v[0] = (Vtx){ x[0], y[0], sZ, u0, v0, argb };
    v[1] = (Vtx){ x[1], y[1], sZ, u1, v0, argb };
    v[2] = (Vtx){ x[2], y[2], sZ, u1, v1, argb };
    v[3] = v[0]; v[4] = v[2];
    v[5] = (Vtx){ x[3], y[3], sZ, u0, v1, argb };
}

/* the textures are gone (a race came in between): load the set again next time */
void fe_forget(void) { sSet[0] = 0; sTex = sTex2 = sBg0 = sBg1 = -1; }

/* the turning icon ring's path (menu_circle_setup) */
int fe_ring_n(void) { return sRingN; }
void fe_ring(int i, f32 *x, f32 *y, f32 *s) {
    if (sRingN == 0) { *x = 320; *y = 240; *s = 1; return; }
    i = ((i % sRingN) + sRingN) % sRingN;
    *x = sRing[i].x;
    *y = sRing[i].y;
    *s = sRing[i].s * 0.001f;
}

static const FeGlyph *glyph(int font, char c) {
    unsigned u = (unsigned char)c;
    if (font < 0 || font > 5 || u < 32 || u > 127) return NULL;
    return &sGlyph[font][u - 32];
}

int fe_width(int font, const char *s) {
    int w = 0;
    for (; *s; s++) {
        const FeGlyph *g = glyph(font, *s);
        if (g) w += g->adv;
    }
    return w;
}

/* text: align 0 left at x, 1 centred on x, 2 right-aligned at x; y = the text's centre */
int fe_text_scaled(int font, const char *s, f32 x, f32 y, int align, u32 argb, f32 sc) {
    int w = fe_width(font, s);
    if (align == 1) x -= w * sc * 0.5f;
    else if (align == 2) x -= w * sc;
    for (; *s; s++) {
        const FeGlyph *g = glyph(font, *s);
        if (g == NULL) continue;
        if (g->w) {
            u16 r[4] = { g->x, g->y, g->w, g->h };
            quad(r, x, y - g->h * sc * 0.5f, x + g->w * sc, y + g->h * sc * 0.5f, argb, 0);
        }
        x += g->adv * sc;
    }
    return w;
}

int fe_text(int font, const char *s, f32 x, f32 y, int align) {
    return fe_text_scaled(font, s, x, y, align, 0xffffffff, 1.0f);
}

int fe_text_c(int font, const char *s, f32 x, f32 y, int align, u32 argb) {
    return fe_text_scaled(font, s, x, y, align, argb, 1.0f);
}

/* a full-screen fade: black at alpha a (0..255) over everything */
static int sFade;
void fe_fade(int a) { sFade = a; }

void fe_end(void) {
    if (sQN[0]) gfx_tris(sQ[0], sQN[0] * 6, sTex, LIST_CUTOUT);
    if (sQN[1]) gfx_tris(sQ[1], sQN[1] * 6, sTex2, LIST_CUTOUT);
    if (sFade > 0) {
        Vtx v[6];
        u32 c = (u32)(sFade > 255 ? 255 : sFade) << 24;
        v[0] = (Vtx){ 0, 0, 1000, 0, 0, c };
        v[1] = (Vtx){ 640, 0, 1000, 0, 0, c };
        v[2] = (Vtx){ 640, 480, 1000, 0, 0, c };
        v[3] = v[0]; v[4] = v[2];
        v[5] = (Vtx){ 0, 480, 1000, 0, 0, c };
        gfx_tris(v, 6, -1, LIST_BLEND);
    }
    sFade = 0;
    gfx_end();
}

/* the menus' sounds (sfx_load_menu_sounds 0x48310): cursor, select / back, quit */
static int sSnd[3] = { -2, -2, -2 };
void fe_sound(int which) {
    static const char *const kName[3] = { "SELSND1", "DESELECT", "QUIT" };
    if (which < 0 || which > 2) return;
    if (sSnd[which] == -2) sSnd[which] = aud_load(kName[which]);
    if (sSnd[which] >= 0) aud_play(10 + (which & 1), sSnd[which], 220, 128, 0);
}
