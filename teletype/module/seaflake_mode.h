#ifndef _SEAFLAKE_MODE_H_
#define _SEAFLAKE_MODE_H_

#include <stdbool.h>
#include <stdint.h>

// Orca Seaflake: the grid screen. The module's only runtime (module/sf_ui.c).

void seaflake_init(void);
// true when seaflake_tick has work to do at time now (ms). Cheap; the
// firmware calls it from the 1 ms timer interrupt.
bool seaflake_due(uint32_t now);
// run frames, bursts, sequences and note-offs that are due
void seaflake_tick(uint32_t now);

void set_seaflake_mode(void);
void process_seaflake_keys(uint8_t key, uint8_t mod_key, bool is_held_key);
// esc: close a picker, the song view, the scan or a selection. returns false
// when there was nothing to close (esc then does nothing)
bool seaflake_escape(void);
// the front-panel button: opens LOAD PATCH; pressed again, loads the slot
// picked with the arrows or the PARAM knob (like the stock scene recall)
void seaflake_front(void);
// the PARAM knob (0..4095): picks the slot while LOAD PATCH is open
void seaflake_knob(uint16_t value);
uint8_t screen_refresh_seaflake(void);

#endif
