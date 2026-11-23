// Render a dream (module/dream.c or module/dreams/*.c) off the module.
//
//   cc -O2 -Imodule -Imodule/dreams tools/dream_render.c module/dreams/rain_lake.c -o dr
//   ./dr stats [seconds]            lit-pixel load, frame by frame (OLED safety)
//   ./dr video seconds fps [speed]  raw RGB 768x384 frames on stdout (for ffmpeg),
//                                   drawn as the emulator draws the OLED
//   ./dr frame ms > f.ppm           one frame
//
// The stats are what matters on the real OLED: the stippled train dream
// (module/dream.c, 9e64f0b) runs on the module without glitching, so a new
// dream should stay at or under its numbers.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dream.h"

static uint8_t fb[64 * 128];
__attribute__((weak)) uint32_t dk_overflow;

static void frame(uint32_t ms) {
    static uint8_t band[128 * 8];
    for (uint8_t i = 0; i < 8; i++) {
        dream_band(ms, i, band);
        memcpy(&fb[i * 8 * 128], band, sizeof(band));
    }
}

static void rgb(FILE *f) {
    static uint8_t out[384 * 768 * 3];
    for (int i = 0; i < 384 * 768; i++) out[i * 3] = 0x0C, out[i * 3 + 1] = 0x0C, out[i * 3 + 2] = 0x0E;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++) {
            const int v = fb[y * 128 + x];
            if (!v) continue;
            const int l = v * 17, g = l * 245 / 255, b = l * 225 / 255;
            for (int dy = 0; dy < 5; dy++)
                for (int dx = 0; dx < 5; dx++) {
                    uint8_t *p = &out[((y * 6 + dy) * 768 + x * 6 + dx) * 3];
                    p[0] = l, p[1] = g, p[2] = b;
                }
        }
    fwrite(out, 1, sizeof(out), f);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    if (!strcmp(argv[1], "frame")) {
        frame((uint32_t)atol(argv[2]));
        printf("P6 768 384 255\n");
        rgb(stdout);
        return 0;
    }
    if (!strcmp(argv[1], "video")) {
        const int secs = atoi(argv[2]), fps = atoi(argv[3]);
        const int speed = argc > 4 ? atoi(argv[4]) : 1;
        for (int i = 0; i < secs * fps; i++) {
            frame((uint32_t)((uint64_t)i * 1000 * speed / fps));
            rgb(stdout);
        }
        return 0;
    }
    if (!strcmp(argv[1], "stats")) {
        const int secs = argc > 2 ? atoi(argv[2]) : 480;
        int max_row = 0, max_lit = 0, max_v = 0;
        long sum_lit = 0, sum_energy = 0, max_energy = 0, n = 0;
        for (uint32_t ms = 0; ms < (uint32_t)secs * 1000; ms += 100) {
            frame(ms);
            int lit = 0;
            long energy = 0;
            for (int y = 0; y < 64; y++) {
                int r = 0;
                for (int x = 0; x < 128; x++) {
                    const int v = fb[y * 128 + x];
                    if (v) r++, lit++, energy += v;
                    if (v > max_v) max_v = v;
                }
                if (r > max_row) max_row = r;
            }
            if (lit > max_lit) max_lit = lit;
            if (energy > max_energy) max_energy = energy;
            sum_lit += lit, sum_energy += energy, n++;
        }
        if (dk_overflow) printf("PRIMITIVES OVERFLOWED %u times\n", (unsigned)dk_overflow);
        printf("max_row %d  max_lit %d  avg_lit %ld  max_energy %ld  avg_energy %ld  max_level %d\n", max_row,
               max_lit, sum_lit / n, max_energy, sum_energy / n, max_v);
        return 0;
    }
    return 1;
}
