// Dream option B: the maintenance train (femishonuga.com/wiki/lbse.html,
// 2026-08-04).
//
//   A maintenance cargo train passes through the tunnel, blinking its
//   bright white lights and ringing its deep, sparse, solemn bell.
//
// Looking down a dark tunnel: dotted ribs coming slowly toward us, two rails
// running into the dark. Every 45 seconds the train comes out of the far
// end: two white lights blinking, its front growing as it nears, and with
// each slow toll of the bell a ring spreads out from it and fades. Then it
// is past, and the tunnel is quiet again. The vanishing point wanders a few
// pixels so nothing stays lit in one place.

#include <stdbool.h>

#include "dream_kit.h"

#define T 180000UL
#define EVERY 45000UL
#define COME 22000UL      // from the far end to passing us
#define BELL 3400UL       // ms between tolls
#define RING 2400UL       // how long a ring spreads

static void scene(uint32_t ms) {
    const uint32_t t = ms % T;
    dk_fade = dk_pass_fade(t, T);
    const int16_t vx = (int16_t)(64 + dk_sin((int32_t)(ms / 230)) * 6 / 256);
    const int16_t vy = (int16_t)(26 + dk_cos((int32_t)(ms / 310)) * 3 / 256);

    // ribs: depth z in 1/16ths, walking toward us (one rib every 64 units)
    const int32_t walk = (int32_t)(ms / 40) % 64;
    for (int32_t k = 0; k < 7; k++) {
        const int32_t z = 40 + k * 64 - walk;   // nearer = smaller z
        if (z < 24) continue;
        const int16_t hw = (int16_t)(64 * 52 / z * 3 / 2), hh = (int16_t)(64 * 40 / z * 3 / 2);
        const uint8_t v = (uint8_t)(z < 120 ? 4 : z < 250 ? 3 : 2);
        // a rib: the arch (sides + a flattened top), cut off by the floor
        const int16_t x0 = vx - hw, x1 = vx + hw, top = vy - hh, floor = vy + hh * 2 / 3;
        const int16_t c = hw / 4;
        dk_dash(x0, floor, x0, top + c, v);
        dk_dash(x1, floor, x1, top + c, v);
        dk_dash(x0, top + c, x0 + c, top, v);
        dk_dash(x1, top + c, x1 - c, top, v);
        dk_dash(x0 + c, top, x1 - c, top, v);
    }
    // the rails, into the dark
    dk_line(vx - 3, vy + 6, vx - 30, DK_H - 1, 3);
    dk_line(vx + 3, vy + 6, vx + 30, DK_H - 1, 3);

    // the train
    const uint32_t tt = ms % EVERY;
    if (tt >= COME) return;
    // depth: far (z 900) to at us (z 20), slowing as it nears
    const int32_t f = (int32_t)(tt * 1024 / COME);          // 0..1024
    const int32_t z = 900 - (880 * f / 1024) * (2048 - f) / 1024;
    const int16_t hw = (int16_t)(64 * 22 / z * 3 / 2), hh = (int16_t)(64 * 30 / z * 3 / 2);
    const int16_t ty = vy + (int16_t)(64 * 20 / z);            // its base, on the rails
    // the front of the train: an outline, then the two lights
    if (hw >= 2) {
        dk_rect(vx - hw, ty - hh, vx + hw, ty, (uint8_t)(z < 200 ? 5 : 3));
        dk_line(vx - hw / 2, ty - hh + hh / 4, vx + hw / 2, ty - hh + hh / 4, 3);   // its windscreen
    }
    const bool lit = (tt / 700) % 3 != 2;                         // blink: on, on, off
    const int16_t lx = hw * 2 / 3 > 1 ? hw * 2 / 3 : 1, ly = ty - hh / 4;
    if (lit) {
        const uint8_t r = (uint8_t)(z < 90 ? 2 : z < 220 ? 1 : 0);
        for (int16_t s = -1; s <= 1; s += 2)
            for (int16_t d = 0; d <= r; d++) dk_line(vx + s * lx - r / 2, ly - r / 2 + d, vx + s * lx + r - r / 2, ly - r / 2 + d, 15);
    }
    // the bell: a slow ring from the train every BELL ms, fading as it spreads
    for (int32_t b = 0; b < 2; b++) {
        const int32_t since = (int32_t)(tt % BELL) + b * (int32_t)BELL;
        if (since > (int32_t)RING || (int32_t)tt < since) continue;
        const int16_t r = (int16_t)(hw + 4 + since * 40 / (int32_t)RING);
        const uint8_t v = (uint8_t)(7 - since * 6 / (int32_t)RING);
        dk_ellipse(vx, ly, r, (int16_t)(r * 2 / 3), v, 24, 1);
    }
}

DK_DREAM_BAND(scene)

// (several dreams can be built into one file: dream.c)
#undef T
#undef EVERY
#undef COME
#undef BELL
#undef RING
