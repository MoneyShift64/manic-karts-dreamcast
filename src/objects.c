/*
 * objects.c - the trackside objects: cones, barriers, bollards, signs, lights...
 *
 * data/trkNN.obj (made by build_assets.py from the original's object lists in
 * K.EXE and the track's map 5, as track_place_objects 0x4331c reads them) gives
 * each object type's picture, height and how hard it is to knock over, and where
 * each object stands: the middle of its cell.
 *
 * Collisions are the original's (the object part of kart_check_cell_collision
 * 0x22200): an object only counts when the kart's height overlaps it (so karts
 * drive under the overhead signs); solid ones stop the kart like a wall; the rest
 * are knocked flying, at the kart's speed divided by how hard they are to knock.
 * Bumps throw the kart up a little.
 *
 * A knocked object flies as the original's (track_object_update 0x41100): off the
 * collision map while it flies, falling under gravity and bouncing (to a stop when
 * slow), slowing along the ground (faster on it than in the air), bouncing off walls
 * and other objects; once still it lies there (one of its fallen pictures, at random)
 * and is back on the collision map, to be knocked again.
 * The original moves objects at 64 to a cell (the karts at 1024): x and y are kept
 * here at 1024 to a cell, the speeds and z (height above its base) in its units.
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

#define MAX_OBJ 400

static const ObjType *sTypes;
static int sTypeN;
static Obj sObj[MAX_OBJ];
static int sObjN;
static u16 sAt[16384];                   /* standing object in each cell (index + 1) */
static void *sFile;

void objects_load(int track) {
    char name[16];
    u32 size;
    const u8 *d;
    int i, n;
    sObjN = 0;
    sTypeN = 0;
    memset(sAt, 0, sizeof sAt);
    if (sFile) free(sFile);
    name[0] = 't'; name[1] = 'r'; name[2] = 'k';
    name[3] = (char)('0' + track / 10); name[4] = (char)('0' + track % 10);
    strcpy(name + 5, ".obj");
    sFile = plat_load_file(name, &size);
    d = (const u8 *)sFile;
    if (d == NULL || size < 4) return;
    sTypeN = d[0] | d[1] << 8;
    n = d[2] | d[3] << 8;
    sTypes = (const ObjType *)(d + 4);
    d += 4 + sTypeN * 16;
    for (i = 0; i < n && sObjN < MAX_OBJ; i++, d += 4) {
        Obj *o = &sObj[sObjN];
        memset(o, 0, sizeof *o);
        o->x = d[0] * 1024 + 512;
        o->y = d[1] * 1024 + 512;
        o->type = d[2];
        o->mirror = !d[3];               /* (the file's flag is the original's: 1 = as drawn, 0 = mirrored) */
        sAt[d[1] * 128 + d[0]] = (u16)(++sObjN);
    }
}

static inline s32 iabs(s32 v) { return v < 0 ? -v : v; }

/* a bit either way: v + rand() % |v| - |v| / 2 (as the original) */
static s32 wobble(s32 v) {
    s32 a = iabs(v);
    return a ? v + rand() % a - (a >> 1) : 0;
}

int objects_at(int cell) { return cell >= 0 && cell < 16384 && sAt[cell] != 0; }

int objects_hit(Kart *k, int cell) {
    Obj *o;
    const ObjType *t;
    int id = cell >= 0 && cell < 16384 ? sAt[cell] : 0, kh = k->z >> 4;
    s32 d, vz;
    if (!id) return 0;
    o = &sObj[id - 1];
    t = &sTypes[o->type];
    if (!(t->z <= kh + 0x34 && kh <= t->z + t->tall)) return 0;    /* over or under it */
    if (t->flags & 1) {                  /* a bump */
        if (k->z == 0) k->vz = (s32)((u32)iabs(k->speed) / (u32)(rand() % 4 + 9));
        return 0;
    }
    if (t->flags & 2) return 0;
    if (t->knock == 0) return 1;         /* solid */
    if (k->speed > 1000 || k->slide > 300) k->hitFlag = 1;
    if (t->knock < 5) {
        d = t->knock * 4;
        vz = k->speed / (t->knock << 3);
    } else {
        d = t->knock;
        vz = k->speed / (t->knock << 2);
    }
    if (vz < 300) vz = 0;
    o->vx = wobble(k->vx / d);
    o->vy = wobble(k->vy / d);
    o->vz = wobble(vz);
    if (o->vx || o->vy) {                /* (off the map while it flies: driven through) */
        o->state = 1;
        o->frame = 0;
        sAt[cell] = 0;
    }
    return 0;
}

static int blocked(s32 x, s32 y) {     /* (x, y at 1024 a cell) */
    int c = x >> 10, r = y >> 10;
    if (c < 0 || r < 0 || c >= 128 || r >= 128) return 1;
    return track_is_wall(c, r) || sAt[r * 128 + c] != 0;
}

/* one axis of the flight: moved, unless into a wall or another object (then back,
   and bouncing the other way); slowed by drag either way */
static void fly_axis(Obj *o, s32 *pos, s32 *v, s32 step, s32 drag) {
    s32 was = *pos, nv;
    if (*v == 0) return;                 /* (this way still) */
    *pos += step << 4;                   /* (its 64 a cell to 1024) */
    if (*pos < 0) *pos = 0;
    if (*pos > 0x1fff << 4) *pos = 0x1fff << 4;
    if (blocked(o->x, o->y)) {
        *pos = was;
        nv = *v > 0 ? *v - drag : *v + drag;
        if ((*v > 0 && nv < 0) || (*v < 0 && nv > 0)) nv = 0;
        *v = -nv;
    } else {
        nv = *v > 0 ? *v - drag : *v + drag;
        if ((*v > 0 && nv < 0) || (*v < 0 && nv > 0)) nv = 0;
        *v = nv;
    }
}

void objects_update(void) {
    int i;
    for (i = 0; i < sObjN; i++) {
        Obj *o = &sObj[i];
        const ObjType *t = &sTypes[o->type];
        s32 dz, drag;
        if (o->state != 1) continue;
        o->frame++;
        /* up and down (objects standing on the floor only) */
        if ((o->vz != 0 || o->z > 0) && t->z == 0) {
            o->vz -= gFT * 0x708 >> 10;
            dz = o->vz * gFT >> 10;
            if (dz == 0) dz = o->vz < 0 ? -1 : o->vz > 0 ? 1 : 0;
            o->z += dz;
            if (o->z < 0) {              /* landed: a bounce, unless slow */
                o->vz = o->vz * -600 >> 11;
                if (iabs(o->vz) < 0x400) o->vz = 0;
                o->z = 0;
            }
        }
        if (o->vx == 0 && o->vy == 0) {
            if (o->z == 0 && o->vz == 0) goto rest;
            continue;
        }
        drag = o->z < 1 ? 0x40 : 0x14;
        fly_axis(o, &o->x, &o->vx, o->vx * gFT >> 10, drag);
        fly_axis(o, &o->y, &o->vy, o->vy * gFT >> 10, drag);
        if (o->vx != 0 || o->vy != 0) continue;
    rest:                                /* still: lying there, and back on the map */
        o->state = 2;
        o->frame = (u8)(rand() % 3);
        o->z = 0;
        {
            int c = o->x >> 10, r = o->y >> 10;
            if (c >= 0 && r >= 0 && c < 128 && r < 128 && sAt[r * 128 + c] == 0) sAt[r * 128 + c] = (u16)(i + 1);
        }
    }
}

/* each object as a sprite standing up, facing the camera: 64 pixels a cell */
void objects_draw(void) {
    int i;
    if (gTrack.texObj < 0 || gTrack.objRect == NULL) return;
    for (i = 0; i < sObjN; i++) {
        const Obj *o = &sObj[i];
        const ObjType *t = &sTypes[o->type];
        int spr = o->state == 0 ? t->sprite : o->state == 1 ? t->fallen[(o->frame >> 3) & 1] : t->fallen[o->frame % 3];
        f32 wx = o->x * (1.0f / 1024), wz = MAP_N - o->y * (1.0f / 1024), dx, dz;
        const u16 *r;
        f32 u0, u1, v0, v1, hw, h, y;
        int b = 255, c = o->x >> 10, rr = o->y >> 10;
        u32 col;
        WV w;
        CV p, q[4];
        if (spr >= gTrack.objRects) continue;
        dx = wx - gCam.pos.x;
        dz = wz - gCam.pos.z;
        if (dx * dx + dz * dz > 40.0f * 40.0f) continue;
        if (dx * gCam.fwd.x + dz * gCam.fwd.z < -2.0f) continue;   /* behind */
        r = gTrack.objRect + spr * 4;
        u0 = r[0] * (1.0f / 1024);
        u1 = (r[0] + r[2]) * (1.0f / 1024);
        v0 = r[1] * (1.0f / 512);
        v1 = (r[1] + r[3]) * (1.0f / 512);
        if (o->mirror) { f32 tmp = u0; u0 = u1; u1 = tmp; }
        hw = r[2] * (0.5f / 64);
        h = r[3] * (1.0f / 64);
        y = (t->z + o->z) * (1.0f / 64);
        if (c >= 0 && c < 128 && rr >= 0 && rr < 128) b = gTrack.light[rr * 128 + c];
        col = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        w = wv(v3(wx, y, wz), 0, 0, col);
        r_xform(&w, &p);
        q[0] = p; q[0].x -= hw; q[0].y += h; q[0].u = u0; q[0].v = v0;
        q[1] = p; q[1].x += hw; q[1].y += h; q[1].u = u1; q[1].v = v0;
        q[2] = p; q[2].x += hw;              q[2].u = u1; q[2].v = v1;
        q[3] = p; q[3].x -= hw;              q[3].u = u0; q[3].v = v1;
        r_tri_cv(&q[0], &q[1], &q[2], gTrack.texObj, LIST_CUTOUT, 0);
        r_tri_cv(&q[0], &q[2], &q[3], gTrack.texObj, LIST_CUTOUT, 0);
    }
    r_flush();
}
