/*
 * track.c - a Manic Karts track: loading (data/ made by tools/build_assets.py)
 * and drawing.
 *
 * The original stores a track as 9 maps of 128x128 cells (see decomp/README).
 * Here: map 0 picks each cell's floor tile (32x32, from the floor texture),
 * map 1 marks wall cells and picks their wall tile (64x64, from the wall
 * texture; the map counts them from 1, 0 being no wall; colour 0 see-through). Wall cells are drawn as blocks: wall tiles on the sides that face
 * open ground, the cell's floor tile on top (that's how the original stores
 * roofs). Map 8 is the lighting, turned into a brightness per cell at build time.
 */
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Track gTrack;
int gTrackCount;

static TrackInfo *sInfo;
static int sTexFloor = -1, sTexWalls = -1, sTexFar = -1, sTexObj = -1;
static int sTexSky[5] = { -1, -1, -1, -1, -1 };
static int sTexKarts[4] = { -1, -1, -1, -1 };
static int sTexHud = -1;
static char sTheme[4];
static int sKartSet = -1;                /* the karts loaded: 0 the 'E' set, 1 the 'H' set */

void track_forget(void) {
    int i;
    sTexFloor = sTexWalls = sTexFar = sTexObj = sTexHud = -1;
    for (i = 0; i < 5; i++) sTexSky[i] = -1;
    for (i = 0; i < 4; i++) sTexKarts[i] = -1;
    sTheme[0] = 0;
    sKartSet = -1;
}
static u8 sCornerLight[(MAP_N + 1) * (MAP_N + 1)];   /* brightness at each cell corner */

/* ---------------------------------------------------------------- animated tiles
 * (track_animate_map_tiles 0x17320): some wall and floor tiles cycle through other
 * tiles, each shown for so many hundredths of a second (signs flashing, water moving).
 * The original changes the cells in the maps; here what's drawn for each map value is
 * looked up instead (the same thing: every cell of a value changes together, and the
 * maps the karts drive on stay as they are). */
#define ANIM_MAX 48
static struct { u8 wall, value, n, at; const u8 *frames; s32 since; } sAnim[ANIM_MAX];
static int sAnimN;
static u8 sShowWall[256], sShowFloor[256];
static void *sAnimFile;
static s32 sAnimCs;

static void anims_load(const char *trk) {
    char fn[16];
    u32 size;
    const u8 *p, *end;
    int i, k;
    for (i = 0; i < 256; i++) sShowWall[i] = sShowFloor[i] = (u8)i;
    sAnimN = 0;
    sAnimCs = 0;
    if (sAnimFile) free(sAnimFile);
    snprintf(fn, sizeof fn, "%s.anm", trk);
    sAnimFile = plat_load_file(fn, &size);
    p = (const u8 *)sAnimFile;
    if (p == NULL || size < 6 || memcmp(p, "MKAN", 4) != 0) return;
    end = p + size;
    {
        int nw = p[4], nf = p[5];
        p += 6;
        for (k = 0; k < nw + nf && p + 2 <= end && sAnimN < ANIM_MAX; k++) {
            int n = p[1];
            if (p + 2 + n * 2 > end) break;
            sAnim[sAnimN].wall = k < nw;
            sAnim[sAnimN].value = p[0];
            sAnim[sAnimN].n = (u8)n;
            sAnim[sAnimN].at = 0;
            sAnim[sAnimN].frames = p + 2;
            sAnim[sAnimN].since = 0;
            if (n) sAnimN++;
            p += 2 + n * 2;
        }
    }
}

/* every 1/60 s: a tile moves on to its next frame once it's been shown long enough */
void track_animate(void) {
    int i;
    sAnimCs = (s32)((race_clock() + 1) * 100 / 60);
    for (i = 0; i < sAnimN; i++) {
        const u8 *f = sAnim[i].frames + sAnim[i].at * 2;
        if (sAnimCs - sAnim[i].since > f[1]) {
            (sAnim[i].wall ? sShowWall : sShowFloor)[sAnim[i].value] = f[0];
            sAnim[i].since = sAnimCs;
            if (++sAnim[i].at >= sAnim[i].n) sAnim[i].at = 0;
        }
    }
}

void track_init(void) {
    u32 size;
    sInfo = (TrackInfo *)plat_load_file("tracks.bin", &size);
    gTrackCount = sInfo ? (int)(size / sizeof(TrackInfo)) : 0;
}

static int load_tex(const char *prefix, const char *kind, int id) {
    char name[32];
    strcpy(name, prefix);
    strcat(name, kind);
    return tex_file(name, id, NULL, NULL);
}

void track_load(int n) {
    char name[16];
    u32 size;
    const u8 *d;
    int x, z;
    if (gTrackCount == 0) return;
    n = (n % gTrackCount + gTrackCount) % gTrackCount;
    gTrack.index = n;
    gTrack.info = sInfo[n];
    name[0] = 't'; name[1] = 'r'; name[2] = 'k';
    name[3] = (char)('0' + n / 10); name[4] = (char)('0' + n % 10);
    strcpy(name + 5, ".map");
    d = (const u8 *)plat_load_file(name, &size);
    if (d == NULL || size < 10 * 16384 + 256) return;
    if (gTrack.maps) free((void *)gTrack.maps);
    gTrack.maps = d;
    gTrack.light = d + 9 * 16384;
    gTrack.surface = d + 10 * 16384;
    if (strncmp(sTheme, gTrack.info.theme, 4) != 0) {
        sTexFloor = load_tex(gTrack.info.theme, "_floor.tex", sTexFloor);
        sTexWalls = load_tex(gTrack.info.theme, "_walls.tex", sTexWalls);

        sTexObj = load_tex(gTrack.info.theme, "_obj.tex", sTexObj);

        sTexHud = load_tex(gTrack.info.theme, "_hud.tex", sTexHud);
        {
            static const char *sky[5] = { "_sky0.tex", "_sky1.tex", "_sky2.tex", "_sky3.tex", "_sky4.tex" };
            char fn[16];
            u16 pal[256];
            const u8 *p;
            int i;
            for (i = 0; i < 5; i++) sTexSky[i] = load_tex(gTrack.info.theme, sky[i], sTexSky[i]);
            /* the theme's colours: bank 1 as they are, bank 0 for sprites with colour 0 see-through */
            strcpy(fn, gTrack.info.theme);
            strcat(fn, ".pal");
            p = (const u8 *)plat_load_file(fn, &size);
            if (p && size >= 516) {
                memcpy(pal, p, 512);
                gfx_palette(1, pal);
                pal[0] = 0;
                gfx_palette(0, pal);
                for (i = 0; i < 3; i++) gTrack.fog[i] = p[512 + i] * (1.0f / 255);
                free((void *)p);
            }
            strcpy(fn, gTrack.info.theme);
            strcat(fn, "_hud.bin");
            if (gTrack.hudRect) free((void *)gTrack.hudRect);
            p = (const u8 *)plat_load_file(fn, &size);
            gTrack.hudRect = p && size >= 302 * 8 ? (const u16 *)p : NULL;
            gTrack.hudNames = p && size >= 302 * 8 + 16 * 16 ? (const char *)p + 302 * 8 : NULL;
            strcpy(fn, gTrack.info.theme);
            strcat(fn, "_obj.bin");
            if (gTrack.objRect) free((void *)(gTrack.objRect - 1));
            p = (const u8 *)plat_load_file(fn, &size);
            gTrack.objRect = p ? (const u16 *)p + 1 : NULL;
            gTrack.objRects = p ? ((const u16 *)p)[0] : 0;
        }
        memcpy(sTheme, gTrack.info.theme, 4);
        sKartSet = -1;
    }
    {   /* the karts: the 'E' set for 50 / 100cc, 'H' for 150 / 250cc (load_kart_sprites),
           every one at full size (as the original, given the memory), compressed (VQ:
           tools/build_vq.py) to fit the video memory: two karts to a texture */
        int h = gGame.cls >= 2, i;
        if (h != sKartSet) {
            for (i = 0; i < 4; i++) {
                char kind[16];
                int was = sTexKarts[i];
                snprintf(kind, sizeof kind, "_kq%c%d.tex", h ? 'H' : 'E', i);
                sTexKarts[i] = load_tex(gTrack.info.theme, kind, sTexKarts[i]);
                if (h && sTexKarts[i] == was) {  /* (no 'H' set in this copy of the game: the 'E' one) */
                    kind[3] = 'E';
                    sTexKarts[i] = load_tex(gTrack.info.theme, kind, sTexKarts[i]);
                }
            }
            sKartSet = h;
        }
    }
    gTrack.texObj = sTexObj;
    memcpy(gTrack.texKarts, sTexKarts, sizeof sTexKarts);
    gTrack.texHud = sTexHud;
    memcpy(gTrack.texSky, sTexSky, sizeof sTexSky);
    name[5] = 0;                         /* "trkNN" */
    sTexFar = load_tex(name, "_far.tex", sTexFar);
    /* corner brightness: the average of the (up to 4) cells that meet there */
    for (z = 0; z <= MAP_N; z++) {
        for (x = 0; x <= MAP_N; x++) {
            int sum = 0, cnt = 0, dx, dz;
            for (dz = -1; dz <= 0; dz++) {
                for (dx = -1; dx <= 0; dx++) {
                    int cx = x + dx, cz = z + dz;
                    if (cx >= 0 && cz >= 0 && cx < MAP_N && cz < MAP_N) {
                        sum += gTrack.light[cz * MAP_N + cx];
                        cnt++;
                    }
                }
            }
            sCornerLight[z * (MAP_N + 1) + x] = (u8)(cnt ? sum / cnt : 255);
        }
    }
    anims_load(name);
    objects_load(n);
    ai_load();
}

/* World layout: map column c covers x c..c+1; map row r covers z
 * (MAP_N-r-1)..(MAP_N-r), so looking down from above with +z up the screen
 * shows the map the right way round (the original's top-down layout). */
int track_is_wall(int c, int r) {
    if (c < 0 || r < 0 || c >= MAP_N || r >= MAP_N) return 1;
    return MAP_WALLS(&gTrack)[r * MAP_N + c] != 0;
}

/* under a roof (the original's race_render_frame: no sky when the camera's cell has a
   surface code of 0x36 or more: the tunnels of London 2, Detroit and Corumba) */
int track_indoors(f32 x, f32 z) {
    int c = (int)shz_floorf(x), r = MAP_N - 1 - (int)shz_floorf(z);
    if (gTrack.maps == NULL || c < 0 || r < 0 || c >= MAP_N || r >= MAP_N) return 0;
    return MAP_SURFACE(&gTrack)[r * MAP_N + c] >= 0x36;
}

int track_wall_at(f32 x, f32 z) {
    return track_is_wall((int)shz_floorf(x), MAP_N - 1 - (int)shz_floorf(z));
}

static inline u32 grey(int b) {
    return 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
}

/* brightness at the corner where column c and row r begin */
static inline u32 corner(int c, int r) {
    return grey(sCornerLight[r * (MAP_N + 1) + c]);
}

/* texture coordinates of a tile: (n % cols, n / cols), size 1/cols, half a texel in */
static inline void tile_uv(int n, int cols, int texSize, f32 *u0, f32 *v0, f32 *u1, f32 *v1) {
    f32 s = 1.0f / cols, e = 0.5f / texSize;
    *u0 = (n % cols) * s + e;
    *v0 = (n / cols) * s + e;
    *u1 = *u0 + s - 2 * e;
    *v1 = *v0 + s - 2 * e;
}

/* the floor (or roof, at height y) of cell (c, r): the tile's top edge at the
 * row's start, as in the original's top-down view */
static void floor_cell(int c, int r, f32 y, int tile) {
    f32 u0, v0, u1, v1, x0 = (f32)c, x1 = (f32)c + 1, zt = (f32)(MAP_N - r), zb = zt - 1;
    WV a, b, cc, d;
    if (tile >= 240) tile = 239;         /* (the game does the same: 0xf0 and up -> 0xef) */
    tile_uv(tile, 16, 512, &u0, &v0, &u1, &v1);
    a = wv(v3(x0, y, zt), u0, v0, corner(c, r));
    b = wv(v3(x1, y, zt), u1, v0, corner(c + 1, r));
    cc = wv(v3(x1, y, zb), u1, v1, corner(c + 1, r + 1));
    d = wv(v3(x0, y, zb), u0, v1, corner(c, r + 1));
    r_quad(&a, &b, &cc, &d, sTexFloor, LIST_OPAQUE, 0);
}

/* one side of a wall block, from (x0,z0) to (x1,z1) as seen from outside (left to right) */
static void wall_side(f32 x0, f32 z0, f32 x1, f32 z1, int tile, u32 col) {
    f32 u0, v0, u1, v1;
    WV a, b, c, d;
    tile_uv(tile, 8, 512, &u0, &v0, &u1, &v1);
    a = wv(v3(x0, WALL_H, z0), u0, v0, col);
    b = wv(v3(x1, WALL_H, z1), u1, v0, col);
    c = wv(v3(x1, 0, z1), u1, v1, col);
    d = wv(v3(x0, 0, z0), u0, v1, col);
    r_quad(&a, &b, &c, &d, sTexWalls, LIST_CUTOUT, 0);   /* (some have see-through parts) */
}

static void wall_cell(int c, int r, int tile, int roof) {
    const u8 *fl = MAP_FLOOR(&gTrack);
    int lt = gTrack.light[r * MAP_N + c];
    u32 c1 = grey(lt * 230 >> 8), c2 = grey(lt * 180 >> 8);   /* two side shades, for depth */
    f32 x0 = (f32)c, x1 = (f32)c + 1, zt = (f32)(MAP_N - r), zb = zt - 1;
    tile = sShowWall[tile & 255] - 1;    /* (the map counts from 1) */
    if (tile < 0) return;
    if (tile >= 60) tile = 59;
    if (roof) floor_cell(c, r, WALL_H, sShowFloor[fl[r * MAP_N + c]]);     /* the roof */
    /* only the sides facing open ground, and facing the camera */
    if (!track_is_wall(c, r - 1) && gCam.pos.z > zt) wall_side(x1, zt, x0, zt, tile, c1);   /* facing +z */
    if (!track_is_wall(c, r + 1) && gCam.pos.z < zb) wall_side(x0, zb, x1, zb, tile, c1);   /* facing -z */
    if (!track_is_wall(c - 1, r) && gCam.pos.x < x0) wall_side(x0, zt, x0, zb, tile, c2);   /* facing -x */
    if (!track_is_wall(c + 1, r) && gCam.pos.x > x1) wall_side(x1, zb, x1, zt, tile, c2);   /* facing +x */
}

/* a whole 8x8-cell block of far-away floor: one quad, from the baked floor texture
 * (the whole track at 8 pixels a cell, lighting included) */
#define BLOCK 8
static void far_block(int bc, int br) {
    f32 x0 = (f32)(bc * BLOCK), x1 = x0 + BLOCK;
    f32 zt = (f32)(MAP_N - br * BLOCK), zb = zt - BLOCK;
    f32 u0 = bc * (1.0f / 16), u1 = u0 + 1.0f / 16, v0 = br * (1.0f / 16), v1 = v0 + 1.0f / 16;
    WV a = wv(v3(x0, 0, zt), u0, v0, 0xFFFFFFFF), b = wv(v3(x1, 0, zt), u1, v0, 0xFFFFFFFF);
    WV c = wv(v3(x1, 0, zb), u1, v1, 0xFFFFFFFF), d = wv(v3(x0, 0, zb), u0, v1, 0xFFFFFFFF);
    r_quad(&a, &b, &c, &d, sTexFar, LIST_OPAQUE, 0);
}

/* nearDist: floor drawn tile by tile within this (blocks further are baked quads);
 * wallDist: walls drawn within this */
void track_draw(f32 nearDist, f32 wallDist) {
    const u8 *fl, *wl;
    int bc, br, c, r;
    f32 fx = gCam.fwd.x, fz = gCam.fwd.z;
    f32 flat = shz_sqrtf(fx * fx + fz * fz);
    int roofs = gCam.pos.y > WALL_H * 3; /* (only from high up: the original never shows wall tops, and a
                                            kart lifted by the stuck rescue would see them as white sheets) */
    if (gTrack.maps == NULL) return;
    fl = MAP_FLOOR(&gTrack);
    wl = MAP_WALLS(&gTrack);
    r_world();
    if (flat > 0.3f) {
        fx = shz_divf(fx, flat);
        fz = shz_divf(fz, flat);
    }
    for (br = 0; br < MAP_N / BLOCK; br++) {
        for (bc = 0; bc < MAP_N / BLOCK; bc++) {
            /* the block's centre, and its nearest point to the camera */
            f32 cx = bc * BLOCK + BLOCK * 0.5f, cz = MAP_N - (br * BLOCK + BLOCK * 0.5f);
            f32 nx = clampf(gCam.pos.x, cx - BLOCK * 0.5f, cx + BLOCK * 0.5f) - gCam.pos.x;
            f32 nz = clampf(gCam.pos.z, cz - BLOCK * 0.5f, cz + BLOCK * 0.5f) - gCam.pos.z;
            f32 near2 = nx * nx + nz * nz;
            int near = near2 < nearDist * nearDist, walls = near2 < wallDist * wallDist;
            /* behind the camera (when it looks along the ground) */
            if (flat > 0.3f && (cx - gCam.pos.x) * fx + (cz - gCam.pos.z) * fz < -BLOCK * 0.75f) continue;
            if (!near) far_block(bc, br);
            if (!near && !walls) continue;
            for (r = br * BLOCK; r < br * BLOCK + BLOCK; r++) {
                for (c = bc * BLOCK; c < bc * BLOCK + BLOCK; c++) {
                    int w = wl[r * MAP_N + c];
                    if (w) {
                        if (walls) wall_cell(c, r, w, roofs);
                    } else if (near) {
                        floor_cell(c, r, 0, sShowFloor[fl[r * MAP_N + c]]);
                    }
                }
            }
        }
    }
    r_flush();
}

/* The sky: the original's panorama (2560 pixels round, 250 tall), behind everything.
 * The middle of the screen shows column SKY_AT0 + heading * 2560 / 3600 (heading 0 =
 * along +x; placed as in the original's London 1 start, Big Ben ahead and to the right),
 * the panorama's bottom row on the horizon, pixels kept square. Drawn as narrow strips,
 * each the right angle round. Fog doesn't reach it. */
#define SKY_W 2560
#define SKY_H 250
#define SKY_AT0 589
#define STRIPS 16
/* depth (1/distance) of the sky: behind everything on the track (1000 cells away) but in
 * front of the PowerVR's background plane, which the depth test would otherwise keep */
#define SKY_Z 0.001f
/* (kept half a texel in from each piece's edge, so the filtering doesn't wrap round) */
static inline f32 sky_u(f32 u) { return u < 0.5f / 512 ? 0.5f / 512 : u > 511.5f / 512 ? 511.5f / 512 : u; }
void sky_draw(f32 bright) {
    u32 col;
    f32 yaw, half = shz_divf(SCREEN_W / 2, gCam.focal);           /* tan of half the view */
    f32 colPerRad = SKY_W / (2 * PI_F), pxPerCol, top, bottom = gCam.centreY;
    int i;
    {
        int g = (int)(bright * 255);
        if (g <= 0) return;
        if (g > 255) g = 255;
        col = 0xFF000000u | (u32)g << 16 | (u32)g << 8 | (u32)g;
    }
    if (gTrack.texSky[0] < 0 || gCam.fwd.y < -0.3f || gCam.fwd.y > 0.3f) return;
    yaw = shz_atan2f(-gCam.fwd.z, gCam.fwd.x);                     /* the original's heading */
    pxPerCol = gCam.focal / colPerRad;                             /* (at the middle of the screen) */
    top = bottom - SKY_H * pxPerCol;
    for (i = 0; i < STRIPS; i++) {
        f32 x0 = (f32)SCREEN_W * i / STRIPS, x1 = (f32)SCREEN_W * (i + 1) / STRIPS;
        f32 a0 = shz_atan2f((x0 - SCREEN_W / 2) * (half / (SCREEN_W / 2)), 1.0f);
        f32 a1 = shz_atan2f((x1 - SCREEN_W / 2) * (half / (SCREEN_W / 2)), 1.0f);
        f32 c0 = SKY_AT0 + (yaw + a0) * colPerRad, c1 = SKY_AT0 + (yaw + a1) * colPerRad;
        int piece;
        f32 base;
        Vtx v[6];
        while (c0 < 0) { c0 += SKY_W; c1 += SKY_W; }
        while (c0 >= SKY_W) { c0 -= SKY_W; c1 -= SKY_W; }
        piece = (int)(c0 / 512);
        base = piece * 512.0f;
        /* (a strip crossing into the next piece: split it there) */
        if (c1 > base + 512) {
            f32 t = (base + 512 - c0) / (c1 - c0), xm = x0 + (x1 - x0) * t;
            int next = (piece + 1) % 5;
            Vtx w[6] = {
                { xm, top, SKY_Z, sky_u(0), 0, col }, { x1, top, SKY_Z, sky_u((c1 - base - 512) / 512), 0, col },
                { x1, bottom, SKY_Z, sky_u((c1 - base - 512) / 512), SKY_H / 256.0f, col },
                { xm, top, SKY_Z, sky_u(0), 0, col }, { x1, bottom, SKY_Z, sky_u((c1 - base - 512) / 512), SKY_H / 256.0f, col },
                { xm, bottom, SKY_Z, sky_u(0), SKY_H / 256.0f, col } };
            gfx_tris(w, 6, gTrack.texSky[next], LIST_OPAQUE);
            x1 = xm;
            c1 = base + 512;
        }
        {
            f32 u0 = sky_u((c0 - base) / 512), u1 = sky_u((c1 - base) / 512), vb = SKY_H / 256.0f;
            Vtx q[6] = {
                { x0, top, SKY_Z, u0, 0, col }, { x1, top, SKY_Z, u1, 0, col },
                { x1, bottom, SKY_Z, u1, vb, col },
                { x0, top, SKY_Z, u0, 0, col }, { x1, bottom, SKY_Z, u1, vb, col },
                { x0, bottom, SKY_Z, u0, vb, col } };
            memcpy(v, q, sizeof q);
        }
        gfx_tris(v, 6, gTrack.texSky[piece], LIST_OPAQUE);
    }
}
