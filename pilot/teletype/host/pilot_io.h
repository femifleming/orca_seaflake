#ifndef _PILOT_IO_H_
#define _PILOT_IO_H_

// Pilot Sealflake: calls from the PI.* Teletype ops into the Pilot synth.
// Implemented by the host (Source/TeletypeHost.cpp). Channels are 0-15,
// notes are MIDI note numbers (60 = C4 = Pilot "04C"), velocity 0-127.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// len_ms: > 0 note length in ms, 0 = the channel's PI.LEN, < 0 = held until note off
void pilot_io_note(int16_t ch, int16_t note, int16_t vel, int16_t len_ms);
void pilot_io_note_off(int16_t ch, int16_t note);
void pilot_io_all_off(void);

// a classic Pilot command such as "0ENV056f", "REV8c", "renv"
void pilot_io_command(const char *cmd);

// parameters (numbers listed in COMMANDS.md); values are whole numbers,
// some parameters are x100 (see pilot_io_param_scale)
int16_t pilot_io_param_get(int16_t ch, int16_t num);
void pilot_io_param_set(int16_t ch, int16_t num, int16_t value);
void pilot_io_param_pv(int16_t ch, int16_t num, int16_t value);  // 0..16384
int16_t pilot_io_param_min(int16_t num);
int16_t pilot_io_param_max(int16_t num);

int16_t pilot_io_preset_get(void);
void pilot_io_preset_load(int16_t n);
void pilot_io_preset_save(int16_t n);
void pilot_io_reset(void);
void pilot_io_rec(int16_t on);
int16_t pilot_io_bpm_get(void);
void pilot_io_bpm_set(int16_t bpm);

#ifdef __cplusplus
}
#endif

#endif
