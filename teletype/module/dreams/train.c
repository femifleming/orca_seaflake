// The screensaver: a dream from femishonuga.com/dd.html, 2024-10-08.
//
//   The above-ground train flies over crystal clear waters in a valley
//   between dark umber mountains and low fog, as the sky dims.
//
// Seen from the side: two ranges of mountains (the far one paler in the haze,
// the near one dark umber), low fog along the valley floor, and clear water
// below that mirrors all of it. Now and then the train crosses over the water,
// its windows lit and reflected. Over one pass of four minutes the sky dims
// from dusk to night and the first stars come out. Everything drifts, and each
// pass is a different valley, so nothing stays lit in one place (OLEDs burn
// in). Drawn one 8-pixel band at a time, integers only.

#include "../dream.h"

#include <string.h>

// Stippled, for the real OLED. Drawn as shades (0-15) the dream lit 66% of
// the panel and whole rows at once, and the module's screen glitched after
// ~20 s (2026-11-20, every build incl. the first). So shading is an ordered
// dither: a pixel is lit, at a fixed level, where the shade beats an 8x8
// Bayer threshold, at most STIPPLE_MAX of the time. The sharp things (train
// windows and headlight, stars, glints) stay solid. And no row ever has more
// than ROW_CAP lit pixels: the grid's busiest row has 34-37.
#define STIPPLE_MAX 3   // densest shading: 3/16 of the pixels
#define DOT 6           // brightness of a stipple dot
#define ROW_CAP 32
#define SOLID 0x10      // flag in sky_img: draw solid, not stippled
static const uint8_t bayer8[8][8] = {
    { 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
    { 3, 35, 11, 43, 1, 33, 9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 },
};
// a shade 0-15 (already faded) at (x, y) -> a dot or nothing
static uint8_t stipple(int16_t x, int16_t y, uint8_t v) {
    return (uint32_t)bayer8[y & 7][x & 7] * 15 < (uint32_t)v * STIPPLE_MAX * 4 ? DOT : 0;
}
// keep at most ROW_CAP lit pixels in a row, spread evenly; solid ones first
static void cap_row(uint8_t *row) {
    uint16_t n = 0;
    for (int16_t x = 0; x < 128; x++) n += row[x] != 0;
    if (n <= ROW_CAP) return;
    uint16_t k = 0;
    for (int16_t x = 0; x < 128; x++)
        if (row[x]) {
            if (row[x] == DOT && (k * ROW_CAP) % n >= ROW_CAP) row[x] = 0;
            k++;
        }
    n = 0;
    for (int16_t x = 0; x < 128; x++)
        if (row[x] && ++n > ROW_CAP) row[x] = 0;
}

#define W 128
#define H 64
#define T 240000UL     // one pass, ms
#define SHORE 40       // the waterline: the row where the mirror is
#define TRAIN_LEN 46
#define TRAIN_Y 33     // the bottom of the train
#define TRAIN_EVERY 40000UL
#define TRAIN_CROSS 16000UL

static uint32_t hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// smooth value noise: x in 1/256ths, a value 0..range
static int32_t noise(uint32_t seed, int32_t x, int32_t range) {
    const int32_t i = x >> 8, f = x & 255;
    const int32_t a = (int32_t)(hash(seed, (uint32_t)i, 0) % (uint32_t)range);
    const int32_t b = (int32_t)(hash(seed, (uint32_t)(i + 1), 0) % (uint32_t)range);
    const int32_t s = f * f * (768 - 2 * f) / 65536;   // smoothstep, 0..256
    return a + (b - a) * s / 256;
}

// this frame: where the ridges are, how dim the sky is, where the train is
static uint32_t frame_ms = 0xFFFFFFFF, seed;
static int16_t far_top[W], near_top[W];
static uint8_t dusk;        // 16 at dusk, down to 8 at night
static uint8_t fade;        // 0..16, the fade in and out of each pass
static int32_t train_x;     // its front, or < -1000 when there is none
static uint32_t tms;
// this frame's fog density per column (it only depends on x and time), and
// the scene above the waterline, drawn once per frame: the water mirrors it
// from here instead of drawing it again. Keeps a frame cheap enough for the
// Teletype's 60 MHz chip (all of it used to be worked out per pixel, twice).
static int16_t fog_d[W];
static uint8_t sky_row[SHORE + 1];  // the sky's brightness on each row
#define STARS 16
static uint8_t sky_img[(SHORE + 1) * W];
static uint8_t above(int32_t x, int32_t y);

static void prepare(uint32_t ms) {
    frame_ms = ms;
    const uint32_t pass = ms / T;
    tms = ms % T;
    seed = hash(pass, 0x5EA, 1);
    // dusk 16 down to 8 at night: never so dark that the OLED shows black
    dusk = (uint8_t)(16 - tms * 8 / T);
    fade = tms < 6000 ? (uint8_t)(tms * 16 / 6000) : tms > T - 6000 ? (uint8_t)((T - tms) * 16 / 6000) : 16;
    // the far range drifts slowly, the near one faster (we are moving)
    const int32_t far_shift = (int32_t)(tms / 160), near_shift = (int32_t)(tms / 70);
    for (int16_t x = 0; x < W; x++) {
        const int32_t fx = (x + far_shift) * 256 / 22, nx = (x + near_shift) * 256 / 15;
        far_top[x] = (int16_t)(14 + noise(seed, fx, 14) + noise(seed + 7, fx * 3, 4));
        near_top[x] = (int16_t)(24 + noise(seed + 1, nx, 12) + noise(seed + 9, nx * 4, 3));
    }
    // the train: one crossing every 40 seconds, left to right
    const uint32_t tt = (tms + 20000) % TRAIN_EVERY;
    train_x = tt < TRAIN_CROSS ? (int32_t)(tt * (W + TRAIN_LEN + 20) / TRAIN_CROSS) - 10 : -10000;
    for (int16_t x = 0; x < W; x++)
        fog_d[x] = (int16_t)(noise(seed + 3, (x * 256 + (int32_t)tms * 2) / 18, 6) + noise(seed + 4, (x * 256 + (int32_t)tms * 3) / 7, 3));
    for (int16_t y = 0; y <= SHORE; y++) sky_row[y] = (uint8_t)(y * y * 9 * dusk / (SHORE * SHORE * 16));
    for (int16_t y = 0; y <= SHORE; y++)
        for (int16_t x = 0; x < W; x++) sky_img[y * W + x] = above(x, y);
    // the ridgelines, solid: one pixel per column along each range, joined
    // down steep slopes, so the mountains read as shapes, not just dots
    for (int16_t x = 0; x < W; x++) {
        const int16_t f = far_top[x], n = near_top[x];
        const int16_t fp = x ? far_top[x - 1] : f, np = x ? near_top[x - 1] : n;
        for (int16_t y = (f < fp ? f : fp); y <= (f < fp ? fp - 1 : f); y++)
            if (y >= 0 && y <= SHORE && y < n) sky_img[y * W + x] = (uint8_t)(6 * dusk / 16 + 2) | SOLID;
        if (f >= 0 && f <= SHORE && f < n) sky_img[f * W + x] = (uint8_t)(6 * dusk / 16 + 2) | SOLID;
        for (int16_t y = (n < np ? n : np); y <= (n < np ? np - 1 : n); y++)
            if (y >= 0 && y <= SHORE) sky_img[y * W + x] = 8 | SOLID;
        if (n >= 0 && n <= SHORE) sky_img[n * W + x] = 8 | SOLID;
    }
    // and the stars, once it is dark enough: STARS of them per pass, drifting
    // left with the far range, twinkling, only where the sky shows
    if (dusk < 12) {
        const int32_t drift = (int32_t)(tms / 160);
        for (uint8_t i = 0; i < STARS; i++) {
            const uint32_t h = hash(seed, 0x57A2 + i, 7);
            const int32_t x = (int32_t)(h % (W + T / 160)) - drift;
            const int32_t y = (int32_t)((h >> 16) % 20);
            if (x < 0 || x >= W || y >= far_top[x]) continue;
            const uint8_t b = (uint8_t)((h >> 12) % 3 + 2 + ((tms / 700 + h) % 7 == 0 ? 2 : 0));
            if (b > (sky_img[y * W + x] & 15)) sky_img[y * W + x] = b | SOLID;
        }
    }
}

// the train at (x, y): -1 where it isn't, else its brightness
static int8_t train_at(int32_t x, int32_t y) {
    const int32_t back = train_x - TRAIN_LEN;
    if (x < back || x > train_x || y > TRAIN_Y || y < TRAIN_Y - 6) return -1;
    if (y == TRAIN_Y - 6 && (x == back || x == train_x)) return -1;          // rounded ends
    if (y == TRAIN_Y && ((x - back) % 15) < 2) return -1;                   // gaps between the cars
    if (y >= TRAIN_Y - 4 && y <= TRAIN_Y - 3) {
        const int32_t k = (x - back) % 15;
        if (k >= 3 && k <= 12 && (k % 3) != 0) return 11;                    // the lit windows
    }
    if (x == train_x && y == TRAIN_Y - 2) return 15;                          // the headlight
    return 1;                                                                  // the dark body
}

// the scene above the waterline
static uint8_t above(int32_t x, int32_t y) {
    if (x < 0) x = 0;
    if (x >= W) x = W - 1;
    // the sky: lightest at the horizon, dimming as the pass goes on
    // (the stars are drawn over the finished frame, in prepare)
    int32_t v = sky_row[y];
    if (y >= far_top[x]) v = 5 * dusk / 16 + 1;            // the far range, pale in the haze
    if (y >= near_top[x]) v = 1;                            // the near range, dark umber
    // low fog along the valley floor, drifting
    if (y >= 27) {
        const int32_t d = fog_d[x];
        const int32_t fog = (d - 2 - (SHORE - y) / 2) * dusk / 16;
        if (fog > v) v = fog;
    }
    const int8_t tr = train_at(x, y);
    if (tr >= 0) v = tr;
    if (tr >= 11) return (uint8_t)(tr | SOLID);  // lit windows, headlight
    return (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
}

void dream_band(uint32_t ms, uint8_t line, uint8_t *buf) {
    if (ms != frame_ms) prepare(ms);
    const int16_t y0 = (int16_t)line * 8;
    for (int16_t j = 0; j < 8; j++) {
        const int16_t y = y0 + j;
        uint8_t *row = &buf[j * W];
        if (y <= SHORE) {
            for (int16_t x = 0; x < W; x++) {
                const uint8_t c = sky_img[y * W + x];
                const uint8_t v = (uint8_t)((c & 15) * fade >> 4);
                row[x] = (c & SOLID) ? v : stipple(x, y, v);
            }
            cap_row(row);
            continue;
        }
        // the water: the scene mirrored, a little darker, broken by ripples
        const int16_t my = (int16_t)(2 * SHORE - y);
        const int32_t ripple = (int32_t)(hash(seed, (uint32_t)y, tms / 240) % 3) - 1;
        // crystal clear: now and then a glint (one chance per row, about as
        // often as the old 1 in 900 pixels)
        const uint32_t g = hash(seed, (uint32_t)y * 64u, tms / 300);
        const int16_t gx = g % 7 == 0 ? (int16_t)((g >> 8) % W) : -1;
        for (int16_t x = 0; x < W; x++) {
            int32_t sx = x + ripple * ((y - SHORE) / 5 + 1);
            if (sx < 0) sx = 0;
            if (sx >= W) sx = W - 1;
            const uint8_t c = sky_img[my * W + sx];
            int32_t v = (c & 15) * 2 / 3;
            if (x == gx) {  // a glint: solid
                row[x] = (uint8_t)((v + 4 > 15 ? 15 : v + 4) * fade >> 4);
                continue;
            }
            v = v * fade >> 4;
            row[x] = (c & SOLID) ? (uint8_t)v : stipple(x, y, (uint8_t)v);
        }
        cap_row(row);
    }
}
