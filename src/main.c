/*
 * main.c - Manic Karts for the Dreamcast: a race on any of the 16 tracks.
 *
 * Eight karts: the player's at the back of the grid, the other seven driven by
 * the original's computer drivers (ai.c), all with the original game's physics
 * (kart.c), seen from behind as in the original.
 *
 * Controls (the manual's keyboard controls, on the pad):
 *            right trigger                accelerate
 *            left trigger                 brake
 *            Y                            reverse
 *            stick or d-pad left/right    steer
 *            X                            cycle through power-ups
 *            A (held)                     activate power-ups
 *            START                        pause (CONTINUE / RETIRE / QUIT)
 *            A / START (on the results)   race again / next track
 *            A + B + X + Y + START        soft reset (back to the title)
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define sKart gKarts[gGame.playerKart & 7]
int gRaceOrder[8];
s32 gRaceBest[8];
u8 gRaceFinished[8];
static int sPause = -1;                  /* the pause menu's pick (-1: not paused) */
static f32 sCamAngle;                    /* the chase camera's heading (radians, the original's convention) */

/* race_run: the track, the karts from the season (grid, upgrades, power-ups left) */
static void start_track(int n) {
    int i;
    track_load(n);
    gfx_set_fog(40.0f, 80.0f, gTrack.fog[0], gTrack.fog[1], gTrack.fog[2]);
    gfx_set_bg(gTrack.fog[0], gTrack.fog[1], gTrack.fog[2]);
    gKartN = MAX_KARTS;
    gRaceLaps = gGame.laps;
    for (i = 0; i < gKartN; i++) {
        kart_reset(&gKarts[i], i, gGame.e[i].grid, i == gGame.playerKart, &gGame.e[i]);
    }
    sCamAngle = sKart.heading * (PI_F / 1800.0f);
    sound_load(gTrack.index);
    powerups_load(gTrack.index);
    race_start();
    sPause = -1;
}

/* behind the kart as the original: low (about helmet height), looking level, the
 * horizon a little below the middle of the screen; pulled in when a wall is in the way */
#define CAM_DIST   3.75f
#define CAM_HEIGHT 1.05f
#define HORIZON    (SCREEN_H * 0.60f)
static void chase_camera(void) {
    f32 kx = kart_wx(&sKart), kz = kart_wz(&sKart), ky = kart_wy(&sKart);
    f32 want = sKart.heading * (PI_F / 1800.0f), d = want - sCamAngle, dist = 0.6f, dx, dz;
    while (d > PI_F) d -= 2 * PI_F;
    while (d < -PI_F) d += 2 * PI_F;
    sCamAngle += d * 0.12f;              /* (swings round smoothly) */
    dx = shz_cosf(sCamAngle);            /* map +column = world +x, map +row = world -z */
    dz = -shz_sinf(sCamAngle);
    while (dist < CAM_DIST && !track_wall_at(kx - dx * (dist + 0.25f), kz - dz * (dist + 0.25f))) dist += 0.25f;
    cam_look(v3(kx - dx * dist, ky + CAM_HEIGHT, kz - dz * dist),
             v3(kx + dx * 20.0f, ky + CAM_HEIGHT, kz + dz * 20.0f), 0.62f);
    gCam.centreY = HORIZON;
}

/* one race of the game (race_run 0x39170): 0 = over (gRaceOrder etc. hold the result),
 * 1 = QUIT from the pause menu */
int race_play(void) {
    Input in, last = { 0 };
    u32 t0, acc = 0;
    int i;

    gfx_free_all();                      /* (the menus' pictures out) */
    track_forget();
    fe_forget();
    start_track(gGame.track);
    t0 = plat_time_us();
    plat_input(&last);
    while (plat_running()) {
        u32 now = plat_time_us(), pressed;
        int steps = 0;
        acc += now - t0;
        t0 = now;
        if (acc > 100000) acc = 100000;

        plat_input(&in);
        pressed = in.buttons & ~last.buttons;
        last = in;
        if ((in.buttons & BTN_RESET) == BTN_RESET) {   /* soft reset: back to the title */
            sound_silence();
            music_stop();
            sPause = -1;
            return 2;
        }
        if (race_results()) break;       /* (over: the results, in the menus) */
        if (sPause >= 0) {               /* paused: up / down pick, A (or START) chooses */
            if (pressed & BTN_UP) sPause = sPause == 0 ? 2 : sPause - 1;
            if (pressed & BTN_DOWN) sPause = sPause == 2 ? 0 : sPause + 1;
            if (pressed & BTN_START) sPause = -1;
            else if (pressed & BTN_A) {
                if (sPause == 1) race_retire();          /* RETIRE: out of the race */
                else if (sPause == 2) {          /* QUIT: back to the main menu, no points */
                    sound_silence();
                    music_stop();
                    return 1;
                }
                sPause = -1;
            }
            acc = 0;
        } else if (pressed & BTN_START) {
            sPause = 0;
            sound_silence();
        }

        /* the game runs in 1/60 s frames, as the original does at 60 fps */
        while (acc >= 16667 && steps < 4) {
            int i;
            /* the race loop's order: who's where, the drivers, then physics, collisions
               and moves for every kart in turn */
            karts_relative();
            karts_surface();
            for (i = 0; i < gKartN; i++) {
                if (race_frozen()) { kart_release(&gKarts[i]); continue; }   /* (on the grid till "go") */
                if (gKarts[i].finished || gKarts[i].retired) { gKarts[i].fire = 0; ai_drive(&gKarts[i], 1); continue; }  /* (finished: the computer drives) */
                if (gKarts[i].human) {
                    ai_drive(&gKarts[i], 0);
                    kart_controls(&gKarts[i], &in);
                    kart_powerup_buttons(&gKarts[i], &in);
                } else {
                    ai_drive(&gKarts[i], 1);
                }
            }
            if (gGame.cheats & (CHEAT_GRIP | CHEAT_NITRO | CHEAT_FUEL)) {   /* (local_player_controls) */
                if (gGame.cheats & CHEAT_GRIP) sKart.supergrip = 0x1194;
                if (gGame.cheats & CHEAT_NITRO) sKart.powerupTimer = 4000;
                if (gGame.cheats & CHEAT_FUEL) sKart.fuel = sKart.fuelMax;
            }
            karts_powerups();
            for (i = 0; i < gKartN; i++) kart_physics(&gKarts[i]);
            if (!(gGame.cheats & CHEAT_NOCOLLIDE))   /* (SOMERVILLE: kart_collisions returns at once) */
                for (i = 0; i < gKartN; i++) kart_collide(&gKarts[i]);
            for (i = 0; i < gKartN; i++) kart_move(&gKarts[i]);
            objects_update();
            track_animate();
            race_tick();
            acc -= 16667;
            steps++;
        }

        chase_camera();

        sound_update();
        gfx_begin();
        /* tunnels (the original's race_render_frame): no sky over a cell with a surface
           code of 0x36 or more, just black behind everything */
        if (track_indoors(gCam.pos.x, gCam.pos.z)) {
            gfx_set_bg(0, 0, 0);
        } else {
            gfx_set_bg(gTrack.fog[0], gTrack.fog[1], gTrack.fog[2]);
            sky_draw(1.0f);
        }
        track_draw(32.0f, 80.0f);        /* (full floor tiles to 32 cells, walls to 80: about 3400 triangles) */
        objects_draw();
        {
            int i;
            for (i = 0; i < gKartN; i++) kart_draw(&gKarts[i]);
        }
        if (sPause >= 0) hud_pause(sPause);
        else hud_draw(&sKart, gRaceLaps);
        gfx_end();
    }
    /* the result: finishers in order, then the rest as they run */
    for (i = 0; i < 8; i++) {
        gRaceOrder[i] = race_order(i);
        gRaceBest[i] = gKarts[i].bestLap;
        gRaceFinished[i] = (u8)(gKarts[i].finished != 0);
        gGame.e[i].nitroN = (s16)gKarts[i].nitroN;
        gGame.e[i].gripN = (s16)gKarts[i].gripN;
    }
    sound_silence();
    return 0;
}

void game_main(void) {
    plat_init();
    track_init();
    physics_init();
    game_init();
    front_run();
}
