// Orca Seaflake for Teletype: where patches and songs are kept.
// Firmware: module/sf_flash.c (its own flash area, separate from the 32
// Teletype scenes). Emulator: emu/tt_emu.c (a file on disk).

#ifndef _SEAFLAKE_STORE_H_
#define _SEAFLAKE_STORE_H_

#include <stdbool.h>
#include <stdint.h>

#include "sf.h"

struct sf_scan;

void sf_store_init(void);
// patch n (1..32): its packed bytes, 0 if the slot is empty
uint16_t sf_store_patch_get(uint8_t n, const uint8_t **data);
// len 0 empties the slot
void sf_store_patch_put(uint8_t n, const uint8_t *data, uint16_t len);
// song n (1..8): false if the slot is empty
bool sf_store_song_get(uint8_t n, sf_song_t *out);
void sf_store_song_put(uint8_t n, const sf_song_t *song);
// the patch shown at power on
uint8_t sf_store_last_patch(void);
void sf_store_set_last_patch(uint8_t n);
// the song played at power on (1..8), 0: none, play the last patch
uint8_t sf_store_boot_song(void);
void sf_store_set_boot_song(uint8_t n);
// what the I2C scan found at the last power-on (read back by the flasher)
void sf_store_set_scan(const struct sf_scan *scan);

#endif
