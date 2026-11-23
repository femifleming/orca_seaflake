// Runs a patch through the C engine (src/seaflake/sf_core.c) and prints the
// grid after every frame, in the same format as run_js.js.
// usage: run_c <patch.orca> <frames> <w> <h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seaflake/sf.h"

void sf_io_ex_note(uint8_t note, uint8_t vel, bool on) {}
void sf_io_jf_note(uint8_t note, uint8_t vel) {}
void sf_io_jf_tr(uint8_t ch, uint8_t state) {}
void sf_io_midi_on(uint8_t ch, uint8_t note, uint8_t vel) {}
void sf_io_midi_off(uint8_t ch, uint8_t note) {}
void sf_io_midi_cc(uint8_t ch, uint8_t cc, uint8_t val) {}
void sf_io_midi_pb(uint8_t ch, uint8_t lsb, uint8_t msb) {}
void sf_io_trig(uint8_t out, uint16_t ms) {}
void sf_io_gate(uint8_t out, bool high) {}
void sf_io_cv(uint8_t out, int16_t value, uint16_t slew_ms) {}
int8_t sf_io_dev(char dev, char sel, const char *cells, int16_t *result) { return 0; }
void sf_io_script(uint8_t n) {}
bool sf_io_patch_load(uint8_t n) { return false; }
void sf_io_config_write(const uint8_t block[32]) {}

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    int frames = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]);
    sf_init();
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    char buf[1024];
    int y = 0;
    while (y < h && fgets(buf, sizeof buf, f)) {
        for (int x = 0; x < w && buf[x] && buf[x] != '\n' && buf[x] != '\r'; x++)
            sf_set(x, y, buf[x] == ' ' ? '.' : buf[x]);
        y++;
    }
    fclose(f);
    for (int fr = 0; fr < frames; fr++) {
        sf_frame(fr * 125);
        printf("frame %d\n", fr);
        for (int yy = 0; yy < h; yy++) {
            for (int x = 0; x < w; x++) putchar(sf_get(x, yy));
            putchar('\n');
        }
    }
    return 0;
}
