// Pilot Sealflake: timers run on one thread under the host lock, no IRQs
#ifndef _TT_SHIM_INTERRUPTS_H_
#define _TT_SHIM_INTERRUPTS_H_
#include "types.h"
static inline u8 irqs_pause(void) { return 0; }
static inline void irqs_resume(u8 f) { (void)f; }
#endif
