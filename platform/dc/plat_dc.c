/*
 * plat_dc.c - Manic Karts on the Dreamcast: KallistiOS, the PowerVR, the
 * controller, files from the disc, the VMU and sound.
 * (From the Checkpoint! engine.)
 *
 * Drawing: polygons are kept per list during the frame (opaque, punch-through
 * for cut-outs, translucent) and sent to the PowerVR in that order at the end
 * of the frame. Depth = 1/w, compared GEQUAL. Textures are twiddled when
 * loaded and filtered bilinearly.
 */
#include <kos.h>
#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/keyboard.h>
#include <dc/vmufs.h>
#include <dc/vmu_pkg.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/stream.h>
#include <dc/sound/aica_comm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "mk.h"

void game_main(void);


void plat_init(void) {
}

int plat_running(void) {
    return 1;
}

u32 plat_time_us(void) {
    return (u32)timer_us_gettime64();
}

void *plat_load_file(const char *name, u32 *size) {
    char path[128];
    FILE *f;
    long n;
    void *d;
    snprintf(path, sizeof path, "/cd/data/%s", name);
    f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = memalign(32, n);
    if (d == NULL || fread(d, 1, n, f) != (size_t)n) n = 0;
    fclose(f);
    *size = (u32)n;
    return d;
}

/* ---------------------------------------------------------------- input */
void plat_input(Input *in) {
    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *st;
    memset(in, 0, sizeof *in);
    if (dev == NULL || (st = (cont_state_t *)maple_dev_status(dev)) == NULL) {
        return;
    }
    if (st->buttons & CONT_A) in->buttons |= BTN_A;
    if (st->buttons & CONT_B) in->buttons |= BTN_B;
    if (st->buttons & CONT_X) in->buttons |= BTN_X;
    if (st->buttons & CONT_Y) in->buttons |= BTN_Y;
    if (st->buttons & CONT_START) in->buttons |= BTN_START;
    if (st->buttons & CONT_DPAD_LEFT) in->buttons |= BTN_LEFT;
    if (st->buttons & CONT_DPAD_RIGHT) in->buttons |= BTN_RIGHT;
    if (st->buttons & CONT_DPAD_UP) in->buttons |= BTN_UP;
    if (st->buttons & CONT_DPAD_DOWN) in->buttons |= BTN_DOWN;
    in->stickx = st->joyx / 128.0f;
    in->sticky = st->joyy / 128.0f;
    if (in->stickx > -0.08f && in->stickx < 0.08f) in->stickx = 0;      /* dead zone */
    in->rtrig = st->rtrig / 255.0f;
    in->ltrig = st->ltrig / 255.0f;
}

/* a key typed on a Dreamcast keyboard (any port): its character, Enter as '\n', Backspace 8,
 * Esc 27; 0 if none waiting (as the Cruis'n USA port's) */
int plat_key(void) {
    maple_device_t *kb = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);
    int k;
    if (kb == NULL) return 0;
    for (;;) {
        k = kbd_queue_pop(kb, 1);        /* translated: the character, or the key's code << 8 */
        if (k < 0) return 0;
        if (k == '\r' || k == (0x28 << 8) || k == (0x58 << 8)) return '\n';   /* Enter, keypad Enter */
        if (k == (0x2A << 8)) return 8;   /* Backspace */
        if (k == (0x29 << 8)) return 27;  /* Esc */
        if (k > 0 && k < 128) return k;
    }
}

/* ---------------------------------------------------------------- VMU saves
 * Files on the first VMU found (any controller, either slot), with the red helmet icon.
 * Writing takes a good part of a second: done by a thread of its own while the game goes
 * on (as the Cruis'n USA port does). */
#include "vmu_icon.h"
static mutex_t gVmuLock = MUTEX_INITIALIZER;
typedef struct SaveJob { maple_device_t *dev; uint8_t *file; int size; char name[16]; volatile int done; } SaveJob;
static kthread_t *gSaveThread;
static SaveJob gSaveJob;

static maple_device_t *save_vmu(void) {
    int i;
    for (i = 0; i < 8; i++) {
        maple_device_t *d = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
        if (d) return d;
    }
    return NULL;
}

static void *save_thread(void *arg) {
    SaveJob *j = (SaveJob *)arg;
    int r;
    mutex_lock(&gVmuLock);
    r = vmufs_write(j->dev, j->name, j->file, j->size, VMUFS_OVERWRITE);
    if (r < 0) { thd_sleep(200); r = vmufs_write(j->dev, j->name, j->file, j->size, VMUFS_OVERWRITE); }
    mutex_unlock(&gVmuLock);
    free(j->file);
    j->file = NULL;
    j->done = 1;
    return NULL;
}

/* wait for the last save to finish */
static void save_wait(void) {
    if (gSaveThread) {
        while (!gSaveJob.done) thd_sleep(20);
        thd_join(gSaveThread, NULL);
        gSaveThread = NULL;
    }
}

int plat_save_present(void) { return save_vmu() != NULL; }

int plat_save_write(const char *name, const char *desc, const void *data, int size) {
    maple_device_t *d = save_vmu();
    vmu_pkg_t pkg;
    uint8_t *file = NULL;
    int fileSize = 0;
    if (d == NULL) return 0;
    memset(&pkg, 0, sizeof pkg);
    strcpy(pkg.desc_short, "MANIC KARTS");
    strncpy(pkg.desc_long, desc, sizeof pkg.desc_long - 1);
    strcpy(pkg.app_id, "MANIC KARTS DC");
    pkg.icon_cnt = 1;
    pkg.icon_anim_speed = 0;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    pkg.data_len = size;
    memcpy(pkg.icon_pal, gVmuIconPal, sizeof gVmuIconPal);
    pkg.icon_data = (uint8_t *)gVmuIcon;
    pkg.data = (const uint8_t *)data;
    if (vmu_pkg_build(&pkg, &file, &fileSize) < 0 || file == NULL) return 0;
    save_wait();
    gSaveJob.dev = d;
    gSaveJob.file = file;
    gSaveJob.size = fileSize;
    strncpy(gSaveJob.name, name, 15);
    gSaveJob.done = 0;
    gSaveThread = thd_create(0, save_thread, &gSaveJob);
    if (gSaveThread == NULL) save_thread(&gSaveJob);
    return 1;
}

/* the game's data out of a save: found from the file's own header (icons, eyecatch,
 * length); failing that, by its magic word (the game's saves start "MK", two letters
 * and a digit), so a header laid out differently still loads. Every VMU is tried, and
 * each read up to three times: a real VMU can be busy for a moment (an emulated one
 * never is) */
static int save_extract(const uint8_t *h, int got, void *buf, int size) {
    static const int eyecatch[4] = { 0, 72 * 56 * 2, 512 + 72 * 56, 32 + 72 * 56 / 2 };
    int at;
    if (got >= 128) {
        int icons = h[0x40] | (h[0x41] << 8), ec = h[0x44] | (h[0x45] << 8);
        at = 128 + icons * 512 + (ec >= 0 && ec < 4 ? eyecatch[ec] : 0);
        if (icons >= 1 && icons <= 3 && at + size <= got && h[at] == 'M' && h[at + 1] == 'K') {
            memcpy(buf, h + at, size);
            return 1;
        }
    }
    for (at = 128; at + size <= got; at += 32) {   /* (the data starts on a 32-byte step) */
        if (h[at] == 'M' && h[at + 1] == 'K' && h[at + 3] >= '0' && h[at + 3] <= '9') {
            memcpy(buf, h + at, size);
            return 1;
        }
    }
    return 0;
}

static int gSaveErr;                     /* the last read's trouble (0: none) */
int plat_save_error(void) { return gSaveErr; }

int plat_save_read(const char *name, void *buf, int size) {
    int port, ok = 0;
    save_wait();
    gSaveErr = 0;
    for (port = 0; port < 8 && !ok; port++) {
        maple_device_t *d = maple_enum_type(port, MAPLE_FUNC_MEMCARD);
        int tries;
        if (d == NULL) continue;
        for (tries = 0; tries < 3 && !ok; tries++) {
            void *data = NULL;
            int got = 0, r;
                    mutex_lock(&gVmuLock);
            r = vmufs_read(d, name, &data, &got);
            mutex_unlock(&gVmuLock);
                    if (r >= 0 && data != NULL) {
                ok = save_extract((const uint8_t *)data, got, buf, size);
                if (!ok) gSaveErr = 100 + got / 512;   /* (read, but no save found in it) */
                free(data);
                break;                   /* (read, whether a save or not: no retrying) */
            }
            if (data) free(data);
            if (r == -2) break;          /* (vmufs_read: no such file; anything else is retried) */
            gSaveErr = -r;
            thd_sleep(60);
        }
    }
    if (ok) gSaveErr = 0;
    return ok;
}

/* ---------------------------------------------------------------- textures */
typedef struct Tex { pvr_ptr_t ptr; int w, h, fmt; } Tex;
static Tex gTexs[64];
static int gTexN;
static u16 gSpread[1024];

static void twiddle_init(void) {
    u32 v, b;
    for (v = 0; v < 1024; v++) {
        u32 s = 0;
        for (b = 0; b < 10; b++) s |= ((v >> b) & 1) << (2 * b);
        gSpread[v] = (u16)s;
    }
}

static void tex_upload(Tex *t, const void *data) {
    int w = t->w, h = t->h;
    const u16 *texels = (const u16 *)data;
    u16 *tw;
    if (TEX_KIND(t->fmt) == TEX_PAL8 || TEX_KIND(t->fmt) == TEX_VQ1555) {   /* (made ready at build time) */
        pvr_txr_load(data, t->ptr, TEX_BYTES(w, h, t->fmt));
        return;
    }
    if (t->fmt & TEX_TWIDDLED) {         /* (converted at build time: straight in) */
        pvr_txr_load(texels, t->ptr, w * h * 2);
        return;
    }
    tw = memalign(32, w * h * 2);
    int x, y, m = w < h ? w : h, lm = 0;
    while ((1 << lm) < m) lm++;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            /* twiddled order; a non-square texture is a row of square blocks */
            u32 idx = gSpread[y & (m - 1)] | (gSpread[x & (m - 1)] << 1);
            idx += (w > h) ? (u32)(x >> lm) << (2 * lm) : (u32)(y >> lm) << (2 * lm);
            tw[idx] = texels[y * w + x];
        }
    }
    pvr_txr_load(tw, t->ptr, w * h * 2);
    free(tw);
}

int gfx_texture(const void *texels, int w, int h, int fmt) {
    Tex *t = &gTexs[gTexN];
    t->w = w;
    t->h = h;
    t->fmt = fmt;
    t->ptr = pvr_mem_malloc(TEX_BYTES(w, h, fmt));
    if (t->ptr == NULL) return -1;       /* (out of video memory) */
    tex_upload(t, texels);
    return gTexN++;
}

void gfx_texture_update(int id, const void *texels) {
    if (id >= 0) tex_upload(&gTexs[id], texels);
}

/* every texture out of video memory (between the menus and a race); ids start again from 0 */
void gfx_free_all(void) {
    int i;
    for (i = 0; i < gTexN; i++) {
        if (gTexs[i].ptr) pvr_mem_free(gTexs[i].ptr);
        gTexs[i].ptr = NULL;
    }
    gTexN = 0;
}

u32 gfx_vram_free(void) {
    return (u32)pvr_mem_available();
}

void gfx_palette(int bank, const u16 *argb1555) {
    int i;
    for (i = 0; i < 256; i++) pvr_set_pal_entry(bank * 256 + i, argb1555[i]);
}

static int pvr_fmt(int fmt) {
    int k = TEX_KIND(fmt);
    if (k == TEX_PAL8) return PVR_TXRFMT_PAL8BPP | PVR_TXRFMT_8BPP_PAL((fmt >> 8) & 3);
    if (k == TEX_VQ1555) return PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE;
    if (k == TEX_ARGB4444) return PVR_TXRFMT_ARGB4444;
    if (k == TEX_RGB565) return PVR_TXRFMT_RGB565;
    return PVR_TXRFMT_ARGB1555;
}

void gfx_set_bg(f32 r, f32 g, f32 b) {
    pvr_set_bg_color(r, g, b);
}

static int gFog;

void gfx_no_fog(void) {
    gFog = 0;
}

void gfx_set_fog(f32 start, f32 end, f32 r, f32 g, f32 b) {
    pvr_fog_table_color(1.0f, r, g, b);
    pvr_fog_table_linear(start, end);
    gFog = 1;
}

/* ---------------------------------------------------------------- lists */
static const int LIST_ITEMS[3] = { 30000, 8000, 4000 };
static pvr_vertex_t *gBuf[3];
static int gN[3];
static const int gPvrList[3] = { PVR_LIST_OP_POLY, PVR_LIST_PT_POLY, PVR_LIST_TR_POLY };
static int gLastTex[3];

static void header(int list, int tex) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t *hdr;
    if (gN[list] + 1 > LIST_ITEMS[list]) return;
    hdr = (pvr_poly_hdr_t *)&gBuf[list][gN[list]++];
    if (tex >= 0) {
        Tex *t = &gTexs[tex];
        pvr_poly_cxt_txr(&cxt, gPvrList[list], pvr_fmt(t->fmt) | PVR_TXRFMT_TWIDDLED,
                         t->w, t->h, t->ptr, (t->fmt & TEX_POINT) ? PVR_FILTER_NONE : PVR_FILTER_BILINEAR);
        cxt.txr.env = PVR_TXRENV_MODULATE;
    } else {
        pvr_poly_cxt_col(&cxt, gPvrList[list]);
    }
    cxt.gen.culling = PVR_CULLING_NONE;   /* (the engine culls what it must) */
    if (gFog && !(tex >= 0 && (gTexs[tex].fmt & TEX_NOFOG))) cxt.gen.fog_type = PVR_FOG_TABLE;
    cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
    cxt.depth.write = list == LIST_BLEND ? PVR_DEPTHWRITE_DISABLE : PVR_DEPTHWRITE_ENABLE;
    if (tex >= 0 && (gTexs[tex].fmt & TEX_NOFOG)) {   /* the backdrop: always drawn, leaves no depth */
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }
    pvr_poly_compile(hdr, &cxt);
}

void gfx_begin(void) {
    pvr_wait_ready();
    gN[0] = gN[1] = gN[2] = 0;
    gLastTex[0] = gLastTex[1] = gLastTex[2] = -2;
}

void gfx_tris(const Vtx *v, int n, int tex, int list) {
    pvr_vertex_t *o;
    int i;
    if (tex != gLastTex[list]) {
        header(list, tex);
        gLastTex[list] = tex;
    }
    if (gN[list] + n > LIST_ITEMS[list]) return;
    o = &gBuf[list][gN[list]];
    for (i = 0; i < n; i++, o++) {
        o->flags = (i % 3 == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        o->x = v[i].x;
        o->y = v[i].y;
        o->z = v[i].z;
        o->u = v[i].u;
        o->v = v[i].v;
        o->argb = v[i].argb;
        o->oargb = 0;
    }
    gN[list] += n;
}

void gfx_end(void) {
    int l;
    pvr_scene_begin();
    for (l = 0; l < 3; l++) {
        pvr_list_begin(gPvrList[l]);
        if (gN[l] > 0) {
            pvr_prim(gBuf[l], gN[l] * 32);
        }
        pvr_list_finish();
    }
    pvr_scene_finish();
}

/* ---------------------------------------------------------------- sound (the AICA)
 * Sounds go into the sound chip's own memory (snd_sfx_load_raw_buf) and play on 12
 * voices of its own, kept for us; a playing voice's pitch, volume and pan are changed
 * with the chip's channel-update command. Music is an ADPCM stream the chip decodes,
 * topped up from the disc by a thread of its own. */
#define MAX_SOUNDS 48
typedef struct Sound { char name[16]; sfxhnd_t h; int rate, loops, ls, le; } Sound;
static Sound gSnd[MAX_SOUNDS];
static int gSndN, gVoice[AUD_VOICES], gAudOk;

static void aud_init(void) {
    int i;
    if (snd_stream_init() < 0) return;
    for (i = 0; i < AUD_VOICES; i++) gVoice[i] = snd_sfx_chn_alloc();
    gAudOk = 1;
}

int aud_load(const char *name) {
    char fn[32];
    u32 size;
    u8 *d;
    const u32 *h;
    int i;
    if (!gAudOk) return -1;
    for (i = 0; i < gSndN; i++) {
        if (strcmp(gSnd[i].name, name) == 0) return i;         /* (already in sound memory) */
    }
    if (gSndN >= MAX_SOUNDS) return -1;
    snprintf(fn, sizeof fn, "%s.pcm", name);
    d = (u8 *)plat_load_file(fn, &size);
    if (d == NULL || size < 24 || memcmp(d, "MKSN", 4) != 0) {
        if (d) free(d);
        return -1;
    }
    h = (const u32 *)(d + 4);            /* rate, samples, loop start, loop end, loops */
    gSnd[gSndN].h = snd_sfx_load_raw_buf((char *)(d + 24), h[1], h[0], 8, 1);
    free(d);
    if (gSnd[gSndN].h == SFXHND_INVALID) return -1;
    strncpy(gSnd[gSndN].name, name, 15);
    gSnd[gSndN].rate = h[0];
    gSnd[gSndN].ls = h[2];
    gSnd[gSndN].le = h[3];
    gSnd[gSndN].loops = h[4];
    return gSndN++;
}

int aud_rate(int snd) {
    return snd >= 0 && snd < gSndN ? gSnd[snd].rate : 22050;
}

void aud_play(int voice, int snd, int vol, int pan, int freq) {
    sfx_play_data_t p;
    if (!gAudOk || snd < 0 || snd >= gSndN || voice < 0 || voice >= AUD_VOICES || gVoice[voice] < 0) return;
    memset(&p, 0, sizeof p);
    p.chn = gVoice[voice];
    p.idx = gSnd[snd].h;
    p.vol = vol < 0 ? 0 : vol > 255 ? 255 : vol;
    p.pan = pan < 0 ? 0 : pan > 255 ? 255 : pan;
    p.loop = gSnd[snd].loops;
    p.freq = freq > 0 ? freq : gSnd[snd].rate;
    p.loopstart = gSnd[snd].ls;
    p.loopend = gSnd[snd].le;
    snd_sfx_play_ex(&p);
}

void aud_set(int voice, int vol, int pan, int freq) {
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);
    if (!gAudOk || voice < 0 || voice >= AUD_VOICES || gVoice[voice] < 0) return;
    memset(tmp, 0, sizeof tmp);
    cmd->cmd = AICA_CMD_CHAN;
    cmd->timestamp = 0;
    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
    cmd->cmd_id = gVoice[voice];
    chan->cmd = AICA_CH_CMD_UPDATE | AICA_CH_UPDATE_SET_VOL | AICA_CH_UPDATE_SET_PAN |
                (freq > 0 ? AICA_CH_UPDATE_SET_FREQ : 0);
    chan->vol = vol < 0 ? 0 : vol > 255 ? 255 : vol;
    chan->pan = pan < 0 ? 0 : pan > 255 ? 255 : pan;
    chan->freq = freq;
    snd_sh4_to_aica(tmp, cmd->size);
}

void aud_stop(int voice) {
    if (!gAudOk || voice < 0 || voice >= AUD_VOICES || gVoice[voice] < 0) return;
    snd_sfx_stop(gVoice[voice]);
}

/* the music stream. The disc is read 256 KB at a time (about 12 seconds of music) into
 * a ring, topped up when half of it has played: the music thread then hardly ever
 * competes with the game's own loading for the drive (each switch costs a seek) */
static snd_stream_hnd_t gMus = SND_STREAM_INVALID;
static FILE *gMusFile;
static u8 *gMusBuf;
static volatile int gMusOn;
static mutex_t gMusLock = MUTEX_INITIALIZER;
#define MUS_RING (256 * 1024)
static u8 *gMusRing;
static int gMusHave, gMusAt;

static void music_fill(void) {
    int left = gMusHave - gMusAt, tries = 0;
    if (gMusFile == NULL || gMusRing == NULL) return;
    if (left > 0 && gMusAt > 0) memmove(gMusRing, gMusRing + gMusAt, left);
    gMusHave = left;
    gMusAt = 0;
    while (gMusHave < MUS_RING && tries < 2) {
        int n = (int)fread(gMusRing + gMusHave, 1, MUS_RING - gMusHave, gMusFile);
        if (n <= 0) {                    /* (the end: round again) */
            fseek(gMusFile, 0, SEEK_SET);
            tries++;
            continue;
        }
        gMusHave += n;
    }
}

static void *music_feed(snd_stream_hnd_t hnd, int req, int *got) {
    int n;
    (void)hnd;
    if (req > SND_STREAM_BUFFER_MAX_ADPCM) req = SND_STREAM_BUFFER_MAX_ADPCM;
    if (gMusHave - gMusAt < req) music_fill();
    n = gMusHave - gMusAt < req ? gMusHave - gMusAt : req;
    if (n > 0) memcpy(gMusBuf, gMusRing + gMusAt, n);
    gMusAt += n > 0 ? n : 0;
    if (n < req) memset(gMusBuf + (n > 0 ? n : 0), 0x08, req - (n > 0 ? n : 0));
    *got = req;
    return gMusBuf;
}

static void *music_thread(void *arg) {
    (void)arg;
    for (;;) {
        mutex_lock(&gMusLock);
        if (gMusOn) snd_stream_poll(gMus);
        if (gMusOn && gMusHave - gMusAt < MUS_RING / 2) music_fill();   /* (topped up ahead) */
        mutex_unlock(&gMusLock);
        thd_sleep(20);
    }
    return NULL;
}

void music_play(const char *name) {
    char path[64];
    static int started;
    if (!gAudOk) return;
    music_stop();
    snprintf(path, sizeof path, "/cd/data/%s", name);
    mutex_lock(&gMusLock);
    gMusFile = fopen(path, "rb");
    if (gMusFile) {
        if (gMusBuf == NULL) gMusBuf = memalign(32, SND_STREAM_BUFFER_MAX_ADPCM);
        if (gMusRing == NULL) gMusRing = memalign(32, MUS_RING);
        gMusHave = gMusAt = 0;
        music_fill();
        if (gMus == SND_STREAM_INVALID) gMus = snd_stream_alloc(music_feed, SND_STREAM_BUFFER_MAX_ADPCM);
        if (gMus != SND_STREAM_INVALID) {
            snd_stream_start_adpcm(gMus, 44100, 0);
            gMusOn = 1;
        }
    }
    mutex_unlock(&gMusLock);
    if (!started) {
        thd_create(1, music_thread, NULL);
        started = 1;
    }
}

void music_stop(void) {
    mutex_lock(&gMusLock);
    if (gMusOn) snd_stream_stop(gMus);
    gMusOn = 0;
    if (gMusFile) fclose(gMusFile);
    gMusFile = NULL;
    mutex_unlock(&gMusLock);
}

void music_volume(int vol) {
    if (gMus != SND_STREAM_INVALID) snd_stream_volume(gMus, vol < 0 ? 0 : vol > 255 ? 255 : vol);
}

int main(int argc, char **argv) {
    pvr_init_params_t params = {
        { PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16 },
        512 * 1024, 0, 0, 0, 8           /* (vertex buffer: a frame uses well under 200k) */
    };
    int l;
    (void)argc;
    (void)argv;
    pvr_init(&params);
    pvr_set_pal_format(PVR_PAL_ARGB1555);
    twiddle_init();
    for (l = 0; l < 3; l++) {
        gBuf[l] = memalign(32, LIST_ITEMS[l] * 32);
    }
    aud_init();
    game_main();
    return 0;
}
