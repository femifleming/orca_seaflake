#ifndef _FLASH_H_
#define _FLASH_H_

// Pilot Sealflake: Teletype's 32 scene slots. On the hardware these live in
// flash; here each slot is a text file in Teletype's USB stick format
// (tt00s.txt .. tt31s.txt), see host/tt_host.c.

#include <stdint.h>

#include "globals.h"
#include "line_editor.h"
#include "teletype.h"

#define SCENE_SLOTS 32

void flash_read(uint8_t preset_no, scene_state_t *scene,
                char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS],
                uint8_t init_pattern, uint8_t init_grid,
                uint8_t init_i2c_op_address);
void flash_write(uint8_t preset_no, scene_state_t *scene,
                 char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]);
uint8_t flash_last_saved_scene(void);
void flash_update_last_saved_scene(uint8_t preset_no);
const char *flash_scene_text(uint8_t preset_no, size_t line);
tele_mode_t flash_last_mode(void);
void flash_update_last_mode(tele_mode_t mode);
void flash_update_cal(cal_data_t *);
void flash_get_cal(cal_data_t *);

#endif
