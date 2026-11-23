// Dream option D: the elevator into the night (femishonuga.com/dd.html,
// 2026-05-18).
//
//   The elevator shot up to my floor, then shot into the night sky and
//   soared through trees lit by flashlights.
//
// First the shaft: the car rising, the floor marks streaming down past it.
// Then the shaft ends and the car is out in the night, small among the
// stars, gliding over a treeline while flashlight beams sweep up from the
// ground. Each pass is a different forest.

#include "dream_kit.h"

#define T 150000UL
#define SHAFT 22000UL   // inside the shaft
#define OPEN 6000UL     // the shaft falling away
#define STARS 22
#define BEAMS 3

static void scene(uint32_t ms) {
    const uint32_t pass = ms / T, t = ms % T;
    const uint32_t seed = dk_hash(pass, 0xE1E, 5);
    dk_fade = dk_pass_fade(t, T);

    if (t < SHAFT + OPEN) {
        // the shaft: walls, floor marks streaming down; after SHAFT they
        // fall away below and the sky takes over
        const int16_t gone = t > SHAFT ? (int16_t)((t - SHAFT) * 70 / OPEN) : 0;  // px the shaft has dropped
        const int16_t speed = (int16_t)(t < 4000 ? 1 + t / 800 : 6);
        const int16_t scroll = (int16_t)((t / 30) * speed / 6 % 22);
        const int16_t top = gone;
        if (top < DK_H) {
            dk_line(40, top, 40, DK_H - 1, 4);
            dk_line(88, top, 88, DK_H - 1, 4);
            for (int16_t y = (int16_t)(top + scroll % 22); y < DK_H; y += 22) {
                dk_line(34, y, 40, y, 5);   // floor marks on both walls
                dk_line(88, y, 94, y, 5);
            }
        }
        // the car: outline, a lit window, the cable
        const int16_t cy = (int16_t)(20 - (t > SHAFT ? (t - SHAFT) * 12 / OPEN : 0));
        dk_line(64, 0, 64, cy - 1, 3);
        dk_rect(52, cy, 76, cy + 26, 7);
        dk_line(64, cy + 1, 64, cy + 25, 3);      // the doors' seam
        dk_patch(56, cy + 4, 72, cy + 10, 10);    // the light inside
        if (t > SHAFT) {
            for (int16_t s = 0; s < STARS; s++) {
                const uint32_t h = dk_hash(seed, (uint32_t)s, 3);
                const int16_t y = (int16_t)(h % 40) + gone - 70;
                if (y >= 0) dk_dot((int16_t)((h >> 8) % DK_W), y, 3);
            }
        }
        return;
    }

    // out in the night
    const uint32_t f = t - SHAFT - OPEN;
    // stars, twinkling, drifting slowly left
    for (int16_t s = 0; s < STARS; s++) {
        const uint32_t h = dk_hash(seed, (uint32_t)s, 3);
        const int16_t x = (int16_t)(((h >> 8) % 200 + 200 - (f / 700) % 200) % 200);
        const int16_t y = (int16_t)(h % 40) - 30 + 30;
        if (x >= DK_W) continue;
        dk_dot(x, (int16_t)(y % 34), (uint8_t)(2 + (h >> 20) % 3 + (((f / 500 + h) % 9) == 0 ? 4 : 0)));
    }
    // the treeline: pointed tops in outline, scrolling left (we fly right)
    const int32_t scroll = (int32_t)(f / 45);
    int16_t px = -1, py = 0;
    for (int16_t x = -(int16_t)(scroll % 7); x <= DK_W + 7; x += 7) {
        const int32_t i = (x + scroll) / 7;
        const uint32_t h = dk_hash(seed, (uint32_t)i, 8);
        const int16_t tip = (int16_t)(40 + h % 12);
        const int16_t base = 56;
        if (px >= -7) dk_line(px, py, x - 3, base, 5);
        dk_line(x - 3, base, x, tip, 5);
        px = x, py = tip;
        if (h % 4 == 0) dk_dot(x, tip + 6, 9);   // a flashlight caught in the branches
    }
    // flashlight beams from the ground, sweeping
    for (int16_t b = 0; b < BEAMS; b++) {
        const uint32_t h = dk_hash(seed, (uint32_t)b, 6);
        const int16_t gx = (int16_t)(((h % 160) + 160 - (f / 45) % 160) % 160) - 16;
        const int32_t a = 256 + dk_sin((int32_t)(f / (9 + h % 6)) + (int32_t)(h >> 8)) * 90 / 256;   // up, swaying
        const int16_t ex = (int16_t)(gx + dk_cos(a) * 46 / 256), ey = (int16_t)(DK_H - 1 - dk_sin(a) * 46 / 256);
        dk_dash(gx, DK_H - 1, ex - 3, ey, 5);
        dk_dash(gx + 1, DK_H - 1, ex + 4, ey + 1, 5);
        dk_line(ex - 3, ey, ex + 4, ey + 1, 8);   // where it lands on the dark
    }
    // the car, small now, gliding across with a gentle bob
    const uint32_t across = T - SHAFT - OPEN;
    const int16_t cx = (int16_t)(-14 + (int32_t)(f * 156 / across));
    const int16_t cy = (int16_t)(16 + dk_sin((int32_t)(f / 12)) * 4 / 256);
    dk_rect(cx, cy, cx + 9, cy + 11, 8);
    dk_patch(cx + 2, cy + 2, cx + 7, cy + 5, 12);
    dk_dash(cx - 10, cy + 5, cx - 2, cy + 5, 3);   // a trail
}

DK_DREAM_BAND(scene)
