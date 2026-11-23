// Dream option C: the street becomes a lake (femishonuga.com/dd.html,
// 2014-09-20).
//
//   The street turns into a lake in the rain, and cars drive over lily
//   pads and salt water.
//
// A low skyline in outline on the far side. Rain falls in short streaks;
// where drops land on the water, rings open and fade. Lily pads drift on
// the lake, smaller toward the far side. Every 30 seconds a car glides across
// over the pads, its headlights reaching ahead on the water. Over a pass the
// lake rises and swallows the bottom of the street.

#include "dream_kit.h"

#define T 240000UL
#define DROPS 22
#define RINGS 7
#define PADS 7

static void scene(uint32_t ms) {
    const uint32_t pass = ms / T, t = ms % T;
    const uint32_t seed = dk_hash(pass, 0x1A6E, 9);
    dk_fade = dk_pass_fade(t, T);
    // the waterline rises over the pass: 30 down to 24
    const int16_t shore = (int16_t)(30 - t * 6 / T);

    // the skyline: building tops in outline, drifting very slowly
    const int32_t sx = (int32_t)(t / 900);
    int16_t py = 0;
    for (int16_t x = 0; x <= DK_W; x += 4) {
        const int32_t i = (x + sx) / 12;
        const uint32_t h = dk_hash(seed, (uint32_t)i, 4);
        const int16_t y = (int16_t)(shore - 4 - h % 14);
        if (x > 0) {
            dk_line(x - 4, py, x - 4, y, 4);   // a wall where the height changes
            dk_line(x - 4, y, x, y, 4);
        }
        // a lit window or two
        if (h % 7 == 0) dk_dot(x - 2, y + 3, 9);
        py = y;
    }
    dk_dots(0, shore, DK_W - 1, shore, 5);

    // the water: a few faint ripple lines moving toward us
    for (int16_t k = 0; k < 4; k++) {
        const int16_t y = (int16_t)(shore + 4 + ((k * 9 + t / 400) % 32));
        if (y >= DK_H) continue;
        const uint32_t h = dk_hash(seed, (uint32_t)k, (uint32_t)(t / 3000));
        const int16_t x0 = (int16_t)(h % 90), w = (int16_t)(10 + (h >> 8) % 30);
        dk_dots(x0, y, x0 + w, y, 2);
    }

    // the car's place this frame (drawn below), so pads it is over hide
    const uint32_t ct = t % 30000;
    const int16_t cx = ct < 14000 ? (int16_t)(-30 + (int32_t)ct * 190 / 14000) : -1000, cy = (int16_t)(shore + 18);

    // lily pads, smaller toward the far side, drifting left
    for (int16_t p = 0; p < PADS; p++) {
        const uint32_t h = dk_hash(seed, (uint32_t)p, 7);
        const int16_t depth = (int16_t)(h % 100);                       // 0 far .. 99 near
        const int16_t y = (int16_t)(shore + 4 + depth * (DK_H - 4 - shore) / 100);
        const int16_t rx = (int16_t)(4 + depth / 14), ry = (int16_t)(2 + depth / 40);
        const int16_t x = (int16_t)((int32_t)((h >> 8) % 170) - (int32_t)(t * (10 + depth / 8) / 6000) % 170 + 170) % 170 - 20;
        if (x + rx > cx - 18 && x - rx < cx + 50 && y + ry > cy - 10 && y - ry < cy + 9) continue;  // under the car or its light
        dk_ellipse(x, y, rx, ry, 6, 12, 0);
        dk_line(x, y, x + rx, y - 1, 3);   // the notch
        if (depth > 50 && (h >> 20) % 3 == 0) {   // now and then a flower
            dk_dot(x - 1, y - ry - 1, 12);
            dk_dot(x - 2, y - ry - 2, 8);
            dk_dot(x, y - ry - 2, 8);
        }
    }

    // the car: every 30 s, gliding over the pads, left to right
    if (ct < 14000) {
        dk_line(cx, cy, cx + 22, cy, 7);                // the body's sill
        dk_line(cx, cy, cx, cy - 4, 7);
        dk_line(cx + 22, cy, cx + 22, cy - 3, 7);
        dk_line(cx, cy - 4, cx + 6, cy - 4, 7);
        dk_line(cx + 6, cy - 4, cx + 9, cy - 8, 7);     // the roof
        dk_line(cx + 9, cy - 8, cx + 16, cy - 8, 7);
        dk_line(cx + 16, cy - 8, cx + 19, cy - 3, 7);
        dk_line(cx + 19, cy - 3, cx + 22, cy - 3, 7);
        dk_dot(cx + 22, cy - 2, 15);                     // the headlight
        dk_line(cx + 4, cy + 1, cx + 6, cy + 1, 5);     // wheels in the water
        dk_line(cx + 15, cy + 1, cx + 17, cy + 1, 5);
        dk_dots(cx + 24, cy - 1, cx + 48, cy + 3, 6);   // its light on the water
        dk_dots(cx + 24, cy, cx + 42, cy + 7, 4);
        dk_dots(cx - 16, cy + 2, cx - 2, cy + 1, 4);   // the wake
    }

    // rain: short slanted streaks, falling fast
    for (int16_t d = 0; d < DROPS; d++) {
        const uint32_t h = dk_hash(seed, (uint32_t)d, 11);
        const uint32_t per = 700 + h % 500;              // ms to fall the screen
        const uint32_t ph = (t + (h >> 12)) % per;
        const int16_t y = (int16_t)((int32_t)ph * (DK_H + 8) / (int32_t)per) - 6;
        const int16_t x = (int16_t)((h >> 4) % DK_W) - y / 4;
        dk_line(x, y, x - 1, y + 4, 4);
    }
    // where drops land: rings opening and fading
    for (int16_t r = 0; r < RINGS; r++) {
        const uint32_t life = 1600;
        const uint32_t k = (t + (uint32_t)r * 233) / life;
        const uint32_t age = (t + (uint32_t)r * 233) % life;
        const uint32_t h = dk_hash(seed, (uint32_t)r, k);
        const int16_t y = (int16_t)(shore + 5 + h % (uint32_t)(DK_H - 6 - shore));
        const int16_t x = (int16_t)((h >> 8) % DK_W);
        const int16_t rad = (int16_t)(1 + age * (4 + (y - shore) / 6) / life);
        const uint8_t v = (uint8_t)(6 - age * 5 / life);
        dk_ellipse(x, y, rad, (int16_t)(rad / 3 + 1), v, 8, 1);
    }
}

DK_DREAM_BAND(scene)

// (several dreams can be built into one file: dream.c)
#undef T
#undef DROPS
#undef RINGS
#undef PADS
