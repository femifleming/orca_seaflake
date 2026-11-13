// Pilot Sealflake: desktop stand-in for the AVR32 ASF compiler.h
#ifndef _TT_SHIM_COMPILER_H_
#define _TT_SHIM_COMPILER_H_
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define COMPILER_PACK_SET(x)
#define COMPILER_PACK_RESET()
typedef uint16_t le16_t;
typedef uint32_t U32;
typedef uint8_t U8;
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif
