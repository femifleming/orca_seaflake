// Orca Seaflake emulator: desktop host for the Teletype firmware.
//
// A port of module/main.c and module/flash.c (from the Pilot Sealflake
// Teletype host): the same globals, modes, global keys, timers and scene
// storage. Everything else (parser, ops, live/edit/pattern screens and the
// Seaflake mode) is the firmware's own code, unchanged. I2C goes to a log and
// MIDI sent to a Disting EX goes to a virtual MIDI port.

#include "tt_emu.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stddef.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>

#include "edit_mode.h"
#include "flash.h"
#include "globals.h"
#include "grid.h"
#include "help_mode.h"
#include "keyboard_helper.h"
#include "live_mode.h"
#include "pattern_mode.h"
#include "preset_r_mode.h"
#include "preset_w_mode.h"
#include "seaflake_mode.h"
#include "sf_ui.h"
#include "seaflake/sf_report.h"
#include "seaflake/sf_config.h"
#include "ops/disting.h"
#include "dream.h"

#include "chaos.h"
#include "helpers.h"
#include "scene_serialization.h"
#include "teletype.h"
#include "teletype_io.h"

#include "ii.h"
#include "monome.h"
#include "region.h"
#include "screen.h"
#include "timers.h"

#include "seaflake/sf.h"
#include "seaflake/sf_ii.h"
#include "seaflake/sf_store.h"

#define RATE_CV 6

// globals.h
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
const char *git_version = "5.0.0 SEAFLAKE";

static uint8_t fb[EMU_W * EMU_H];
static bool fb_changed = true;
static bool ready = false;
static char dir[1024];

static emu_log_fn log_fn;
static uint8_t ex_last_cmd[128];  // last command byte sent to each I2C address
static emu_midi_fn midi_fn;

static uint32_t now_ms, last_tick_ms, boot_ms;
// the dream picks one of its four at random when it starts (dream.c)
uint32_t dream_entropy(void) { return now_ms; }
// the screensaver, as in main.c: the dream after 5 idle minutes, until a key
#define DREAM_AFTER (5UL * 60 * 1000)
static uint32_t idle_ms;
static bool metro_on;
static uint32_t metro_next, cv_next, midi_next, fader_next;
static int32_t tr_remain[TR_COUNT];
static bool tr_pulse_on[TR_COUNT];
static bool tr_out[TR_COUNT];

typedef struct {
    int16_t now, off, target, slew, step;
    int32_t delta, a;
} aout_t;
static aout_t aout[4];

static int16_t er_cv[27];
static bool er_tr[27];
static uint32_t er_seen[27];
static int32_t er_pulse_remain[27];
static uint16_t er_pulse_ms[27];

// no USB grid: an LED buffer for the grid visualizer
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

static void logf_(int kind, const char *text) {
    if (log_fn) log_fn(kind, text);
}

////////////////////////////////////////////////////////////////////////////
// OLED

void screen_draw_region(u8 x, u8 y, u8 w, u8 h, u8 *data) {
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy >= EMU_H) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= EMU_W) break;
            fb[yy * EMU_W + xx] = data[j * w + i] & 0xF;
        }
    }
    fb_changed = true;
}

void screen_draw_region_offset(u8 x, u8 y, u8 w, u8 h, u32 len, u8 *data,
                               u32 off) {
    for (u32 k = 0; k < (u32)w * h; k++) {
        int xx = x + (int)(k % w), yy = y + (int)(k / w);
        if (xx < EMU_W && yy < EMU_H) fb[yy * EMU_W + xx] = data[(k + off) % len] & 0xF;
    }
    fb_changed = true;
}

////////////////////////////////////////////////////////////////////////////
// Teletype scenes: 32 slots in memory, each saved as ttNNs.txt (the format
// of the Teletype's USB stick backups, so real backups can be dropped in)

typedef struct {
    scene_script_t scripts[EDITABLE_SCRIPT_COUNT];
    scene_pattern_t patterns[PATTERN_COUNT];
    char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
} slot_t;
static slot_t slots[SCENE_SLOTS];
static uint8_t last_scene;
static scene_state_t tmp_scene;

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

static void slot_from_scene(uint8_t n, scene_state_t *s,
                            char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    memcpy(slots[n].scripts, ss_scripts_ptr(s), ss_scripts_size(EDITABLE_SCRIPT_COUNT));
    memcpy(slots[n].patterns, ss_patterns_ptr(s), ss_patterns_size());
    memcpy(slots[n].text, text, sizeof(slots[n].text));
}

static void load_slots(void) {
    for (uint8_t n = 0; n < SCENE_SLOTS; n++) {
        ss_init(&tmp_scene);
        char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
        memset(text, 0, sizeof(text));
        char path[1100];
        snprintf(path, sizeof(path), "%s/tt%02ds.txt", dir, n);
        FILE *f = fopen(path, "rb");
        if (f) {
            file_stream_t fs = { f };
            tt_deserializer_t d = {
                .read_char = fs_read_char, .eof = fs_eof, .print_dbg = no_dbg, .data = &fs
            };
            deserialize_scene(&d, &tmp_scene, &text);
            fclose(f);
        }
        slot_from_scene(n, &tmp_scene, &text);
    }
    char path[1100];
    snprintf(path, sizeof(path), "%s/last_scene.txt", dir);
    FILE *f = fopen(path, "rb");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1 && v >= 0 && v < SCENE_SLOTS) last_scene = v;
        fclose(f);
    }
}

void flash_read(uint8_t preset_no, scene_state_t *scene,
                char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS],
                uint8_t init_pattern, uint8_t init_grid, uint8_t init_i2c_op_address) {
    (void)init_grid;
    if (preset_no >= SCENE_SLOTS) return;
    memcpy(ss_scripts_ptr(scene), slots[preset_no].scripts,
           ss_scripts_size(EDITABLE_SCRIPT_COUNT));
    if (init_pattern)
        memcpy(ss_patterns_ptr(scene), slots[preset_no].patterns, ss_patterns_size());
    memcpy(text, slots[preset_no].text, SCENE_TEXT_LINES * SCENE_TEXT_CHARS);
    for (size_t i = 0; i < TOTAL_SCRIPT_COUNT; i++)
        scene->scripts[i].last_time = now_ms;
    scene->variables.time = 0;
    if (init_i2c_op_address) scene->i2c_op_address = -1;
    ss_midi_init(scene);
}

void flash_write(uint8_t preset_no, scene_state_t *scene,
                 char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    if (preset_no >= SCENE_SLOTS) return;
    slot_from_scene(preset_no, scene, text);
    char path[1100];
    snprintf(path, sizeof(path), "%s/tt%02ds.txt", dir, preset_no);
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
    char path[1100];
    snprintf(path, sizeof(path), "%s/last_scene.txt", dir);
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

////////////////////////////////////////////////////////////////////////////
// Seaflake patches and songs: the same layout as module/sf_flash.c, kept in
// one file (seaflake.bin)

typedef struct {
    uint16_t len;
    uint8_t data[1020];
} emu_patch_t;
static struct {
    uint32_t magic;
    uint8_t last_patch;
    emu_patch_t patch[SF_PATCH_SLOTS];
    sf_song_t song[SF_SONG_SLOTS];
    uint8_t boot_song;   // (added later: files from before it simply end before it)
} store;

static void store_save(void) {
    char path[1100];
    snprintf(path, sizeof(path), "%s/seaflake.bin", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(&store, sizeof(store), 1, f);
    fclose(f);
}

// example patches put in the first time the emulator starts (see the manual)
static const char *const examples[] = {
    // P01: arpeggio over MIDI through the Disting EX (EX.M.N#)
    "$bpm:120\n"
    "........\n"
    "........\n"
    "........\n"
    "........\n"
    "..C4....\n"
    "D1.4TCEGb\n"
    ".:04.f2.\n",
    // P02: notes as CV + triggers on Teletype outputs and ER-301 port a
    "$bpm:100\n"
    "D4......D3.....\n"
    ".\"14C....\"a3E...\n"
    "...............\n"
    "D2..Rz.........\n"
    ".'30...........\n",
    // P03: the Disting EX: N (>n octave note velocity) and EX.P (>p), and
    // the ER-301 operator <
    "$apm:28\n"
    "...C4..........\n"
    ".D8.4TCEGb.....\n"
    "..>n3..........\n"
    "D4.............\n"
    ".<p05..........\n"
    "D8.............\n"
    ".>p01..........\n",
    // P04: loadbang (loads Disting EX preset 0 when the patch loads),
    // trigger only (-), trigger + CV (_), shaped trigger (})
    "$bpm:110\n"
    "`.......D4...D8......\n"
    ".>l00....-1....}214a.\n"
    "D2...................\n"
    "._24C................\n",
};

static void seed_examples(void) {
    static uint8_t buf[SF_PACK_MAX];
    for (int i = 0; i < (int)(sizeof(examples) / sizeof(examples[0])); i++) {
        sf_clear();
        int x = 0, y = 0;
        for (const char *c = examples[i]; *c; c++) {
            if (*c == '\n') {
                x = 0;
                y++;
            }
            else
                sf_set(x++, y, *c);
        }
        uint16_t len = sf_pack(buf, sizeof(buf));
        store.patch[i].len = len;
        memcpy(store.patch[i].data, buf, len);
    }
    sf_clear();
    sf_song_t s;
    memset(&s, 0, sizeof(s));
    s.magic = SF_SONG_MAGIC;
    s.loop = 1;
    s.slots[0] = (sf_song_slot_t){ .patch = 1, .len = 16, .mul = 2, .bpm = 120 };
    s.slots[1] = (sf_song_slot_t){ .patch = 2, .len = 16, .mul = 1, .bpm = 100 };
    s.slots[2] = (sf_song_slot_t){ .patch = 3, .len = 8, .mul = 2, .bpm = 110 };
    store.song[0] = s;
    store.last_patch = 1;
    store_save();
}

void sf_store_init(void) {
    char path[1100];
    snprintf(path, sizeof(path), "%s/seaflake.bin", dir);
    FILE *f = fopen(path, "rb");
    memset(&store, 0, sizeof(store));
    bool fresh = true;
    if (f) {
        // read what the file has: files written before boot_song existed are shorter
        if (fread(&store, 1, sizeof(store), f) >= offsetof(typeof(store), boot_song) && store.magic == 0x5EAF1A01)
            fresh = false;
        else
            memset(&store, 0, sizeof(store));
        fclose(f);
    }
    store.magic = 0x5EAF1A01;
    if (fresh) seed_examples();
}

uint16_t sf_store_patch_get(uint8_t n, const uint8_t **data) {
    if (n < 1 || n > SF_PATCH_SLOTS) return 0;
    emu_patch_t *p = &store.patch[n - 1];
    if (!p->len || p->len > sizeof(p->data)) return 0;
    *data = p->data;
    return p->len;
}

void sf_store_patch_put(uint8_t n, const uint8_t *data, uint16_t len) {
    if (n < 1 || n > SF_PATCH_SLOTS || len > sizeof(store.patch[0].data)) return;
    store.patch[n - 1].len = len;
    memcpy(store.patch[n - 1].data, data, len);
    store_save();
}

bool sf_store_song_get(uint8_t n, sf_song_t *out) {
    if (n < 1 || n > SF_SONG_SLOTS || store.song[n - 1].magic != SF_SONG_MAGIC) return false;
    *out = store.song[n - 1];
    return true;
}

void sf_store_song_put(uint8_t n, const sf_song_t *s) {
    if (n < 1 || n > SF_SONG_SLOTS) return;
    store.song[n - 1] = *s;
    store_save();
}

uint8_t sf_store_last_patch(void) {
    return store.last_patch;
}

uint8_t sf_store_boot_song(void) {
    return store.boot_song <= SF_SONG_SLOTS ? store.boot_song : 0;
}

void sf_store_set_scan(const struct sf_scan *scan) {
    (void)scan;  // the Mac keeps no flash head
}

void sf_store_set_boot_song(uint8_t n) {
    store.boot_song = n;
    store_save();
}

void sf_store_set_last_patch(uint8_t n) {
    store.last_patch = n;
    store_save();
}

////////////////////////////////////////////////////////////////////////////
// modes: set_mode() / set_last_mode() and the key routing are the
// firmware's own module/sf_ui.c

void clear_delays_and_slews(scene_state_t *ss) {
    clear_delays(ss);
    for (int i = 0; i < 4; i++) aout[i].step = 1;
}

void assign_main_event_handlers(void) {}

////////////////////////////////////////////////////////////////////////////
// keys, the front button and the knob: as main.c, through module/sf_ui.c

void emu_set_idle(uint32_t ms) {
    idle_ms = ms;
}

static bool wake(void) {
    // a key while the screensaver is on only wakes it (as on the module)
    const bool was = idle_ms >= DREAM_AFTER;
    idle_ms = 0;
    if (was) set_mode(sf_ui_mode());
    return was;
}

void emu_key(uint8_t key, uint8_t mod_key, bool held) {
    if (!ready) return;
    if (wake()) return;
    sf_ui_key(key, mod_key, held);  // as main.c process_keypress
}

void emu_key_release(uint8_t key, uint8_t mod_key) {
    (void)key;
    (void)mod_key;  // releases only mattered to the stock live screen
}

void emu_front(void) {
    if (!ready) return;
    if (wake()) return;  // as main.c handler_Front
    sf_ui_front();
}

void emu_knob(uint16_t value) {
    if (!ready) return;
    ss_set_param(&scene_state, value << 2);  // as main.c handler_PollADC
    sf_ui_knob(value);
}

bool emu_refresh(void) {
    if (!ready) return false;
    if (idle_ms >= DREAM_AFTER) {
        static uint8_t band[128 * 8];
        for (uint8_t i = 0; i < 8; i++) {
            dream_band(idle_ms - DREAM_AFTER, i, band);
            screen_draw_region(0, i * 8, 128, 8, band);
        }
        fb_changed = false;
        return true;
    }
    uint8_t dirty = sf_ui_refresh();
    for (int i = 0; i < 8; i++)
        if (dirty & (1 << i)) region_draw(&line[i]);
    bool c = fb_changed;
    fb_changed = false;
    return c;
}

const uint8_t *emu_framebuffer(void) {
    return fb;
}
int emu_mode(void) {
    return sf_ui_mode();
}

////////////////////////////////////////////////////////////////////////////
// init + timers

static void trace(const char *cmd) {
    logf_(0, cmd);
}

void emu_set_log(emu_log_fn fn) {
    log_fn = fn;
}
void emu_set_midi(emu_midi_fn fn) {
    midi_fn = fn;
}

void emu_init(const char *d, uint32_t now) {
    now_ms = last_tick_ms = boot_ms = now;
    snprintf(dir, sizeof(dir), "%s", d);
    mkdir(dir, 0755);
    for (int i = 0; i < 8; i++) region_alloc(&line[i]);
    init_timers();

    ss_init(&scene_state);
    memset(scene_text, 0, sizeof(scene_text));
    load_slots();
    preset_select = flash_last_saved_scene();
    ss_set_scene(&scene_state, preset_select);
    flash_read(preset_select, &scene_state, &scene_text, 1, 1, 1);

    metro_on = false;
    tele_metro_updated();
    chaos_init();
    clear_delays(&scene_state);
    for (int i = 0; i < 4; i++) {
        aout[i].slew = 1;
        aout[i].now = aout[i].target = aout[i].off = 0;
        aout[i].step = 0;
    }
    cv_next = midi_next = fader_next = now;
    sf_trace = trace;

    ready = true;
    init_live_mode();  // (as main.c: the scene's init script runs, its live screen is never shown)
    run_script(&scene_state, INIT_SCRIPT);
    scene_state.initializing = false;

    seaflake_init();
    set_mode(M_SEAFLAKE);  // boots into Orca, like the module
}

static void midi_script_timer(void) {
    uint8_t executed[EDITABLE_SCRIPT_COUNT] = { 0 };
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
        scene_state.midi.cc_script < EDITABLE_SCRIPT_COUNT &&
        !executed[scene_state.midi.cc_script])
        run_script(&scene_state, scene_state.midi.cc_script);
    scene_state.midi.on_count = scene_state.midi.off_count = scene_state.midi.cc_count = 0;
}

void emu_tick(uint32_t now) {
    if (!ready) return;
    uint32_t elapsed = now - last_tick_ms;
    if (elapsed == 0) return;
    if (elapsed > 1000) elapsed = 1000;
    uint32_t dt = elapsed;

    // one ms at a time, as the module's timers do
    for (uint32_t t = 0; t < dt; t++) {
        now_ms = last_tick_ms + t + 1;

        if (now_ms % 10 == 0) tele_tick(&scene_state, 10);
        process_timers();

        for (int i = 0; i < TR_COUNT; i++) {
            if (tr_pulse_on[i] && --tr_remain[i] <= 0) {
                tr_pulse_on[i] = false;
                tele_tr_pulse_end(&scene_state, i);
            }
        }
        for (int p = 1; p <= 26; p++) {
            if (er_pulse_remain[p] > 0 && --er_pulse_remain[p] == 0) er_tr[p] = false;
        }
        if ((int32_t)(now_ms - cv_next) >= 0) {
            cv_next += RATE_CV;
            for (int i = 0; i < 4; i++) {
                if (!aout[i].step) continue;
                aout[i].step--;
                if (aout[i].step == 0)
                    aout[i].now = aout[i].target;
                else {
                    aout[i].a += aout[i].delta;
                    aout[i].now = aout[i].a >> 16;
                }
            }
        }
        if ((int32_t)(now_ms - fader_next) >= 0) {
            fader_next = now_ms + 25;
            grid_process_fader_slew(&scene_state);
        }
        if ((int32_t)(now_ms - midi_next) >= 0) {
            midi_next = now_ms + 25;
            midi_script_timer();
        }
        if (metro_on && (int32_t)(now_ms - metro_next) >= 0) {
            int32_t period = scene_state.variables.m;
            if (period < METRO_MIN_UNSUPPORTED_MS) period = METRO_MIN_UNSUPPORTED_MS;
            metro_next = now_ms + period;
            if (ss_get_script_len(&scene_state, METRO_SCRIPT)) {
                set_metro_icon(true);
                run_script(&scene_state, METRO_SCRIPT);
            }
            else
                set_metro_icon(false);
        }
        if (seaflake_due(now_ms)) seaflake_tick(now_ms);
    }
    idle_ms += dt;
    last_tick_ms = now;
}

////////////////////////////////////////////////////////////////////////////
// teletype_io.h

uint32_t tele_get_ticks(void) {
    return now_ms;
}

void tele_metro_updated(void) {
    uint32_t t = scene_state.variables.m;
    if (t < METRO_MIN_UNSUPPORTED_MS) t = METRO_MIN_UNSUPPORTED_MS;
    bool act = scene_state.variables.m_act;
    if (act && !metro_on) metro_next = now_ms + t;
    metro_on = act;
    if (ready) set_metro_icon(metro_on && ss_get_script_len(&scene_state, METRO_SCRIPT));
}

void tele_metro_reset(void) {
    uint32_t t = scene_state.variables.m;
    if (t < METRO_MIN_UNSUPPORTED_MS) t = METRO_MIN_UNSUPPORTED_MS;
    if (metro_on) metro_next = now_ms + t;
}

void tele_tr(uint8_t i, int16_t v) {
    if (i < TR_COUNT) tr_out[i] = v != 0;
}

void tele_tr_pulse(uint8_t i, int16_t time) {
    if (i >= TR_COUNT) return;
    tr_pulse_on[i] = true;
    tr_remain[i] = time;
}

void tele_tr_pulse_clear(uint8_t i) {
    if (i < TR_COUNT) tr_pulse_on[i] = false;
}

void tele_tr_pulse_time(uint8_t i, int16_t time) {
    if (i < TR_COUNT && tr_pulse_on[i]) tr_remain[i] = time;
}

void tele_cv(uint8_t i, int16_t v, uint8_t s) {
    if (i >= 4) return;
    int16_t t = v + aout[i].off;
    if (t < 0) t = 0;
    if (t > 16383) t = 16383;
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
    if (i < 4) aout[i].off = v;
}

uint16_t tele_get_cv(uint8_t i) {
    return i < 4 ? aout[i].now : 0;
}

void tele_cv_cal(uint8_t n, int32_t b, int32_t m) {
    if (n > 3) return;
    scene_state.cal.cv_scale[n].b = b;
    scene_state.cal.cv_scale[n].m = m;
}

void tele_update_adc(uint8_t force) {
    (void)force;
}

static const char *const note_names[12] = { "C",  "C#", "D",  "D#", "E",  "F",
                                            "F#", "G",  "G#", "A",  "A#", "B" };

static void log_midi(int unit, const uint8_t *m, int n) {
    char s[96];
    int ch = (m[0] & 15) + 1;
    switch (m[0] & 0xF0) {
        case 0x90:
            snprintf(s, sizeof(s), "EX%d MIDI ch%-2d NOTE ON  %s%d (%d) vel %d", unit, ch,
                     note_names[m[1] % 12], m[1] / 12 - 1, m[1], m[2]);
            break;
        case 0x80:
            snprintf(s, sizeof(s), "EX%d MIDI ch%-2d NOTE OFF %s%d (%d)", unit, ch,
                     note_names[m[1] % 12], m[1] / 12 - 1, m[1]);
            break;
        case 0xB0:
            snprintf(s, sizeof(s), "EX%d MIDI ch%-2d CC %d = %d", unit, ch, m[1], m[2]);
            break;
        case 0xE0:
            snprintf(s, sizeof(s), "EX%d MIDI ch%-2d PITCH BEND %d", unit, ch,
                     (m[1] & 127) | ((m[2] & 127) << 7));
            break;
        default: snprintf(s, sizeof(s), "EX%d MIDI %02X %02X %02X", unit, m[0], n > 1 ? m[1] : 0, n > 2 ? m[2] : 0);
    }
    logf_(2, s);
    if (midi_fn) midi_fn(m, n);
}

static bool dev_acks(uint8_t addr);

void tele_ii_tx(uint8_t addr, uint8_t *data, uint8_t l) {
    sf_ii_note(addr, dev_acks(addr) ? 0 : -5, data, l);
    char s[96];
    int k;
    if (addr >= disting_ex_base && addr < disting_ex_base + 4)
        k = snprintf(s, sizeof(s), "I2C EX%d  (0x%02X):", addr - disting_ex_base + 1, addr);
    else if (addr >= ER301_1 && addr < ER301_1 + 3)
        k = snprintf(s, sizeof(s), "I2C ER301 %d (0x%02X):", addr - ER301_1 + 1, addr);
    else if (addr == 0x70 || addr == 0x75)
        k = snprintf(s, sizeof(s), "I2C JF %d (0x%02X):", addr == 0x70 ? 1 : 2, addr);
    else
        k = snprintf(s, sizeof(s), "I2C 0x%02X:", addr);
    for (int i = 0; i < l && k < (int)sizeof(s) - 4; i++)
        k += snprintf(s + k, sizeof(s) - k, " %02X", data[i]);
    logf_(1, s);

    if (addr < 128 && l >= 1) ex_last_cmd[addr] = data[0];

    // Disting EX MIDI (EX.M.*): 0x4F then the raw MIDI message
    if (addr >= disting_ex_base && addr < disting_ex_base + 4 && l >= 2 && data[0] == 0x4F)
        log_midi(addr - disting_ex_base + 1, data + 1, l - 1);

    // ER-301 (SC.*): command, port (0-based), value high, value low
    if (addr == ER301_1 && l >= 2) {
        int port = data[1] + 1;
        int16_t v = l >= 4 ? (int16_t)((data[2] << 8) | data[3]) : 0;
        if (port >= 1 && port <= 26) {
            er_seen[port] = now_ms;
            switch (data[0]) {
                case 0x00: er_tr[port] = v != 0; break;            // SC.TR
                case 0x01: er_tr[port] = !er_tr[port]; break;      // SC.TR.TOG
                case 0x02: er_pulse_ms[port] = v; break;           // SC.TR.TIME
                case 0x05:                                         // SC.TR.PULSE
                    er_tr[port] = true;
                    er_pulse_remain[port] = er_pulse_ms[port] ? er_pulse_ms[port] : 100;
                    break;
                case 0x10:
                case 0x11: er_cv[port] = v; break;                 // SC.CV / SET
            }
        }
    }
}

// the devices the emulator pretends are on the bus: EX1 and ER-301 1 until
// a scan report from the module is imported (emu_import_dir) or a test sets
// them (emu_set_devices): which addresses acknowledge, whether the bus is
// dead, and how long after power-on the devices start answering
static struct {
    bool ex[4];
    int16_t alg[4], preset[4];
    bool er[3];
    bool jf[2];  // Just Friends at 0x70 / 0x75
    bool dead;
    uint32_t ready_after;
    bool at[128];  // a Disting EX set to exactly this address (emu_set_ex_at)
} dev = { { true, false, false, false }, { 0 }, { 0 }, { true, false, false }, { true, false }, false, 0, { 0 } };
// ex[u]: a Disting EX set to whatever address the firmware uses for unit
// u+1 (the emulated Disting follows the Teletype's setting); at[a]: one set
// to address a, which answers only if the Teletype's address matches
static int dev_ex_unit(uint8_t addr) {
    if (addr >= disting_ex_base && addr < disting_ex_base + 4 && dev.ex[addr - disting_ex_base])
        return addr - disting_ex_base;
    if (addr < 128 && dev.at[addr]) return addr >= disting_ex_base && addr < disting_ex_base + 4 ? addr - disting_ex_base : 0;
    return -1;
}
static bool dev_acks(uint8_t addr) {
    if (dev.dead || now_ms - boot_ms < dev.ready_after) return false;
    if (dev_ex_unit(addr) >= 0) return true;
    if (addr >= ER301_1 && addr < ER301_1 + 3) return dev.er[addr - ER301_1];
    if (addr == 0x70) return dev.jf[0];
    if (addr == 0x75) return dev.jf[1];
    return false;
}

void emu_set_ex_at(uint8_t addr, bool on) {
    if (addr < 128) dev.at[addr] = on;
}

void emu_set_devices(uint8_t ex_mask, uint8_t er_mask, bool bus_dead, uint32_t ready_after_ms) {
    for (int u = 0; u < 4; u++) dev.ex[u] = (ex_mask >> u) & 1;
    for (int n = 0; n < 3; n++) dev.er[n] = (er_mask >> n) & 1;
    dev.dead = bus_dead;
    dev.ready_after = ready_after_ms;
}

void emu_set_config(const uint8_t block[32]) {
    memcpy(&sf_config, block, sizeof(sf_config));
}

// the Teletype rewriting its configuration block (sf_config_set_ex_addr)
static uint32_t config_writes;
void sf_io_config_write(const uint8_t block[32]) {
    memcpy(&sf_config, block, sizeof(sf_config));
    config_writes++;
}
uint32_t emu_config_writes(void) {
    return config_writes;
}
void emu_get_config(uint8_t block[32]) {
    memcpy(block, &sf_config, 32);
}

// the raw bus for the shared device scan (sf_ii.c): the bytes go to the log
// like any other I2C message; the answer is whether the device is there
bool sf_io_ii_tx(uint8_t addr, const uint8_t *data, uint8_t len) {
    tele_ii_tx(addr, (uint8_t *)data, len);
    return dev_acks(addr);
}

bool sf_io_ii_rx(uint8_t addr, uint8_t *data, uint8_t len) {
    tele_ii_rx(addr, data, len);
    return dev_acks(addr);
}

void tele_ii_rx(uint8_t addr, uint8_t *data, uint8_t l) {
    sf_ii_note(addr, dev_acks(addr) ? 0 : -5, 0, 0);
    memset(data, 0, l);
    // a Disting EX that is "there" answers EX.ALG and EX.PRESET
    int u = dev_ex_unit(addr);
    if (u >= 0 && dev_acks(addr)) {
        if (ex_last_cmd[addr] == 0x45 && l >= 1) data[0] = dev.alg[u];
        if (ex_last_cmd[addr] == 0x43 && l >= 2) {
            data[0] = dev.preset[u] >> 8;
            data[1] = dev.preset[u] & 0xFF;
        }
    }
    // a Just Friends (firmware 4) answers its getters: SPEED 1 (sound),
    // TSC 0 (transient), the knobs (RAMP .. INTONE) at half way (5 V)
    if ((addr == 0x70 || addr == 0x75) && dev_acks(addr) && (ex_last_cmd[addr] & 0x80)) {
        const uint8_t c = ex_last_cmd[addr] & 0x7F;
        if (c == 15 && l >= 1) data[0] = 1;
        else if (c >= 17 && c <= 21 && l >= 2) {
            data[0] = 8192 >> 8;
            data[1] = 0;
        }
    }
}

void emu_set_jf(uint8_t mask) {
    dev.jf[0] = mask & 1;
    dev.jf[1] = (mask >> 1) & 1;
}

////////////////////////////////////////////////////////////////////////////
// import what the module wrote to a USB stick (src/seaflake/sf_report.c)

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (b && fread(b, 1, n, f) != (size_t)n) n = 0;
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

// sf_import_patches over a string
static uint16_t str_getc(void *d) {
    const char **p = d;
    return (uint16_t)(unsigned char)*(*p)++;
}
static bool str_eof(void *d) {
    return **(const char **)d == 0;
}

// the line starting at *p (without \r\n); advances *p. NULL at the end
static char *next_line(char **p) {
    if (!**p) return NULL;
    char *l = *p, *e = strpbrk(l, "\r\n");
    if (e) {
        char *n = e;
        while (*n == '\r' || *n == '\n') n++;
        *e = 0;
        *p = n;
    }
    else
        *p = l + strlen(l);
    return l;
}

int emu_import_text(const char *scan, const char *patches, char *summary, int max) {
    int np = 0, ns = 0;
    if (scan) {
        char *buf = strdup(scan), *p = buf, *l;
        memset(&dev, 0, sizeof(dev));
        while ((l = next_line(&p))) {
            int u, a, pr;
            if (sscanf(l, "EX%d FOUND ALG %d PRESET %d", &u, &a, &pr) == 3 && u >= 1 && u <= 4) {
                dev.ex[u - 1] = true;
                dev.alg[u - 1] = a;
                dev.preset[u - 1] = pr;
            }
            if (sscanf(l, "ER301 %d", &u) == 1 && strstr(l, "FOUND") && u >= 1 && u <= 3)
                dev.er[u - 1] = true;
        }
        free(buf);
        sf_scan_er_known(dev.er[0] | dev.er[1] << 1 | dev.er[2] << 2);
        sf_scan_run();
    }
    if (patches) {
        const char *p = patches;
        uint8_t a = 0, b = 0;
        sf_import_patches(str_getc, str_eof, &p, &a, &b);
        np = a;
        ns = b;
    }
    if (summary) {
        int k = snprintf(summary, max, "IMPORTED");
        if (scan) {
            for (int u = 0; u < 4; u++)
                if (dev.ex[u]) k += snprintf(summary + k, max - k, " EX%d", u + 1);
            for (int n = 0; n < 3; n++)
                if (dev.er[n]) k += snprintf(summary + k, max - k, " ER301-%d", n + 1);
        }
        if (patches) snprintf(summary + k, max - k, " %d PATCHES %d SONGS", np, ns);
    }
    return (scan ? 1 : 0) + (patches ? 2 : 0);
}

// find sfscan.txt / sfpatch.txt in dir (any case, FAT sticks often say
// SFSCAN.TXT) and import them
int emu_import_dir(const char *d, char *summary, int max) {
    DIR *dd = opendir(d);
    if (!dd) return 0;
    char scanp[1200] = "", patchp[1200] = "";
    struct dirent *e;
    while ((e = readdir(dd))) {
        if (!strcasecmp(e->d_name, "sfscan.txt")) snprintf(scanp, sizeof scanp, "%s/%s", d, e->d_name);
        if (!strcasecmp(e->d_name, "sfpatch.txt")) snprintf(patchp, sizeof patchp, "%s/%s", d, e->d_name);
    }
    closedir(dd);
    if (!scanp[0] && !patchp[0]) return 0;
    char *a = scanp[0] ? slurp(scanp) : NULL, *b = patchp[0] ? slurp(patchp) : NULL;
    int r = emu_import_text(a, b, summary, max);
    free(a);
    free(b);
    return r;
}

static FILE *export_file;
static void export_put(const char *text) {
    if (export_file) fputs(text, export_file);
}

bool emu_export_dir(const char *d, char *summary, int max) {
    char path[1200];
    static char scratch[SF_CELLS];
    snprintf(path, sizeof path, "%s/SFSCAN.TXT", d);
    export_file = fopen(path, "wb");
    if (!export_file) {
        if (summary) snprintf(summary, max, "CAN'T WRITE %s", path);
        return false;
    }
    sf_report_scan(export_put);
    fclose(export_file);
    snprintf(path, sizeof path, "%s/SFPATCH.TXT", d);
    export_file = fopen(path, "wb");
    if (!export_file) {
        if (summary) snprintf(summary, max, "CAN'T WRITE %s", path);
        return false;
    }
    sf_report_patches(export_put, scratch);
    fclose(export_file);
    export_file = NULL;
    int np = 0, ns = 0;
    for (int n = 1; n <= SF_PATCH_SLOTS; n++) {
        const uint8_t *dd;
        if (sf_store_patch_get(n, &dd)) np++;
    }
    for (int n = 1; n <= SF_SONG_SLOTS; n++) {
        sf_song_t s;
        if (sf_store_song_get(n, &s)) ns++;
    }
    if (summary) snprintf(summary, max, "WROTE SFSCAN.TXT + SFPATCH.TXT (%d PATCHES %d SONGS) TO %s", np, ns, d);
    return true;
}

// look through every mounted volume (USB sticks show up in /Volumes)
int emu_import_usb(char *summary, int max) {
    DIR *v = opendir("/Volumes");
    if (!v) return 0;
    struct dirent *e;
    int r = 0;
    while (!r && (e = readdir(v))) {
        if (e->d_name[0] == '.') continue;
        char path[1100];
        snprintf(path, sizeof path, "/Volumes/%s", e->d_name);
        r = emu_import_dir(path, summary, max);
    }
    closedir(v);
    if (!r && summary) snprintf(summary, max, "NO SFSCAN.TXT / SFPATCH.TXT ON ANY USB STICK");
    return r;
}

void tele_scene(uint8_t i, uint8_t init_grid, uint8_t init_pattern) {
    (void)init_grid;
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
void reset_midi_counter(void) {}

////////////////////////////////////////////////////////////////////////////
// panel

bool emu_tr(int i) {
    return i >= 0 && i < 4 && tr_out[i];
}
int16_t emu_cv(int i) {
    return i >= 0 && i < 4 ? aout[i].now : 0;
}
bool emu_er_tr(int p) {
    return p >= 1 && p <= 26 && er_tr[p];
}
int16_t emu_er_cv(int p) {
    return p >= 1 && p <= 26 ? er_cv[p] : 0;
}
uint32_t emu_er_seen(int p) {
    return p >= 1 && p <= 26 ? er_seen[p] : 0;
}
uint16_t emu_bpm(void) {
    return sf.bpm;
}
bool emu_playing(void) {
    return sf.playing;
}
uint32_t emu_frame(void) {
    return sf.f;
}

// libavr32/src/interrupts.h: no interrupts on the desktop, one thread
u8 irqs_pause(void) {
    return 0;
}
void irqs_resume(u8 flags) {
    (void)flags;
}

// for the tests: ms since emu_init
uint32_t emu_now_since_boot(void) {
    return now_ms - boot_ms;
}
