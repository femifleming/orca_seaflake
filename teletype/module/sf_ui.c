// Orca Seaflake for Teletype: the mode logic (see sf_ui.h).
//
// Why this file exists: Orca Seaflake used to be one more mode (M_SEAFLAKE)
// inside the stock Teletype mode machine of main.c. Every stock route out of
// it stayed live: ESC is the stock "scene read" key in every Teletype screen
// and set_last_mode() only goes back one step, so once the module was in any
// stock screen (after a USB stick menu, which ends in M_LIVE; the front
// button; alt-ESC; alt-F1..F10; F11 / F12; alt-o) ESC toggled between SCENE
// READ and the Teletype screens and never came back to Orca. That looked like
// the module "booting back into the Teletype firmware". There is only one
// firmware on the chip; it was its stock UI. Here every request for a stock
// screen lands on the Orca grid, and only Orca's own keys exist.

#include "sf_ui.h"

#include "help_mode.h"
#include "keyboard_helper.h"
#include "seaflake_mode.h"
#include "state.h"
#include "teletype.h"
#include "teletype_io.h"

extern scene_state_t scene_state;
void clear_delays_and_slews(scene_state_t *ss);

static tele_mode_t mode = M_SEAFLAKE;

tele_mode_t sf_ui_mode(void) {
    return mode;
}

// globals.h. Callers in the stock Teletype code (the USB stick menu, the
// grid visualizer, scene read) still ask for M_LIVE, M_PRESET_R...: they all
// get the Orca grid.
void set_mode(tele_mode_t m) {
    if (m == M_HELP) {
        set_help_mode();
        mode = M_HELP;
    }
    else {
        set_seaflake_mode();
        mode = M_SEAFLAKE;
    }
}

// globals.h: "back" is always the Orca grid
void set_last_mode(void) {
    set_mode(M_SEAFLAKE);
}

void sf_ui_key(uint8_t k, uint8_t m, bool held) {
    // keys that are the same everywhere and never change the screen
    if (!held) {
        // alt-h or alt-?: help on the OLED, again (or esc) to go back
        if (match_alt(m, k, HID_H) || match_shift_alt(m, k, HID_SLASH)) {
            set_mode(mode == M_HELP ? M_SEAFLAKE : M_HELP);
            return;
        }
        // win-esc: clear Teletype delays and slews (as stock)
        if (match_win(m, k, HID_ESCAPE)) {
            clear_delays_and_slews(&scene_state);
            return;
        }
        // F1-F10: run Teletype script 1-8, metro, init ($script:N does the
        // same from the grid); ctrl-F1..F8 mute a script's trigger input,
        // ctrl-F9 metro on / off
        if (no_mod(m) && k >= HID_F1 && k <= HID_F10) {
            run_script(&scene_state, k - HID_F1);  // F9 = METRO_SCRIPT, F10 = INIT_SCRIPT
            return;
        }
        if (mod_only_ctrl(m) && k >= HID_F1 && k <= HID_F8) {
            ss_set_mute(&scene_state, k - HID_F1, !ss_get_mute(&scene_state, k - HID_F1));
            return;
        }
        if (mod_only_ctrl(m) && k == HID_F9) {
            scene_state.variables.m_act = !scene_state.variables.m_act;
            tele_metro_updated();
            return;
        }
    }

    if (mode == M_HELP) {
        // esc (or anything process_help_keys does not use) stays in help
        // until esc: process_help_keys sends esc to set_last_mode()
        process_help_keys(k, m, held);
        return;
    }

    // the grid: esc closes a picker, the song view, the scan or a
    // selection, and otherwise does nothing; tab does nothing
    if (match_no_mod(m, k, HID_ESCAPE)) {
        if (!held) seaflake_escape();
        return;
    }
    if (k == HID_ESCAPE || match_no_mod(m, k, HID_TAB)) return;  // alt-esc, shift-esc...
    process_seaflake_keys(k, m, held);
}

void sf_ui_front(void) {
    if (mode != M_SEAFLAKE) set_mode(M_SEAFLAKE);
    seaflake_front();
}

void sf_ui_knob(uint16_t value) {
    if (mode == M_SEAFLAKE) seaflake_knob(value);
}

uint8_t sf_ui_refresh(void) {
    return mode == M_HELP ? screen_refresh_help() : screen_refresh_seaflake();
}
