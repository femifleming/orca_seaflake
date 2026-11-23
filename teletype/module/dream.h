#ifndef _DREAM_H_
#define _DREAM_H_

#include <stdbool.h>
#include <stdint.h>

// The screensaver (dream.c): one of the dreams from femishonuga.com. Draws
// 8-pixel band 'line' (0..7) of the picture at 'ms' milliseconds into the
// screensaver into buf (128 x 8 pixels, one byte each, brightness 0-15).
void dream_band(uint32_t ms, uint8_t line, uint8_t *buf);
// which of the dreams is showing (0-3), 0xFF before the first one
uint8_t dream_current(void);
// true: a random one of the four each start; false: one fixed dream (DREAM_ONLY)
bool dream_random(void);
// the host's clock in ms, mixed into the pick when a dream starts
uint32_t dream_entropy(void);

#endif
