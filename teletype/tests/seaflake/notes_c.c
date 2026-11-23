#include <stdio.h>
#include "seaflake/sf.h"
void sf_io_midi_on(uint8_t a, uint8_t b, uint8_t c) {} void sf_io_midi_off(uint8_t a, uint8_t b) {}
void sf_io_midi_cc(uint8_t a, uint8_t b, uint8_t c) {} void sf_io_midi_pb(uint8_t a, uint8_t b, uint8_t c) {}
void sf_io_trig(uint8_t o, uint16_t m) {} void sf_io_gate(uint8_t o, bool h) {} void sf_io_cv(uint8_t o, int16_t v, uint16_t s) {}
int8_t sf_io_dev(char d, char s, const char *c, int16_t *r) { return 0; } void sf_io_script(uint8_t n) {} bool sf_io_patch_load(uint8_t n) { return false; }
int main(void) { const char *L = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (const char *p = L; *p; p++) for (int o = 0; o <= 8; o++) printf("%c%d %d\n", *p, o, sf_note_id(*p, o)); }
