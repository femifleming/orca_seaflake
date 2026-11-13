// Pilot Sealflake: desktop host for the Teletype firmware.
//
// A port of module/main.c and module/flash.c: the same globals, mode handling,
// global keys, timers (metro, delays, TR pulses, CV slew, MIDI scripts) and
// scene storage. Hardware outputs that have no meaning here (TR/CV jacks,
// grid, front button) keep their state so ops read back correctly; i2c goes
// to the host (i2c2midi becomes real MIDI out).

#include "tt_host.h"

#include <stdio.h>
#include <string.h>

#include "edit_mode.h"
#include "flash.h"
#include "grid.h"
#include "gitversion.h"
#include "globals.h"
#include "help_mode.h"
#include "keyboard_helper.h"
#include "live_mode.h"
#include "pattern_mode.h"
#include "preset_r_mode.h"
#include "preset_w_mode.h"

#include "chaos.h"
#include "helpers.h"
#include "scene_serialization.h"
#include "teletype.h"
#include "teletype_io.h"

#include "font.h"
#include "region.h"
#include "monome.h"
#include "screen.h"
#include "timers.h"
#include "tt_text.h"
#include "util.h"

#define RATE_CV 6


////////////////////////////////////////////////////////////////////////////////
// globals (globals.h)

scene_state_t scene_state;
char scene_text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
uint8_t preset_select;
region line[8] = { { .w = 128, .h = 8, .x = 0, .y = 0 },
                   { .w = 128, .h = 8, .x = 0, .y = 8 },
                   { .w = 128, .h = 8, .x = 0, .y = 16 },
                   { .w = 128, .h = 8, .x = 0, .y = 24 },
                   { .w = 128, .h = 8, .x = 0, .y = 32 },
                   { .w = 128, .h = 8, .x = 0, .y = 40 },
                   { .w = 128, .h = 8, .x = 0, .y = 48 },
                   { .w = 128, .h = 8, .x = 0, .y = 56 } };
char copy_buffer[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
uint8_t copy_buffer_len = 0;
const char *git_version = "5.0.0 PILOT";

static tele_mode_t mode = M_LIVE;
static tele_mode_t last_mode = M_LIVE;
static uint8_t fb[TT_SCREEN_W * TT_SCREEN_H];
static bool fb_changed = true;
static bool ready = false;

// timers
static uint32_t now_ms = 0, last_tick_ms = 0;
static bool metro_timer_enabled = false;
static uint32_t metro_next = 0;
static uint32_t cv_next = 0, midi_next = 0;
static int32_t tr_pulse_remain[TR_COUNT];
static bool tr_pulse_on[TR_COUNT];
static uint32_t midi_clock_counter = 0;
static uint32_t fader_next = 0;

// no USB grid: a 16x8 varibright LED buffer for the on-screen grid visualizer
static u8 led_buffer[MONOME_MAX_LED_BYTES];
u8 *monomeLedBuffer = led_buffer;
u8 monome_size_x(void) {
    return 16;
}
u8 monome_size_y(void) {
    return 8;
}
u8 monome_is_vari(void) {
    return 1;
}

typedef struct {
    int16_t now, off, target, slew, step;
    int32_t delta, a;
} aout_t;
static aout_t aout[4];

////////////////////////////////////////////////////////////////////////////////
// screen text capture (libavr32 font / region hooks, shim/tt_text.h)

#define MAX_SEGS 32
typedef struct {
    tt_seg_t seg[MAX_SEGS];
    int n;
    uint8_t bg;
    uint8_t mask[128];
} line_text_t;
static line_text_t pending_text[8], shown_text[8];
static uint8_t text_mask[TT_SCREEN_W * TT_SCREEN_H];

static int line_index(const struct _region *reg) {
    const region *r = (const region *)reg;
    if (r >= &line[0] && r < &line[8]) return (int)(r - &line[0]);
    return -1;
}

void tt_fill_hook(const struct _region *reg, u8 c) {
    int l = line_index(reg);
    if (l < 0) return;
    pending_text[l].n = 0;
    pending_text[l].bg = c;
    memset(pending_text[l].mask, 0, sizeof(pending_text[l].mask));
}

void tt_text_hook(const struct _region *reg, const char *str, int len, int x,
                  int y, u8 fg, u8 bg, int hi, u8 hid, int right) {
    int l = line_index(reg);
    if (l < 0 || y > 7 || x < 0 || x >= TT_SCREEN_W) return;
    line_text_t *t = &pending_text[l];
    tt_seg_t s;
    memset(&s, 0, sizeof(s));
    size_t n = len < 0 ? strlen(str) : (size_t)len;
    if (n > sizeof(s.text) - 1) n = sizeof(s.text) - 1;
    memcpy(s.text, str, n);
    s.text[n] = 0;
    int w = font_string_pixels(s.text);
    if (x + w > TT_SCREEN_W) w = TT_SCREEN_W - x;
    s.x = (int16_t)x;
    s.w = (uint8_t)(w < 0 ? 0 : w);
    s.fg = fg;
    s.bg = bg;
    s.hi = (int8_t)hi;
    s.hid = hid;
    s.right = (uint8_t)(right != 0);
    for (size_t i = 0; i < n; i++) s.cfg[i] = fg;

    // Text drawn over earlier text replaces only the characters it covers, like
    // pixels on the OLED (e.g. edit mode draws the script number over the
    // input line's first character).
    tt_seg_t keep[MAX_SEGS * 2];
    int k = 0;
    bool merged = false;
    const int s0 = s.x, s1 = s.x + (s.w ? s.w : 1);
    for (int i = 0; i < t->n; i++) {
        tt_seg_t *o = &t->seg[i];
        const int o1 = o->x + (o->w ? o->w : 1);
        if (!(o->x < s1 && s0 < o1)) {
            keep[k++] = *o;
            continue;
        }
        const int len = (int)strlen(o->text);
        int cs[49];  // pixel start of each character
        char tmp[49];
        for (int c = 0; c <= len; c++) {
            memcpy(tmp, o->text, (size_t)c);
            tmp[c] = 0;
            cs[c] = o->x + (c ? font_string_pixels(tmp) : 0);
        }
        // starts on a character boundary and fits: write the characters in place
        int at = -1;
        for (int c = 0; c < len; c++)
            if (cs[c] == s.x) at = c;
        if (!merged && at >= 0 && at + (int)n <= len) {
            for (size_t j = 0; j < n; j++) {
                o->text[at + j] = s.text[j];
                o->cfg[at + j] = s.fg;
            }
            if (s.hi >= 0) {
                o->hi = (int8_t)(at + s.hi);
                o->hid = s.hid;
            }
            merged = true;
            keep[k++] = *o;
            continue;
        }
        // otherwise keep the characters left and right of the new text
        int a = 0, b = len;
        while (a < len && cs[a + 1] <= s0) a++;
        while (b > 0 && cs[b - 1] >= s1) b--;
        if (a > 0) {
            tt_seg_t l = *o;
            l.text[a] = 0;
            l.w = (uint8_t)(cs[a] - o->x);
            if (l.hi >= a) l.hi = -1;
            keep[k++] = l;
        }
        if (b < len) {
            tt_seg_t r = *o;
            memmove(r.text, o->text + b, (size_t)(len - b + 1));
            memmove(r.cfg, o->cfg + b, (size_t)(len - b));
            r.x = (int16_t)cs[b];
            r.w = (uint8_t)(o1 - cs[b]);
            r.hi = (int8_t)(o->hi >= b ? o->hi - b : -1);
            r.right = 0;
            keep[k++] = r;
        }
    }
    t->n = 0;
    for (int i = 0; i < k && t->n < MAX_SEGS; i++) t->seg[t->n++] = keep[i];
    if (!merged && (n > 0 || hi >= 0) && t->n < MAX_SEGS) t->seg[t->n++] = s;
    for (int i = s.x; i < s.x + s.w && i < TT_SCREEN_W; i++) t->mask[i] = 1;
}

int tt_screen_segments(int l, tt_seg_t *out, int max) {
    if (l < 0 || l > 7) return 0;
    int n = shown_text[l].n < max ? shown_text[l].n : max;
    memcpy(out, shown_text[l].seg, sizeof(tt_seg_t) * (size_t)n);
    return n;
}

uint8_t tt_screen_line_bg(int l) {
    return (l >= 0 && l < 8) ? shown_text[l].bg : 0;
}

const uint8_t *tt_text_mask(void) {
    return text_mask;
}

////////////////////////////////////////////////////////////////////////////////
// OLED

void screen_draw_region(u8 x, u8 y, u8 w, u8 h, u8 *data) {
    if (x == 0 && w == TT_SCREEN_W && h == 8 && (y & 7) == 0 && y < TT_SCREEN_H) {
        int l = y >> 3;
        shown_text[l] = pending_text[l];
        for (int j = 0; j < 8; j++)
            memcpy(&text_mask[(y + j) * TT_SCREEN_W], shown_text[l].mask, TT_SCREEN_W);
    }
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= TT_SCREEN_H) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= TT_SCREEN_W) break;
            fb[yy * TT_SCREEN_W + xx] = data[j * w + i] & 0xF;
        }
    }
    fb_changed = true;
}

void screen_draw_region_offset(u8 x, u8 y, u8 w, u8 h, u32 len, u8 *data,
                               u32 off) {
    for (u32 k = 0; k < (u32)w * h; k++) {
        int xx = x + (int)(k % w), yy = y + (int)(k / w);
        if (xx < TT_SCREEN_W && yy < TT_SCREEN_H)
            fb[yy * TT_SCREEN_W + xx] = data[(k + off) % len] & 0xF;
    }
    fb_changed = true;
}

////////////////////////////////////////////////////////////////////////////////
// scene slots (flash.h): 32 scenes kept in memory, each saved as ttNNs.txt

typedef struct {
    scene_script_t scripts[EDITABLE_SCRIPT_COUNT];
    scene_pattern_t patterns[PATTERN_COUNT];
    char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
} slot_t;

static slot_t slots[SCENE_SLOTS];
static uint8_t last_scene = 0;
static char scene_dir[1024];

typedef struct {
    FILE *f;
} file_stream_t;

static void fs_write_buffer(void *d, uint8_t *b, uint16_t n) {
    fwrite(b, 1, n, ((file_stream_t *)d)->f);
}
static void fs_write_char(void *d, uint8_t c) {
    fputc(c, ((file_stream_t *)d)->f);
}
static uint16_t fs_read_char(void *d) {
    int c = fgetc(((file_stream_t *)d)->f);
    return c == EOF ? 0 : (uint16_t)c;
}
static bool fs_eof(void *d) {
    return feof(((file_stream_t *)d)->f) != 0;
}
static void no_dbg(const char *s) {
    (void)s;
}

typedef struct {
    const char *s;
    size_t pos;
} str_in_t;
typedef struct {
    char *s;
    size_t len, cap;
} str_out_t;

static uint16_t ss_read_char(void *d) {
    str_in_t *in = (str_in_t *)d;
    char c = in->s[in->pos];
    if (c) in->pos++;
    return (uint16_t)(uint8_t)c;
}
static bool ss_eof(void *d) {
    str_in_t *in = (str_in_t *)d;
    return in->s[in->pos] == 0;
}
static void so_write_buffer(void *d, uint8_t *b, uint16_t n) {
    str_out_t *o = (str_out_t *)d;
    for (uint16_t i = 0; i < n; i++)
        if (o->len + 1 < o->cap) o->s[o->len++] = (char)b[i];
    if (o->cap) o->s[o->len] = 0;
}
static void so_write_char(void *d, uint8_t c) {
    so_write_buffer(d, &c, 1);
}

static void slot_path(uint8_t n, char *out, size_t cap) {
    snprintf(out, cap, "%s/tt%02ds.txt", scene_dir, n);
}

static scene_state_t tmp_scene;

static void slot_from_scene(uint8_t n, scene_state_t *s,
                            char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    memcpy(slots[n].scripts, ss_scripts_ptr(s),
           ss_scripts_size(EDITABLE_SCRIPT_COUNT));
    memcpy(slots[n].patterns, ss_patterns_ptr(s), ss_patterns_size());
    memcpy(slots[n].text, text, sizeof(slots[n].text));
}

static void load_slots(void) {
    for (uint8_t n = 0; n < SCENE_SLOTS; n++) {
        ss_init(&tmp_scene);
        char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
        memset(text, 0, sizeof(text));
        char path[1100];
        slot_path(n, path, sizeof(path));
        FILE *f = fopen(path, "rb");
        if (f) {
            file_stream_t fs = { f };
            tt_deserializer_t d = { .read_char = fs_read_char,
                                    .eof = fs_eof,
                                    .print_dbg = no_dbg,
                                    .data = &fs };
            deserialize_scene(&d, &tmp_scene, &text);
            fclose(f);
        }
        slot_from_scene(n, &tmp_scene, &text);
    }
    char path[1100];
    snprintf(path, sizeof(path), "%s/last_scene.txt", scene_dir);
    FILE *f = fopen(path, "rb");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1 && v >= 0 && v < SCENE_SLOTS)
            last_scene = (uint8_t)v;
        fclose(f);
    }
}

void flash_read(uint8_t preset_no, scene_state_t *scene,
                char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS],
                uint8_t init_pattern, uint8_t init_grid,
                uint8_t init_i2c_op_address) {
    (void)init_grid;
    if (preset_no >= SCENE_SLOTS) return;
    memcpy(ss_scripts_ptr(scene), slots[preset_no].scripts,
           ss_scripts_size(EDITABLE_SCRIPT_COUNT));
    if (init_pattern)
        memcpy(ss_patterns_ptr(scene), slots[preset_no].patterns,
               ss_patterns_size());
    memcpy(text, slots[preset_no].text, SCENE_TEXT_LINES * SCENE_TEXT_CHARS);
    uint32_t ticks = tele_get_ticks();
    for (size_t i = 0; i < TOTAL_SCRIPT_COUNT; i++)
        scene->scripts[i].last_time = ticks;
    scene->variables.time = 0;
    if (init_i2c_op_address) scene->i2c_op_address = -1;
    ss_midi_init(scene);
}

void flash_write(uint8_t preset_no, scene_state_t *scene,
                 char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    if (preset_no >= SCENE_SLOTS) return;
    slot_from_scene(preset_no, scene, text);
    if (!scene_dir[0]) return;
    char path[1100];
    slot_path(preset_no, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;
    file_stream_t fs = { f };
    tt_serializer_t s = { .write_buffer = fs_write_buffer,
                          .write_char = fs_write_char,
                          .print_dbg = no_dbg,
                          .data = &fs };
    serialize_scene(&s, scene, text);
    fclose(f);
}

uint8_t flash_last_saved_scene(void) {
    return last_scene;
}

void flash_update_last_saved_scene(uint8_t preset_no) {
    if (preset_no >= SCENE_SLOTS) return;
    last_scene = preset_no;
    if (!scene_dir[0]) return;
    char path[1100];
    snprintf(path, sizeof(path), "%s/last_scene.txt", scene_dir);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "%d\n", preset_no);
        fclose(f);
    }
}

const char *flash_scene_text(uint8_t preset_no, size_t l) {
    if (preset_no >= SCENE_SLOTS || l >= SCENE_TEXT_LINES) return "";
    return slots[preset_no].text[l];
}

tele_mode_t flash_last_mode(void) {
    return M_LIVE;
}
void flash_update_last_mode(tele_mode_t m) {
    (void)m;
}
void flash_update_cal(cal_data_t *cal) {
    (void)cal;
}
void flash_get_cal(cal_data_t *cal) {
    (void)cal;
}

////////////////////////////////////////////////////////////////////////////////
// mode handling (globals.h)

void set_mode(tele_mode_t m) {
    last_mode = mode;
    switch (m) {
        case M_LIVE: set_live_mode(); break;
        case M_EDIT: set_edit_mode(); break;
        case M_PATTERN: set_pattern_mode(); break;
        case M_PRESET_W: set_preset_w_mode(); break;
        case M_PRESET_R: set_preset_r_mode(preset_select); break;
        case M_HELP: set_help_mode(); break;
    }
    mode = m;
}

void set_last_mode(void) {
    if (mode == last_mode) return;
    if (last_mode == M_LIVE || last_mode == M_EDIT || last_mode == M_PATTERN)
        set_mode(last_mode);
    else
        set_mode(M_LIVE);
}

void clear_delays_and_slews(scene_state_t *ss) {
    clear_delays(ss);
    for (int i = 0; i < 4; i++) aout[i].step = 1;
}

void assign_main_event_handlers(void) {}

////////////////////////////////////////////////////////////////////////////////
// keys (main.c process_keypress / process_global_keys)

static bool process_global_keys(uint8_t k, uint8_t m, bool is_held_key) {
    if (is_held_key) return false;

    if (match_no_mod(m, k, HID_TAB)) {
        if (mode == M_LIVE)
            set_mode(M_EDIT);
        else if (mode == M_EDIT)
            set_mode(M_PATTERN);
        else
            set_mode(M_LIVE);
        return true;
    }
    else if (match_no_mod(m, k, HID_ESCAPE)) {
        if (mode == M_PRESET_R)
            set_last_mode();
        else
            set_mode(M_PRESET_R);
        return true;
    }
    else if (match_alt(m, k, HID_ESCAPE)) {
        set_mode(M_PRESET_W);
        return true;
    }
    else if (match_win(m, k, HID_ESCAPE)) {
        clear_delays_and_slews(&scene_state);
        return true;
    }
    else if (match_shift_alt(m, k, HID_SLASH) || match_alt(m, k, HID_H)) {
        if (mode == M_HELP)
            set_last_mode();
        else
            set_mode(M_HELP);
        return true;
    }
    else if (no_mod(m) && k >= HID_F1 && k <= HID_F8) {
        run_script(&scene_state, k - HID_F1);
        return true;
    }
    else if (no_mod(m) && k == HID_F9) {
        run_script(&scene_state, METRO_SCRIPT);
        return true;
    }
    else if (no_mod(m) && k == HID_F10) {
        run_script(&scene_state, INIT_SCRIPT);
        return true;
    }
    else if (mod_only_alt(m) && k >= HID_F1 && k <= HID_F8) {
        set_edit_mode_script(k - HID_F1);
        set_mode(M_EDIT);
        return true;
    }
    else if (mod_only_alt(m) && k == HID_F9) {
        set_edit_mode_script(METRO_SCRIPT);
        set_mode(M_EDIT);
        return true;
    }
    else if (mod_only_alt(m) && k == HID_F10) {
        set_edit_mode_script(INIT_SCRIPT);
        set_mode(M_EDIT);
        return true;
    }
    else if (mod_only_ctrl(m) && k >= HID_F1 && k <= HID_F8) {
        bool muted = ss_get_mute(&scene_state, (k - HID_F1));
        ss_set_mute(&scene_state, (k - HID_F1), !muted);
        screen_mutes_updated();
        set_mutes_updated();
        return true;
    }
    else if (mod_only_ctrl(m) && k == HID_F9) {
        scene_state.variables.m_act = !scene_state.variables.m_act;
        tele_metro_updated();
        return true;
    }
    else if (no_mod(m) && k >= HID_KEYPAD_1 && k <= HID_KEYPAD_8) {
        run_script(&scene_state, k - HID_KEYPAD_1);
        return true;
    }
    else if (match_no_mod(m, k, HID_KEYPAD_NUM_LOCK) ||
             match_no_mod(m, k, HID_F11)) {
        if (mode != M_PATTERN) set_mode(M_PATTERN);
        return true;
    }
    else if (match_no_mod(m, k, HID_PRINTSCREEN) ||
             match_no_mod(m, k, HID_F12)) {
        if (mode != M_LIVE) set_mode(M_LIVE);
        return true;
    }
    return false;
}

void tt_key(uint8_t key, uint8_t mod_key, bool is_held_key) {
    if (!ready) return;
    if (process_global_keys(key, mod_key, is_held_key)) return;
    switch (mode) {
        case M_EDIT: process_edit_keys(key, mod_key, is_held_key); break;
        case M_LIVE:
            process_live_keys(key, mod_key, is_held_key, false, &scene_state);
            break;
        case M_PATTERN: process_pattern_keys(key, mod_key, is_held_key); break;
        case M_PRESET_W: process_preset_w_keys(key, mod_key, is_held_key); break;
        case M_PRESET_R: process_preset_r_keys(key, mod_key, is_held_key); break;
        case M_HELP: process_help_keys(key, mod_key, is_held_key); break;
    }
}

void tt_key_release(uint8_t key, uint8_t mod_key) {
    if (!ready) return;
    if (mode == M_LIVE)
        process_live_keys(key, mod_key, false, true, &scene_state);
}

////////////////////////////////////////////////////////////////////////////////
// screen

bool tt_refresh(void) {
    if (!ready) return false;
    uint8_t dirty = 0;
    switch (mode) {
        case M_PATTERN: dirty = screen_refresh_pattern(); break;
        case M_PRESET_W: dirty = screen_refresh_preset_w(); break;
        case M_PRESET_R: dirty = screen_refresh_preset_r(); break;
        case M_HELP: dirty = screen_refresh_help(); break;
        case M_LIVE: dirty = screen_refresh_live(); break;
        case M_EDIT: dirty = screen_refresh_edit(); break;
    }
    for (size_t i = 0; i < 8; i++)
        if (dirty & (1 << i)) region_draw(&line[i]);
    bool changed = fb_changed;
    fb_changed = false;
    return changed;
}

const uint8_t *tt_framebuffer(void) {
    return fb;
}

int tt_mode(void) {
    return (int)mode;
}

int tt_live_submode(void) {
    return get_live_sub_mode();
}

////////////////////////////////////////////////////////////////////////////////
// init / timers

void tt_init(const char *dir, uint32_t now) {
    now_ms = last_tick_ms = now;
    snprintf(scene_dir, sizeof(scene_dir), "%s", dir ? dir : "");
    for (int i = 0; i < 8; i++) region_alloc(&line[i]);
    init_timers();

    ss_init(&scene_state);
    memset(scene_text, 0, sizeof(scene_text));
    load_slots();

    preset_select = flash_last_saved_scene();
    ss_set_scene(&scene_state, preset_select);
    flash_read(preset_select, &scene_state, &scene_text, 1, 1, 1);

    tele_update_adc(1);
    metro_timer_enabled = false;
    tele_metro_updated();
    chaos_init();
    clear_delays(&scene_state);
    for (int i = 0; i < 4; i++) {
        aout[i].slew = 1;
        aout[i].now = aout[i].target = aout[i].off = 0;
        aout[i].step = 0;
    }
    for (int i = 0; i < TR_COUNT; i++) tr_pulse_on[i] = false;
    cv_next = midi_next = fader_next = now;

    ready = true;
    init_live_mode();
    set_mode(M_LIVE);

    run_script(&scene_state, INIT_SCRIPT);
    scene_state.initializing = false;
}

static void cv_timer(void) {
    for (size_t i = 0; i < 4; i++) {
        if (aout[i].step) {
            aout[i].step--;
            if (aout[i].step == 0) { aout[i].now = aout[i].target; }
            else {
                aout[i].a += aout[i].delta;
                aout[i].now = aout[i].a >> 16;
            }
        }
    }
}

static void midi_script_timer(void) {
    uint8_t executed[EDITABLE_SCRIPT_COUNT];
    memset(executed, 0, sizeof(executed));
    if (scene_state.midi.on_count && scene_state.midi.on_script >= 0 &&
        scene_state.midi.on_script < EDITABLE_SCRIPT_COUNT) {
        run_script(&scene_state, scene_state.midi.on_script);
        executed[scene_state.midi.on_script] = 1;
    }
    if (scene_state.midi.off_count && scene_state.midi.off_script >= 0 &&
        scene_state.midi.off_script < EDITABLE_SCRIPT_COUNT) {
        if (!executed[scene_state.midi.off_script])
            run_script(&scene_state, scene_state.midi.off_script);
        executed[scene_state.midi.off_script] = 1;
    }
    if (scene_state.midi.cc_count && scene_state.midi.cc_script >= 0 &&
        scene_state.midi.cc_script < EDITABLE_SCRIPT_COUNT) {
        if (!executed[scene_state.midi.cc_script])
            run_script(&scene_state, scene_state.midi.cc_script);
    }
    scene_state.midi.on_count = 0;
    scene_state.midi.off_count = 0;
    scene_state.midi.cc_count = 0;
}

void tt_tick(uint32_t now) {
    if (!ready) return;
    now_ms = now;
    uint32_t elapsed = now - last_tick_ms;
    if (elapsed == 0) return;
    if (elapsed > 1000) elapsed = 1000;  // after a stall, don't fire everything
    last_tick_ms = now;
    const uint32_t dt = elapsed;

    // delays, turtle, stack (main.c: tele_tick every RATE_CLOCK ms)
    while (elapsed > 0) {
        uint8_t step = elapsed > 200 ? 200 : (uint8_t)elapsed;
        tele_tick(&scene_state, step);
        elapsed -= step;
    }

    // soft timers (grid key repeat / grid script triggers), 1 tick = 1 ms
    for (uint32_t i = 0; i < dt; i++) process_timers();

    // grid fader slews
    if ((int32_t)(now - fader_next) >= 0) {
        fader_next = now + 25;
        grid_process_fader_slew(&scene_state);
    }

    // TR pulses
    for (int i = 0; i < TR_COUNT; i++) {
        if (!tr_pulse_on[i]) continue;
        tr_pulse_remain[i] -= (int32_t)dt;
        if (tr_pulse_remain[i] <= 0) {
            tr_pulse_on[i] = false;
            tele_tr_pulse_end(&scene_state, i);
        }
    }

    // CV slew
    while ((int32_t)(now - cv_next) >= 0) {
        cv_timer();
        cv_next += RATE_CV;
    }

    // MIDI-in scripts
    if ((int32_t)(now - midi_next) >= 0) {
        midi_next = now + 25;
        midi_script_timer();
    }

    // metro
    if (metro_timer_enabled && (int32_t)(now - metro_next) >= 0) {
        bool ran = false;
        int32_t period = scene_state.variables.m;
        if (period < METRO_MIN_UNSUPPORTED_MS) period = METRO_MIN_UNSUPPORTED_MS;
        metro_next += (uint32_t)period;
        if ((int32_t)(now - metro_next) > period) metro_next = now + period;
        if (ss_get_script_len(&scene_state, METRO_SCRIPT)) {
            set_metro_icon(true);
            run_script(&scene_state, METRO_SCRIPT);
            ran = true;
        }
        else
            set_metro_icon(false);
        if (ran) grid_metro_triggered(&scene_state);
    }
}

bool tt_metro_active(void) {
    return scene_state.variables.m_act;
}

int16_t tt_metro_period(void) {
    return scene_state.variables.m;
}

////////////////////////////////////////////////////////////////////////////////
// live commands from Pilot's command line

int tt_run_command(const char *text, int16_t *value, char *err) {
    err[0] = 0;
    if (!ready) {
        snprintf(err, 64, "NOT READY");
        return -1;
    }
    char buf[256];
    size_t n = 0;
    for (; text[n] && n < sizeof(buf) - 1; n++) {
        char c = text[n];
        buf[n] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    buf[n] = 0;

    tele_command_t command;
    command.comment = false;
    char msg[TELE_ERROR_MSG_LENGTH];
    msg[0] = 0;
    error_t status = parse(buf, &command, msg);
    if (status == E_OK) status = validate(&command, msg);
    if (status != E_OK) {
        snprintf(err, 64, "%s%s%s", tele_error(status), msg[0] ? ": " : "",
                 msg);
        return -1;
    }
    if (!command.length) return 0;

    ss_clear_script(&scene_state, LIVE_SCRIPT);
    ss_overwrite_script_command(&scene_state, LIVE_SCRIPT, 0, &command);
    exec_state_t es;
    es_init(&es);
    es_push(&es);
    es_variables(&es)->script_number = LIVE_SCRIPT;
    process_result_t o = run_script_with_exec_state(&scene_state, &es,
                                                    LIVE_SCRIPT);
    if (o.has_value) {
        *value = o.value;
        return 1;
    }
    return 0;
}

bool tt_is_known_op(const char *word) {
    char buf[32];
    size_t n = 0;
    for (; word[n] && n < sizeof(buf) - 1; n++) {
        char c = word[n];
        buf[n] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    buf[n] = 0;
    tele_command_t c;
    char msg[TELE_ERROR_MSG_LENGTH];
    return parse(buf, &c, msg) == E_OK && c.length == 1 &&
           (c.data[0].tag == OP || c.data[0].tag == MOD);
}

////////////////////////////////////////////////////////////////////////////////
// scene text <-> state (plugin state)

size_t tt_scene_serialize(char *buf, size_t cap) {
    str_out_t o = { buf, 0, cap };
    if (cap) buf[0] = 0;
    tt_serializer_t s = { .write_buffer = so_write_buffer,
                          .write_char = so_write_char,
                          .print_dbg = no_dbg,
                          .data = &o };
    serialize_scene(&s, &scene_state, &scene_text);
    return o.len;
}

void tt_scene_deserialize(const char *text, bool run_init) {
    if (!ready) return;
    str_in_t in = { text, 0 };
    tt_deserializer_t d = { .read_char = ss_read_char,
                            .eof = ss_eof,
                            .print_dbg = no_dbg,
                            .data = &in };
    // like loading from a USB stick: blank scripts/patterns/text first
    for (int s = 0; s < EDITABLE_SCRIPT_COUNT; s++)
        ss_clear_script(&scene_state, s);
    ss_patterns_init(&scene_state);
    memset(scene_text, 0, sizeof(scene_text));
    deserialize_scene(&d, &scene_state, &scene_text);
    set_dash_updated();
    set_mode(mode);
    if (run_init) {
        scene_state.initializing = true;
        run_script(&scene_state, INIT_SCRIPT);
        scene_state.initializing = false;
    }
}

uint8_t tt_scene_number(void) {
    return preset_select;
}

////////////////////////////////////////////////////////////////////////////////
// MIDI input (main.c midi handlers)

void tt_midi_note_on(uint8_t ch, uint8_t num, uint8_t vel) {
    scene_state.midi.last_event_type = 1;
    scene_state.midi.last_channel = ch;
    scene_state.midi.last_note = num;
    scene_state.midi.last_velocity = vel;
    if (scene_state.midi.on_script != -1 &&
        scene_state.midi.on_count < MAX_MIDI_EVENTS) {
        scene_state.midi.on_channel[scene_state.midi.on_count] = ch;
        scene_state.midi.note_on[scene_state.midi.on_count] = num;
        scene_state.midi.note_vel[scene_state.midi.on_count] = vel;
        scene_state.midi.on_count++;
    }
}

void tt_midi_note_off(uint8_t ch, uint8_t num, uint8_t vel) {
    (void)vel;
    scene_state.midi.last_event_type = 2;
    scene_state.midi.last_channel = ch;
    scene_state.midi.last_note = num;
    scene_state.midi.last_velocity = vel;
    if (scene_state.midi.off_script != -1 &&
        scene_state.midi.off_count < MAX_MIDI_EVENTS) {
        scene_state.midi.off_channel[scene_state.midi.off_count] = ch;
        scene_state.midi.note_off[scene_state.midi.off_count] = num;
        scene_state.midi.off_count++;
    }
}

void tt_midi_cc(uint8_t ch, uint8_t num, uint8_t val) {
    scene_state.midi.last_event_type = 3;
    scene_state.midi.last_channel = ch;
    scene_state.midi.last_controller = num;
    scene_state.midi.last_cc = val;
    if (scene_state.midi.cc_script != -1) {
        bool found = false;
        for (uint8_t i = 0; i < scene_state.midi.cc_count; i++) {
            if (scene_state.midi.cn[i] == num &&
                scene_state.midi.cc_channel[i] == ch) {
                scene_state.midi.cc[i] = val;
                found = true;
                break;
            }
        }
        if (!found && scene_state.midi.cc_count < MAX_MIDI_EVENTS) {
            scene_state.midi.cc_channel[scene_state.midi.cc_count] = ch;
            scene_state.midi.cn[scene_state.midi.cc_count] = num;
            scene_state.midi.cc[scene_state.midi.cc_count] = val;
            scene_state.midi.cc_count++;
        }
    }
}

void tt_midi_clock(void) {
    if (++midi_clock_counter >= scene_state.midi.clock_div) {
        midi_clock_counter = 0;
        scene_state.midi.last_event_type = 4;
        if (scene_state.midi.clk_script >= 0 &&
            scene_state.midi.clk_script < EDITABLE_SCRIPT_COUNT)
            run_script(&scene_state, scene_state.midi.clk_script);
    }
}

void tt_midi_start(void) {
    scene_state.midi.last_event_type = 5;
    if (scene_state.midi.start_script >= 0 &&
        scene_state.midi.start_script < EDITABLE_SCRIPT_COUNT)
        run_script(&scene_state, scene_state.midi.start_script);
}

void tt_midi_stop(void) {
    scene_state.midi.last_event_type = 6;
    if (scene_state.midi.stop_script >= 0 &&
        scene_state.midi.stop_script < EDITABLE_SCRIPT_COUNT)
        run_script(&scene_state, scene_state.midi.stop_script);
}

void tt_midi_continue(void) {
    scene_state.midi.last_event_type = 7;
    if (scene_state.midi.continue_script >= 0 &&
        scene_state.midi.continue_script < EDITABLE_SCRIPT_COUNT)
        run_script(&scene_state, scene_state.midi.continue_script);
}

////////////////////////////////////////////////////////////////////////////////
// teletype_io.h

uint32_t tele_get_ticks(void) {
    return now_ms;
}

void tele_metro_updated(void) {
    uint32_t metro_time = scene_state.variables.m;
    if (metro_time < METRO_MIN_UNSUPPORTED_MS) metro_time = METRO_MIN_UNSUPPORTED_MS;
    bool m_act = scene_state.variables.m_act;
    if (m_act && !metro_timer_enabled) {
        metro_timer_enabled = true;
        metro_next = now_ms + metro_time;
    }
    else if (!m_act && metro_timer_enabled) {
        metro_timer_enabled = false;
    }
    if (ready) {
        if (metro_timer_enabled && ss_get_script_len(&scene_state, METRO_SCRIPT))
            set_metro_icon(true);
        else
            set_metro_icon(false);
    }
}

void tele_metro_reset(void) {
    if (metro_timer_enabled) {
        uint32_t metro_time = scene_state.variables.m;
        if (metro_time < METRO_MIN_UNSUPPORTED_MS) metro_time = METRO_MIN_UNSUPPORTED_MS;
        metro_next = now_ms + metro_time;
    }
}

void tele_tr(uint8_t i, int16_t v) {
    (void)i;
    (void)v;
}

void tele_tr_pulse(uint8_t i, int16_t time) {
    if (i >= TR_COUNT) return;
    tr_pulse_on[i] = true;
    tr_pulse_remain[i] = time;
}

void tele_tr_pulse_clear(uint8_t i) {
    if (i >= TR_COUNT) return;
    tr_pulse_on[i] = false;
}

void tele_tr_pulse_time(uint8_t i, int16_t time) {
    if (i >= TR_COUNT || !tr_pulse_on[i]) return;
    tr_pulse_remain[i] = time;
}

void tele_cv(uint8_t i, int16_t v, uint8_t s) {
    if (i >= 4) return;
    int16_t t = v + aout[i].off;
    if (t < 0)
        t = 0;
    else if (t > 16383)
        t = 16383;
    aout[i].target = t;
    if (s) {
        aout[i].step = aout[i].slew;
        aout[i].delta = ((aout[i].target - aout[i].now) << 16) / aout[i].step;
    }
    else {
        aout[i].step = 1;
        aout[i].now = aout[i].target;
    }
    aout[i].a = aout[i].now << 16;
}

void tele_cv_slew(uint8_t i, int16_t v) {
    if (i >= 4) return;
    aout[i].slew = v / RATE_CV;
    if (aout[i].slew == 0) aout[i].slew = 1;
}

void tele_cv_off(uint8_t i, int16_t v) {
    if (i >= 4) return;
    aout[i].off = v;
}

uint16_t tele_get_cv(uint8_t i) {
    return i < 4 ? (uint16_t)aout[i].now : 0;
}

void tele_cv_cal(uint8_t n, int32_t b, int32_t m) {
    if (n > 3) return;
    scene_state.cal.cv_scale[n].b = b;
    scene_state.cal.cv_scale[n].m = m;
}

void tele_update_adc(uint8_t force) {
    (void)force;
}

void tele_ii_tx(uint8_t addr, uint8_t *data, uint8_t l) {
    tt_hw_ii_tx(addr, data, l);
}

void tele_ii_rx(uint8_t addr, uint8_t *data, uint8_t l) {
    tt_hw_ii_rx(addr, data, l);
}

void tele_scene(uint8_t i, uint8_t init_grid, uint8_t init_pattern) {
    if (i >= SCENE_SLOTS) return;
    preset_select = i;
    flash_read(i, &scene_state, &scene_text, init_pattern, init_grid, 0);
    set_dash_updated();
}

void tele_kill(void) {
    for (int i = 0; i < 4; i++) {
        aout[i].step = 1;
        tele_tr(i, 0);
    }
    tt_hw_kill();
}

bool tele_get_input_state(uint8_t n) {
    (void)n;
    return false;
}

void tele_vars_updated(void) {
    set_vars_updated();
}

void tele_save_calibration(void) {}

void grid_key_press(uint8_t x, uint8_t y, uint8_t z) {
    grid_process_key(&scene_state, x, y, z, 1);
}

void device_flip(void) {}

void reset_midi_counter(void) {
    midi_clock_counter = 0;
}
