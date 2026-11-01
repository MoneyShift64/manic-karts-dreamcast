/*
 * mkmusic.c - the original's music (the CD audio tracks of MANICKARTS.bin) for the
 * Dreamcast's sound chip: each track becomes data/musNN.adp, mono 44100 Hz Yamaha
 * 4-bit ADPCM, which the AICA decodes itself while it streams.
 *
 *     cc -O2 -o build/mkmusic tools/mkmusic.c
 *     build/mkmusic MANICKARTS.cue MANICKARTS.bin data   (run by build.sh)
 *
 * Reads the .cue for where each AUDIO track starts (INDEX 01, minutes:seconds:frames,
 * 75 frames a second, 2352 bytes a frame of 16-bit stereo); a track ends where the
 * next begins, the last at the end of the .bin.
 *
 * The encoder is the AICA's ADPCM (as KallistiOS's wav2adpcm: superctr's algorithm;
 * the first sample of each byte in the low nibble, as the chip decodes).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CLAMP(x, lo, hi) ((x) > (hi) ? (hi) : (x) < (lo) ? (lo) : (x))

static int16_t ymz_step(uint8_t step, int16_t *history, int16_t *step_size) {
    static const int step_table[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };
    int sign = step & 8, delta = step & 7;
    int diff = ((1 + (delta << 1)) * *step_size) >> 3;
    int newval = *history;
    int nstep = (step_table[delta] * *step_size) >> 8;
    diff = CLAMP(diff, 0, 32767);
    if (sign) newval -= diff;
    else newval += diff;
    *step_size = (int16_t)CLAMP(nstep, 127, 24576);
    *history = (int16_t)(newval = CLAMP(newval, -32768, 32767));
    return (int16_t)newval;
}

typedef struct { int16_t history, step; int nibble; uint8_t byte; } Enc;

static void encode(Enc *e, int16_t sample, FILE *out) {
    int step = (sample & -8) - e->history;
    uint32_t a = (uint32_t)((abs(step) << 16) / (e->step << 14));
    a = CLAMP(a, 0, 7);
    if (step < 0) a |= 8;
    if (!e->nibble) e->byte = (uint8_t)(a & 15);
    else fputc(e->byte | (a << 4), out);
    e->nibble ^= 1;
    ymz_step((uint8_t)a, &e->history, &e->step);
}

int main(int argc, char **argv) {
    FILE *cue, *bin;
    char line[256], path[512];
    long start[100];
    int number[100], audio[100], n = 0, cur = -1, i;
    long binSize;
    if (argc < 4) {
        fprintf(stderr, "usage: mkmusic game.cue game.bin outdir\n");
        return 1;
    }
    cue = fopen(argv[1], "r");
    bin = fopen(argv[2], "rb");
    if (!cue || !bin) {
        fprintf(stderr, "mkmusic: can't open %s or %s\n", argv[1], argv[2]);
        return 1;
    }
    fseek(bin, 0, SEEK_END);
    binSize = ftell(bin);
    while (fgets(line, sizeof line, cue)) {
        int t, m, s, f;
        char type[32];
        if (sscanf(line, " TRACK %d %31s", &t, type) == 2) {
            cur = n++;
            number[cur] = t;
            audio[cur] = strcmp(type, "AUDIO") == 0;
            start[cur] = -1;
        } else if (cur >= 0 && sscanf(line, " INDEX 01 %d:%d:%d", &m, &s, &f) == 3) {
            start[cur] = ((long)(m * 60 + s) * 75 + f) * 2352;
        }
    }
    fclose(cue);
    for (i = 0; i < n; i++) {
        long end = i + 1 < n ? start[i + 1] : binSize, pos;
        Enc e = { 0, 127, 0, 0 };
        FILE *out;
        if (!audio[i] || start[i] < 0) continue;
        snprintf(path, sizeof path, "%s/mus%02d.adp", argv[3], number[i]);
        out = fopen(path, "wb");
        if (!out) {
            fprintf(stderr, "mkmusic: can't write %s\n", path);
            return 1;
        }
        fseek(bin, start[i], SEEK_SET);
        for (pos = start[i]; pos + 4 <= end; pos += 4) {
            uint8_t b[4];
            int l, r;
            if (fread(b, 1, 4, bin) != 4) break;
            l = (int16_t)(b[0] | b[1] << 8);
            r = (int16_t)(b[2] | b[3] << 8);
            encode(&e, (int16_t)((l + r) / 2), out);
        }
        if (e.nibble) fputc(e.byte, out);
        /* (pad to a whole 2 KB, as the streamer reads in blocks) */
        while (ftell(out) % 2048) fputc(0x08, out);   /* (alternating -/+ smallest steps: silence) */
        printf("music: track %d, %ld seconds -> %s\n", number[i], (end - start[i]) / 176400, path);
        fclose(out);
    }
    fclose(bin);
    return 0;
}
