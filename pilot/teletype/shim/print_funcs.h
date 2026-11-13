// Pilot Sealflake: debug printing is a no-op on the desktop
#ifndef _TT_SHIM_PRINT_FUNCS_H_
#define _TT_SHIM_PRINT_FUNCS_H_
#include <stdlib.h>
#include "compiler.h"
static inline void print_dbg(const char* s) { (void)s; }
static inline void print_dbg_ulong(unsigned long n) { (void)n; }
static inline void print_dbg_hex(unsigned long n) { (void)n; }
static inline void print_dbg_char(int c) { (void)c; }
#endif
