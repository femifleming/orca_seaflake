// Orca Seaflake for Teletype: the grid screen, patch/song pickers and the
// song tracker. Shared by the firmware and the desktop emulator.
//
// The OLED shows 21 x 7 cells of the 80 x 40 grid plus a status line; the
// view scrolls to follow the cursor.

#include "seaflake_mode.h"

#include "seaflake/sf_str.h"
#include <string.h>

#include "font.h"
#include "globals.h"
#include "keyboard_helper.h"
#include "kbd.h"
#include "region.h"
#include "seaflake/sf.h"
#include "seaflake/sf_ii.h"
#include "seaflake/sf_config.h"
#include "seaflake/sf_store.h"
#include "teletype_io.h"

#define COLS 21
#define ROWS 7

enum { V_GRID, V_PATCH_SAVE, V_PATCH_LOAD, V_SONG, V_SONG_SAVE, V_SONG_LOAD, V_SCAN };

static uint8_t view = V_GRID;
static int16_t cx, cy, vx, vy;    // cursor, top left of the view
static int16_t ax = -1, ay;       // selection anchor, ax < 0 = no selection
static bool insert_mode;
static bool caps;   // caps lock on the grid
static bool dirty = true;

static char undo_grid[SF_CELLS];
static bool undo_ok;
#define CLIP_MAX 1024
static char clip[CLIP_MAX];
static uint8_t clip_w, clip_h;

static char msg[22];
static uint32_t msg_until;

// pickers and song view
static uint8_t pick, pick_top, confirm;
static uint8_t srow, stop, sfield;
static uint16_t sbuf;
static bool sbuf_on;

// frame clock: next frame at next_ms + next_frac_us / 1000
static uint32_t next_ms, next_frac;
static volatile bool pending;

static void say(const char *s) {
    sf_fmt(msg, sizeof(msg), "%s", s);
    msg_until = tele_get_ticks() + 1500;
    dirty = true;
}

////////////////////////////////////////////////////////////////////////////
// patches

static void reset_clock(uint32_t now) {
    next_ms = now;
    next_frac = 0;
}

bool sf_io_patch_load(uint8_t n) {
    const uint8_t *d;
    uint16_t len = sf_store_patch_get(n, &d);
    if (!len) return false;
    uint32_t f = sf.f;
    sf_unpack(d, len);
    // keep the frame count while playing so D, C and U stay in time
    if (sf.playing) sf.f = f;
    sf.patch = n;
    sf.modified = 0;
    dirty = true;
    return true;
}

static void snapshot(void) {
    memcpy(undo_grid, sf.grid, SF_CELLS);
    undo_ok = true;
}

static void undo(void) {
    if (!undo_ok) return;
    for (int i = 0; i < SF_CELLS; i++) {
        char t = sf.grid[i];
        sf.grid[i] = undo_grid[i];
        undo_grid[i] = t;
    }
    sf.modified = 1;
    say("UNDO");
}

// say what the last scan found
static void say_scan(void) {
    char s[22];
    const bool jf = sf_jf_state == 1;
    if (sf_scan.ex_unit && sf_scan.er_unit)
        sf_fmt(s, sizeof(s), jf ? "FOUND EX ER-301 JF" : "FOUND EX + ER-301");
    else if (sf_scan.ex_unit)
        sf_fmt(s, sizeof(s), jf ? "FOUND EX + JF" : "FOUND DISTING EX");
    else if (sf_scan.er_unit)
        sf_fmt(s, sizeof(s), jf ? "FOUND ER-301 + JF" : "FOUND ER-301, NO EX");
    else
        sf_fmt(s, sizeof(s), jf ? "FOUND JUST FRIENDS" : "NO EX OR ER-301 FOUND");
    say(s);
}

// look for the Disting EX (0x41) again (ctrl-i, enter) and say what was found
static void scan(void) {
    sf_scan_run();
    sf_store_set_scan(&sf_scan);
    say_scan();
}

// Power-on. The Disting EX and the ER-301 can still be starting when the
// Teletype is (an ER-301 takes well over 10 s from a cold case), so the scan
// repeats without holding up the grid: every half second for the first
// BOOT_FAST_MS, then every BOOT_SLOW_EVERY until BOOT_SCAN_MS, until both kinds
// have answered. The startup preset is sent the first time the Disting EX
// answers (whenever that is); the ` loadbangs fire then too, or at
// BOOT_LOADBANG_MS at the latest.
#define BOOT_FAST_MS 10000
#define BOOT_FAST_EVERY 500
#define BOOT_SLOW_EVERY 2000
#define BOOT_LOADBANG_MS 30000
#define BOOT_SCAN_MS 120000
static uint32_t boot_start, boot_next;  // boot_next 0 = the power-on scan is over
static bool boot_ex_done;

static void boot_scan(uint32_t now) {
    sf_scan_run();
    const uint32_t t = now - boot_start;
    if (!boot_ex_done && sf_scan_ex_here()) {
        boot_ex_done = true;
        sf_boot_preset();  // sends nothing if none is set
        say_scan();
    }
    if (sf.loadbang == 2 && (boot_ex_done || t >= BOOT_LOADBANG_MS))
        sf.loadbang = 1;  // the loadbangs fire on the next frame
    // (the ER-301 is not probed: it shows up when an SC write is acknowledged)
    if (sf_scan_ex_here() || t >= BOOT_SCAN_MS) {
        boot_next = 0;
        if (sf.loadbang == 2) sf.loadbang = 1;
        sf_store_set_scan(&sf_scan);  // (a flash write only if it changed)
        say_scan();
    }
    else {
        if (sf_scan.ex_unit || sf_scan.er_unit) sf_store_set_scan(&sf_scan);
        boot_next = now + (t < BOOT_FAST_MS ? BOOT_FAST_EVERY : BOOT_SLOW_EVERY);
        if (!boot_next) boot_next = 1;
    }
}

static void play(bool on);

void seaflake_init(void) {
    sf_init();
    sf_store_init();
    sf.loadbang = 2;  // ` waits for the power-on scan (boot_scan)
    uint8_t n = sf_store_last_patch();
    if (n >= 1 && n <= SF_PATCH_SLOTS) sf_io_patch_load(n);   // (at the tempo it was saved with)
    const uint32_t now = tele_get_ticks();
    boot_start = now;
    boot_ex_done = false;
    boot_scan(now);

    // and it plays at once: the song that was playing when you last saved,
    // or else the last patch
    sf_song_t song;
    const uint8_t boot = sf_store_boot_song();
    if (boot && sf_store_song_get(boot, &song)) {
        sf.song = song;
        sf.song_no = boot;
        reset_clock(tele_get_ticks());
        sf_song_start();
    }
    else {
        // song 1, if there is one, is ready in the song view
        if (sf_store_song_get(1, &song)) {
            sf.song = song;
            sf.song_no = 1;
        }
        play(true);
    }
}

// what plays at the next power-on, decided at every save: the song, if one is
// playing (and has been saved), or else the patch
static void remember(void) {
    sf_store_set_boot_song(sf.song_playing && sf.song_no ? sf.song_no : 0);
}

////////////////////////////////////////////////////////////////////////////
// clock

bool seaflake_due(uint32_t now) {
    if (pending) return false;
    bool d = (sf.playing && (int32_t)(now - next_ms) >= 0) ||
             (sf.next_due != 0xFFFFFFFF && (int32_t)(now - sf.next_due) >= 0) ||
             (boot_next && (int32_t)(now - boot_next) >= 0);
    if (d) pending = true;
    return d;
}

void seaflake_tick(uint32_t now) {
    pending = false;
    if (boot_next && (int32_t)(now - boot_next) >= 0) {
        boot_scan(now);
        dirty = true;
    }
    if (sf.playing) {
        // more than 4 frames late (a long flash write): skip ahead
        if ((int32_t)(now - next_ms) > (int32_t)(4 * sf_frame_us() / 1000 + 4))
            reset_clock(now);
        uint8_t guard = 0;
        while (sf.playing && (int32_t)(now - next_ms) >= 0 && guard++ < 4) {
            sf_frame(now);
            next_frac += sf_frame_us();
            next_ms += next_frac / 1000;
            next_frac %= 1000;
            dirty = true;
        }
    }
    if (sf.next_due != 0xFFFFFFFF && (int32_t)(now - sf.next_due) >= 0) {
        sf_ms(now);
        dirty = true;
    }
    if (sf.dirty) dirty = true;
}

static void play(bool on) {
    if (on && !sf.playing) reset_clock(tele_get_ticks());
    sf_play(on);
    dirty = true;
}

////////////////////////////////////////////////////////////////////////////
// drawing

// The Teletype font's lowercase letters are its capitals a little smaller, so
// C and c look alike. In Orca case matters (lowercase waits for a bang, and a
// lowercase note is a sharp), so the grid has its own lowercase: short letters
// on rows 3-6 with ascenders and descenders, under capitals that fill rows 1-6.
// One byte per column, bit y = row y, as in the Teletype font.
static const struct { uint8_t w, col[5]; } lower[26] = {
    { 4, { 0x30, 0x48, 0x48, 0x78, 0x00 } },   // a
    { 4, { 0x7e, 0x48, 0x48, 0x30, 0x00 } },   // b
    { 4, { 0x30, 0x48, 0x48, 0x48, 0x00 } },   // c
    { 4, { 0x30, 0x48, 0x48, 0x7e, 0x00 } },   // d
    { 4, { 0x30, 0x58, 0x58, 0x50, 0x00 } },   // e
    { 4, { 0x08, 0x7c, 0x0a, 0x02, 0x00 } },   // f
    { 4, { 0x10, 0xa8, 0xa8, 0x78, 0x00 } },   // g
    { 4, { 0x7e, 0x08, 0x08, 0x70, 0x00 } },   // h
    { 4, { 0x48, 0x7a, 0x40, 0x00, 0x00 } },   // i
    { 4, { 0x80, 0x88, 0x7a, 0x00, 0x00 } },   // j
    { 4, { 0x7e, 0x20, 0x50, 0x08, 0x00 } },   // k
    { 4, { 0x42, 0x7e, 0x40, 0x00, 0x00 } },   // l
    { 5, { 0x78, 0x08, 0x78, 0x08, 0x70 } },   // m
    { 4, { 0x78, 0x08, 0x08, 0x70, 0x00 } },   // n
    { 4, { 0x30, 0x48, 0x48, 0x30, 0x00 } },   // o
    { 4, { 0xf8, 0x28, 0x28, 0x10, 0x00 } },   // p
    { 4, { 0x10, 0x28, 0x28, 0xf8, 0x00 } },   // q
    { 4, { 0x78, 0x10, 0x08, 0x08, 0x00 } },   // r
    { 4, { 0x50, 0x58, 0x68, 0x28, 0x00 } },   // s
    { 4, { 0x08, 0x3e, 0x48, 0x40, 0x00 } },   // t
    { 4, { 0x38, 0x40, 0x40, 0x78, 0x00 } },   // u
    { 5, { 0x18, 0x20, 0x40, 0x20, 0x18 } },   // v
    { 5, { 0x38, 0x40, 0x30, 0x40, 0x38 } },   // w
    { 4, { 0x48, 0x30, 0x30, 0x48, 0x00 } },   // x
    { 4, { 0x18, 0xa0, 0xa0, 0x78, 0x00 } },   // y
    { 4, { 0x48, 0x68, 0x58, 0x48, 0x00 } },   // z

};

static void draw_char(region *r, uint8_t col, char ch, uint8_t fg, uint8_t bg) {
    uint8_t x0 = col * 6;
    r->dirty = 1;  // region_draw only sends dirty lines to the OLED
    for (uint8_t y = 0; y < 8; y++)
        for (uint8_t x = 0; x < 6; x++) r->data[y * r->w + x0 + x] = bg;
    if (ch >= 'a' && ch <= 'z') {
        const uint8_t w = lower[ch - 'a'].w, off = (uint8_t)((6 - w) / 2);
        for (uint8_t i = 0; i < w; i++) {
            const uint8_t bits = lower[ch - 'a'].col[i];
            for (uint8_t y = 0; y < 8; y++)
                if (bits & (1 << y)) r->data[y * r->w + x0 + off + i] = fg;
        }
        return;
    }
    if (ch == '`') {
        // the ` loadbang: the Teletype font draws 0x60 as a pound sign
        for (uint8_t i = 0; i < 3; i++) r->data[(1 + i) * r->w + x0 + 2 + i] = fg;
        return;
    }
    if (ch == '-' || ch == '_') {
        // the Teletype font's - is 2 pixels and its _ sits on the cell's bottom
        // edge: draw both 4 wide so the trigger operators read on the grid
        const uint8_t y = ch == '-' ? 4 : 6;
        for (uint8_t i = 0; i < 4; i++) r->data[y * r->w + x0 + 1 + i] = fg;
        return;
    }
    if (ch < 0x21 || ch > 0x7e) return;
    const glyph_t *g = &font_data[ch - FONT_ASCII_OFFSET];
    uint8_t w = FONT_CHARW - g->first - g->last;
    uint8_t off = (6 - w) / 2;
    for (uint8_t i = 0; i < w; i++) {
        uint8_t bits = g->data[g->first + i];
        for (uint8_t y = 0; y < 8; y++)
            if (bits & (1 << y)) r->data[y * r->w + x0 + off + i] = fg;
    }
}

static void draw_text(region *r, uint8_t col, const char *s, uint8_t fg, uint8_t bg) {
    while (*s && col < COLS) draw_char(r, col++, *s++, fg, bg);
}

static void sel_rect(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1) {
    if (ax < 0) {
        *x0 = *x1 = cx;
        *y0 = *y1 = cy;
        return;
    }
    *x0 = ax < cx ? ax : cx;
    *x1 = ax < cx ? cx : ax;
    *y0 = ay < cy ? ay : cy;
    *y1 = ay < cy ? cy : ay;
}

static void follow(void) {
    if (cx < vx + 1) vx = cx - 1;
    if (cx > vx + COLS - 2) vx = cx - COLS + 2;
    if (cy < vy + 1) vy = cy - 1;
    if (cy > vy + ROWS - 2) vy = cy - ROWS + 2;
    if (vx < 0) vx = 0;
    if (vy < 0) vy = 0;
    if (vx > SF_W - COLS) vx = SF_W - COLS;
    if (vy > SF_H - ROWS) vy = SF_H - ROWS;
}

static void draw_grid(void) {
    int16_t x0, y0, x1, y1;
    sel_rect(&x0, &y0, &x1, &y1);
    follow();
    for (uint8_t row = 0; row < ROWS; row++) {
        region *r = &line[row];
        int16_t y = vy + row;
        for (uint8_t col = 0; col < COLS; col++) {
            int16_t x = vx + col;
            char g = sf_get(x, y);
            bool sel = x >= x0 && x <= x1 && y >= y0 && y <= y1;
            bool cur = x == cx && y == cy;
            uint8_t fg, bg = 0;
            if (g == '.') {
                // Orca's grid markers every 8 cells
                if (x % 8 == 0 && y % 8 == 0) g = '+';
                fg = sf_locked(x, y) ? 5 : 2;
                if (sf_picked(x, y)) fg = 12, bg = 4;
            }
            else if (g == '*')
                fg = 15, bg = 5;
            else if (sf_picked(x, y))
                fg = 15, bg = 4;   // the value T (or O, P, Q) has picked, lit as in Orca
            else if (sf_locked(x, y))
                fg = 8;  // operand or output of an operator
            else if (sf_is_op(g))
                fg = (g >= 'a' && g <= 'z') ? 11 : 15;
            else
                fg = 6;
            if (sel) {
                bg = 4;
                if (fg < 9) fg = 10;
            }
            if (cur) {
                fg = 0;
                bg = insert_mode ? 10 : 15;
            }
            draw_char(r, col, g, fg, bg);
        }
    }

    // status line
    region *r = &line[7];
    region_fill(r, 0);
    char s[24];
    if (msg[0] && (int32_t)(tele_get_ticks() - msg_until) < 0) {
        draw_text(r, 0, msg, 15, 0);
        return;
    }
    msg[0] = 0;
    // what the cursor is on: an operator, or the inlet of the operator that
    // reads it (so N inside >n.4C says EX OCTAVE, not NORTH)
    char nbuf[18];
    const char *name = sf_cell_name(cx, cy, nbuf, sizeof(nbuf));
    // an I2C message the device did not acknowledge, shown for 2 s
    if (sf_ii_fail_at && tele_get_ticks() - sf_ii_fail_at < 2000) {
        sf_fmt(s, sizeof(s), "I2C %X %s CMD %02X", sf_ii_fail_addr,
               sf_ii_status_name(sf_ii_fail_code), sf_ii_fail_cmd);
        draw_text(r, 0, s, 15, 0);
    }
    else if (sf.error && sf_ii_error[0]) {
        draw_text(r, 0, "ERR ", 15, 0);
        draw_text(r, 4, sf_ii_error, 15, 0);
    }
    else if (ax >= 0) {
        sf_fmt(s, sizeof(s), "SELECT %dx%d", x1 - x0 + 1, y1 - y0 + 1);
        draw_text(r, 0, s, 10, 0);
    }
    else if (name) {
        char t[17];
        sf_copy(t, name, sizeof(t));
        draw_text(r, 0, t, 12, 0);
    }
    else {
        if (sf.patch)
            sf_fmt(s, sizeof(s), "P%02d%c %02d,%02d", sf.patch, sf.modified ? '*' : ' ', cx, cy);
        else
            sf_fmt(s, sizeof(s), "NEW%c %02d,%02d", sf.modified ? '*' : ' ', cx, cy);
        draw_text(r, 0, s, 6, 0);
    }
    sf_fmt(s, sizeof(s), "%3d", sf.bpm);
    draw_text(r, 17, s, 8, 0);
    if (caps) draw_char(r, 16, 'A', 10, 3);   // caps lock is on
    // play state, S while a song plays; bright on the beat
    draw_char(r, 20, sf.playing ? (sf.song_playing ? 'S' : '>') : '|',
              sf.playing && (sf.f % 4) == 1 ? 15 : 7, 0);
}

// first glyphs of a packed patch, for the picker
static void patch_preview(uint8_t n, char *out, uint8_t max) {
    const uint8_t *d;
    uint16_t len = sf_store_patch_get(n, &d);
    uint8_t k = 0;
    if (!len) {
        sf_fmt(out, max, "--");
        return;
    }
    uint16_t i;
    sf_patch_bpm(d, len, &i);
    for (; i < len && k < max - 1; i++) {
        if (d[i] >= 0x80) {
            if (k && out[k - 1] != ' ') out[k++] = ' ';
        }
        else
            out[k++] = d[i];
    }
    out[k] = 0;
}

static uint8_t song_count(const sf_song_t *s) {
    uint8_t n = 0;
    for (int i = 0; i < SF_SONG_LEN; i++)
        if (s->slots[i].patch) n++;
    return n;
}

static void draw_picker(void) {
    bool patches = view == V_PATCH_SAVE || view == V_PATCH_LOAD;
    bool saving = view == V_PATCH_SAVE || view == V_SONG_SAVE;
    uint8_t count = patches ? SF_PATCH_SLOTS : SF_SONG_SLOTS;
    if (pick < pick_top) pick_top = pick;
    if (pick >= pick_top + 6) pick_top = pick - 5;

    region_fill(&line[0], 0);
    draw_text(&line[0], 0,
              patches ? (saving ? "SAVE PATCH" : "LOAD PATCH")
                      : (saving ? "SAVE SONG" : "LOAD SONG"),
              15, 0);
    for (uint8_t i = 0; i < 6; i++) {
        region *r = &line[i + 1];
        region_fill(r, 0);
        uint8_t n = pick_top + i;
        if (n >= count) continue;
        bool on = n == pick;
        uint8_t bg = on ? 4 : 0;
        char s[24], p[18];
        if (patches)
            patch_preview(n + 1, p, sizeof(p));
        else {
            sf_song_t song;
            if (sf_store_song_get(n + 1, &song))
                sf_fmt(p, sizeof(p), "%d SLOTS", song_count(&song));
            else
                sf_fmt(p, sizeof(p), "--");
        }
        sf_fmt(s, sizeof(s), "%c%02d %s", patches ? 'P' : 'S', n + 1, p);
        for (uint8_t c = 0; c < COLS; c++) draw_char(r, c, ' ', 0, bg);
        draw_text(r, 0, s, on ? 15 : 7, bg);
    }
    region_fill(&line[7], 0);
    if (confirm)
        draw_text(&line[7], 0, "ENTER AGAIN: REPLACE", 15, 0);
    else
        draw_text(&line[7], 0, saving ? "ENTER SAVE  ESC BACK" : "ENTER/BUTTON LOAD", 6, 0);
}

static void draw_song(void) {
    static const char *const modes[3] = { "ORD", "REV", "RND" };
    char s[24];
    if (srow < stop) stop = srow;
    if (srow >= stop + 6) stop = srow - 5;

    region_fill(&line[0], 0);
    if (sf.song_no)
        sf_fmt(s, sizeof(s), "SONG %d%c", sf.song_no, sf.song_modified ? '*' : ' ');
    else
        sf_fmt(s, sizeof(s), "SONG -%c", sf.song_modified ? '*' : ' ');
    draw_text(&line[0], 0, s, 15, 0);
    draw_text(&line[0], 8, modes[sf.song.mode % 3], 8, 0);
    draw_text(&line[0], 12, sf.song.loop ? "LOOP" : "ONCE", 8, 0);
    draw_text(&line[0], 17, sf.song_playing ? "PLAY" : "STOP", sf.song_playing ? 15 : 6, 0);

    int8_t active = sf.song_playing ? sf.order[sf.order_idx] : -1;
    for (uint8_t i = 0; i < 6; i++) {
        region *r = &line[i + 1];
        region_fill(r, 0);
        uint8_t n = stop + i;
        if (n >= SF_SONG_LEN) continue;
        sf_song_slot_t *sl = &sf.song.slots[n];
        sf_fmt(s, sizeof(s), "%c%02d", n == active ? '>' : ' ', n + 1);
        draw_text(r, 0, s, n == active ? 15 : 6, 0);
        char f[4][5];
        if (sl->patch) {
            sf_fmt(f[0], 5, "P%02d", sl->patch);
            sf_fmt(f[1], 5, "%03d", sl->len);
            sf_fmt(f[2], 5, "%03d", sl->mul);
            sf_fmt(f[3], 5, "%03d", sl->bpm);
        }
        else
            for (int k = 0; k < 4; k++) sf_fmt(f[k], 5, "...");
        for (uint8_t k = 0; k < 4; k++) {
            bool on = n == srow && k == sfield;
            if (on && sbuf_on) sf_fmt(f[k], 5, k == 0 ? "P%02d" : "%03d", sbuf);
            uint8_t bg = on ? 15 : (n == srow ? 3 : 0);
            uint8_t fg = on ? 0 : (sl->patch ? 12 : 3);
            draw_text(r, 4 + k * 4, f[k], fg, bg);
            if (k < 3) draw_char(r, 7 + k * 4, ' ', 0, n == srow ? 3 : 0);
        }
    }
    region_fill(&line[7], 0);
    if (msg[0] && (int32_t)(tele_get_ticks() - msg_until) < 0)
        draw_text(&line[7], 0, msg, 15, 0);
    else {
        msg[0] = 0;
        draw_text(&line[7], 0, " #  PAT LEN MUL BPM", 5, 0);
    }
}

static void draw_scan(void) {
    char s[24];
    // fixed addresses: the Disting EX at 0x41, the ER-301 at 0x31
    for (uint8_t i = 0; i < 6; i++) region_fill(&line[i], 0);
    draw_text(&line[0], 0, "I2C  ENTER=LOOK AGAIN", 15, 0);
    if (sf_scan.ex[0]) {
        draw_text(&line[1], 0, "DISTING EX 41 FOUND", 15, 0);
        sf_fmt(s, sizeof(s), "  ALG %d  PRESET %d", sf_scan.ex_alg[0], sf_scan.ex_preset[0]);
        draw_text(&line[2], 0, s, 10, 0);
    }
    else
        draw_text(&line[1], 0, "DISTING EX 41 --", 4, 0);
    draw_text(&line[4], 0, sf_scan.er[0] ? "ER-301 31 FOUND" : "ER-301 31 -- TRY <P1", sf_scan.er[0] ? 15 : 4, 0);
    draw_text(&line[5], 0, sf_jf_state == 1 ? "JUST FRIENDS 70 FOUND" : sf_jf_state == 2 ? "JUST FRIENDS 70 --" : "JUST FRIENDS 70 ?",
              sf_jf_state == 1 ? 15 : 4, 0);
    // the bus at power-on (sf_bus_init in main.c): SCL rate and pull-ups
    {
        region *r = &line[6];
        region_fill(r, 0);
        // (21 characters fit)
        const char *pu = sf_bus.recovered ? "SDA FREED"
                         : sf_bus.stuck ? (sf_bus.stuck & 1 ? "SDA HELD LOW" : "SCL HELD LOW")
                         : sf_bus.ext_pullup == 3 ? "PULLUPS OK"
                         : sf_bus.ext_pullup ? (sf_bus.ext_pullup & 1 ? "NO SCL PULLUP" : "NO SDA PULLUP")
                                             : "NO PULLUPS";
        sf_fmt(s, sizeof(s), "BUS %dK %s", sf_bus.khz, pu);
        draw_text(r, 0, s, sf_bus.stuck || sf_bus.ext_pullup != 3 ? 15 : 10, 0);
    }
    // the operators' I2C messages since power-on: sent, and not answered
    region *r = &line[7];
    region_fill(r, 0);
    // OK n FAIL n, and the last failure: NACK = the device refused a byte,
    // BUSY = gave up waiting (a device holding the clock), then its command
    if (sf_ii_failed)
        sf_fmt(s, sizeof(s), "OK %d FAIL %d %s %02X", sf_ii_sent - sf_ii_failed, sf_ii_failed,
               sf_ii_status_name(sf_ii_fail_code), sf_ii_fail_cmd);
    else
        sf_fmt(s, sizeof(s), "I2C OK %d FAIL 0", sf_ii_sent);
    draw_text(r, 0, s, sf_ii_failed ? 15 : 10, 0);
}

uint8_t screen_refresh_seaflake(void) {
    bool timed = msg[0] != 0;
    if (!dirty && !sf.dirty && !timed) return 0;
    dirty = false;
    sf.dirty = 0;
    switch (view) {
        case V_GRID: draw_grid(); break;
        case V_SONG: draw_song(); break;
        case V_SCAN: draw_scan(); break;
        default: draw_picker(); break;
    }
    return 0xFF;
}

void set_seaflake_mode(void) {
    dirty = true;
}

////////////////////////////////////////////////////////////////////////////
// keys: grid

static void erase_selection(void) {
    int16_t x0, y0, x1, y1;
    sel_rect(&x0, &y0, &x1, &y1);
    snapshot();
    for (int16_t y = y0; y <= y1; y++)
        for (int16_t x = x0; x <= x1; x++) sf_set(x, y, '.');
}

static void copy_selection(void) {
    int16_t x0, y0, x1, y1;
    sel_rect(&x0, &y0, &x1, &y1);
    uint16_t w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w * h > CLIP_MAX) {
        say("TOO BIG TO COPY");
        return;
    }
    clip_w = w;
    clip_h = h;
    for (int16_t y = 0; y < h; y++)
        for (int16_t x = 0; x < w; x++) clip[y * w + x] = sf_get(x0 + x, y0 + y);
}

static void paste(void) {
    if (!clip_w) return;
    snapshot();
    for (int16_t y = 0; y < clip_h; y++)
        for (int16_t x = 0; x < clip_w; x++) sf_set(cx + x, cy + y, clip[y * clip_w + x]);
}

static void move(int16_t dx, int16_t dy, bool select) {
    if (select && ax < 0) {
        ax = cx;
        ay = cy;
    }
    if (!select) ax = -1;
    cx += dx;
    cy += dy;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= SF_W) cx = SF_W - 1;
    if (cy >= SF_H) cy = SF_H - 1;
}

static void open_picker(uint8_t v) {
    view = v;
    confirm = 0;
    bool patches = v == V_PATCH_SAVE || v == V_PATCH_LOAD;
    uint8_t cur = patches ? sf.patch : sf.song_no;
    pick = cur ? cur - 1 : 0;
    pick_top = pick > 5 ? pick - 5 : 0;
}

// What a key types on the grid. The Teletype's own table gives capitals for
// every letter (Teletype has no lowercase); Orca needs both: a letter alone is
// lowercase, with shift uppercase, and caps lock swaps them.
static uint8_t grid_ascii(uint8_t k, uint8_t m) {
    if (k >= HID_A && k <= HID_Z) {
        const bool upper = (mod_only_shift(m) != 0) != caps;
        return (uint8_t)((upper ? 'A' : 'a') + (k - HID_A));
    }
    return hid_to_ascii(k, m);
}

static void grid_keys(uint8_t k, uint8_t m) {
    bool alt = mod_only_alt(m), shift = mod_only_shift(m), ctrl = mod_only_ctrl(m);
    int16_t step = alt ? 8 : 1;
    if (k == HID_UP && (no_mod(m) || alt || shift)) move(0, -step, shift);
    else if (k == HID_DOWN && (no_mod(m) || alt || shift)) move(0, step, shift);
    else if (k == HID_LEFT && (no_mod(m) || alt || shift)) move(-step, 0, shift);
    else if (k == HID_RIGHT && (no_mod(m) || alt || shift)) move(step, 0, shift);
    else if (match_no_mod(m, k, HID_PAGEUP)) move(0, -ROWS, false);
    else if (match_no_mod(m, k, HID_PAGEDOWN)) move(0, ROWS, false);
    else if (match_no_mod(m, k, HID_HOME)) move(-cx, 0, false);
    else if (match_no_mod(m, k, HID_END)) move(SF_W - 1 - cx, 0, false);
    else if (match_no_mod(m, k, HID_SPACEBAR)) play(!sf.playing);
    else if (match_no_mod(m, k, HID_ENTER)) {
        insert_mode = !insert_mode;
        say(insert_mode ? "INSERT ON" : "INSERT OFF");
    }
    else if (match_no_mod(m, k, HID_BACKSPACE)) {
        if (ax >= 0)
            erase_selection();
        else {
            snapshot();
            if (insert_mode && cx > 0) cx--;
            sf_set(cx, cy, '.');
        }
        ax = -1;
    }
    else if (match_no_mod(m, k, HID_DELETE)) {
        erase_selection();
        ax = -1;
    }
    else if (ctrl && k == HID_C) {
        copy_selection();
        say("COPIED");
    }
    else if (ctrl && k == HID_X) {
        copy_selection();
        erase_selection();
        ax = -1;
    }
    else if (ctrl && k == HID_V) paste();
    else if (ctrl && k == HID_Z) undo();
    else if (ctrl && k == HID_A) {
        ax = 0;
        ay = 0;
        cx = SF_W - 1;
        cy = SF_H - 1;
    }
    else if (ctrl && k == HID_F) {
        // one frame while stopped
        if (!sf.playing) sf_frame(tele_get_ticks());
    }
    else if (ctrl && k == HID_R) {
        sf.f = 0;
        say("FRAME 0");
    }
    else if (ctrl && k == HID_N) {
        snapshot();
        uint16_t bpm = sf.bpm;
        sf_clear();
        sf.bpm = bpm;
        sf.patch = 0;
        say("NEW PATCH");
    }
    else if (ctrl && k == HID_S) open_picker(V_PATCH_SAVE);
    else if (ctrl && k == HID_O) open_picker(V_PATCH_LOAD);
    else if (ctrl && k == HID_T) view = V_SONG;
    else if (ctrl && k == HID_I) {
        view = V_SCAN;
    }
    else if (alt && (k == HID_OPEN_BRACKET || k == HID_CLOSE_BRACKET))
        sf_set_bpm(sf.bpm + (k == HID_OPEN_BRACKET ? -1 : 1));
    else if (ctrl && (k == HID_OPEN_BRACKET || k == HID_CLOSE_BRACKET))
        sf_set_bpm(sf.bpm + (k == HID_OPEN_BRACKET ? -10 : 10));
    else if (match_no_mod(m, k, HID_CAPS_LOCK)) {
        caps = !caps;
        say(caps ? "CAPS ON" : "CAPS OFF");
    }
    else if (no_mod(m) || shift) {
        uint8_t c = grid_ascii(k, m);
        if (c > 32 && c < 127) {
            snapshot();
            sf_set(cx, cy, c);
            ax = -1;
            if (insert_mode && cx < SF_W - 1) cx++;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// keys: pickers

static void picker_keys(uint8_t k, uint8_t m) {
    bool patches = view == V_PATCH_SAVE || view == V_PATCH_LOAD;
    uint8_t count = patches ? SF_PATCH_SLOTS : SF_SONG_SLOTS;
    if (k == HID_UP && pick > 0) pick--, confirm = 0;
    else if (k == HID_DOWN && pick < count - 1) pick++, confirm = 0;
    else if (k == HID_PAGEUP) pick = pick > 6 ? pick - 6 : 0, confirm = 0;
    else if (k == HID_PAGEDOWN) pick = pick + 6 < count ? pick + 6 : count - 1, confirm = 0;
    else if (match_no_mod(m, k, HID_ENTER)) {
        uint8_t n = pick + 1;
        char s[22];
        if (view == V_PATCH_SAVE) {
            const uint8_t *d;
            if (sf_store_patch_get(n, &d) && n != sf.patch && !confirm) {
                confirm = 1;
                return;
            }
            static uint8_t buf[SF_PACK_MAX];
            uint16_t len = sf_pack(buf, sizeof(buf));
            if (!len) {
                say("PATCH TOO BIG");
                view = V_GRID;
                return;
            }
            sf_store_patch_put(n, buf, len);
            sf_store_set_last_patch(n);
            sf.patch = n;
            sf.modified = 0;
            remember();
            sf_fmt(s, sizeof(s), "SAVED P%02d %dB", n, len);
            say(s);
            view = V_GRID;
        }
        else if (view == V_PATCH_LOAD) {
            snapshot();
            if (sf_io_patch_load(n)) {
                sf_store_set_last_patch(n);
                sf_fmt(s, sizeof(s), "LOADED P%02d", n);
            }
            else
                sf_fmt(s, sizeof(s), "P%02d IS EMPTY", n);
            say(s);
            view = V_GRID;
        }
        else if (view == V_SONG_SAVE) {
            sf_song_t old;
            if (sf_store_song_get(n, &old) && n != sf.song_no && !confirm) {
                confirm = 1;
                return;
            }
            sf_store_song_put(n, &sf.song);
            sf.song_no = n;
            sf.song_modified = 0;
            remember();
            sf_fmt(s, sizeof(s), "SAVED SONG %d", n);
            say(s);
            view = V_SONG;
        }
        else if (view == V_SONG_LOAD) {
            sf_song_t song;
            if (sf_store_song_get(n, &song)) {
                if (sf.song_playing) sf_song_stop();
                sf.song = song;
                sf.song_no = n;
                sf.song_modified = 0;
                sf_fmt(s, sizeof(s), "LOADED SONG %d", n);
            }
            else
                sf_fmt(s, sizeof(s), "SONG %d IS EMPTY", n);
            say(s);
            view = V_SONG;
        }
    }
}

////////////////////////////////////////////////////////////////////////////
// keys: song tracker

static uint16_t *song_field(sf_song_slot_t *sl) {
    switch (sfield) {
        case 1: return &sl->len;
        case 2: return &sl->mul;
        case 3: return &sl->bpm;
    }
    return NULL;
}

static void song_set(sf_song_slot_t *sl, int32_t v) {
    if (sfield == 0) {
        if (v < 1) v = 1;
        if (v > SF_PATCH_SLOTS) v = SF_PATCH_SLOTS;
        sl->patch = v;
    }
    else {
        int32_t mn = sfield == 3 ? SF_BPM_MIN : 1, mx = sfield == 3 ? SF_BPM_MAX : 999;
        if (v < mn) v = mn;
        if (v > mx) v = mx;
        *song_field(sl) = v;
    }
    sf.song_modified = 1;
}

static void song_fill(sf_song_slot_t *sl) {
    if (sl->patch) return;
    sl->patch = sf.patch ? sf.patch : 1;
    sl->len = 16;
    sl->mul = 1;
    sl->bpm = sf.bpm;
}

static void song_commit(void) {
    if (!sbuf_on) return;
    sf_song_slot_t *sl = &sf.song.slots[srow];
    song_fill(sl);
    song_set(sl, sbuf);
    sbuf_on = false;
}

static void song_keys(uint8_t k, uint8_t m) {
    sf_song_slot_t *sl = &sf.song.slots[srow];
    bool ctrl = mod_only_ctrl(m);
    char s[22];
    if (k == HID_UP || k == HID_DOWN || k == HID_LEFT || k == HID_RIGHT) song_commit();

    if (k == HID_UP && srow > 0) srow--;
    else if (k == HID_DOWN && srow < SF_SONG_LEN - 1) srow++;
    else if (k == HID_LEFT && sfield > 0) sfield--;
    else if (k == HID_RIGHT && sfield < 3) sfield++;
    else if (ctrl && k == HID_T) {
        song_commit();
        view = V_GRID;
    }
    else if (ctrl && k == HID_S) {
        song_commit();
        open_picker(V_SONG_SAVE);
    }
    else if (ctrl && k == HID_O) open_picker(V_SONG_LOAD);
    else if (ctrl && k == HID_N) {
        sf_song_clear();
        sf.song_no = 0;
        say("NEW SONG");
    }
    else if (match_no_mod(m, k, HID_SPACEBAR)) {
        song_commit();
        if (sf.song_playing)
            sf_song_stop();
        else {
            reset_clock(tele_get_ticks());
            sf_song_start();
            if (!sf.song_playing) say("NO SONG SLOTS");
        }
    }
    else if (match_no_mod(m, k, HID_ENTER)) {
        if (sbuf_on) {
            song_commit();
            return;
        }
        // this slot plays the patch on the grid
        if (!sf.patch || sf.modified) {
            say("SAVE PATCH: CTRL-S");
            return;
        }
        bool was = sl->patch != 0;
        song_fill(sl);
        sl->patch = sf.patch;
        if (!was) sl->bpm = sf.bpm;
        sf.song_modified = 1;
        sf_fmt(s, sizeof(s), "SLOT %02d = P%02d", srow + 1, sf.patch);
        say(s);
    }
    else if (match_no_mod(m, k, HID_BACKSPACE) || match_no_mod(m, k, HID_DELETE)) {
        if (sbuf_on) {
            sbuf /= 10;
            if (!sbuf) sbuf_on = false;
            return;
        }
        memset(sl, 0, sizeof(*sl));
        sf.song_modified = 1;
    }
    else if (no_mod(m) || mod_only_shift(m)) {
        uint8_t c = hid_to_ascii(k, m);
        if (c >= '0' && c <= '9') {
            sbuf = sbuf_on ? (sbuf * 10 + c - '0') % 1000 : c - '0';
            sbuf_on = true;
        }
        else if (c == '+' || c == '=' || c == '-' || c == ']' || c == '[') {
            song_commit();
            song_fill(sl);
            int32_t v = sfield == 0 ? sl->patch : *song_field(sl);
            song_set(sl, v + ((c == '-' || c == '[') ? -1 : 1));
        }
        else if (c == 'l' || c == 'L') {
            if (sf_song_load_slot(srow)) {
                sf_fmt(s, sizeof(s), "LOADED P%02d", sl->patch);
                say(s);
            }
        }
        else if (c == 'm' || c == 'M') {
            sf.song.mode = (sf.song.mode + 1) % 3;
            sf.song_modified = 1;
        }
        else if (c == 'r' || c == 'R') {
            sf.song.loop = !sf.song.loop;
            sf.song_modified = 1;
        }
    }
}

////////////////////////////////////////////////////////////////////////////

void process_seaflake_keys(uint8_t k, uint8_t m, bool is_held_key) {
    switch (view) {
        case V_GRID: grid_keys(k, m); break;
        case V_SCAN:
            if (!is_held_key && match_no_mod(m, k, HID_ENTER)) scan();
            break;
        case V_SONG:
            if (!is_held_key || k == HID_UP || k == HID_DOWN) song_keys(k, m);
            break;
        default:
            if (!is_held_key || k == HID_UP || k == HID_DOWN) picker_keys(k, m);
            break;
    }
    dirty = true;
}

void seaflake_front(void) {
    dirty = true;
    if (view == V_PATCH_LOAD)
        picker_keys(HID_ENTER, 0);
    else
        open_picker(V_PATCH_LOAD);
}

void seaflake_knob(uint16_t value) {
    if (view != V_PATCH_LOAD) return;
    // as the stock scene recall reads the knob: 32 steps with a dead zone
    // between them so a knob at a boundary does not flicker
    uint8_t p = value >> 6;
    const bool deadzone = p & 1;
    p >>= 1;
    if (p >= SF_PATCH_SLOTS) p = SF_PATCH_SLOTS - 1;
    if (p == pick) return;
    if (!deadzone || (p > pick ? p - pick : pick - p) > 1) {
        pick = p;
        confirm = 0;
        dirty = true;
    }
}

bool seaflake_escape(void) {
    dirty = true;
    if (view == V_SONG_SAVE || view == V_SONG_LOAD) {
        view = V_SONG;
        return true;
    }
    if (view != V_GRID) {
        song_commit();
        view = V_GRID;
        return true;
    }
    if (ax >= 0) {
        ax = -1;
        return true;
    }
    return false;
}
