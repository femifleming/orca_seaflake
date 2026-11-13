// Pilot Sealflake: no USB grid is attached; Teletype's on-screen grid
// visualizer still uses the grid state, so keep a 16x8 varibright LED buffer.
#ifndef _TT_SHIM_MONOME_H_
#define _TT_SHIM_MONOME_H_
#include "types.h"
#include "compiler.h"
#define MONOME_MAX_LED_BYTES 256
extern u8 *monomeLedBuffer;
u8 monome_size_x(void);
u8 monome_size_y(void);
u8 monome_is_vari(void);
#endif
