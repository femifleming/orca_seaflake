// Orca Seaflake for Teletype (alt firmware): the mode logic (see sf_ui.h).
//
// Two sides, switched with alt+o (option+o on a Mac keyboard):
//
//   Orca      the grid (M_SEAFLAKE) and its help (M_HELP). Boots here.
//   Teletype  the stock screens: live, edit, pattern, scene read and write
//             (M_LIVE, M_EDIT, M_PATTERN, M_PRESET_R, M_PRESET_W), with the
//             stock keys. No Teletype help (it does not fit; alt+h does
//             nothing there).
//
// Why the sides are explicit: the 32-scene firmware once had Orca as one more
// mode inside the stock mode machine, and every stock route (ESC = scene
// read, set_last_mode() one step back, the USB menu ending in M_LIVE...) left
// Orca with no way home. Here alt+o always crosses, from any screen, and the
// stock keys only move between the Teletype screens.
//
// Orca keeps playing while the Teletype side is on show (its clock is a
// timer in main.c), and Teletype scripts, metro and triggers run on both
// sides. The screensaver (main.c) covers both.

#include "sf_ui.h"

#include "edit_mode.h"
#include "flash.h"
#include "help_mode.h"
#include "keyboard_helper.h"
#include "live_mode.h"
#include "pattern_mode.h"
#include "preset_r_mode.h"
#include "preset_w_mode.h"
#include "seaflake_mode.h"
#include "state.h"
#include "teletype.h"
#include "teletype_io.h"

extern scene_state_t scene_state;
void clear_delays_and_slews(scene_state_t *ss);

static tele_mode_t mode = M_SEAFLAKE;
static tele_mode_t tt_screen = M_LIVE;  // live / edit / pattern: where "back" goes
static tele_mode_t tt_last = M_LIVE;    // the Teletype screen alt+o returns to
static uint8_t last_mod;                // for the pattern screen's knob

static bool is_tt(tele_mode_t m) {
    return m == M_LIVE || m == M_EDIT || m == M_PATTERN || m == M_PRESET_R ||
           m == M_PRESET_W;
}

tele_mode_t sf_ui_mode(void) {
    return mode;
}

bool sf_ui_teletype(void) {
    return is_tt(mode);
}

// globals.h
void set_mode(tele_mode_t m) {
    switch (m) {
        case M_LIVE: set_live_mode(); break;
        case M_EDIT: set_edit_mode(); break;
        case M_PATTERN: set_pattern_mode(); break;
        case M_PRESET_W: set_preset_w_mode(); break;
        case M_PRESET_R: set_preset_r_mode(preset_select); break;
        case M_HELP: set_help_mode(); break;
        default: m = M_SEAFLAKE; set_seaflake_mode(); break;
    }
    mode = m;
    if (m == M_LIVE || m == M_EDIT || m == M_PATTERN) tt_screen = m;
    if (is_tt(m)) tt_last = m;
}

// globals.h: "back" stays on the side it is on. Scene read / write go back
// to the Teletype screen they came from; Orca's help goes back to the grid.
void set_last_mode(void) {
    set_mode(is_tt(mode) ? tt_screen : M_SEAFLAKE);
}

void sf_ui_redraw(void) {
    set_mode(mode);
}

// the stock global keys, Teletype side only (main.c process_global_keys of
// the stock firmware, minus help and the screens that are not there)
static bool tt_global_keys(uint8_t k, uint8_t m) {
    if (match_no_mod(m, k, HID_TAB)) {  // live -> edit -> pattern -> live
        set_mode(mode == M_LIVE ? M_EDIT : mode == M_EDIT ? M_PATTERN : M_LIVE);
    }
    else if (match_no_mod(m, k, HID_ESCAPE)) {  // scene read, or back
        if (mode == M_PRESET_R) set_last_mode();
        else set_mode(M_PRESET_R);
    }
    else if (match_alt(m, k, HID_ESCAPE)) {  // scene write
        set_mode(M_PRESET_W);
    }
    else if (match_shift_alt(m, k, HID_SLASH) || match_alt(m, k, HID_H)) {
        // no Teletype help in this firmware
    }
    else if (mod_only_alt(m) && k >= HID_F1 && k <= HID_F10) {
        // alt-F1..F8 edit script 1-8, alt-F9 metro, alt-F10 init
        set_edit_mode_script(k - HID_F1);
        set_mode(M_EDIT);
    }
    else if (no_mod(m) && k >= HID_KEYPAD_1 && k <= HID_KEYPAD_8) {
        run_script(&scene_state, k - HID_KEYPAD_1);
    }
    else if (match_no_mod(m, k, HID_KEYPAD_NUM_LOCK) ||
             match_no_mod(m, k, HID_F11)) {
        if (mode != M_PATTERN) set_mode(M_PATTERN);
    }
    else if (match_no_mod(m, k, HID_PRINTSCREEN) ||
             match_no_mod(m, k, HID_F12)) {
        if (mode != M_LIVE) set_mode(M_LIVE);
    }
    else
        return false;
    return true;
}

void sf_ui_key(uint8_t k, uint8_t m, bool held) {
    last_mod = m;
    if (!held) {
        // alt+o: the other side, from any screen
        if (match_alt(m, k, HID_O)) {
            set_mode(is_tt(mode) ? M_SEAFLAKE : tt_last);
            return;
        }
        // win-esc: clear Teletype delays and slews (as stock)
        if (match_win(m, k, HID_ESCAPE)) {
            clear_delays_and_slews(&scene_state);
            return;
        }
        // F1-F10: run Teletype script 1-8, metro, init; ctrl-F1..F8 mute a
        // script's trigger input, ctrl-F9 metro on / off
        if (no_mod(m) && k >= HID_F1 && k <= HID_F10) {
            run_script(&scene_state, k - HID_F1);  // F9 = METRO_SCRIPT, F10 = INIT_SCRIPT
            return;
        }
        if (mod_only_ctrl(m) && k >= HID_F1 && k <= HID_F8) {
            ss_set_mute(&scene_state, k - HID_F1, !ss_get_mute(&scene_state, k - HID_F1));
            screen_mutes_updated();
            set_mutes_updated();
            return;
        }
        if (mod_only_ctrl(m) && k == HID_F9) {
            scene_state.variables.m_act = !scene_state.variables.m_act;
            tele_metro_updated();
            return;
        }
    }

    if (is_tt(mode)) {
        if (!held && tt_global_keys(k, m)) return;
        switch (mode) {
            case M_LIVE: process_live_keys(k, m, held, false, &scene_state); break;
            case M_EDIT: process_edit_keys(k, m, held); break;
            case M_PATTERN: process_pattern_keys(k, m, held); break;
            case M_PRESET_R: process_preset_r_keys(k, m, held); break;
            case M_PRESET_W: process_preset_w_keys(k, m, held); break;
            default: break;
        }
        return;
    }

    // Orca side
    if (!held && (match_alt(m, k, HID_H) || match_shift_alt(m, k, HID_SLASH))) {
        // alt-h or alt-?: help on the OLED, again (or esc) to go back
        set_mode(mode == M_HELP ? M_SEAFLAKE : M_HELP);
        return;
    }
    if (mode == M_HELP) {
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

void sf_ui_key_release(uint8_t k, uint8_t m) {
    last_mod = m;
    // releases only matter to the stock live screen
    if (mode == M_LIVE) process_live_keys(k, m, false, true, &scene_state);
}

// Orca: the LOAD PATCH picker. Teletype: scene read, the button again loads
// the scene the knob (or the arrows) picked.
void sf_ui_front(void) {
    if (is_tt(mode)) {
        if (mode == M_PRESET_R) process_preset_r_load();
        else set_mode(M_PRESET_R);
        return;
    }
    if (mode != M_SEAFLAKE) set_mode(M_SEAFLAKE);
    seaflake_front();
}

void sf_ui_knob(uint16_t value) {
    if (mode == M_SEAFLAKE) seaflake_knob(value);
    else if (mode == M_PATTERN) process_pattern_knob(value, last_mod);
    else if (mode == M_PRESET_R) {
        // stock: two knob steps per scene with a dead zone between them
        uint8_t step = (uint32_t)value * (SCENE_SLOTS * 2) >> 12;
        uint8_t preset = step >> 1;
        int d = (int)preset - (int)get_preset();
        if (!(step & 1) || d > 1 || d < -1) process_preset_r_preset(preset);
    }
}

uint8_t sf_ui_refresh(void) {
    switch (mode) {
        case M_LIVE: return screen_refresh_live();
        case M_EDIT: return screen_refresh_edit();
        case M_PATTERN: return screen_refresh_pattern();
        case M_PRESET_R: return screen_refresh_preset_r();
        case M_PRESET_W: return screen_refresh_preset_w();
        case M_HELP: return screen_refresh_help();
        default: return screen_refresh_seaflake();
    }
}
