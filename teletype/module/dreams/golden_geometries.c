// Dream option A: golden geometries (femishonuga.com/dd.html, 2021-06-21).
//
//   Sunlight comes through shattered windows, casting golden geometries
//   onto the dusty, broken tiles of a warehouse floor.
//
// A row of tall windows on the back wall, some panes cracked. The floor
// below in perspective: dotted tile lines, some tiles missing. Through each
// window the sun lays a slanted quadrilateral of light on the floor (lit
// edges, a stippled inside), with a crack across it where the glass is
// broken. Over a pass (three minutes) the sun crosses: the shapes slide and
// stretch across the floor, and dust turns in the beams.

#include "dream_kit.h"

#define T 180000UL
#define WALL 20     // the floor meets the back wall here
#define VPX 64      // vanishing point
#define VPY (-30)

static void scene(uint32_t ms) {
    const uint32_t pass = ms / T, t = ms % T;
    const uint32_t seed = dk_hash(pass, 0xA1, 3);
    dk_fade = dk_pass_fade(t, T);
    const int32_t sun = (int32_t)(t * 1024 / T);   // 0..1024 across the pass
    const int16_t drift = (int16_t)(dk_sin((int32_t)(t / 180)) * 3 / 256);  // the whole room breathes 3 px

    // the back wall's base and the floor: perspective tile lines, dotted
    dk_dots(0, WALL, DK_W - 1, WALL, 3);
    for (int16_t k = -4; k <= 4; k++) {
        const int16_t xb = (int16_t)(VPX + drift + k * 30);
        const int16_t xt = (int16_t)(VPX + drift + (xb - VPX - drift) * (WALL - VPY) / (DK_H - 1 - VPY));
        if (dk_hash(seed, (uint32_t)k + 40, 0) % 5 == 0) continue;   // a broken run of tiles
        dk_dots(xt, WALL + 1, xb, DK_H - 1, 2);
    }
    for (int16_t r = 1; r <= 6; r++) {
        const int16_t y = (int16_t)(WALL + r * r * (DK_H - WALL) / 40);
        if (y >= DK_H) break;
        const uint32_t h = dk_hash(seed, (uint32_t)r, 1);
        const int16_t gap = (int16_t)(h % DK_W), gw = (int16_t)(8 + (h >> 8) % 20);
        dk_dots(0, y, gap - 1, y, 2);              // a row of tiles, one stretch broken out
        dk_dots(gap + gw, y, DK_W - 1, y, 2);
    }

    // the windows: three tall frames with a cross of mullions; two are
    // shattered (a few cracks from one point)
    for (int16_t i = 0; i < 3; i++) {
        const int16_t x0 = (int16_t)(14 + i * 38 + drift), x1 = x0 + 22;
        // the frames dotted (the light on the floor is the bright thing)
        dk_dash(x0, 1, x1, 1, 5);
        dk_dash(x0, WALL - 3, x1, WALL - 3, 5);
        dk_dash(x0, 2, x0, WALL - 4, 5);
        dk_dash(x1, 2, x1, WALL - 4, 5);
        dk_dots(x0 + 11, 2, x0 + 11, WALL - 4, 3);
        dk_dots(x0 + 1, 8, x1 - 1, 8, 3);
        const uint32_t h = dk_hash(seed, (uint32_t)i, 2);
        if (i != (int16_t)(h % 3)) {
            const int16_t cx = (int16_t)(x0 + 3 + h % 7), cy = (int16_t)(10 + (h >> 4) % 4);
            dk_line(cx, cy, cx - 3, cy - 5, 2);
            dk_line(cx, cy, cx + 6, cy + 2, 2);
            dk_line(cx, cy, cx + 1, WALL - 4, 2);
        }
    }

    // the sun's shapes: each window's light lands on the floor as a slanted
    // quadrilateral that slides with the sun (left to right over the pass)
    // and leans further as it gets low
    const int16_t lean = (int16_t)((sun - 512) * 44 / 512);     // -44..44
    const int16_t len = (int16_t)(20 + (dk_sin(sun / 2) * 8) / 256);
    for (int16_t i = 0; i < 3; i++) {
        const int16_t wx = (int16_t)(14 + i * 38 + drift);
        const int16_t ty = WALL + 10, by = (int16_t)(ty + len);
        const int16_t tx0 = wx + lean / 3, tx1 = tx0 + 20;
        const int16_t bx0 = wx + lean - 3, bx1 = bx0 + 28;
        const uint8_t e = 7;
        dk_line(tx0, ty, tx1, ty, e);
        dk_line(bx0, by, bx1, by, e);
        dk_line(tx0, ty, bx0, by, e);
        dk_line(tx1, ty, bx1, by, e);
        // the mullions' shadow: a cross of dark through the light
        const int16_t mx0 = tx0 + 10, mx1 = bx0 + 14, my = ty + len / 3;
        // inside: stippled light, a few bands following the slant
        for (int16_t y = ty + 2; y < by - 1; y += 4) {
            const int16_t xa = tx0 + (bx0 - tx0) * (y - ty) / (by - ty) + 2;
            const int16_t xb = tx1 + (bx1 - tx1) * (y - ty) / (by - ty) - 2;
            const int16_t xm = mx0 + (mx1 - mx0) * (y - ty) / (by - ty);
            if (y == my || y + 1 == my) continue;
            dk_patch(xa, y, xm - 2, y + 1, 5);
            dk_patch(xm + 2, y, xb, y + 1, 5);
        }
        // the beam's edges, faint, from the sill to the shape
        dk_dash(wx, WALL - 3, tx0, ty, 2);
        dk_dash(wx + 22, WALL - 3, tx1, ty, 2);

        // dust turning in the beam
        for (uint8_t d = 0; d < 5; d++) {
            const uint32_t dh = dk_hash(seed, (uint32_t)(i * 8 + d), 5);
            const int32_t ph = (int32_t)(t / (37 + dh % 23)) + (int32_t)(dh >> 8);
            const int16_t f = (int16_t)((dh >> 16) % 12);
            const int16_t y = (int16_t)(WALL - 2 + f + dk_sin(ph) * 2 / 256);
            const int16_t x = (int16_t)(wx + (tx0 - wx) * f / 13 + (dh >> 4) % 20 + dk_cos(ph * 2) * 2 / 256);
            const uint8_t tw = (uint8_t)(((t / 300 + dh) % 5) == 0 ? 11 : 6);
            dk_dot(x, y, tw);
        }
    }
}

DK_DREAM_BAND(scene)

// (several dreams can be built into one file: dream.c)
#undef T
#undef WALL
#undef VPX
#undef VPY
