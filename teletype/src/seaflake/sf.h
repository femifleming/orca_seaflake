// Orca Seaflake for Teletype: the grid engine.
//
// This is a C port of the Orca Seaflake engine (terminal/src/core in the
// orca_seaflake repo). It knows nothing about the Teletype hardware: every
// output goes through the sf_io_* functions declared at the bottom, which
// the firmware (sf_tt.c) and the host tests each implement.

#ifndef _SEAFLAKE_H_
#define _SEAFLAKE_H_

#include <stdbool.h>
#include <stdint.h>

#define SF_W 80
#define SF_H 40
#define SF_CELLS (SF_W * SF_H)

#define SF_PATCH_SLOTS 32
#define SF_SONG_SLOTS 8
#define SF_SONG_LEN 32
// most bytes a packed patch may take in its flash slot
#define SF_PACK_MAX 1020

#define SF_BPM_MIN 60
#define SF_BPM_MAX 300
#define SF_PULSE_DEFAULT 20

#define SF_NOTES 16
#define SF_SEQS 6
#define SF_BURSTS 6
#define SF_OFFS 16

#define SF_SONG_MAGIC 0x5F53
enum { SF_ORD = 0, SF_REV = 1, SF_RND = 2 };

typedef struct {
    uint8_t patch;  // 0 = empty slot, otherwise patch number 1..32
    uint8_t pad;
    uint16_t len;
    uint16_t mul;
    uint16_t bpm;
} sf_song_slot_t;

typedef struct {
    uint16_t magic;
    uint8_t mode;
    uint8_t loop;
    sf_song_slot_t slots[SF_SONG_LEN];
} sf_song_t;

typedef struct {
    uint8_t active, played, ch, note, vel, bent;
    int8_t len;
    uint8_t ex;  // 1: a Disting EX voice (EX.NOTE), 0: MIDI (EX.M.N#)
    uint16_t pb;
} sf_note_t;

// what a shaped sequence (@ { } and >@) plays at each step
enum { SF_SEQ_MIDI = 0, SF_SEQ_TRIG_CV = 1, SF_SEQ_TRIG = 2, SF_SEQ_EX = 3, SF_SEQ_JF = 4 };
// what a timed off (sf_off_t.ex) sends: 0 MIDI note off, 1 Disting EX note off,
// SF_OFF_JF_TR = JF.TR channel 0 (the end of a , t trigger)
enum { SF_OFF_JF_TR = 2 };

typedef struct {
    uint8_t active, trig, x, y, ch, note, vel, shape, count, idx, pw;  // trig: SF_SEQ_*
    int16_t cv;
    uint32_t start, dur;
} sf_seq_t;

typedef struct {
    uint8_t active, x, y, count, idx;
    uint32_t start, interval_us;
} sf_burst_t;

typedef struct {
    uint8_t active, ch, note, ex;
    uint32_t due;
} sf_off_t;

typedef struct {
    char grid[SF_CELLS];
    char snap[SF_CELLS];  // the grid as it was when the frame started
    uint8_t lock[SF_CELLS / 8];
    uint8_t port[SF_CELLS / 8];   // cells an operator picked this frame (T's value, O's read...)

    uint32_t f;        // frame counter
    uint16_t bpm;
    uint16_t bpm_directive;
    uint8_t playing;
    uint8_t pulse_ms;  // length of one trigger pulse unit
    uint8_t dirty;     // something on screen changed
    uint8_t modified;  // grid edited since last save/load
    uint8_t patch;     // patch slot the grid came from (1..32), 0 = none
    uint8_t error;     // last ; or = message could not be sent
    // the ` loadbang: 1 = bang on the next frame (a patch was loaded),
    // 2 = power-on, waiting for the I2C devices to answer first
    uint8_t loadbang;

    // variables: one glyph each, or a 12 bit scale mask (see + operator)
    char var[96];
    uint16_t mask[96];

    sf_note_t notes[SF_NOTES];
    sf_note_t mono[16];
    int8_t gate[36];    // frames left on a held gate, per output
    int16_t cv_note[36];
    int16_t cv_bend[36];

    uint32_t now;       // milliseconds
    uint32_t next_due;  // when sf_ms next has work, 0xFFFFFFFF = never
    sf_seq_t seqs[SF_SEQS];
    sf_burst_t bursts[SF_BURSTS];
    sf_off_t offs[SF_OFFS];

    sf_song_t song;
    uint8_t song_no;       // song slot the song came from (1..8), 0 = none
    uint8_t song_playing;
    uint8_t song_modified;
    uint8_t order[SF_SONG_LEN];
    uint8_t order_len, order_idx;
    uint32_t song_pos;

    uint32_t rng;
} sf_t;

extern sf_t sf;

void sf_init(void);
void sf_clear(void);  // empty grid, frame 0
// run one frame. now = current time in ms
void sf_frame(uint32_t now);
// run bursts, shaped sequences and their note-offs that are due
void sf_ms(uint32_t now);
// microseconds per frame at the current tempo
uint32_t sf_frame_us(void);

void sf_set_bpm(int16_t bpm);
void sf_play(bool on);
// all notes off, gates low, cancel bursts and sequences
void sf_silence(void);

// editing
char sf_get(int16_t x, int16_t y);
void sf_set(int16_t x, int16_t y, char g);
bool sf_is_op(char g);
bool sf_locked(int16_t x, int16_t y);
// the cell an operator is reading or writing by its key or offset this frame:
// T's selected value, O's read cell, P's slot, Q's read cells (as Orca shows them)
bool sf_picked(int16_t x, int16_t y);
const char *sf_op_name(char g);
// what the cell at x,y is, for the status line: the inlet/outlet of the
// operator that uses it ("D RATE", "EX PARAM"), or the operator itself
// ("DISTING PRESET"). NULL for a cell nothing uses
const char *sf_cell_name(int16_t x, int16_t y, char *buf, uint8_t max);
// note letter + octave digit to MIDI note number, -1 if not a note
int16_t sf_note_id(char note, int16_t octave);

// patches. sf_pack returns the packed size, or 0 if it does not fit in max
uint16_t sf_pack(uint8_t *dst, uint16_t max);
void sf_unpack(const uint8_t *src, uint16_t len);
// the BPM a packed patch was saved with (0: an older patch without one), and
// where its grid begins
int16_t sf_patch_bpm(const uint8_t *src, uint16_t len, uint16_t *start);
// unpack into any 80 x 40 buffer (not the live grid)
void sf_unpack_to(char *grid, const uint8_t *src, uint16_t len);

// song mode
void sf_song_clear(void);
void sf_song_start(void);
void sf_song_stop(void);
// load a song slot's patch and tempo onto the grid
bool sf_song_load_slot(uint8_t i);
uint32_t sf_song_slot_frames(uint8_t i);

// run a $ command, e.g. "bpm:140". returns false if not understood
bool sf_command(const char *msg, uint8_t len);

////////////////////////////////////////////////////////////////////////////
// outputs, implemented by the host (firmware: sf_tt.c)

// a Disting EX voice (the N operator >n and >@): note on / off through
// EX.NOTE / EX.NOTE.O on the Disting EX the scan found. note = MIDI note
// number, vel = Orca velocity 0-16 (g)
void sf_io_ex_note(uint8_t note, uint8_t vel, bool on);
// Just Friends (JF.NOTE / JF.TR, the stock ops): a note at a MIDI note number
// (4C = 72 = 0 V, Just Friends' own pitch) and Orca velocity 0-16 (0-8 V);
// a trigger channel (1-6, 0 all, 7-12 the second JF) to a state
void sf_io_jf_note(uint8_t note, uint8_t vel);
void sf_io_jf_tr(uint8_t ch, uint8_t state);
// MIDI, channel 0..15
void sf_io_midi_on(uint8_t ch, uint8_t note, uint8_t vel);
void sf_io_midi_off(uint8_t ch, uint8_t note);
void sf_io_midi_cc(uint8_t ch, uint8_t cc, uint8_t val);
void sf_io_midi_pb(uint8_t ch, uint8_t lsb, uint8_t msb);
// outputs: 1..4 = Teletype TR/CV 1..4, 10..35 (a..z) = ER-301 port 1..26
void sf_io_trig(uint8_t out, uint16_t ms);
void sf_io_gate(uint8_t out, bool high);
void sf_io_cv(uint8_t out, int16_t value, uint16_t slew_ms);
// a > (Disting EX) or < (ER-301) operator: sel = its selector cell, cells =
// the operand cells after it (raw glyphs, '.' = empty), as many as
// sf_dev_format(dev, sel) asks for. returns -1 on error, 0 when sent, 1 when
// the op answered with a value in *result
int8_t sf_io_dev(char dev, char sel, const char *cells, int16_t *result);
// the operand layout of a > or < selector, NULL if the selector is unknown.
// one letter per operand: n = number (1 cell), w = wide number (2 cells,
// 0-1295), N = note (2 cells: octave, note), v = velocity (1 cell), l =
// level 0-z (1 cell), s = side (1 cell, last: empty = single algorithm),
// a = action letter (1 cell). The selector may also be @ (>@, N with @)
const char *sf_dev_format(char dev, char sel);
uint8_t sf_dev_cells(const char *format);
// the raw I2C bus, for the device scan (sf_scan_run in sf_ii.c): true if
// the follower at addr acknowledged. The firmware uses the TWI driver; the
// emulator answers for the devices it pretends are there.
bool sf_io_ii_tx(uint8_t addr, const uint8_t *data, uint8_t len);
bool sf_io_ii_rx(uint8_t addr, uint8_t *data, uint8_t len);
// run Teletype script n (1..8, 9 = metro, 10 = init)
void sf_io_script(uint8_t n);
// load patch slot n (1..32) onto the grid. false if the slot is empty
bool sf_io_patch_load(uint8_t n);

#endif
