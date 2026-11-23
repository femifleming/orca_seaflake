// Orca Seaflake emulator: the Teletype firmware (src/ + module/) running on
// the Mac. tt_emu.c replaces module/main.c and module/flash.c.

#ifndef _TT_EMU_H_
#define _TT_EMU_H_

#include <stdbool.h>
#include <stdint.h>

#define EMU_W 128
#define EMU_H 64

// dir holds the Teletype scenes (tt00s.txt ...) and seaflake.bin
void emu_init(const char *dir, uint32_t now_ms);
void emu_tick(uint32_t now_ms);  // call at least every millisecond
// USB HID key code + HID modifier bits, like the module's keyboard
void emu_key(uint8_t hid_key, uint8_t hid_mod, bool held);
void emu_key_release(uint8_t hid_key, uint8_t hid_mod);
// the front-panel button pressed, the PARAM knob turned (0..4095)
void emu_front(void);
void emu_knob(uint16_t value);
bool emu_refresh(void);
// how long nothing has been pressed (ms): set it to 600000 to start the screensaver now
void emu_set_idle(uint32_t ms);              // draw; true if the screen changed
const uint8_t *emu_framebuffer(void);  // 128 x 64, brightness 0-15
int emu_mode(void);

// outputs, for the panel
bool emu_tr(int i);         // Teletype TR 1-4 (0-3)
int16_t emu_cv(int i);      // Teletype CV 1-4 (0-3), 0-16383 = 0-10 V
bool emu_er_tr(int port);   // ER-301 SC.TR port 1-26
int16_t emu_er_cv(int port);
uint32_t emu_er_seen(int port);  // last time the port got a message (ms)

// log lines: kind 0 = Teletype command sent by Seaflake, 1 = I2C bytes,
// 2 = MIDI, 3 = message
typedef void (*emu_log_fn)(int kind, const char *text);
void emu_set_log(emu_log_fn fn);
// raw MIDI bytes that went to a Disting EX (EX.M.*), for the virtual port
typedef void (*emu_midi_fn)(const uint8_t *bytes, int n);
void emu_set_midi(emu_midi_fn fn);

// import SFSCAN.TXT / SFPATCH.TXT written by the module to a USB stick:
// from text, from a folder, or from whatever stick is mounted in /Volumes.
// summary gets a one-line result. returns 0 if nothing was found
int emu_import_text(const char *scan, const char *patches, char *summary, int max);
int emu_import_dir(const char *dir, char *summary, int max);
int emu_import_usb(char *summary, int max);
// write SFSCAN.TXT + SFPATCH.TXT (what the module writes with WRITE TO USB)
// into dir, e.g. a USB stick, so READ FROM USB on the module loads them.
// summary gets a one-line result. false if the files could not be written
bool emu_export_dir(const char *dir, char *summary, int max);

// the I2C bus the emulator pretends: bit u of ex_mask = Disting EX unit u+1
// (the Teletype's EX address + u) acknowledges, bit n of er_mask = ER-301 n+1 (0x31+n); bus_dead =
// nothing acknowledges; the devices answer only ready_after_ms after power-on
void emu_set_devices(uint8_t ex_mask, uint8_t er_mask, bool bus_dead, uint32_t ready_after_ms);
// run with this 32-byte ports configuration block (what the Flash window
// writes into the .hex), e.g. with a startup preset
void emu_set_config(const uint8_t block[32]);
// the block as it is now (after a change on the scan screen), and how many
// times the Teletype has written it
void emu_get_config(uint8_t block[32]);
uint32_t emu_config_writes(void);
// a Disting EX set to exactly this I2C address (answers only when the
// Teletype's Disting EX address matches it), on top of emu_set_devices
// Just Friends on the bus: bit 0 = 0x70, bit 1 = 0x75 (default: one at 0x70)
void emu_set_jf(uint8_t mask);
void emu_set_ex_at(uint8_t addr, bool on);

// the emulator's grid state, for the side panel
uint16_t emu_bpm(void);
bool emu_playing(void);
uint32_t emu_frame(void);

#endif
