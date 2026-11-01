#ifndef GAME_H
#define GAME_H
#include "mk.h"

/* ---------------------------------------------------------------- render.c */
typedef struct Camera {
    V3 pos, right, up, fwd;
    f32 focal;
    f32 centreY;                         /* screen line of the view's centre (the horizon, looking level) */
} Camera;
extern Camera gCam;

typedef struct WV { V3 p; f32 u, v; u32 c; } WV;  /* a world vertex */
static inline WV wv(V3 p, f32 u, f32 v, u32 c) { WV r; r.p = p; r.u = u; r.v = v; r.c = c; return r; }
typedef struct CV { f32 x, y, z, u, v; u32 c; } CV; /* a camera-space vertex */

void cam_look(V3 eye, V3 target, f32 halfWidth);
void r_world(void);
void r_xform(const WV *w, CV *out);
void r_tri_cv(const CV *a, const CV *b, const CV *c, int tex, int list, int cull);
void r_quad(const WV *a, const WV *b, const WV *c, const WV *d, int tex, int list, int cull);
void r_flush(void);

/* ---------------------------------------------------------------- track.c */
/* World units: 1.0 = one map cell (the original's 1024 position units).
 * x = map column, z = MAP_N - map row (so the map reads the right way round
 * from above), y = up. */
#define MAP_N 128
#define WALL_H 1.6f                      /* wall height: 1.6 x a kart (64 pixels, 1 cell), measured from the original */

typedef struct TrackInfo {               /* data/tracks.bin, 64 bytes each */
    char title[32];
    char theme[4];                       /* "UK", "IC" ... (texture file prefix) */
    u8 startX, startY;                   /* (the viewer's start place) */
    u16 startHeading;                    /* (the viewer's: tenths of a degree from +row towards +column) */
    u16 heading;                         /* the original's start heading (tenths, 0 = +column, 900 = +row) */
    u8 pad[6];
    u8 grid[16];                         /* starting grid: kart n at x = grid[2n]*1024, y = grid[2n+1]*1024 */
} TrackInfo;

typedef struct Track {
    int index;
    TrackInfo info;
    const u8 *maps;                      /* the 9 maps of the .KTF, 16384 bytes each */
    const u8 *light;                     /* brightness per cell 0..255 */
    const u8 *surface;                   /* floor tile -> surface type (the original's track table +0x142) */
    int texObj;                          /* this theme's scenery sprites */
    int texKarts[4];                     /* the 8 karts' views, full size, compressed: karts 2p and 2p+1 in texture p */
    int texHud;                          /* the on-screen display's fonts, icons and maps */
    const u16 *hudRect;                  /* x, y, w, h of each: 5 fonts of 60, then the 2 maps */
    const char *hudNames;                /* the drivers' names, 16 bytes each */
    int texSky[5];                       /* the sky panorama, 5 pieces of 512 */
    const u16 *objRect;                  /* each sprite: x, y, w, h in texObj */
    int objRects;
    f32 fog[3];                          /* the colour along the bottom of the sky */
} Track;
#define MAP_FLOOR(t)   ((t)->maps)
#define MAP_WALLS(t)   ((t)->maps + 1 * 16384)
#define MAP_OBJECTS(t) ((t)->maps + 5 * 16384)   /* checkpoint lines, objects, pickups, grid */
#define MAP_SURFACE(t) ((t)->maps + 7 * 16384)

extern Track gTrack;
extern int gTrackCount;
void track_init(void);                   /* reads tracks.bin, makes the textures */
void track_load(int n);
void track_forget(void);
int tex_file(const char *name, int id, int *w, int *h);   /* render.c: a texture file into video memory */
int tex_from_mem(const u8 *d, u32 size, int id, int *w, int *h);                 /* its textures are gone (gfx_free_all): load them again */
void track_animate(void);             /* the animated wall and floor tiles, every 1/60 s */
void track_draw(f32 nearDist, f32 wallDist);   /* tile-by-tile floor within nearDist, walls within wallDist */
void sky_draw(f32 bright);               /* the panorama behind everything (looking level only), 0..1 bright */
int track_indoors(f32 x, f32 z);         /* in a tunnel (no sky over it) */
int track_is_wall(int c, int r);        /* by map column and row */
int track_wall_at(f32 x, f32 z);        /* by world position */

/* ---------------------------------------------------------------- objects.c */
/* The trackside objects: cones, barriers, signs... (data/trkNN.obj, made from the
 * original's object lists and map 5). Positions as the karts': 1024 a cell. */
typedef struct ObjType {                 /* 16 bytes, as in trkNN.obj */
    u8 sprite, flags;                    /* flags: 1 = a bump (a jump when driven over), 2 = no collision */
    u8 fallen[3];                        /* pictures when knocked flying, and lying down */
    u8 pad;
    s16 z, tall;                         /* height off the floor, and how tall (64 a cell) */
    u8 pad2[2];
    s32 knock;                           /* 0 = solid; else how hard to knock flying */
} ObjType;

typedef struct Obj {
    s32 x, y, z, vx, vy, vz;             /* (z: off its own base height) */
    u8 type, mirror, state, frame;       /* state: 0 standing, 1 flying, 2 knocked over */
} Obj;

struct Kart;
void objects_load(int track);
int objects_at(int cell);                /* an object standing or lying in the cell now */
int objects_hit(struct Kart *k, int cell);  /* a kart touching an object's cell: 1 = solid */
void objects_update(void);               /* one 1/60 s frame */
void objects_draw(void);

/* ---------------------------------------------------------------- kart.c */
/* The original's kart, with its own units (decomp/game/kart.h has the original
 * layout; the offsets are noted here). Position: 1024 = one map cell, x = column,
 * y = row. Heading: tenths of a degree, 0 = +x (column), 900 = +y (row).
 * Speeds: position units a second (1024 = a cell a second). */
typedef struct Kart {
    s32 x, y, z;                         /* +3a2 +3a6 +3aa  position, height above the floor */
    s32 cellX, cellY;                    /* +3ae +3b2 */
    s32 heading, headingPrev;            /* +39a +39e */
    s32 moveDir;                         /* +3d6  direction of travel */
    s32 speed, side;                     /* +3da +3de  along / across the kart */
    s32 vx, vy, vz;                      /* +3e2 +3e6 +3ea  world velocity */
    s32 mx, my;                          /* +3ee +3f2  this frame's movement */
    s32 spinA, spinB;                    /* +3f6 +3fa  spins (tenths of a degree a second) */
    s32 mass;                            /* +3fe */
    s32 steer;                           /* +38e  -250..250 */
    s32 power;                           /* +382  engine multiplier, 64 = 1 */
    s32 gripFactor;                      /* +386  64 = 1 */
    s32 braking, engineOn;               /* +40a +40e */
    s32 fFwd, fSide, fX, fY;             /* +416 +41a +41e +422  forces */
    s32 engineForce;                     /* +426 */
    s32 rollingDrag;                     /* +42a */
    s32 brakeForce;                      /* +42e */
    s32 gripA, gripB;                    /* +436 +43a */
    s32 spinFriction, spinDecay;         /* +43e +442 */
    s32 slide;                           /* +446  sideways speed (for the skid sound) */
    s32 powerupTimer, supergrip, oil;    /* +459 +461 +481 */
    s32 damage, damageMax, damagePct;    /* +489 +485 +48d */
    s32 fuel, fuelMax;                   /* +499 +495 */
    s32 fuelLapStart, fuelPerLap;        /* +49d +4a1  fuel at the start; used a lap (x 1.5, after lap 1) */
    s32 nitroN, nitroMax, gripN, gripMax;   /* +457 +455 +45f +45d  power-ups left (3 of 5 at the start) */
    s32 selected, fire;                  /* +44f +451  power-up picked (0 nitro, 1 oil, 2 super grip); button held */
    s32 pitTime;                         /* +154  time in the pit lane towards a power-up */
    s32 awardT[3], awardN[3];            /* +148.. +14e..  pit awards: countdown, how many next time */
    u8 wantPit;                          /* +136  (computer) low on fuel: go in at the pits */
    s32 lineStamp;                       /* +18a  race clock (frames) when it last crossed the line */
    struct { s32 laps, time; char text[8]; } gap[8];   /* +74  the gap to each kart, as last timed
                                            at the line (laps: + behind, - ahead), and as shown */
    s32 torque[40];                      /* +2e2  engine force by speed (1024 a step) */
    u8 surface;                          /* +130  surface under the kart */
    u8 human;                            /* +12e */
    u8 engineLevel, gripLevel;           /* +129 +12a  shop upgrades */
    u8 hitFlag, soundFlag;               /* +44a +44e */
    s32 checkpoint;                      /* +140  next checkpoint line (9..14) */
    s32 index;                           /* +104  which kart this is */
    struct { s32 dist, angle, closing; } rel[8];   /* +14..  each other kart: how far, which way
                                            (from it to this one), how fast the gap changes */
    s32 stuck, stuckX, stuckY;           /* +18e +192 +196  stuck timer and where to escape to */
    u8 altRoute, rescue;                 /* +106 +12f */
    u8 driver;                           /* which of the 8 drivers' pictures */
    s32 progress;                        /* +ed76-ish  how far round the race (for positions) */
    s32 position;                        /* 1st..8th */
    /* (the port's own) */
    s32 laps, lapFrames, lastLap, bestLap, started;
    s32 finished, raceFrames, retired;            /* finishing place (0: still racing); race time */
} Kart;

#define MAX_KARTS 8
extern Kart gKarts[MAX_KARTS];
extern int gKartN;

extern int gFT;                          /* frame time, 1024 = a second (the original's; 17 at 60 fps) */
void physics_init(void);                 /* reads physics.bin */
struct Entrant;
void kart_reset(Kart *k, int slot, int gridPos, int human, const struct Entrant *e);   /* on the grid of the current track */
void kart_controls(Kart *k, const Input *in);
void kart_engine(Kart *k, int dir);      /* the controls the computer drivers use too */
void kart_brake(Kart *k);
void kart_release(Kart *k);
void kart_physics(Kart *k);              /* one 1/60 s frame, in the race loop's order: */
void kart_collide(Kart *k);              /*   physics for all, then collisions for all, */
void kart_move(Kart *k);                 /*   then moves for all */
void kart_turn_test(Kart *k);            /* kart_corner_wall_test: turned into a wall? turn back */
int kart_boxed_in(Kart *k);              /* kart_corner_wall_test2: a corner (or the middle) in a wall */
s32 kart_cos(int a);
s32 kart_sin(int a);
void kart_draw(const Kart *k);
void kart_world_velocity(Kart *k);       /* kart_compute_world_velocity: speeds -> vx, vy */
s32 kart_laps_done(const Kart *k);       /* the original's laps_done (the first crossing counts) */

/* ---------------------------------------------------------------- powerup.c */
void powerups_load(int track);           /* data/race.bin: the track's tables */
void karts_surface(void);                /* FUN_43978: grip and power by surface, pit lane, pit awards */
void karts_powerups(void);               /* karts_update_powerups: fire the picked power-up */
void kart_powerup_buttons(Kart *k, const Input *in);   /* the player's: pick, fire */
void ai_powerups(Kart *k, int pitZone);  /* the computer's: when to pit, when to fire */
int powerups_pit_no_brake(void);

/* ---------------------------------------------------------------- sound.c */
void sound_load(int track);              /* the track's sounds and music */
void sound_update(void);                 /* once a frame, after the camera */
void sound_beep(int high);
void sound_silence(void);                /* the pause: race sounds off */               /* the start beeps (FUELL; high: an octave up) */

/* ---------------------------------------------------------------- hud.c */
void hud_draw(const Kart *player, int lapsTotal);
void hud_results(const Kart *player);    /* the results table */
void hud_pause(int sel);                 /* the pause menu: CONTINUE / RETIRE / QUIT */

/* ---------------------------------------------------------------- race.c */
extern int gRaceLaps;                    /* 4 */
void race_start(void);                   /* the grid: the clock at 0 */
void race_tick(void);                    /* one frame: beeps, laps, finishes */
int race_frozen(void);                   /* still on the grid (not "go" yet) */
int race_results(void);                  /* time for the results table */
void race_retire(void);                  /* the pause menu's RETIRE */
int race_order(int place);               /* which kart is in this place (0 = 1st) */
s32 race_clock(void);                    /* frames since the grid */

/* ---------------------------------------------------------------- season.c */
enum { MODE_SINGLE = 0, MODE_EASY, MODE_HARD, MODE_MANIC };
enum { CHEAT_GRIP = 1, CHEAT_NITRO = 2, CHEAT_FUEL = 4, CHEAT_NOCOLLIDE = 8, CHEAT_3D = 16, CHEAT_TRINITY = 32 };
typedef struct Entrant {                 /* a kart's season (the original's kart record, the parts kept) */
    s32 money, moneyTotal, points, racePoints, prizeBonus;   /* +0x11b +0x123 +0x117 +0x113 +0x11f */
    u16 wins, fastest, bribes;           /* +0x10f +0x111 +0x10d */
    u8 engine, tyres, tank, standing;    /* +0x129 +0x12a +0x12c +0x127 */
    s16 nitroN, gripN;                   /* +0x457 +0x45f (carried from race to race) */
    u8 pos, grid, finished, pad;         /* last race's place (0 = 1st), next grid slot */
    s32 bestLap;                         /* last race's best lap (frames, 0 = none) */
} Entrant;
typedef struct Game {
    u8 mode, engineChoice, cls, playerKart;   /* 0x12d24b, 0x12d23b, 0x12d233, 0x12a8f0 */
    u8 round, anyTrack, track, cheats;       /* 0x12d245, ORIEL 0x12cd74, the current track */
    signed char fastestKart, pad[3];
    u16 laps, startIdx, racesDone, pad2;     /* 0x12cd6e, 0x12cd72, 0x12cd6a */
    char playerName[16], playerHandle[10];
    Entrant e[8];
    s32 records[16][4];                  /* best lap by track and engine class (frames) */
} Game;
typedef struct HiScore { char name[20]; u8 laps, pad[3]; s32 earnings, points; } HiScore;
extern Game gGame;
extern HiScore gHi[8];
void game_init(void);                    /* names, the saved records and greats */
void game_settings_save(void);
void game_start_new(void);
void game_prepare_race(void);
void game_award(const int *order, const s32 *bestLap, const u8 *finished);
void game_sort_standings(void);
int game_next_race(void);
int game_list(int i);                    /* the game type's i-th track (99: the end) */
int game_kart_at_standing(int s);
int game_hiscore_insert(void);
void game_lap_record(int track, s32 frames);
int game_save(int slot, const char *name);
int game_load(int slot, char *nameOut, int load);
int game_cheat_key(int c);
int game_cheat_word(const char *s);
const char *driver_name(int kart);
const char *driver_handle(int kart);

/* ---------------------------------------------------------------- fe.c */
void fe_load(const char *set);
void fe_begin(void);
void fe_end(void);
void fe_bg(void);
void fe_sprite(int id, f32 x, f32 y, f32 scale, u32 argb);
void fe_sprite_rect(int id, f32 x0, f32 y0, f32 x1, f32 y1, u32 argb);
int fe_sprite_w(int id);
int fe_sprite_h(int id);
int fe_ring_n(void);
void fe_ring(int i, f32 *x, f32 *y, f32 *s);
int fe_width(int font, const char *s);
int fe_text(int font, const char *s, f32 x, f32 y, int align);
int fe_text_c(int font, const char *s, f32 x, f32 y, int align, u32 argb);
int fe_text_scaled(int font, const char *s, f32 x, f32 y, int align, u32 argb, f32 sc);
void fe_fade(int a);
void fe_sprite_pts(int id, const f32 *x, const f32 *y, u32 argb);
void fe_sprite_pts_part(int id, const f32 *x, const f32 *y, f32 s0, f32 t0, f32 s1, f32 t1, u32 argb);
void fe_forget(void);
int fe_preload(void);                    /* one more menu file into memory (1: all done) */
void fe_sound(int which);                /* 0 cursor, 1 select / back, 2 quit */
enum { FA = 0, FB, FC, FD, FE, FF };     /* the six fonts */

/* ---------------------------------------------------------------- front.c / main.c */
void front_run(void);                    /* the menus, for good */
int race_play(void);                     /* one race of gGame: 0 done, 1 QUIT, 2 soft reset */
extern int gRaceOrder[8];                /* the last race's order (kart numbers) */
extern s32 gRaceBest[8];
extern u8 gRaceFinished[8];

/* ---------------------------------------------------------------- ai.c */
void ai_load(void);                      /* the route maps of the current track */
void ai_drive(Kart *k, int computer);    /* ai_drive_kart: computer = 0 for the player (stuck rescue only) */
void karts_relative(void);               /* karts_update_relative_angles: distances, angles, positions */
static inline f32 kart_wx(const Kart *k) { return k->x * (1.0f / 1024); }
static inline f32 kart_wz(const Kart *k) { return MAP_N - k->y * (1.0f / 1024); }
static inline f32 kart_wy(const Kart *k) { return k->z * (1.0f / 1024); }

#endif
