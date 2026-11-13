// Pilot Sealflake: the OLED is a 128x64 frame buffer read by the host (tt_host.c)
#ifndef _TT_SHIM_SCREEN_H_
#define _TT_SHIM_SCREEN_H_
#include "types.h"
void screen_draw_region(u8 x, u8 y, u8 w, u8 h, u8* data);
void screen_draw_region_offset(u8 x, u8 y, u8 w, u8 h, u32 len, u8* data, u32 off);
#endif
