#ifndef _TT_HOST_H_
#define _TT_HOST_H_

// Pilot Sealflake: runs the Teletype firmware (src/ + module/) on the desktop.
// This replaces module/main.c: timers, the key dispatcher, the OLED and the
// scene flash. None of these functions lock; the C++ side (TeletypeHost)
// serializes every call with one mutex.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TT_SCREEN_W 128
#define TT_SCREEN_H 64

// scene_dir holds tt00s.txt .. tt31s.txt (Teletype USB stick format)
void tt_init(const char *scene_dir, uint32_t now_ms);
void tt_tick(uint32_t now_ms);  // call every millisecond

// keyboard: USB HID usage codes + HID modifier bits, like the hardware
void tt_key(uint8_t hid_key, uint8_t hid_mod, bool is_held_key);
void tt_key_release(uint8_t hid_key, uint8_t hid_mod);

// draws the current mode into the frame buffer; returns true if it changed
bool tt_refresh(void);
const uint8_t *tt_framebuffer(void);  // 128 x 64, one byte per pixel, 0-15
int tt_mode(void);
int tt_live_submode(void);           // SUB_MODE_* (teletype_io.h)                    // tele_mode_t (0 live, 1 edit, 2 pattern, 3 write, 4 read, 5 help)

// the screen as text (what the firmware drew with its font), per OLED line 0-7.
// x is in OLED pixels (0-127); fg / bg are OLED brightness 0-15; hi = index of a
// highlighted character (the line editor's cursor) or -1.
typedef struct {
    int16_t x;
    uint8_t w, fg, bg, hid, right;  // right: drawn right-aligned (ends at x + w)
    int8_t hi;
    char text[48];
    uint8_t cfg[48];  // brightness of each character (text drawn on top of other text)
} tt_seg_t;
int tt_screen_segments(int line, tt_seg_t *out, int max);
uint8_t tt_screen_line_bg(int line);
const uint8_t *tt_text_mask(void);  // 128 x 64: 1 where text is (the rest is graphics)

// one live-mode command (Pilot's command line). Returns 0 = ok, no value,
// 1 = ok with a value in *value, -1 = error (message in err, 64 bytes)
int tt_run_command(const char *text, int16_t *value, char *err);
bool tt_is_known_op(const char *word);  // word is a Teletype op or mod

// whole-scene text (USB stick format), for plugin state
size_t tt_scene_serialize(char *buf, size_t cap);
void tt_scene_deserialize(const char *text, bool run_init);
uint8_t tt_scene_number(void);

// MIDI input (MI.* ops and their scripts)
void tt_midi_note_on(uint8_t ch, uint8_t num, uint8_t vel);
void tt_midi_note_off(uint8_t ch, uint8_t num, uint8_t vel);
void tt_midi_cc(uint8_t ch, uint8_t num, uint8_t val);
void tt_midi_clock(void);
void tt_midi_start(void);
void tt_midi_stop(void);
void tt_midi_continue(void);

// status for the host UI
bool tt_metro_active(void);
int16_t tt_metro_period(void);

// implemented by the host (C++)
uint32_t tt_hw_ticks(void);
void tt_hw_ii_tx(uint8_t addr, const uint8_t *data, uint8_t l);
void tt_hw_ii_rx(uint8_t addr, uint8_t *data, uint8_t l);
void tt_hw_kill(void);

#ifdef __cplusplus
}
#endif

#endif
