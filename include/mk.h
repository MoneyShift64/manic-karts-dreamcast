#ifndef MK_H
#define MK_H
/*
 * Manic Karts for the Dreamcast: shared types, the platform interface, maths.
 * The game code in src/ touches no hardware itself: everything that does is behind
 * the platform interface, implemented in platform/dc (KallistiOS + PowerVR).
 *
 * All maths goes through SH4ZAM (github.com/gyrovorbis/sh4zam).
 */
#include <stdint.h>
#include <stddef.h>
#include <sh4zam/shz_sh4zam.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;
typedef float f32;

#define SCREEN_W 640
#define SCREEN_H 480

/* ---------------------------------------------------------------- input */
enum {
    BTN_A = 1, BTN_B = 2, BTN_START = 4, BTN_LEFT = 8, BTN_RIGHT = 16,
    BTN_UP = 32, BTN_DOWN = 64, BTN_Y = 128, BTN_X = 256,
};
/* the Dreamcast's soft reset: all five held together, back to the title */
#define BTN_RESET (BTN_A | BTN_B | BTN_X | BTN_Y | BTN_START)
typedef struct Input {
    u32 buttons;
    f32 stickx, sticky;                  /* -1 .. 1 */
    f32 rtrig, ltrig;                    /* 0 .. 1 */
} Input;

/* ---------------------------------------------------------------- drawing */
/* a vertex ready for the hardware: screen position, depth = 1/w (bigger is
 * nearer), texture coordinates, colour (0xAARRGGBB, multiplied with the texture) */
typedef struct Vtx {
    f32 x, y, z;
    f32 u, v;
    u32 argb;
} Vtx;

enum { LIST_OPAQUE = 0, LIST_CUTOUT = 1, LIST_BLEND = 2 };
enum { TEX_ARGB1555 = 0, TEX_ARGB4444 = 1, TEX_RGB565 = 2, TEX_PAL8 = 3,
       TEX_VQ1555 = 4 };                 /* (VQ: 2 KB codebook of 256 2x2 ARGB1555 blocks, then a byte a block, twiddled) */
/* bytes of video memory a texture of the format takes */
#define TEX_BYTES(w, h, f) (TEX_KIND(f) == TEX_PAL8 ? (u32)(w) * (h) : TEX_KIND(f) == TEX_VQ1555 ? 2048u + (u32)(w) * (h) / 4 : (u32)(w) * (h) * 2)
#define TEX_PAL_BANK(b) ((b) << 8)        /* TEX_PAL8: which of the 4 palettes (gfx_palette) it uses */
#define TEX_NOFOG 0x4000                  /* format flag: a backdrop (the sky): never fogged, no depth
                                            test, drawn first and everything else drawn over it */
#define TEX_POINT 0x2000                  /* format flag: no smoothing (pixel art: the on-screen display) */
#define TEX_TWIDDLED 0x8000              /* format flag: texels already in the PowerVR's twiddled order */
#define TEX_KIND(f) ((f) & 0xff)

/* ---------------------------------------------------------------- platform */
void plat_init(void);
void plat_input(Input *in);
u32 plat_time_us(void);
int plat_running(void);                  /* 0: the PC build's frames are done */
void *plat_load_file(const char *name, u32 *size);   /* from data/ (/cd/data on the Dreamcast) */
int plat_key(void);                      /* a key typed on a keyboard: its character ('\n' Enter, 8 Backspace, 27 Esc), 0: none */
int plat_save_present(void);             /* a VMU is plugged in */
int plat_save_write(const char *name, const char *desc, const void *data, int size);   /* 1: written (or being written) */
int plat_save_read(const char *name, void *buf, int size);   /* 1: read */
int  plat_save_error(void);             /* the last plat_save_read's trouble, 0 if none (for the screen) */

int gfx_texture(const void *texels, int w, int h, int fmt);  /* -> texture id (TEX_PAL8: 1 byte a texel, twiddled) */
void gfx_texture_update(int id, const void *texels);         /* new texels, same size and format */
void gfx_palette(int bank, const u16 *argb1555);             /* one of the 4 palettes of 256 colours */
u32 gfx_vram_free(void);                                     /* video memory left, bytes (0: unknown) */
void gfx_free_all(void);                                     /* every texture freed; ids start from 0 again */
void gfx_no_fog(void);                                       /* (the menus) */
void gfx_begin(void);
void gfx_tris(const Vtx *v, int n, int tex, int list);       /* n/3 triangles */
void gfx_end(void);
void gfx_set_bg(f32 r, f32 g, f32 b);
void gfx_set_fog(f32 start, f32 end, f32 r, f32 g, f32 b);   /* opaque polygons fade to this colour with distance */

/* ---------------------------------------------------------------- maths */
typedef shz_vec3_t V3;
static inline V3 v3(f32 x, f32 y, f32 z) { return shz_vec3_init(x, y, z); }
static inline V3 v3add(V3 a, V3 b) { return shz_vec3_add(a, b); }
static inline V3 v3sub(V3 a, V3 b) { return shz_vec3_sub(a, b); }
static inline V3 v3mul(V3 a, f32 s) { return shz_vec3_scale(a, s); }
static inline f32 v3dot(V3 a, V3 b) { return shz_vec3_dot(a, b); }
static inline V3 v3cross(V3 a, V3 b) { return shz_vec3_cross(a, b); }
static inline V3 v3norm(V3 a) { return shz_vec3_normalize_safe(a); }
static inline f32 clampf(f32 x, f32 lo, f32 hi) { return shz_clampf(x, lo, hi); }
#define PI_F 3.14159265f
/* ---------------------------------------------------------------- sound
 * On the Dreamcast all of this runs on the AICA sound chip: sounds are loaded into
 * its own memory and played on its voices (pitch, volume and pan changed live);
 * music is ADPCM the chip decodes itself, streamed from the disc. */
#define AUD_VOICES 12
int aud_load(const char *name);                              /* data/NAME.pcm -> sound (-1: none) */
void aud_play(int voice, int snd, int vol, int pan, int freq);   /* vol 0..255, pan 0..255 (128 middle), Hz (0: the sound's own) */
void aud_set(int voice, int vol, int pan, int freq);         /* change a playing voice */
void aud_stop(int voice);
int aud_rate(int snd);                                       /* the sound's own playback rate */
void music_play(const char *name);                           /* data/NAME (ADPCM, mono 44100 Hz), looping */
void music_stop(void);
void music_volume(int vol);                                  /* 0..255 */

#endif
