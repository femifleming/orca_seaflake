// Shared drawing kit for the sparse dreams (module/dreams/*.c).
//
// The rules the real module's OLED taught us (2026-11-20): full shades over
// big areas glitch the screen; stippled line art with at most 32 lit pixels
// in any row does not. So a dream here is a short list of primitives (lines,
// dots, small stippled patches) worked out once per frame, then drawn into
// each 8-pixel band clipped to it, and every row is capped. No frame buffer:
// the list is the only memory (DK_MAX primitives, ~1.5 KB).
//
// A dream file includes this, defines scene(ms) (which calls dk_line,
// dk_dot, dk_patch ...), and ends with DK_DREAM_BAND(scene).

#ifndef DREAM_KIT_H
#define DREAM_KIT_H

#include <stdint.h>
#include <string.h>

#include "../dream.h"

#define DK_W 128
#define DK_H 64
#define DK_ROW_CAP 32   // lit pixels in a row, at most
#define DK_DOT 6        // brightness of a stipple dot
#define DK_MAX 300      // primitives per frame (10 bytes each)

enum { DK_LINE = 0, DK_STIP = 1, DK_DASH = 2, DK_DOTS = 3 };

typedef struct {
    int16_t x0, y0, x1, y1;
    uint8_t v, kind;   // brightness 1-15 (DK_STIP: a shade 0-15), kind
} dk_prim_t;

static dk_prim_t dk_p[DK_MAX];
static uint16_t dk_n;
uint32_t dk_overflow;   // primitives that did not fit (tools/dream_render.c reports it)
static uint32_t dk_frame = 0xFFFFFFFF;
static uint8_t dk_fade = 16;   // 0..16, applied to everything

static uint32_t dk_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// a smooth value 0..range at x (1/256ths)
static int32_t dk_noise(uint32_t seed, int32_t x, int32_t range) {
    const int32_t i = x >> 8, f = x & 255;
    const int32_t a = (int32_t)(dk_hash(seed, (uint32_t)i, 0) % (uint32_t)range);
    const int32_t b = (int32_t)(dk_hash(seed, (uint32_t)(i + 1), 0) % (uint32_t)range);
    const int32_t s = f * f * (768 - 2 * f) / 65536;
    return a + (b - a) * s / 256;
}

// sine in Q8 (-256..256) of an angle in 1/1024ths of a turn
static int16_t dk_sin(int32_t a) {
    a &= 1023;
    int32_t q = a & 255, s;
    // a parabola per half turn is close enough for drifting things
    const int32_t h = (a & 511) < 256 ? q : 256 - q;   // 0..256..0 over a half turn
    s = (h * (512 - h)) >> 8;                          // 0..256
    return (int16_t)(a < 512 ? s : -s);
}
static int16_t dk_cos(int32_t a) { return dk_sin(a + 256); }

static void dk_add(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v, uint8_t kind) {
    if (!v) return;
    if (dk_n >= DK_MAX) {
        dk_overflow++;
        return;
    }
    dk_prim_t *p = &dk_p[dk_n++];
    p->x0 = x0, p->y0 = y0, p->x1 = x1, p->y1 = y1, p->v = v > 15 ? 15 : v, p->kind = kind;
}
static void dk_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v) { dk_add(x0, y0, x1, y1, v, DK_LINE); }
// a dotted line: every other pixel (lighter on the panel, reads as a line)
static void dk_dash(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v) { dk_add(x0, y0, x1, y1, v, DK_DASH); }
// a line of dots, every fourth pixel (long faint guides: floors, horizons)
static void dk_dots(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v) { dk_add(x0, y0, x1, y1, v, DK_DOTS); }
static void dk_dot(int16_t x, int16_t y, uint8_t v) { dk_add(x, y, x, y, v, DK_LINE); }
// a stippled patch (x0..x1, y0..y1) at a shade 0-15: ordered dots, never solid
static void dk_patch(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t shade) { dk_add(x0, y0, x1, y1, shade, DK_STIP); }
static void dk_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v) {
    dk_line(x0, y0, x1, y0, v);
    dk_line(x0, y1, x1, y1, v);
    dk_line(x0, y0 + 1, x0, y1 - 1, v);
    dk_line(x1, y0 + 1, x1, y1 - 1, v);
}
// an ellipse outline, centre (cx, cy), radii rx, ry, as n short lines
static void dk_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint8_t v, uint8_t n, uint8_t dashed) {
    int16_t px = cx + rx, py = cy;
    for (uint8_t i = 1; i <= n; i++) {
        const int32_t a = (int32_t)i * 1024 / n;
        const int16_t x = (int16_t)(cx + rx * dk_cos(a) / 256), y = (int16_t)(cy + ry * dk_sin(a) / 256);
        dk_add(px, py, x, y, v, dashed ? DK_DASH : DK_LINE);
        px = x, py = y;
    }
}

static const uint8_t dk_bayer[8][8] = {
    { 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
    { 3, 35, 11, 43, 1, 33, 9, 41 },  { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 },
};

static void dk_px(uint8_t *band, int16_t y0, int16_t x, int16_t y, uint8_t v) {
    if (x < 0 || x >= DK_W || y < y0 || y >= y0 + 8) return;
    uint8_t *p = &band[(y - y0) * DK_W + x];
    if (v > *p) *p = v;
}

static void dk_draw(const dk_prim_t *p, uint8_t *band, int16_t y0) {
    const uint8_t v = (uint8_t)(p->v * dk_fade >> 4);
    if (p->kind == DK_STIP) {
        // shade 0-15 -> at most 3/16 of the pixels lit, at DK_DOT
        int16_t ya = p->y0 > y0 ? p->y0 : y0, yb = p->y1 < y0 + 7 ? p->y1 : y0 + 7;
        for (int16_t y = ya; y <= yb; y++)
            for (int16_t x = p->x0 < 0 ? 0 : p->x0; x <= p->x1 && x < DK_W; x++)
                if ((uint32_t)dk_bayer[y & 7][x & 7] * 15 < (uint32_t)v * 12) dk_px(band, y0, x, y, DK_DOT);
        return;
    }
    if (!v) return;
    // quick reject: the line's rows miss this band
    const int16_t lo = p->y0 < p->y1 ? p->y0 : p->y1, hi = p->y0 < p->y1 ? p->y1 : p->y0;
    if (hi < y0 || lo >= y0 + 8) return;
    int16_t x = p->x0, y = p->y0;
    const int16_t dx = p->x1 > p->x0 ? p->x1 - p->x0 : p->x0 - p->x1, sx = p->x0 < p->x1 ? 1 : -1;
    const int16_t dy = -(p->y1 > p->y0 ? p->y1 - p->y0 : p->y0 - p->y1), sy = p->y0 < p->y1 ? 1 : -1;
    int16_t err = dx + dy;
    uint16_t k = 0;
    for (;;) {
        if ((p->kind != DK_DASH || !(k & 1)) && (p->kind != DK_DOTS || !(k & 3))) dk_px(band, y0, x, y, v);
        k++;
        if (x == p->x1 && y == p->y1) break;
        const int16_t e2 = 2 * err;
        if (e2 >= dy) err += dy, x += sx;
        if (e2 <= dx) err += dx, y += sy;
    }
}

// at most DK_ROW_CAP lit pixels a row: stipple dots go first, evenly
static void dk_cap(uint8_t *row) {
    uint16_t n = 0;
    for (int16_t x = 0; x < DK_W; x++) n += row[x] != 0;
    if (n <= DK_ROW_CAP) return;
    uint16_t k = 0;
    for (int16_t x = 0; x < DK_W; x++)
        if (row[x]) {
            if (row[x] == DK_DOT && (k * DK_ROW_CAP) % n >= DK_ROW_CAP) row[x] = 0;
            k++;
        }
    // still too many: thin what is left evenly too (never just the right end)
    n = 0;
    for (int16_t x = 0; x < DK_W; x++) n += row[x] != 0;
    if (n <= DK_ROW_CAP) return;
    k = 0;
    for (int16_t x = 0; x < DK_W; x++)
        if (row[x]) {
            if ((k * DK_ROW_CAP) % n >= DK_ROW_CAP) row[x] = 0;
            k++;
        }
}

// fade in over the first 4 s of each pass of t ms, and out over its last 4
static uint8_t dk_pass_fade(uint32_t t, uint32_t pass) {
    return t < 4000 ? (uint8_t)(t * 16 / 4000) : t > pass - 4000 ? (uint8_t)((pass - t) * 16 / 4000) : 16;
}

#define DK_DREAM_BAND(scene)                                            \
    void dream_band(uint32_t ms, uint8_t line, uint8_t *buf) {          \
        if (ms != dk_frame) {                                           \
            dk_frame = ms;                                              \
            dk_n = 0;                                                   \
            scene(ms);                                                  \
        }                                                               \
        memset(buf, 0, DK_W * 8);                                       \
        const int16_t y0 = (int16_t)line * 8;                           \
        for (uint16_t i = 0; i < dk_n; i++) dk_draw(&dk_p[i], buf, y0);  \
        for (uint8_t j = 0; j < 8; j++) dk_cap(&buf[j * DK_W]);         \
    }

#endif
