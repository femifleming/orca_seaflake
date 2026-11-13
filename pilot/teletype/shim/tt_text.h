// Pilot Sealflake: libavr32's font / region calls report the text they draw, so the
// host can show Teletype's screen as text in Pilot's own font (host/tt_host.c).
#ifndef _TT_TEXT_H_
#define _TT_TEXT_H_
#include "types.h"
struct _region;
void tt_text_hook(const struct _region* reg, const char* str, int len, int x, int y, u8 fg, u8 bg, int hi, u8 hid, int right);
void tt_fill_hook(const struct _region* reg, u8 c);
#endif
