/*
 * race.c - the race's rules, as the original's race state machine (counted in
 * hundredths of a second):
 *
 *   1.00 s        a beep ("ready")
 *   2.50 s        a beep ("set")
 *   4.00 + r s    the high beep ("go"): the karts may go (r: 0 to 0.99 s at random,
 *                 so the start can't be timed by the beeps alone)
 *
 * Then the laps; a kart finishes as it crosses the line the last time (the original's
 * laps_done == laps + 1, counting the first crossing off the grid). The finishing
 * order is fixed as karts cross; the player's kart is driven by the computer after
 * its finish, as in the original. Once the first three and the player have finished
 * (or the player has retired) the race is over 3 seconds later: the karts still
 * racing are placed as they run.
 *
 * There are no start lights on screen in the original: just the three beeps.
 */
#include "game.h"
#include <stdlib.h>

int gRaceLaps = 4;
static int sFrames, sCs;                 /* frames, and hundredths of a second, since the grid */
static int sGo;                          /* when it's "go" */
static int sFinishers, sPlayerDone;      /* how many have finished; when the player did (cs) */
static int sOrder[MAX_KARTS];            /* finishing order (kart numbers) */
static int sOver = -1;                   /* when the race was over (cs) */

void race_start(void) {
    int i;
    sFrames = sCs = 0;
    sGo = 400 + rand() % 100;
    sFinishers = 0;
    sPlayerDone = -1;
    sOver = -1;
    for (i = 0; i < MAX_KARTS; i++) sOrder[i] = -1;
}

int race_frozen(void) { return sCs < sGo; }
s32 race_clock(void) { return sFrames; }

/* one 1/60 s frame, after the karts have moved */
void race_tick(void) {
    int prev = sCs, i;
    sCs = ++sFrames * 100 / 60;
    if (prev < 100 && sCs >= 100) sound_beep(0);
    if (prev < 250 && sCs >= 250) sound_beep(0);
    if (prev < sGo && sCs >= sGo) sound_beep(1);
    if (sCs < sGo) return;
    for (i = 0; i < gKartN; i++) {
        Kart *k = &gKarts[i];
        if (k->finished || k->retired) continue;
        k->raceFrames++;
        if (k->laps >= gRaceLaps) {
            k->finished = ++sFinishers;
            sOrder[sFinishers - 1] = i;
            if (k->human) sPlayerDone = sCs;
        }
    }
}

/* the race is over (race_main_loop: state 3, then 2000 three seconds later) when the first
 * three karts and the player have finished (or the player has retired) */
int race_results(void) {
    int i, humans = 0;
    if (sOver >= 0) return sCs - sOver >= 300;
    for (i = 0; i < gKartN; i++) {
        if (gKarts[i].human && !gKarts[i].finished && !gKarts[i].retired) humans++;
    }
    if (humans == 0 && sFinishers >= (gKartN < 3 ? gKartN : 3)) sOver = sCs;
    return 0;
}

/* RETIRE in the pause menu: the player is out (the computer drives the kart on, but
 * it isn't placed), and the results come straight up */
void race_retire(void) {
    int i;
    for (i = 0; i < gKartN; i++) {
        if (gKarts[i].human && !gKarts[i].finished) {
            gKarts[i].retired = 1;
            sOver = sCs - 300;           /* (straight to the results) */
        }
    }
}

/* the results table's order: the finishers as they crossed, then the rest as they run */
int race_order(int place) {
    int i, n = sFinishers;
    if (place < n) return sOrder[place];
    for (i = 0; i < gKartN; i++) {
        if (!gKarts[i].finished && gKarts[i].position - 1 == place) return i;
    }
    return -1;
}
