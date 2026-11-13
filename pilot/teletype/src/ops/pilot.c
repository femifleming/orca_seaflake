// Pilot Sealflake: PI.* ops play and control the Pilot synth.
//
// They follow Teletype's Disting EX ops (EX.N -> PI.N, EX.VOX -> PI.VOX,
// EX.P -> PI.P ...), with the Pilot channel (0-15) as the "synth", plus ops
// that mirror Pilot's own commands (PI.ENV = "0ENV056f", PI.FX = "REV8c" ...).

#include "ops/pilot.h"

#include <stdio.h>

#include "helpers.h"
#include "pilot_io.h"
#include "teletype.h"

#define PI_PARAM_ENG 0
#define PI_PARAM_LEN 26

static int16_t pi_ch = 0;
static int16_t vox_note[16] = { -1, -1, -1, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1, -1, -1 };
static int16_t vox_vel[16];

static int16_t ch16(int16_t c) {
    return c < 0 ? 0 : c > 15 ? 15 : c;
}

// 0-127: MIDI note. Over 127: Teletype pitch (1V/oct, 1638.4 per octave, from
// N / N.S / N.C / V ...), N 60 = MIDI 60 = C4 = Pilot "04C".
static int16_t note7(int16_t n) {
    if (n > 127) n = (int16_t)(((int32_t)n * 12 * 10 + 8192) / 16384);
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

// 0-127, or Teletype's 0-16383 range (scaled down) like the EX ops
static int16_t vel7(int16_t v) {
    if (v > 127) v = (int16_t)(((int32_t)v * 127) / 16383);
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

static int16_t hex16(int16_t v) {
    return v < 0 ? 0 : v > 15 ? 15 : v;
}

static const char hexd[] = "0123456789abcdef";

static void vox_play(int16_t ch, int16_t note, int16_t vel) {
    if (vox_note[ch] >= 0) pilot_io_note_off(ch, vox_note[ch]);
    vox_note[ch] = note;
    vox_vel[ch] = vel;
    pilot_io_note(ch, note, vel, -1);
}

static void vox_off(int16_t ch) {
    if (vox_note[ch] >= 0) pilot_io_note_off(ch, vox_note[ch]);
    vox_note[ch] = -1;
}

// clang-format off

// ---- channel ----
static void op_PI_CH_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pi_ch);
}
static void op_PI_CH_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pi_ch = ch16(cs_pop(cs));
}

// ---- notes ----
static void op_PI_N_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), note = note7(cs_pop(cs)), vel = vel7(cs_pop(cs));
    pilot_io_note(ch, note, vel, 0);
}
static void op_PI_NT_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), note = note7(cs_pop(cs)), vel = vel7(cs_pop(cs)), ms = cs_pop(cs);
    pilot_io_note(ch, note, vel, ms < 1 ? 1 : ms);
}
static void op_PI_N_POUND_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), note = note7(cs_pop(cs)), vel = vel7(cs_pop(cs));
    pilot_io_note(ch, note, vel, -1);
}
static void op_PI_NO_POUND_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), note = note7(cs_pop(cs));
    pilot_io_note_off(ch, note);
}
static void op_PI_NOTE_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t note = note7(cs_pop(cs)), vel = vel7(cs_pop(cs));
    pilot_io_note(pi_ch, note, vel, -1);
}
static void op_PI_NOTE_O_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pilot_io_note_off(pi_ch, note7(cs_pop(cs)));
}
static void op_PI_ALLOFF_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *NOTUSED(cs)) {
    for (int i = 0; i < 16; i++) vox_note[i] = -1;
    pilot_io_all_off();
}

// ---- voices: one held note per channel ----
static void op_PI_VOX_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), note = note7(cs_pop(cs)), vel = vel7(cs_pop(cs));
    vox_play(ch, note, vel);
}
static void op_PI_VOX_P_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), note = note7(cs_pop(cs));
    vox_play(ch, note, vox_note[ch] >= 0 || vox_vel[ch] > 0 ? vox_vel[ch] : 100);
}
static void op_PI_VOX_O_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    vox_off(ch16(cs_pop(cs)));
}

// ---- triggers (drum channels): the voice's last note, or C4 ----
static void op_PI_T_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs));
    pilot_io_note(ch, vox_note[ch] >= 0 ? vox_note[ch] : 60, 64, 0);
}
static void op_PI_TV_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), vel = vel7(cs_pop(cs));
    pilot_io_note(ch, vox_note[ch] >= 0 ? vox_note[ch] : 60, vel, 0);
}

// ---- engine / parameters ----
static void op_PI_ALG_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_param_get(pi_ch, PI_PARAM_ENG));
}
static void op_PI_ALG_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pilot_io_param_set(pi_ch, PI_PARAM_ENG, cs_pop(cs));
}
static void op_PI_PARAM_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_param_get(pi_ch, cs_pop(cs)));
}
static void op_PI_PARAM_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t num = cs_pop(cs), value = cs_pop(cs);
    pilot_io_param_set(pi_ch, num, value);
}
static void op_PI_P_POUND_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), num = cs_pop(cs);
    cs_push(cs, pilot_io_param_get(ch, num));
}
static void op_PI_P_POUND_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), num = cs_pop(cs), value = cs_pop(cs);
    pilot_io_param_set(ch, num, value);
}
static void op_PI_PV_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t num = cs_pop(cs), value = cs_pop(cs);
    pilot_io_param_pv(pi_ch, num, value);
}
static void op_PI_MIN_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_param_min(cs_pop(cs)));
}
static void op_PI_MAX_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_param_max(cs_pop(cs)));
}
static void op_PI_ENG_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_param_get(ch16(cs_pop(cs)), PI_PARAM_ENG));
}
static void op_PI_ENG_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), e = cs_pop(cs);
    pilot_io_param_set(ch, PI_PARAM_ENG, e);
}
static void op_PI_LEN_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_param_get(ch16(cs_pop(cs)), PI_PARAM_LEN));
}
static void op_PI_LEN_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), ms = cs_pop(cs);
    pilot_io_param_set(ch, PI_PARAM_LEN, ms);
}

// ---- Pilot's own commands, with its 0-15 (hex digit) values ----
// envelope: attack, decay, sustain, release, 1-10 each (1 = shortest / no
// sustain, 10 = longest / full sustain), spread evenly over Pilot's range
static void op_PI_ENV_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs));
    for (int16_t i = 0; i < 4; i++) {
        int16_t v = cs_pop(cs);
        if (v < 1) v = 1;
        if (v > 10) v = 10;
        pilot_io_param_pv(ch, 3 + i, (int16_t)(((int32_t)(v - 1) * 16384) / 9));
    }
}
static void op_PI_FIL_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), f = hex16(cs_pop(cs)), q = hex16(cs_pop(cs)), e = hex16(cs_pop(cs));
    char c[16];
    snprintf(c, sizeof(c), "%cFIL%c%c%c", hexd[ch], hexd[f], hexd[q], hexd[e]);
    pilot_io_command(c);
}
static void op_PI_MOD_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), r = hex16(cs_pop(cs)), i = hex16(cs_pop(cs));
    char c[16];
    snprintf(c, sizeof(c), "%cMOD%c%c", hexd[ch], hexd[r], hexd[i]);
    pilot_io_command(c);
}
// oscillator waves: -1 off, 0-3 si tr sq sw, 4-7 2x unison, 8-11 4x, 12-15 8x
static void op_PI_OSC_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t ch = ch16(cs_pop(cs)), a = cs_pop(cs), b = cs_pop(cs);
    if (a < -1) a = -1; if (a > 15) a = 15;
    if (b < -1) b = -1; if (b > 15) b = 15;
    pilot_io_param_set(ch, 1, a);
    pilot_io_param_set(ch, 2, b);
}
static const char *fx_codes[16] = { "BIT", "DIS", "CHE", "WAH", "PHA", "CHO", "VIB", "TRE",
                                    "FEE", "DEL", "REV", "STE", "EQU", "COM", "VOL", "LIM" };
static void op_PI_FX_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t fx = cs_pop(cs), w = hex16(cs_pop(cs)), v = hex16(cs_pop(cs));
    if (fx < 0 || fx > 15) return;
    char c[16];
    snprintf(c, sizeof(c), "%s%c%c", fx_codes[fx], hexd[w], hexd[v]);
    pilot_io_command(c);
}
static void op_PI_RENV_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *NOTUSED(cs)) {
    pilot_io_command("renv");
}
static void op_PI_ROSC_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *NOTUSED(cs)) {
    pilot_io_command("rosc");
}
static void op_PI_REFX_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *NOTUSED(cs)) {
    pilot_io_command("refx");
}
static void op_PI_BPM_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_bpm_get());
}
static void op_PI_BPM_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pilot_io_bpm_set(cs_pop(cs));
}

// ---- presets / recording ----
static void op_PI_PRESET_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, pilot_io_preset_get());
}
static void op_PI_PRESET_set(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pilot_io_preset_load(cs_pop(cs));
}
static void op_PI_SAVE_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pilot_io_preset_save(cs_pop(cs));
}
static void op_PI_RESET_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *NOTUSED(cs)) {
    for (int i = 0; i < 16; i++) vox_note[i] = -1;
    pilot_io_reset();
}
static void op_PI_REC_get(const void *NOTUSED(data), scene_state_t *NOTUSED(ss), exec_state_t *NOTUSED(es), command_state_t *cs) {
    pilot_io_rec(cs_pop(cs));
}

const tele_op_t op_PI_CH       = MAKE_GET_SET_OP(PI.CH,     op_PI_CH_get,      op_PI_CH_set, 0, true);
const tele_op_t op_PI_POUND    = MAKE_ALIAS_OP(PI.#,        op_PI_CH_get,      op_PI_CH_set, 0, true);
const tele_op_t op_PI_N        = MAKE_GET_OP(PI.N,          op_PI_N_get,       3, false);
const tele_op_t op_PI_NT       = MAKE_GET_OP(PI.NT,         op_PI_NT_get,      4, false);
const tele_op_t op_PI_N_POUND  = MAKE_GET_OP(PI.N#,         op_PI_N_POUND_get, 3, false);
const tele_op_t op_PI_NO_POUND = MAKE_GET_OP(PI.NO#,        op_PI_NO_POUND_get, 2, false);
const tele_op_t op_PI_NOTE     = MAKE_GET_OP(PI.NOTE,       op_PI_NOTE_get,    2, false);
const tele_op_t op_PI_NOTE_O   = MAKE_GET_OP(PI.NOTE.O,     op_PI_NOTE_O_get,  1, false);
const tele_op_t op_PI_NO       = MAKE_ALIAS_OP(PI.NO,       op_PI_NOTE_O_get,  NULL, 1, false);
const tele_op_t op_PI_ALLOFF   = MAKE_GET_OP(PI.ALLOFF,     op_PI_ALLOFF_get,  0, false);
const tele_op_t op_PI_AO       = MAKE_ALIAS_OP(PI.AO,       op_PI_ALLOFF_get,  NULL, 0, false);
const tele_op_t op_PI_VOX      = MAKE_GET_OP(PI.VOX,        op_PI_VOX_get,     3, false);
const tele_op_t op_PI_V        = MAKE_ALIAS_OP(PI.V,        op_PI_VOX_get,     NULL, 3, false);
const tele_op_t op_PI_VOX_P    = MAKE_GET_OP(PI.VOX.P,      op_PI_VOX_P_get,   2, false);
const tele_op_t op_PI_VP       = MAKE_ALIAS_OP(PI.VP,       op_PI_VOX_P_get,   NULL, 2, false);
const tele_op_t op_PI_VOX_O    = MAKE_GET_OP(PI.VOX.O,      op_PI_VOX_O_get,   1, false);
const tele_op_t op_PI_VO       = MAKE_ALIAS_OP(PI.VO,       op_PI_VOX_O_get,   NULL, 1, false);
const tele_op_t op_PI_T        = MAKE_GET_OP(PI.T,          op_PI_T_get,       1, false);
const tele_op_t op_PI_TV       = MAKE_GET_OP(PI.TV,         op_PI_TV_get,      2, false);
const tele_op_t op_PI_ALG      = MAKE_GET_SET_OP(PI.ALG,    op_PI_ALG_get,     op_PI_ALG_set, 0, true);
const tele_op_t op_PI_A        = MAKE_ALIAS_OP(PI.A,        op_PI_ALG_get,     op_PI_ALG_set, 0, true);
const tele_op_t op_PI_PARAM    = MAKE_GET_SET_OP(PI.PARAM,  op_PI_PARAM_get,   op_PI_PARAM_set, 1, true);
const tele_op_t op_PI_P        = MAKE_ALIAS_OP(PI.P,        op_PI_PARAM_get,   op_PI_PARAM_set, 1, true);
const tele_op_t op_PI_P_POUND  = MAKE_GET_SET_OP(PI.P#,     op_PI_P_POUND_get, op_PI_P_POUND_set, 2, true);
const tele_op_t op_PI_PV       = MAKE_GET_OP(PI.PV,         op_PI_PV_get,      2, false);
const tele_op_t op_PI_MIN      = MAKE_GET_OP(PI.MIN,        op_PI_MIN_get,     1, true);
const tele_op_t op_PI_MAX      = MAKE_GET_OP(PI.MAX,        op_PI_MAX_get,     1, true);
const tele_op_t op_PI_ENG      = MAKE_GET_SET_OP(PI.ENG,    op_PI_ENG_get,     op_PI_ENG_set, 1, true);
const tele_op_t op_PI_LEN      = MAKE_GET_SET_OP(PI.LEN,    op_PI_LEN_get,     op_PI_LEN_set, 1, true);
const tele_op_t op_PI_ENV      = MAKE_GET_OP(PI.ENV,        op_PI_ENV_get,     5, false);
const tele_op_t op_PI_FIL      = MAKE_GET_OP(PI.FIL,        op_PI_FIL_get,     4, false);
const tele_op_t op_PI_MOD      = MAKE_GET_OP(PI.MOD,        op_PI_MOD_get,     3, false);
const tele_op_t op_PI_OSC      = MAKE_GET_OP(PI.OSC,        op_PI_OSC_get,     3, false);
const tele_op_t op_PI_FX       = MAKE_GET_OP(PI.FX,         op_PI_FX_get,      3, false);
const tele_op_t op_PI_RENV     = MAKE_GET_OP(PI.RENV,       op_PI_RENV_get,    0, false);
const tele_op_t op_PI_ROSC     = MAKE_GET_OP(PI.ROSC,       op_PI_ROSC_get,    0, false);
const tele_op_t op_PI_REFX     = MAKE_GET_OP(PI.REFX,       op_PI_REFX_get,    0, false);
const tele_op_t op_RENV        = MAKE_ALIAS_OP(RENV,        op_PI_RENV_get,    NULL, 0, false);
const tele_op_t op_ROSC        = MAKE_ALIAS_OP(ROSC,        op_PI_ROSC_get,    NULL, 0, false);
const tele_op_t op_REFX        = MAKE_ALIAS_OP(REFX,        op_PI_REFX_get,    NULL, 0, false);
const tele_op_t op_PI_BPM      = MAKE_GET_SET_OP(PI.BPM,    op_PI_BPM_get,     op_PI_BPM_set, 0, true);
const tele_op_t op_PI_PRESET   = MAKE_GET_SET_OP(PI.PRESET, op_PI_PRESET_get,  op_PI_PRESET_set, 0, true);
const tele_op_t op_PI_PRE      = MAKE_ALIAS_OP(PI.PRE,      op_PI_PRESET_get,  op_PI_PRESET_set, 0, true);
const tele_op_t op_PI_SAVE     = MAKE_GET_OP(PI.SAVE,       op_PI_SAVE_get,    1, false);
const tele_op_t op_PI_RESET    = MAKE_GET_OP(PI.RESET,      op_PI_RESET_get,   0, false);
const tele_op_t op_PI_REC      = MAKE_GET_OP(PI.REC,        op_PI_REC_get,     1, false);

// clang-format on
