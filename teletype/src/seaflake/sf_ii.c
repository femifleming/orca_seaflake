// Orca Seaflake for Teletype: everything the grid sends out goes through the
// real Teletype ops, so the I2C bytes on the bus are exactly the ones the
// EX.* and SC.* ops send when typed in a script.
//
//   MIDI operators (: % ! ? ( ) @)  -> EX.M.N#  EX.M.NO#  EX.M.CC#  EX.M.PB
//   ; message                       -> any EX.* op  (Disting EX)
//   = message                       -> any SC.* op  (ER-301)
//   trigger / CV operators          -> Teletype TR 1-4 + CV 1-4, or
//                                      SC.TR.P / SC.TR / SC.CV on ER-301 ports
//
// This file builds and runs those commands; it is shared by the firmware and
// the desktop emulator.

#include "sf_ii.h"
#include "sf_config.h"

#include <ctype.h>
#include "sf_str.h"
#include <string.h>

#include "ops/op.h"
#include "ops/disting.h"
#include "ops/op_enum.h"
#include "sf.h"
#include "state.h"
#include "teletype.h"
#include "teletype_io.h"

extern scene_state_t scene_state;

char sf_ii_error[24];
char sf_ii_last[40];

sf_bus_t sf_bus = { 3, 0, 0, 1, 0, 100 };  // the emulator's bus: pulled up, 100 kHz
uint16_t sf_ii_sent, sf_ii_failed;
uint8_t sf_ii_fail_addr;
uint32_t sf_ii_fail_at;
sf_scan_t sf_scan;
static uint8_t er_seen;  // ER-301s that acknowledged an SC write (bit n-1)
static bool scanning;  // the scan's probes fail on purpose for absent units

int8_t sf_ii_fail_code;
// Just Friends at 0x70: 0 not asked yet, 1 answered, 2 no answer. Set by the
// scan (a write-only probe) and by every , message after it
uint8_t sf_jf_state;
uint8_t sf_ii_fail_cmd, sf_ii_fail_len;

const char *sf_ii_status_name(int status) {
    switch (status) {
        case 0: return "OK";
        case -5: return "NACK";
        case -8: return "BUSY";
        case -2: return "ARB";
        default: return "ERR";
    }
}

void sf_ii_note(uint8_t addr, int status, const uint8_t *data, uint8_t len) {
    if (scanning) return;
    sf_ii_sent++;
    // an ER-301 may not answer the scan's read but takes the SC writes:
    // an acknowledged write counts as found
    if (!status && len && addr == 0x31) {
        er_seen = 1;
        sf_scan.er[0] = 1;
        sf_scan.er_unit = 1;
    }
    if (status && addr == 0x31) er_seen = 0;
    if (len && addr == 0x70) sf_jf_state = status ? 2 : 1;
    if (status) {
        sf_ii_failed++;
        sf_ii_fail_addr = addr;
        sf_ii_fail_code = status;
        sf_ii_fail_cmd = data && len ? data[0] : 0;
        sf_ii_fail_len = len;
        sf_ii_fail_at = tele_get_ticks();
        if (!sf_ii_fail_at) sf_ii_fail_at = 1;
    }
}

// ER-301 trigger pulse length last sent per port, so SC.TR.TIME goes on the
// bus only when the length changes
static uint16_t er_time[27];
// and the CV slew last sent per port, plus one (0 = not known yet), so
// SC.CV.SLEW goes on the bus only when it changes
static uint16_t er_slew[27];

void (*sf_trace)(const char *command) = 0;
static int16_t er_port(int16_t port);

// kept out of run() so its buffer is not on the stack of every command
static void __attribute__((noinline)) trace(const tele_command_t *c) {
    char t[256];
    print_command(c, t);
    sf_trace(t);
}

static process_result_t run(const tele_command_t *c) {
    if (sf_trace) trace(c);
    exec_state_t es;
    es_init(&es);
    es_push(&es);
    es_variables(&es)->script_number = LIVE_SCRIPT;
    return process_command(&scene_state, &es, c);
}

// run op with up to 4 number arguments
static process_result_t op(int16_t o, uint8_t n, int16_t a, int16_t b,
                           int16_t c, int16_t d) {
    tele_command_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.separator = -1;
    cmd.data[0].tag = OP;
    cmd.data[0].value = o;
    int16_t args[4] = { a, b, c, d };
    for (uint8_t i = 0; i < n; i++) {
        cmd.data[i + 1].tag = NUMBER;
        cmd.data[i + 1].value = args[i];
    }
    cmd.length = n + 1;
    return run(&cmd);
}

////////////////////////////////////////////////////////////////////////////
// MIDI through the Disting EX (EX.M.* ops, channels 1-16)

void sf_io_midi_on(uint8_t ch, uint8_t note, uint8_t vel) {
    op(E_OP_EX_M_N_POUND, 3, ch + 1, note, vel, 0);
}

void sf_io_midi_off(uint8_t ch, uint8_t note) {
    op(E_OP_EX_M_NO_POUND, 2, ch + 1, note, 0, 0);
}

void sf_io_midi_cc(uint8_t ch, uint8_t cc, uint8_t val) {
    op(E_OP_EX_M_CC_POUND, 3, ch + 1, cc > 127 ? 127 : cc, val, 0);
}

void sf_io_midi_pb(uint8_t ch, uint8_t lsb, uint8_t msb) {
    // EX.M.PB only has a "current channel" form and sends its argument as
    // the two raw data bytes (low byte, high byte). Select the channel,
    // send, and put the user's EX.M.CH back.
    process_result_t r = op(E_OP_EX_M_CH, 0, 0, 0, 0, 0);
    op(E_OP_EX_M_CH, 1, ch + 1, 0, 0, 0);
    op(E_OP_EX_M_PB, 1, (int16_t)(lsb | (msb << 8)), 0, 0, 0);
    if (r.has_value) op(E_OP_EX_M_CH, 1, r.value, 0, 0, 0);
}

////////////////////////////////////////////////////////////////////////////
// Disting EX voices (the N operator): EX.NOTE / EX.NOTE.O, the stock ops.
//
// EX.NOTE takes a pitch, not a note number: 1 V/octave in Teletype units
// (16384 = 10 V, so a semitone is 16384 / 120), with 0 V = MIDI note 48, the
// Disting EX's own convention (its manual: "0 Volts corresponding to MIDI
// note 48"). The stock op derives the note id from the pitch
// (calculate_note), so sf_ex_pitch(n) gives back exactly n. Velocity is
// 0..16384; Orca's 0-g maps onto it as it maps onto MIDI 0-127.

// the stock op's calculate_note (src/ops/disting.c), to check against
static int16_t ex_note_of(int16_t pitch) {
    int32_t n = ((int32_t)pitch * 240) / 16384;
    n = n / 2 + (n & 1) + 48;
    return n < 0 ? 0 : n > 127 ? 127 : n;
}

int16_t sf_ex_pitch(int16_t note) {
    int32_t semis = (int32_t)note - 48;
    int32_t p = semis * 16384;
    p = p >= 0 ? (p + 60) / 120 : -((-p + 60) / 120);  // rounded
    // calculate_note rounds by truncating toward zero, which below 0 V
    // (note 48) can land one note high: nudge the pitch by a unit (well
    // under a cent) until the note id the Disting gets is the note
    for (int8_t d = 0; d < 4; d++) {
        if (ex_note_of(p - d) == note) return p - d;
        if (ex_note_of(p + d) == note) return p + d;
    }
    return (int16_t)p;
}

int16_t sf_ex_velocity(int16_t v) {
    if (v < 0) v = 0;
    return v >= 16 ? 16384 : v * 1024;
}

////////////////////////////////////////////////////////////////////////////
// Just Friends (JF.* ops). Pitch is 1 V/octave relative to Just Friends' own
// tuning: Orca's octave 4 C (MIDI 72) = 0 V = the note its TIME knob sets, so
// octave 3 is a volt down and octave 5 a volt up. Velocity is volts: Orca
// 0-g = 0-8 V in half-volt steps (5 V, a, is a strong ordinary note).

int16_t sf_jf_pitch(int16_t note) {
    int32_t p = ((int32_t)note - 72) * 16384;
    return (int16_t)(p >= 0 ? (p + 60) / 120 : -((-p + 60) / 120));
}

int16_t sf_jf_velocity(int16_t v) {
    if (v < 0) v = 0;
    if (v > 16) v = 16;
    return (int16_t)((int32_t)v * 8192 / 10);  // 10 = a = 5 V
}

void sf_io_jf_note(uint8_t note, uint8_t vel) {
    op(E_OP_JF_NOTE, 2, sf_jf_pitch(note), sf_jf_velocity(vel), 0, 0);
}

void sf_io_jf_tr(uint8_t ch, uint8_t state) {
    op(E_OP_JF_TR, 2, ch, state, 0, 0);
}

void sf_io_ex_note(uint8_t note, uint8_t vel, bool on) {
    if (on)
        op(E_OP_EX_NOTE, 2, sf_ex_pitch(note), sf_ex_velocity(vel), 0, 0);
    else
        op(E_OP_EX_NOTE_O, 1, sf_ex_pitch(note), 0, 0, 0);
}

////////////////////////////////////////////////////////////////////////////
// triggers and CV. out 1-4 = Teletype, 10-35 = ER-301 port 1-26

void sf_io_trig(uint8_t out, uint16_t ms) {
    if (out >= 1 && out <= 4) {
        uint8_t i = out - 1;
        // what TR.PULSE does, with our own length so the scene's TR.TIME
        // is left alone
        scene_state.variables.tr[i] = scene_state.variables.tr_pol[i];
        tele_tr(i, scene_state.variables.tr[i]);
        tele_tr_pulse(i, ms);
    }
    else if (out >= 10 && out <= 35) {
        uint8_t i = out - 9;
        int16_t port = er_port(i);
        if (er_time[i] != ms) {
            er_time[i] = ms;
            op(E_OP_SC_TR_TIME, 2, port, ms, 0, 0);
        }
        op(E_OP_SC_TR_PULSE, 1, port, 0, 0, 0);
    }
}

void sf_io_gate(uint8_t out, bool high) {
    if (out >= 1 && out <= 4) {
        uint8_t i = out - 1;
        tele_tr_pulse_clear(i);
        scene_state.variables.tr[i] = high ? scene_state.variables.tr_pol[i]
                                           : !scene_state.variables.tr_pol[i];
        tele_tr(i, scene_state.variables.tr[i]);
    }
    else if (out >= 10 && out <= 35)
        op(E_OP_SC_TR, 2, er_port(out - 9), high ? 1 : 0, 0, 0);
}

void sf_io_cv(uint8_t out, int16_t value, uint16_t slew_ms) {
    if (out >= 1 && out <= 4) {
        uint8_t i = out - 1;
        scene_state.variables.cv[i] = value;
        // slew for this move only; the scene's CV.SLEW is put back after
        tele_cv_slew(i, slew_ms);
        tele_cv(i, value, slew_ms > 0);
        tele_cv_slew(i, scene_state.variables.cv_slew[i]);
    }
    else if (out >= 10 && out <= 35) {
        const uint8_t i = out - 9;
        int16_t port = er_port(i);
        if (er_slew[i] != slew_ms + 1) {
            er_slew[i] = slew_ms + 1;
            op(E_OP_SC_CV_SLEW, 2, port, slew_ms, 0, 0);
        }
        op(E_OP_SC_CV, 2, port, value, 0, 0);
    }
}

void sf_io_script(uint8_t n) {
    // 1-8, 9 = metro, 10 = init (the same numbering as the SCRIPT op)
    op(E_OP_SCRIPT, 1, n, 0, 0, 0);
}

////////////////////////////////////////////////////////////////////////////
// > Disting EX and < ER-301 operators
//
// The selector cell picks the op; the operand cells after it are read as
// sf_dev_format() lays them out (sf_core.c). Every op is sent by running the
// real Teletype op, so the I2C bytes are the same as typing it in a script.

static int8_t vbase(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;  // empty
}

typedef struct {
    int16_t v[6];   // decoded operands, -1 = empty
    char a[6];      // action letters (lowercase)
} args_t;

static void decode(const char *fmt, const char *c, args_t *o) {
    uint8_t k = 0, i = 0;
    for (; *fmt && i < 6; fmt++, i++) {
        o->a[i] = 0;
        switch (*fmt) {
            case 'w': {
                // two cells, base 36: "01" = 1, "3c" = 120
                int8_t hi = vbase(c[k]), lo = vbase(c[k + 1]);
                o->v[i] = (hi < 0 && lo < 0) ? -1 : (hi < 0 ? 0 : hi) * 36 + (lo < 0 ? 0 : lo);
                k += 2;
                break;
            }
            case 'N': {
                int8_t oct = vbase(c[k]);
                int16_t id = sf_note_id(c[k + 1], oct < 0 ? 4 : (oct > 8 ? 8 : oct));
                o->v[i] = id;
                k += 2;
                break;
            }
            case 'v': {
                // Orca velocity 0-g -> the Disting EX's 0..16384, empty = f
                // (as : does). Only the Disting voice ops use it.
                int8_t v = vbase(c[k++]);
                o->a[i] = v < 0;  // remembers "empty"
                if (v < 0) v = 15;
                o->v[i] = sf_ex_velocity(v);
                break;
            }
            case 'a':
                o->a[i] = (c[k] >= 'A' && c[k] <= 'Z') ? c[k] + 32 : c[k];
                o->v[i] = c[k] == '.' ? -1 : 0;
                k++;
                break;
            case 'V': {
                // Just Friends velocity: Orca 0-g = 0-8 V, empty = a (5 V)
                int8_t v = vbase(c[k++]);
                o->v[i] = sf_jf_velocity(v < 0 ? 10 : v);
                break;
            }
            case 'b': {
                // bipolar level: 0-z = -5..+5 V, i (18) and empty = 0 V
                int8_t v = vbase(c[k++]);
                o->v[i] = v < 0 ? 0 : (int16_t)(((int32_t)v - 18) * 8192 / 18);
                break;
            }
            default: o->v[i] = vbase(c[k++]); break;  // n, l, s
        }
    }
}

// level 0-z to 0..max
static int16_t lvl(int16_t v, int32_t max) {
    if (v < 0) v = 0;
    return (int16_t)((int32_t)v * max / 35);
}

// 1 V/octave from a MIDI note number, octave 0 C = 0 V (as the trigger ops)
static int16_t note_cv(int16_t id) {
    int16_t semis = id < 24 ? 0 : id - 24;
    int32_t cv = ((int32_t)semis * 16384 + 60) / 120;
    return cv > 16383 ? 16383 : cv;
}

static int8_t done(process_result_t r, int16_t *result) {
    if (r.has_value) {
        *result = r.value;
        return 1;
    }
    return 0;
}

static int8_t fail(const char *why) {
    sf_copy(sf_ii_error, why, sizeof(sf_ii_error));
    return -1;
}

// side: empty or 0 = single algorithm, 1 = left, 2 = right
#define SIDE(s, single, left, right) ((s) == 1 ? (left) : (s) == 2 ? (right) : (single))

static int8_t disting(char sel, const args_t *g, int16_t *res) {
    const int16_t *v = g->v;
    switch (sel) {
        // presets: preset, side. preset empty = read the current one
        case 'l': {
            int16_t o = SIDE(v[1], E_OP_EX_PRESET, E_OP_EX_PRE1, E_OP_EX_PRE2);
            if (v[0] < 0) {
                if (o != E_OP_EX_PRESET) return fail("NO PRESET");
                return done(op(o, 0, 0, 0, 0, 0), res);
            }
            return done(op(o, 1, v[0], 0, 0, 0), res);
        }
        case 's':
            return done(op(SIDE(v[1], E_OP_EX_SAVE, E_OP_EX_SAVE1, E_OP_EX_SAVE2), 1,
                           v[0] < 0 ? 0 : v[0], 0, 0, 0), res);
        case 'r': return done(op(E_OP_EX_RESET, 0, 0, 0, 0, 0), res);
        // algorithm, side, algorithm 2 (side 3: both dual algorithms at once)
        case 'a': {
            if (v[1] == 3) {
                if (v[0] < 0 || v[2] < 0) return fail("TWO ALGORITHMS");
                return done(op(E_OP_EX_A12, 2, v[0], v[2], 0, 0), res);
            }
            int16_t o = SIDE(v[1], E_OP_EX_ALG, E_OP_EX_A1, E_OP_EX_A2);
            if (v[0] < 0) return done(op(o, 0, 0, 0, 0, 0), res);
            return done(op(o, 1, v[0], 0, 0, 0), res);
        }
        // parameters, as Teletype's EX.P family: param, value, side
        // >p = EX.P (the parameter's own value; empty = read it)
        case 'p': {
            if (v[0] < 0) return fail("NO PARAM");
            int16_t o = SIDE(v[2], E_OP_EX_PARAM, E_OP_EX_P1, E_OP_EX_P2);
            if (v[1] < 0) return done(op(o, 1, v[0], 0, 0, 0), res);
            return done(op(o, 2, v[0], v[1], 0, 0), res);
        }
        // >v = EX.PV (a level 0-z across the parameter's min..max)
        case 'v': {
            if (v[0] < 0) return fail("NO PARAM");
            int16_t o = SIDE(v[2], E_OP_EX_PV, E_OP_EX_PV1, E_OP_EX_PV2);
            return done(op(o, 2, v[0], lvl(v[1], 16384), 0, 0), res);
        }
        case 'i':
        case 'j': {
            if (v[0] < 0) return fail("NO PARAM");
            int16_t o = sel == 'i' ? SIDE(v[1], E_OP_EX_MIN, E_OP_EX_MIN1, E_OP_EX_MIN2)
                                   : SIDE(v[1], E_OP_EX_MAX, E_OP_EX_MAX1, E_OP_EX_MAX2);
            return done(op(o, 1, v[0], 0, 0, 0), res);
        }
        case 'c': return done(op(E_OP_EX_CTRL, 2, v[0] < 0 ? 0 : v[0], lvl(v[1], 16384), 0, 0), res);
        case 'z': {
            if (v[0] != 1 && v[0] != 2) return fail("SIDE 1 OR 2");
            if (v[1] < 0) return done(op(v[0] == 1 ? E_OP_EX_ZO1 : E_OP_EX_ZO2, 0, 0, 0, 0, 0), res);
            return done(op(v[0] == 1 ? E_OP_EX_Z1 : E_OP_EX_Z2, 1, lvl(v[1], 127), 0, 0, 0), res);
        }
        case 'k': return done(op(E_OP_EX_CH, 1, v[0] < 0 ? 1 : v[0], 0, 0, 0), res);
        // N (note, velocity): grid operators time it (sf_core.c); sent
        // directly it is a note on, as EX.NOTE
        case 'n':
            if (v[0] < 0) return fail("NO NOTE");
            return done(op(E_OP_EX_NOTE, 2, sf_ex_pitch(v[0]), v[1], 0, 0), res);
        // note on with a channel (EX.N#): channel 0-f = 1-16, empty = EX.NOTE
        case 'd':
            if (v[1] < 0) return fail("NO NOTE");
            if (v[0] < 0) return done(op(E_OP_EX_NOTE, 2, sf_ex_pitch(v[1]), v[2], 0, 0), res);
            return done(op(E_OP_EX_N_POUND, 3, v[0] + 1, sf_ex_pitch(v[1]), v[2], 0), res);
        case 'o':
            if (v[1] < 0) return fail("NO NOTE");
            if (v[0] < 0) return done(op(E_OP_EX_NOTE_O, 1, sf_ex_pitch(v[1]), 0, 0, 0), res);
            return done(op(E_OP_EX_NO_POUND, 2, v[0] + 1, sf_ex_pitch(v[1]), 0, 0), res);
        case 'x': return done(op(E_OP_EX_ALLOFF, 0, 0, 0, 0, 0), res);
        case 'w': {
            int16_t voice = v[0] < 0 ? 0 : v[0];
            if (v[1] < 0) return fail("NO NOTE");
            // velocity 0 = voice off
            if (v[2] == 0) return done(op(E_OP_EX_VOX_O, 1, voice, 0, 0, 0), res);
            return done(op(E_OP_EX_VOX, 3, voice, sf_ex_pitch(v[1]), v[2], 0), res);
        }
        case 'f':
            if (v[1] < 0) return fail("NO NOTE");
            return done(op(E_OP_EX_VOX_P, 2, v[0] < 0 ? 0 : v[0], sf_ex_pitch(v[1]), 0, 0), res);
        case 't':
            if (v[0] < 0) return fail("NO VOICE");
            // empty velocity: EX.T (medium velocity)
            if (g->a[1]) return done(op(E_OP_EX_T, 1, v[0], 0, 0, 0), res);
            return done(op(E_OP_EX_TV, 2, v[0], v[1], 0, 0), res);
        case 'e': return done(op(E_OP_EX_REC, 1, v[0] > 0, 0, 0, 0), res);
        case 'y': return done(op(E_OP_EX_PLAY, 1, v[0] > 0, 0, 0, 0), res);
        case 'g':
            if (v[0] < 0) return fail("NO NOTE");
            return done(op(E_OP_EX_AL_P, 1, note_cv(v[0]), 0, 0, 0), res);
        case 'h': return done(op(E_OP_EX_AL_CLK, 0, 0, 0, 0, 0), res);
        case 'q': {
            int16_t n = v[1] < 0 ? 1 : v[1], o;
            switch (g->a[0]) {
                case 'r': o = E_OP_EX_LP_REC; break;
                case 'p': o = E_OP_EX_LP_PLAY; break;
                case 'c': o = E_OP_EX_LP_CLR; break;
                case 'v': o = E_OP_EX_LP_REV; break;
                case 'd': o = E_OP_EX_LP_DOWN; break;
                case 's': o = E_OP_EX_LP; break;
                case 'i': o = E_OP_EX_LP_REVQ; break;
                case 'j': o = E_OP_EX_LP_DOWNQ; break;
                default: return fail("LOOP: RPCVDSIJ");
            }
            return done(op(o, 1, n, 0, 0, 0), res);
        }
        case 'm':
            switch (g->a[0]) {
                case 'p': return done(op(E_OP_EX_M_PRG, 1, v[1] < 0 ? 0 : v[1], 0, 0, 0), res);
                case 'c': return done(op(E_OP_EX_M_CLK, 0, 0, 0, 0, 0), res);
                case 's': return done(op(E_OP_EX_M_START, 0, 0, 0, 0, 0), res);
                case 't': return done(op(E_OP_EX_M_STOP, 0, 0, 0, 0, 0), res);
                case 'o': return done(op(E_OP_EX_M_CONT, 0, 0, 0, 0, 0), res);
                case 'h':
                    if (v[1] < 0) return done(op(E_OP_EX_M_CH, 0, 0, 0, 0, 0), res);
                    return done(op(E_OP_EX_M_CH, 1, v[1], 0, 0, 0), res);
                // these are what : ! and ? send on a channel; here on the
                // EX.M.CH channel
                case 'n': return done(op(E_OP_EX_M_N, 2, v[1] < 0 ? 60 : v[1], v[2] < 0 ? 100 : v[2], 0, 0), res);
                case 'f': return done(op(E_OP_EX_M_NO, 1, v[1] < 0 ? 60 : v[1], 0, 0, 0), res);
                case 'e': return done(op(E_OP_EX_M_CC, 2, v[1] < 0 ? 0 : v[1], v[2] < 0 ? 0 : v[2], 0, 0), res);
                case 'w': return done(op(E_OP_EX_M_PB, 1, v[1] < 0 ? 0 : v[1], 0, 0, 0), res);
            }
            return fail("MIDI: PCSTOHNFEW");
        case 'b': {
            int16_t a = v[1] < 0 ? 0 : v[1], b = v[2] < 0 ? 0 : v[2];
            switch (g->a[0]) {
                case 'n': return done(op(E_OP_EX_SB_N, 2, a, v[2] < 0 ? 100 : b, 0, 0), res);
                case 'f': return done(op(E_OP_EX_SB_NO, 1, a, 0, 0, 0), res);
                case 'e': return done(op(E_OP_EX_SB_CC, 2, a, b, 0, 0), res);
                case 'w': return done(op(E_OP_EX_SB_PB, 1, a, 0, 0, 0), res);
                case 'p': return done(op(E_OP_EX_SB_PRG, 1, a, 0, 0, 0), res);
                case 'c': return done(op(E_OP_EX_SB_CLK, 0, 0, 0, 0, 0), res);
                case 's': return done(op(E_OP_EX_SB_START, 0, 0, 0, 0, 0), res);
                case 't': return done(op(E_OP_EX_SB_STOP, 0, 0, 0, 0, 0), res);
                case 'o': return done(op(E_OP_EX_SB_CONT, 0, 0, 0, 0, 0), res);
                case 'h':
                    if (v[1] < 0) return done(op(E_OP_EX_SB_CH, 0, 0, 0, 0, 0), res);
                    return done(op(E_OP_EX_SB_CH, 1, a, 0, 0, 0), res);
            }
            return fail("BUS: NFEWPCSTOH");
        }
    }
    return fail("UNKNOWN");
}

static int8_t er301(char sel, const args_t *g, int16_t *res) {
    const int16_t *v = g->v;
    if (v[0] < 1 || v[0] > 300) return fail("PORT 01-8C");
    int16_t port = er_port(v[0]);
    switch (sel) {
        case 't': return done(op(E_OP_SC_TR, 2, port, v[1] > 0, 0, 0), res);
        case 'p': return done(op(E_OP_SC_TR_PULSE, 1, port, 0, 0, 0), res);
        case 'g': return done(op(E_OP_SC_TR_TOG, 1, port, 0, 0, 0), res);
        case 'l': return done(op(E_OP_SC_TR_POL, 2, port, v[1] > 0, 0, 0), res);
        case 'm':
            if (v[0] <= 26) er_time[v[0]] = 0;  // (sf_io_trig sends its length again)
            return done(op(E_OP_SC_TR_TIME, 2, port, v[1] < 0 ? SF_PULSE_DEFAULT : v[1], 0, 0), res);
        case 'c': return done(op(E_OP_SC_CV, 2, port, lvl(v[1], 16383), 0, 0), res);
        case 'n':
            if (v[1] < 0) return fail("NO NOTE");
            return done(op(E_OP_SC_CV, 2, port, note_cv(v[1]), 0, 0), res);
        case 's': return done(op(E_OP_SC_CV_SET, 2, port, lvl(v[1], 16383), 0, 0), res);
        case 'o': return done(op(E_OP_SC_CV_OFF, 2, port, lvl(v[1], 16383), 0, 0), res);
        case 'w':
            if (v[0] <= 26) er_slew[v[0]] = 0;  // (sf_io_cv sends its slew again)
            return done(op(E_OP_SC_CV_SLEW, 2, port, v[1] < 0 ? 0 : v[1], 0, 0), res);
    }
    return fail("UNKNOWN");
}

// , Just Friends. Channels: 1-6, 0 = all six, 7-c = the second Just
// Friends' 1-6 (as Teletype), empty = 1
static int8_t justfriends(char sel, const args_t *g, int16_t *res) {
    const int16_t *v = g->v;
#define CH(i) (v[i] < 0 ? 1 : v[i] > 12 ? 12 : v[i])
#define ONOFF(i) (v[i] < 0 ? 1 : v[i] > 0)
    switch (sel) {
        case 'n':
            if (v[0] < 0) return fail("NO NOTE");
            return done(op(E_OP_JF_NOTE, 2, sf_jf_pitch(v[0]), v[1], 0, 0), res);
        case 'p':
            if (v[0] < 0) return fail("NO NOTE");
            return done(op(E_OP_JF_POLY, 2, sf_jf_pitch(v[0]), v[1], 0, 0), res);
        case 'x': return done(op(E_OP_JF_POLY_RESET, 0, 0, 0, 0, 0), res);
        case 'v':
            if (v[1] < 0) return fail("NO NOTE");
            return done(op(E_OP_JF_VOX, 3, CH(0), sf_jf_pitch(v[1]), v[2], 0), res);
        case 'f':
            if (v[1] < 0) return fail("NO NOTE");
            return done(op(E_OP_JF_PITCH, 2, CH(0), sf_jf_pitch(v[1]), 0, 0), res);
        case 'r': return done(op(E_OP_JF_VTR, 2, CH(0), v[1], 0, 0), res);
        case 't': return done(op(E_OP_JF_TR, 2, CH(0), ONOFF(1), 0, 0), res);
        case 's': return done(op(E_OP_JF_SHIFT, 1, v[0] < 0 ? 0 : sf_jf_pitch(v[0]), 0, 0, 0), res);
        case 'u': return done(op(E_OP_JF_RUN, 1, v[0], 0, 0, 0), res);
        case 'm': return done(op(E_OP_JF_MODE, 1, ONOFF(0), 0, 0, 0), res);
        case 'o': return done(op(E_OP_JF_RMODE, 1, ONOFF(0), 0, 0, 0), res);
        case 'd': return done(op(E_OP_JF_GOD, 1, ONOFF(0), 0, 0, 0), res);
        case 'q': return done(op(E_OP_JF_QT, 1, v[0] < 0 ? 0 : v[0], 0, 0, 0), res);
        case 'k':
            if (v[0] < 1 || v[0] > 255) return fail("TICK 01-73");
            return done(op(E_OP_JF_TICK, 1, v[0], 0, 0, 0), res);
        case 'a':
            if (v[0] != 1 && v[0] != 2) return fail("ADDRESS 1 OR 2");
            return done(op(E_OP_JF_ADDR, 1, v[0], 0, 0, 0), res);
        case 'e':
            if (v[0] != 1 && v[0] != 2) return fail("JF 1 OR 2");
            return done(op(E_OP_JF_SEL, 1, v[0], 0, 0, 0), res);
        case 'j':
            return done(op(E_OP_JF_TUNE, 3, CH(0), v[1] < 1 ? 1 : v[1], v[2] < 1 ? 1 : v[2], 0), res);
        case 'g': {
            // readers (Just Friends firmware 4): s speed (0 shape, 1 sound),
            // t mode (0 transient, 1 sustain, 2 cycle) as they are; the knobs
            // r ramp, c curve, f fm, i time, o intone as 0-z over 0-10 V
            int16_t o;
            bool knob = true;
            switch (g->a[0]) {
                case 's': o = E_OP_JF_SPEED; knob = false; break;
                case 't': o = E_OP_JF_TSC; knob = false; break;
                case 'r': o = E_OP_JF_RAMP; break;
                case 'c': o = E_OP_JF_CURVE; break;
                case 'f': o = E_OP_JF_FM; break;
                case 'i': o = E_OP_JF_TIME; break;
                case 'o': o = E_OP_JF_INTONE; break;
                default: return fail("READ: STRCFIO");
            }
            int8_t r = done(op(o, 0, 0, 0, 0, 0), res);
            if (r == 1 && knob) {
                int32_t k = (int32_t)*res * 35 / 16383;
                *res = (int16_t)(k < 0 ? 0 : k > 35 ? 35 : k);
            }
            return r;
        }
    }
#undef CH
#undef ONOFF
    return fail("UNKNOWN");
}

int8_t sf_io_dev(char dev, char sel, const char *cells, int16_t *result) {
    const char *fmt = sf_dev_format(dev, sel);
    sf_ii_error[0] = 0;
    if (!fmt || sel == '@') return fail("UNKNOWN");  // >@ is timed by sf_core.c
    args_t g;
    memset(&g, 0, sizeof(g));
    decode(fmt, cells, &g);
    if (dev == ',') return justfriends(sel, &g, result);
    return dev == '>' ? disting(sel, &g, result) : er301(sel, &g, result);
}

////////////////////////////////////////////////////////////////////////////
// device scan: which Disting EX units and ER-301s answer on the bus
//
// The Teletype is the I2C leader. A follower that is not there does not
// acknowledge its address; the TWI driver reports that (and gives up after
// a bounded wait on a dead bus, so the scan never hangs the grid). The
// Disting EX answers reads: EX.ALG's getter (0x45, 1 byte back) and
// EX.PRESET's (0x43, 2 bytes). The ER-301 acknowledges a read of its
// address. Shared by the firmware and the emulator: only sf_io_ii_tx/rx
// (the raw bus) differ.

static bool ex_probe(uint8_t unit, int16_t *alg, int16_t *preset) {
    const uint8_t addr = disting_ex_base + unit - 1;  // the EX address + unit - 1
    uint8_t d[2] = { 0x45, 0 };            // the EX.ALG getter
    if (!sf_io_ii_tx(addr, d, 1)) return false;
    d[0] = 0;
    sf_io_ii_rx(addr, d, 1);
    *alg = d[0];
    d[0] = 0x43;  // the EX.PRESET getter
    sf_io_ii_tx(addr, d, 1);
    d[0] = d[1] = 0;
    sf_io_ii_rx(addr, d, 2);
    *preset = (d[0] << 8) + d[1];
    return true;
}

void sf_scan_er_known(uint8_t mask) { er_seen = mask & 7; }

// The ER-301 is not read: stock Teletype only ever writes to it, and a read
// it acknowledges but never answers left the TWI driver stuck busy (every later
// message failed with BUSY; hardware 2026-11-20). It counts as found once an SC
// write to it was acknowledged (sf_ii_note).
static bool er_probe(uint8_t n) {
    return (er_seen >> (n - 1)) & 1;
}

// The addresses are fixed (the user's rack, 2026-11-20): one Disting EX at
// 0x41 (unit 1) and one ER-301 at 0x31. Nothing to configure.
#define SF_EX_ADDR 0x41
void sf_scan_run(void) {
    scanning = true;
    memset(&sf_scan, 0, sizeof(sf_scan));
    disting_ex_base = SF_EX_ADDR;
    sf_scan.ex_addr = SF_EX_ADDR;
    sf_scan.ex[0] = ex_probe(1, &sf_scan.ex_alg[0], &sf_scan.ex_preset[0]);
    sf_scan.er[0] = er_probe(1);
    {
        // Just Friends: one byte, the JF.SPEED request (0x8F), and no read
        // after it (a read a device never answers is what once left the
        // bus stuck): an acknowledged address is a Just Friends there. It
        // changes nothing on the module, whatever its firmware
        const uint8_t d[1] = { 0x8F };
        sf_jf_state = sf_io_ii_tx(0x70, d, 1) ? 1 : 2;
    }
    sf_scan.done = 1;
    scanning = false;
    sf_scan.ex_unit = sf_scan.ex[0] ? 1 : 0;
    op(E_OP_EX, 1, 1, 0, 0, 0);  // the stock EX ops' unit: always unit 1 (0x41)
    sf_scan.er_unit = sf_scan.er[0] ? 1 : 0;
}

bool sf_scan_ex_here(void) {
    return sf_scan.ex_unit && sf_scan.ex[sf_scan.ex_unit - 1];
}

bool sf_boot_preset(void) {
    const int16_t p = sf_config_boot_preset();
    if (p < 0 || !sf_scan_ex_here()) return false;  // not set, or no Disting EX: send nothing
    op(E_OP_EX_PRESET, 1, p, 0, 0, 0);
    return true;
}

// ER-301 port as the user writes it (1-100 = the first ER-301 found)
static int16_t er_port(int16_t port) {
    if (port >= 1 && port <= 100 && sf_scan.er_unit > 1) port += (sf_scan.er_unit - 1) * 100;
    return port;
}

void sf_ii_reset(void) {
    memset(er_time, 0, sizeof(er_time));
    memset(er_slew, 0, sizeof(er_slew));
}
