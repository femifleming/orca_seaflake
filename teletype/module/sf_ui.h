// Orca Seaflake for Teletype: which screen the module shows, and where the
// keyboard, the front button and the PARAM knob go.
//
// Shared by the firmware (module/main.c) and the emulator (emu/tt_emu.c), so
// both run the same mode logic. Orca Seaflake is the only runtime: the stock
// Teletype screens (live, edit, pattern, scene read/write) cannot be reached.
// See docs/reverse-engineering.md, "ESC and the stock Teletype screens".

#ifndef _SF_UI_H_
#define _SF_UI_H_

#include <stdbool.h>
#include <stdint.h>

#include "globals.h"

// the screen on show: M_SEAFLAKE (the grid and its pickers) or M_HELP
tele_mode_t sf_ui_mode(void);
// a key press (not a release) from the USB keyboard
void sf_ui_key(uint8_t key, uint8_t mod_key, bool is_held_key);
// the front-panel button went down
void sf_ui_front(void);
// the PARAM knob, 0..4095 (adc[1] on the module)
void sf_ui_knob(uint16_t value);
// draw the current screen; returns the dirty line mask
uint8_t sf_ui_refresh(void);

#endif
