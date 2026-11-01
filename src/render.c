/*
 * render.c - the camera, and triangles to screen: transform (SH4ZAM's XMTRX,
 * the SH4's matrix bank, so each point is one FTRV instruction on the
 * Dreamcast), clip at the near plane, project, and batch by texture and list.
 * (From the Checkpoint! engine.)
 */
#include "game.h"
#include <stdlib.h>
#include <string.h>

Camera gCam;

#define BATCH 3072                       /* vertices */
static Vtx sBatch[BATCH];
static int sBatchN, sBatchTex = -1, sBatchList = -1;

void r_flush(void) {
    if (sBatchN > 0) {
        gfx_tris(sBatch, sBatchN, sBatchTex, sBatchList);
    }
    sBatchN = 0;
}

static void emit(const Vtx *a, const Vtx *b, const Vtx *c, int tex, int list) {
    if (tex != sBatchTex || list != sBatchList || sBatchN + 3 > BATCH) {
        r_flush();
        sBatchTex = tex;
        sBatchList = list;
    }
    sBatch[sBatchN++] = *a;
    sBatch[sBatchN++] = *b;
    sBatch[sBatchN++] = *c;
}

void cam_look(V3 eye, V3 target, f32 halfWidth) {
    V3 f = v3norm(v3sub(target, eye));
    V3 r = v3norm(v3cross(v3(0, 1, 0), f));
    gCam.pos = eye;
    gCam.fwd = f;
    gCam.right = r;
    gCam.up = v3cross(f, r);
    gCam.focal = shz_divf(SCREEN_W / 2, halfWidth);
    gCam.centreY = SCREEN_H / 2;
}

/* world -> camera (x right, y up, z ahead) into XMTRX */
void r_world(void) {
    shz_vec4_t r0 = { .x = gCam.right.x, .y = gCam.right.y, .z = gCam.right.z, .w = -v3dot(gCam.right, gCam.pos) };
    shz_vec4_t r1 = { .x = gCam.up.x, .y = gCam.up.y, .z = gCam.up.z, .w = -v3dot(gCam.up, gCam.pos) };
    shz_vec4_t r2 = { .x = gCam.fwd.x, .y = gCam.fwd.y, .z = gCam.fwd.z, .w = -v3dot(gCam.fwd, gCam.pos) };
    shz_vec4_t r3 = { .x = 0, .y = 0, .z = 0, .w = 1 };
    shz_xmtrx_load_rows_4x4(&r0, &r1, &r2, &r3);
}

#define NEAR_Z 0.1f

static void project(const CV *p, Vtx *o) {
    f32 iz = shz_invf(p->z);
    o->x = SCREEN_W / 2 + p->x * gCam.focal * iz;
    o->y = gCam.centreY - p->y * gCam.focal * iz;
    o->z = iz;
    o->u = p->u;
    o->v = p->v;
    o->argb = p->c;
}

static CV lerp_cv(const CV *a, const CV *b, f32 t) {
    CV r;
    r.x = a->x + (b->x - a->x) * t;
    r.y = a->y + (b->y - a->y) * t;
    r.z = a->z + (b->z - a->z) * t;
    r.u = a->u + (b->u - a->u) * t;
    r.v = a->v + (b->v - a->v) * t;
    r.c = t < 0.5f ? a->c : b->c;
    return r;
}

/* a point through XMTRX into camera space (one FTRV on the Dreamcast) */
void r_xform(const WV *w, CV *o) {
    V3 c = shz_xmtrx_transform_point3(w->p);
    o->x = c.x;
    o->y = c.y;
    o->z = c.z;
    o->u = w->u;
    o->v = w->v;
    o->c = w->c;
}

/* one triangle already in camera space: cull = drop it when it faces away */
void r_tri_cv(const CV *p0, const CV *p1, const CV *p2, int tex, int list, int cull) {
    const CV *in[3] = { p0, p1, p2 };
    CV out[4];
    int i, n = 0, behind = (p0->z < NEAR_Z) + (p1->z < NEAR_Z) + (p2->z < NEAR_Z);

    if (behind == 3) {
        return;
    }
    if ((p0->x > p0->z && p1->x > p1->z && p2->x > p2->z) ||
        (-p0->x > p0->z && -p1->x > p1->z && -p2->x > p2->z) ||
        (p0->y > p0->z && p1->y > p1->z && p2->y > p2->z) ||
        (-p0->y > p0->z && -p1->y > p1->z && -p2->y > p2->z)) {
        return;
    }
    if (behind == 0) {
        Vtx a, b, c;
        project(p0, &a);
        project(p1, &b);
        project(p2, &c);
        if (cull && (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0) {
            return;
        }
        emit(&a, &b, &c, tex, list);
        return;
    }
    for (i = 0; i < 3; i++) {
        const CV *p = in[i], *q = in[(i + 1) % 3];
        int pin = p->z >= NEAR_Z, qin = q->z >= NEAR_Z;
        if (pin) {
            out[n++] = *p;
        }
        if (pin != qin) {
            out[n++] = lerp_cv(p, q, shz_divf(NEAR_Z - p->z, q->z - p->z));
        }
    }
    {
        Vtx v[4];
        for (i = 0; i < n; i++) {
            project(&out[i], &v[i]);
        }
        if (cull && (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x) <= 0) {
            return;
        }
        emit(&v[0], &v[1], &v[2], tex, list);
        if (n == 4) {
            emit(&v[0], &v[2], &v[3], tex, list);
        }
    }
}

void r_quad(const WV *a, const WV *b, const WV *c, const WV *d, int tex, int list, int cull) {
    CV p[4];
    r_xform(a, &p[0]);
    r_xform(b, &p[1]);
    r_xform(c, &p[2]);
    r_xform(d, &p[3]);
    r_tri_cv(&p[0], &p[1], &p[2], tex, list, cull);
    r_tri_cv(&p[0], &p[2], &p[3], tex, list, cull);
}

/* a texture file (data/, made by the tools): "MKTX" = w, h, format, then the texels as
 * they go into video memory; "MKTR" = the same, packed (tools/build_assets.py rle()).
 * Into texture id (new when id < 0); its size into *w, *h. -> the id (-1: no file) */
int tex_from_mem(const u8 *d, u32 size, int id, int *w, int *h) {
    u32 n, i, o = 0;
    const u16 *hd;
    u8 *t = NULL;
    if (d == NULL || size < 12 || (memcmp(d, "MKTX", 4) != 0 && memcmp(d, "MKTR", 4) != 0)) return id;
    hd = (const u16 *)(d + 4);            /* w, h, format, 0 */
    n = TEX_BYTES(hd[0], hd[1], hd[2]);
    if (d[3] == 'R') {                    /* (unpacked here) */
        t = (u8 *)malloc(n);
        if (t == NULL) return id;
        for (i = 12; i < size && o < n;) {
            u32 c = d[i++];
            if (c < 128) {
                c++;
                if (o + c > n) c = n - o;
                memcpy(t + o, d + i, c);
                i += c;
                o += c;
            } else {
                c -= 126;
                if (o + c > n) c = n - o;
                memset(t + o, d[i++], c);
                o += c;
            }
        }
    }
    if (id < 0) id = gfx_texture(t ? t : d + 12, hd[0], hd[1], hd[2]);
    else gfx_texture_update(id, t ? t : d + 12);
    if (w) *w = hd[0];
    if (h) *h = hd[1];
    if (t) free(t);
    return id;
}

int tex_file(const char *name, int id, int *w, int *h) {
    u32 size;
    const u8 *d = (const u8 *)plat_load_file(name, &size);
    if (d == NULL) return id;
    id = tex_from_mem(d, size, id, w, h);
    free((void *)d);                      /* (it's in video memory now) */
    return id;
}
