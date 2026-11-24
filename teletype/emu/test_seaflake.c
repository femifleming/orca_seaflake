// End-to-end tests: Seaflake grid -> real Teletype ops -> I2C bytes.
// Runs on the emulator host (tt_emu.c) without a window.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "seaflake/sf.h"
#include "seaflake/sf_ii.h"
#include "seaflake/sf_store.h"
#include "tt_emu.h"
#include "seaflake/sf_config.h"
#include "seaflake/sf_report.h"
#include "ops/op.h"
#include "ops/op_enum.h"
#include "seaflake_mode.h"
#include "globals.h"
#include "state.h"
#include "teletype.h"
#include "dream.h"
#include "sf_ui.h"
#include "flash.h"

extern scene_state_t scene_state;

static char logtxt[64][100];
static int nlog;
static int fails, passes;

static void on_log(int kind, const char *t) {
    (void)kind;
    if (nlog < 64) snprintf(logtxt[nlog++], 100, "%s", t);
}

static char report[60000];
static void report_put(const char *t) {
    strncat(report, t, sizeof(report) - strlen(report) - 1);
}

static char cover[200][24];
static int ncover;
static void on_cover(int kind, const char *t) {
    if (kind != 0) return;
    char name[24];
    int n = 0;
    while (t[n] && t[n] != ' ' && n < 23) name[n] = t[n], n++;
    name[n] = 0;
    for (int i = 0; i < ncover; i++)
        if (!strcmp(cover[i], name)) return;
    if (ncover < 200) strcpy(cover[ncover++], name);
}

static void clear(void) {
    nlog = 0;
    sf_silence();
    sf_clear();
}

static void put(int x, int y, const char *s) {
    for (int i = 0; s[i]; i++) sf_set(x + i, y, s[i]);
}

static bool logged(const char *want) {
    for (int i = 0; i < nlog; i++)
        if (strstr(logtxt[i], want)) return true;
    return false;
}

static uint32_t now = 1000;
static void frames(int n);

// bang op once: D1 above-left writes a locked * next to it (as in Orca, a
// typed * is erased before its neighbours see it)
static void fire(const char *op) {
    put(0, 0, "D1");
    put(1, 1, op);
    frames(1);
    sf_set(0, 0, '.');
    sf_set(1, 0, '.');
}

// ---- the manual's and the help's examples, run as written ----
// rows joined with '/'; runs n frames watching the Teletype TR jacks
static void frames(int n);
static uint8_t ex_tr_seen;  // bit i: TR i+1 went high
static void run_layout(const char *rows, int n) {
    sf_clear();
    nlog = 0;
    int x = 0, y = 0;
    for (const char *c = rows; *c; c++) {
        if (*c == '/') { y++; x = 0; continue; }
        sf_set(x++, y, *c);
    }
    sf_play(false);  // frames by hand (frames()), as fire() does
    ex_tr_seen = 0;
    for (int i = 0; i < n; i++) {
        frames(1);
        for (int t = 0; t < 4; t++) if (emu_tr(t)) ex_tr_seen |= 1 << t;
    }
}
static void frames(int n) {
    for (int i = 0; i < n; i++) {
        now += 125;
        emu_tick(now);
        sf_frame(now);
    }
}

// what stock Teletype sends for a script line: parse it and run it, as if
// typed in a script (the reference for the grid operators' bytes)
static void stock(const char *line) {
    tele_command_t c;
    char err[TELE_ERROR_MSG_LENGTH];
    if (parse(line, &c, err) != E_OK) {
        printf("  stock: cannot parse %s\n", line);
        return;
    }
    exec_state_t es;
    es_init(&es);
    es_push(&es);
    process_command(&scene_state, &es, &c);
}

// the I2C byte lines of the log, joined with |
static const char *i2c(void) {
    static char out[2000];
    out[0] = 0;
    for (int i = 0; i < nlog; i++)
        if (!strncmp(logtxt[i], "I2C", 3)) {
            strncat(out, logtxt[i], sizeof(out) - strlen(out) - 2);
            strcat(out, "|");
        }
    return out;
}

static int count_logged(const char *want) {
    int n = 0;
    for (int i = 0; i < nlog; i++)
        if (strstr(logtxt[i], want)) n++;
    return n;
}

#define CHECK(name, cond)                                         \
    do {                                                          \
        if (cond) {                                               \
            passes++;                                             \
        }                                                         \
        else {                                                    \
            fails++;                                              \
            printf("FAIL %s\n", name);                            \
            for (int i = 0; i < nlog; i++) printf("     %s\n", logtxt[i]); \
        }                                                         \
    } while (0)

int main(void) {
    char dir[] = "/tmp/sf-emu-test-XXXXXX";
    mkdtemp(dir);
    emu_set_log(on_log);
    emu_init(dir, now);
    CHECK("Orca plays as soon as it starts", sf.playing);
    CHECK("ports config block intact (magic + checksum)", sf_config_ok());
    CHECK("ports config: auto units, 20 ms pulse", sf_config_ex_unit() == 0 && sf_config_er_unit() == 0 && sf_config_pulse_ms() == 20 && sf.pulse_ms == 20);
    sf_play(false);   // (the tests below step the frames themselves)

    // : MIDI note -> EX.M.N# / EX.M.NO#
    clear();
    fire(":04C");
    CHECK(": runs EX.M.N# 1 72 119", logged("EX.M.N# 1 72 119"));
    CHECK(": sends 4F 90 48 77 to EX1", logged("I2C EX1  (0x41): 4F 90 48 77"));
    nlog = 0;
    frames(2);
    CHECK(": note off EX.M.NO# 1 72", logged("EX.M.NO# 1 72"));

    // channel 2, octave 3, sharp, velocity 8, length 3
    clear();
    fire(":13d83");
    CHECK(":13d83 -> ch 2 note 63 vel 64", logged("EX.M.N# 2 63 64"));

    // % mono
    clear();
    fire("%04C");
    CHECK("% mono note", logged("EX.M.N# 1 72 119"));

    // ! CC: ch0 cc 7 value z -> 127
    clear();
    fire("!07z");
    CHECK("! -> EX.M.CC# 1 7 127", logged("EX.M.CC# 1 7 127"));

    // ? pitch bend: channel select, raw bytes, channel restored
    clear();
    fire("?0hh");
    CHECK("? -> EX.M.PB on channel 1", logged("EX.M.CH 1") && logged("EX.M.PB"));
    CHECK("? bytes E0 3E 3E", logged("4F E0 3E 3E"));

    // ( just intonation: E (4) has a bend
    clear();
    fire("(04E");
    CHECK("( sends pitch bend then note", logged("EX.M.PB") && logged("EX.M.N# 1 76"));

    // + scale and ) scale note: degree 3 of C major = E
    clear();
    put(3, 0, "0+101011010101");
    fire(")0430");
    CHECK(") degree 3 of C major = 76", logged("EX.M.N# 1 76"));

    // > Disting EX functions, 2026-11-19 layout: value first, side last
    // N: octave, note, velocity -> EX.NOTE, pitch 1 V/oct with 0 V = note 48
    clear();
    nlog = 0;
    fire(">n4C");
    CHECK(">n4C -> EX.NOTE 3277 15360 (note 72, velocity f)", logged("EX.NOTE 3277 15360"));
    CHECK(">n4C bytes: 6A 01 48 | 68 01 48 0C CD | 69 01 48 3C 00",
          logged("(0x41): 6A 01 48") && logged("(0x41): 68 01 48 0C CD") && logged("(0x41): 69 01 48 3C 00"));
    {
        char grid_bytes[2000];
        strcpy(grid_bytes, i2c());
        nlog = 0;
        stock("EX.NOTE N 24 15360");  // the same note typed as a Teletype script
        if (strcmp(grid_bytes, i2c())) printf("  grid  %s\n  stock %s\n", grid_bytes, i2c());
        CHECK(">n4C sends exactly what stock EX.NOTE N 24 15360 sends", !strcmp(grid_bytes, i2c()));
    }
    nlog = 0;
    frames(2);
    CHECK(">n note off one frame later: EX.NOTE.O -> 6A 01 48", logged("EX.NOTE.O 3277") && logged("(0x41): 6A 01 48"));
    clear();
    fire(">N4c9");  // uppercase N is the same; c = C#, velocity 9
    CHECK(">N4c9 -> note 73, velocity 9216", logged("EX.NOTE 3413 9216") && logged("69 01 49 24 00"));
    clear();
    fire(">n.Cg");  // octave empty = 4, velocity g = 16384 (the maximum)
    CHECK(">n.Cg -> octave 4, velocity 16384", logged("EX.NOTE 3277 16384"));
    {
        // every MIDI note survives the pitch conversion through the stock op
        bool all = true;
        for (int n = 0; n < 128; n++) {
            nlog = 0;
            sf_io_ex_note(n, 0, false);
            char want[40];
            snprintf(want, sizeof want, "(0x41): 6A 01 %02X", n);
            if (!logged(want)) all = false, printf("  note %d: %s\n", n, nlog ? logtxt[nlog - 1] : "-");
        }
        CHECK("EX.NOTE note id round trip for all 128 notes", all);
    }
    // d: note on with a channel (what >n was before), o: note off
    clear();
    fire(">d14C9");
    CHECK(">d14C9 -> EX.N# 2 3277 9216", logged("EX.N# 2 3277 9216") && logged("69 02 48 24 00"));
    clear();
    fire(">o.4C");
    CHECK(">o.4C -> EX.NOTE.O 3277", logged("EX.NOTE.O 3277") && logged("(0x41): 6A 01 48"));
    // parameters, like EX.P: >p param value (exact), >v param level (0-z)
    clear();
    fire(">p030a");
    CHECK(">p030a -> EX.PARAM 3 10 = 46 03 00 0A", logged("EX.PARAM 3 10") && logged("(0x41): 46 03 00 0A"));
    {
        char g[2000];
        strcpy(g, i2c());
        nlog = 0;
        stock("EX.P 3 10");
        CHECK(">p030a sends what stock EX.P 3 10 sends", !strcmp(g, i2c()));
    }
    clear();
    fire(">p030a2");
    CHECK(">p030a2 -> EX.P2 3 10 = 5D 13 0A", logged("EX.P2 3 10") && logged("(0x41): 5D 13 0A"));
    clear();
    fire(">p01");
    CHECK(">p01 (no value) reads EX.PARAM 1 and writes it below", logged("EX.PARAM 1") && logged("(0x41): 48 01") && sf_get(1, 2) == '0');
    clear();
    fire(">v03z");
    CHECK(">v03z -> EX.PV 3 16384 = 47 03 40 00", logged("EX.PV 3 16384") && logged("(0x41): 47 03 40 00"));
    clear();
    fire(">v03i2");
    CHECK(">v03i2 -> EX.PV2 3 8426", logged("EX.PV2 3 8426"));
    clear();
    fire(">i07");
    CHECK(">i07 -> EX.MIN 7", logged("EX.MIN 7") && logged("(0x41): 49 07"));
    // presets
    clear();
    fire(">l05");
    CHECK(">l05 -> EX.PRESET 5 = 40 00 05", logged("EX.PRESET 5") && logged("(0x41): 40 00 05"));
    clear();
    fire(">l");
    CHECK(">l (empty) reads the preset: 43", logged("(0x41): 43") && sf_get(1, 2) == '0');
    clear();
    fire(">s0a");
    CHECK(">s0a -> EX.SAVE 10 = 41 00 0A", logged("EX.SAVE 10") && logged("(0x41): 41 00 0A"));
    clear();
    fire(">a5");
    CHECK(">a5 -> EX.ALG 5 = 44 05", logged("EX.ALG 5") && logged("(0x41): 44 05"));
    clear();
    fire(">a537");
    CHECK(">a537 -> EX.A12 5 7", logged("EX.A12 5 7"));
    clear();
    fire(">qr1");
    CHECK(">qr1 -> EX.LP.REC 1", logged("EX.LP.REC 1"));
    // voices: pitch and velocity in the Disting's units too
    clear();
    fire(">w14C9");
    CHECK(">w14C9 -> EX.VOX 1 3277 9216", logged("EX.VOX 1 3277 9216"));
    clear();
    fire(">t2");
    CHECK(">t2 (velocity empty) -> EX.T 2", logged("EX.T 2"));
    // >u is gone: the Disting EX is always 0x41 (fixed, 2026-11-20)
    clear();
    fire(">u2");
    CHECK(">u is gone: nothing sent", !logged("EX 2") && !logged("(0x42)"));
    fire(">n4C");
    CHECK("N always goes to 0x41", logged("(0x41)"));
    clear();
    put(3, 3, "#");
    fire(">qx1");
    CHECK("bad looper action is an error", sf.error == 1);

    // < ER-301 operator
    clear();
    fire("<p03");
    CHECK("<p03 -> SC.TR.PULSE 3", logged("SC.TR.PULSE 3"));
    CHECK("<p03 bytes 31: 05 02", logged("(0x31): 05 02"));
    clear();
    fire("<c02z");
    CHECK("<c02z -> 31: 10 01 3F FF", logged("(0x31): 10 01 3F FF"));
    clear();
    fire("<n014C");
    CHECK("<n014C -> SC.CV 1 6554 (4 V)", logged("SC.CV 1 6554"));
    clear();
    fire("<m0105");
    CHECK("<m0105 -> SC.TR.TIME 1 5", logged("SC.TR.TIME 1 5"));

    // ; and = stay Seaflake's UDP / OSC: they send nothing here
    clear();
    fire(";M_N_60_100");
    CHECK("; sends nothing", nlog == 0);

    // " trigger note on Teletype output 1
    clear();
    fire("\"14C");
    CHECK("\" TR 1 goes high", emu_tr(0));
    CHECK("\" CV 1 = C4 (4 V)", emu_cv(0) == 6554);
    for (int i = 0; i < 25; i++) emu_tick(now += 1);
    CHECK("\" TR 1 low after 20 ms", !emu_tr(0));

    // " trigger note on ER-301 port a (1)
    clear();
    fire("\"a4C");
    CHECK("\" on ER-301 a -> SC.TR.TIME 1 20", logged("SC.TR.TIME 1 20"));
    CHECK("\" on ER-301 a -> SC.CV 1 6554", logged("SC.CV 1 6554"));
    CHECK("\" on ER-301 a -> SC.TR.P 1", logged("SC.TR.PULSE 1"));

    // / gate held for 3 frames
    clear();
    fire("/24E.3");
    CHECK("/ gate high", emu_tr(1));
    frames(3);
    CHECK("/ gate low after 3 frames", !emu_tr(1));

    // ' CV level: z = 10 V, slew 0, and no trigger (only " and / fire TR)
    clear();
    fire("'30z");
    CHECK("' CV 3 = 10 V", emu_cv(2) == 16383);
    CHECK("' does not fire TR 3", !emu_tr(2));
    CHECK("' sends no TR command", !logged("TR.PULSE") && !logged("TR.TIME"));

    // ' next to " on the same row, banged together: " fires TR 1, ' only moves CV 2
    clear();
    sf_silence();
    put(0, 0, "D1");
    put(1, 1, "\"14C");
    put(6, 0, "D1");
    put(7, 1, "'20z");
    frames(1);
    CHECK("\" beside ' : TR 1 fires", emu_tr(0));
    CHECK("\" beside ' : CV 1 = 4 V", emu_cv(0) == 6554);
    CHECK("' beside \" : CV 2 = 10 V", emu_cv(1) == 16383);
    CHECK("' beside \" : TR 2 stays low", !emu_tr(1));
    for (int i = 0; i < 25; i++) emu_tick(now += 1);
    CHECK("\" beside ' : TR 1 low again after 20 ms", !emu_tr(0));
    CHECK("' beside \" : TR 2 still low", !emu_tr(1));
    sf_set(0, 0, '.'); sf_set(1, 0, '.'); sf_set(6, 0, '.'); sf_set(7, 0, '.');

    // \ CV bend: CV only, no trigger either
    clear();
    fire("\\4hh");
    CHECK("\\ bends CV 4 without a trigger", !emu_tr(3) && !logged("TR.PULSE"));

    // $bpm: directive without a bang, $apm: as well, $ with a bang
    clear();
    put(0, 0, "$bpm:140");
    frames(1);
    CHECK("$bpm:140 directive", sf.bpm == 140);
    clear();
    put(0, 0, "$apm:25");
    frames(1);
    CHECK("$apm:25 directive = 100 bpm", sf.bpm == 100);
    clear();
    fire("$bpm:90");
    CHECK("banged $bpm:90", sf.bpm == 90);
    clear();
    fire("$apm:40");
    CHECK("banged $apm:40 = 160 bpm", sf.bpm == 160);
    clear();
    fire("$pulse:50");
    CHECK("$pulse:50", sf.pulse_ms == 50);
    sf.pulse_ms = 20;

    // patches: pack, store, load
    clear();
    put(3, 2, "D4");
    put(3, 3, "*:04C");
    put(79, 39, "#");
    uint8_t buf[SF_PACK_MAX];
    uint16_t len = sf_pack(buf, sizeof buf);
    sf_store_patch_put(5, buf, len);
    clear();
    CHECK("cleared", sf_get(3, 2) == '.');
    CHECK("load P05", sf_io_patch_load(5) && sf_get(3, 2) == 'D' && sf_get(79, 39) == '#');
    CHECK("patch size small", len < 40);

    // a full grid still fits or is refused, never corrupted
    for (int y = 0; y < SF_H; y++)
        for (int x = 0; x < SF_W; x++) sf_set(x, y, "abc"[(x + y) % 3]);
    CHECK("full grid is refused", sf_pack(buf, sizeof buf) == 0);

    // song mode: two slots, 2 frames each, ordered
    clear();
    put(0, 0, "A");
    len = sf_pack(buf, sizeof buf);
    sf_store_patch_put(1, buf, len);
    clear();
    put(0, 0, "B");
    len = sf_pack(buf, sizeof buf);
    sf_store_patch_put(2, buf, len);
    sf_song_clear();
    sf.song.slots[0] = (sf_song_slot_t){ .patch = 1, .len = 2, .mul = 1, .bpm = 120 };
    sf.song.slots[1] = (sf_song_slot_t){ .patch = 2, .len = 1, .mul = 2, .bpm = 150 };
    sf_song_start();
    char seen[8] = { 0 };
    // step the engine by hand: emu_tick would also run the song's own clock
    for (int i = 0; i < 6; i++) {
        sf_frame(now += 125);
        seen[i] = sf_get(0, 0);
    }
    if (strcmp(seen, "AABBAA")) printf("song saw %s\n", seen);
    CHECK("song plays P01 P01 P02 P02 P01 P01", !strcmp(seen, "AABBAA"));
    sf_song_stop();

    sf_song_t song = sf.song;
    sf_store_song_put(3, &song);
    sf_song_t back;
    CHECK("song 3 saved", sf_store_song_get(3, &back) && back.slots[1].bpm == 150);

    // the screensaver picks one of its four dreams each time it starts, at
    // random, never the same one twice in a row, and every dream stays
    // under the OLED limit (32 lit pixels a row)
    {
        static uint8_t band[128 * 8];
        int seen[4] = { 0 }, repeats = 0, over = 0;
        uint8_t prev = 0xFF;
        for (int k = 0; k < 60; k++) {
            frames(1 + k % 7);
            for (uint32_t ms = 0; ms < 3000; ms += 1000)
                for (uint8_t i = 0; i < 8; i++) {
                    dream_band(ms, i, band);
                    for (int r = 0; r < 8; r++) {
                        int lit = 0;
                        for (int x = 0; x < 128; x++) lit += band[r * 128 + x] != 0;
                        over += lit > 32;
                    }
                }
            const uint8_t d = dream_current();
            if (d < 4) seen[d]++;
            repeats += d == prev;
            prev = d;
        }
        printf("  dreams over 60 starts: %d %d %d %d\n", seen[0], seen[1], seen[2], seen[3]);
        if (dream_random()) {
            CHECK("screensaver: all four dreams come up", seen[0] && seen[1] && seen[2] && seen[3]);
            CHECK("screensaver: never the same dream twice in a row", repeats == 0);
        }
        else
            CHECK("screensaver: always the one dream", repeats == 59);
        CHECK("screensaver: no row over 32 lit pixels", over == 0);
    }

    // , Just Friends: each function sends exactly what the stock JF.* op
    // typed in a script sends
    {
        static const struct { const char *grid, *script; } jf[] = {
            { ",n4Ca", "JF.NOTE 0 8192" },
            { ",n5C.", "JF.NOTE 1638 8192" },
            { ",n3Gg", "JF.NOTE -683 13107" },
            { ",v24Ca", "JF.VOX 2 0 8192" },
            { ",v.3C.", "JF.VOX 1 -1638 8192" },
            { ",f34E", "JF.PITCH 3 546" },
            { ",r1a", "JF.VTR 1 8192" },
            { ",r0g", "JF.VTR 0 13107" },
            { ",t31", "JF.TR 3 1" },
            { ",t70", "JF.TR 7 0" },
            { ",s5C", "JF.SHIFT 1638" },
            { ",ui", "JF.RUN 0" },
            { ",u0", "JF.RUN -8192" },
            { ",m1", "JF.MODE 1" },
            { ",o0", "JF.RMODE 0" },
            { ",d1", "JF.GOD 1" },
            { ",qc", "JF.QT 12" },
            { ",k3c", "JF.TICK 120" },
            { ",j132", "JF.TUNE 1 3 2" },
            { ",p4Ca", "JF.POLY 0 8192" },
            { ",a2", "JF.ADDR 2" },
        };
        for (unsigned i = 0; i < sizeof(jf) / sizeof(jf[0]); i++) {
            char mine[2000], name[80];
            clear();
            fire(jf[i].grid);
            strcpy(mine, i2c());
            nlog = 0;
            stock(jf[i].script);
            snprintf(name, sizeof(name), "%s = %s (same bytes)", jf[i].grid, jf[i].script);
            CHECK(name, mine[0] && !strcmp(mine, i2c()) && strstr(mine, i == 9 ? "JF 2 (0x75)" : "JF 1 (0x70)"));
        }
        // the scan asks Just Friends with one write-only byte (no read)
        nlog = 0;
        sf_scan_run();
        CHECK("scan finds Just Friends at 0x70 (write 8F, no read)", sf_jf_state == 1 && logged("(0x70): 8F"));
        emu_set_jf(0);
        sf_scan_run();
        CHECK("scan: no Just Friends -> --", sf_jf_state == 2);
        emu_set_jf(1);
        sf_scan_run();
        clear();
        fire(",e2");
        CHECK(",e2 -> JF.SEL 2", logged("JF.SEL 2"));
        clear();
        fire(",n4C");
        CHECK("after ,e2 notes go to the second Just Friends (0x75)", logged("(0x75): 09 00 00 20 00"));
        clear();
        fire(",e1");
        fire(",x");
        CHECK(",x -> JF.POLY.RESET", logged("JF.POLY.RESET"));
        clear();
        fire(",n4C");
        CHECK(",n4C bytes: 09 00 00 20 00 to 0x70", logged("I2C JF 1 (0x70): 09 00 00 20 00"));
        // ,t with the state empty is a trigger: high now, low one $pulse later
        clear();
        fire(",t3.");
        frames(1);
        CHECK(",t3. -> JF.TR 3 1 then JF.TR 3 0", logged("(0x70): 01 03 01") && logged("(0x70): 01 03 00"));
        // ,@ : @'s shaped run as Just Friends notes, no note offs
        clear();
        fire(",@4Ca131");
        frames(10);
        CHECK(",@4Ca31 plays 3 JF notes", count_logged("(0x70): 09 00 00 20 00") == 3);
        // readers: the knob comes back as 0-z below the operator
        clear();
        fire(",gr");
        CHECK(",gr reads JF.RAMP (0x91) and writes h (5 V) below", logged("(0x70): 91") && sf_get(1, 2) == 'h');
        clear();
        fire(",gs");
        CHECK(",gs reads JF.SPEED: 1 below", logged("(0x70): 8F") && sf_get(1, 2) == '1');
        clear();
        fire(",n.");
        CHECK(",n with no note: ERR, nothing sent", !strstr(i2c(), "0x70"));
        // the status line names the cells
        clear();
        put(0, 0, ",v24Ca");
        frames(1);
        char nm[32];
        CHECK("status: , = JF VOICE NOTE", !strcmp(sf_cell_name(0, 0, nm, sizeof(nm)), "JF VOICE NOTE"));
        CHECK("status: ,v cell 1 = JF CHANNEL", !strcmp(sf_cell_name(2, 0, nm, sizeof(nm)), "JF CHANNEL"));
        CHECK("status: ,v cell 5 = JF VELOCITY", !strcmp(sf_cell_name(5, 0, nm, sizeof(nm)), "JF VELOCITY"));
        clear();
    }

    // every EX.* and SC.* op is reachable from > or <: fuzz every selector
    // and compare the ops it ran with Teletype's op table (aliases count as
    // their twin: same get/set functions)
    emu_set_log(on_cover);
    const char *alpha = ".01239azg";
    srand(7);
    static const char devs[3] = { '>', '<', ',' };
    for (int d = 0; d < 3; d++)
        for (char sel = 'a'; sel <= 'z'; sel++) {
            if (!sf_dev_format(devs[d], sel)) continue;
            for (int t = 0; t < 600; t++) {
                char cells[12];
                for (int k = 0; k < 12; k++) cells[k] = alpha[rand() % 9];
                if (t % 3 == 0) cells[0] = 'a' + rand() % 26;  // action letters
                int16_t r;
                sf_io_dev(devs[d], sel, cells, &r);
            }
        }
    emu_set_log(on_log);
    // sent by the MIDI operators : % ! (tested above), not by >
    strcpy(cover[ncover++], "EX.M.N#");
    strcpy(cover[ncover++], "EX.M.NO#");
    strcpy(cover[ncover++], "EX.M.CC#");
    strcpy(cover[ncover++], "EX");  // the unit: set to 1 by the scan (fixed address)
    int nops = 0, missing = 0;
    for (int i = 0; i < E_OP__LENGTH; i++) {
        const tele_op_t *o = tele_ops[i];
        if (strncmp(o->name, "EX.", 3) && strncmp(o->name, "SC.", 3) && strncmp(o->name, "JF.", 3) &&
            strcmp(o->name, "EX"))
            continue;
        nops++;
        bool hit = false;
        for (int k = 0; k < ncover && !hit; k++) {
            for (int j = 0; j < E_OP__LENGTH; j++)
                if (!strcmp(tele_ops[j]->name, cover[k]) && tele_ops[j]->get == o->get &&
                    tele_ops[j]->set == o->set)
                    hit = true;
        }
        if (!hit) {
            missing++;
            printf("  not reachable: %s\n", o->name);
        }
    }
    printf("%d EX/SC/JF ops, %d reachable from > < and ,\n", nops, nops - missing);
    CHECK("every EX.* SC.* and JF.* op is reachable", missing == 0);
    nlog = 0;

    // the status line names what the cursor is on
    clear();
    put(0, 0, ".>n4N.....");
    put(0, 1, ".>p0n.....");
    put(0, 2, "....n.....");
    put(0, 6, "D4........");
    put(0, 3, ".:04C.....");
    put(0, 4, "<n014C....");
    put(0, 5, ".>@4Cf1...");
    put(0, 8, "-1_14C}1..");
    put(0, 9, "`.........");
    frames(1);
    {
        char b[20];
        struct { int x, y; const char *want; } t[] = {
            { 1, 0, "DISTING N" }, { 2, 0, "EX N" }, { 3, 0, "EX OCTAVE" },
            { 4, 0, "EX NOTE" }, { 5, 0, "EX VELOCITY" }, { 3, 1, "EX PARAM HI" },
            { 4, 1, "EX PARAM LO" }, { 5, 1, "EX VALUE HI" }, { 0, 6, "DELAY" }, { 1, 6, "D MODULO" },
            { 1, 2, "> EX READ VALUE" }, { 2, 3, ": CHANNEL" }, { 4, 3, ": NOTE" },
            { 1, 4, "ER CV NOTE" }, { 2, 4, "ER PORT HI" }, { 4, 4, "ER OCTAVE" },
            { 0, 7, "D BANG OUT" }, { 2, 5, "EX N@" }, { 6, 5, "EX SHAPE 1-4" }, { 4, 2, "NORTH" },
            { 0, 8, "TRIG" }, { 1, 8, "- OUTPUT" }, { 3, 8, "_ OUTPUT" }, { 5, 8, "_ NOTE" },
            { 7, 8, "} OUTPUT" }, { 0, 9, "LOADBANG" }, { 0, 10, "` BANG OUT" },
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
            const char *n = sf_cell_name(t[i].x, t[i].y, b, sizeof b);
            if (!n || strcmp(n, t[i].want)) printf("  cell %d,%d: '%s' want '%s'\n", t[i].x, t[i].y, n ? n : "(none)", t[i].want);
            CHECK("status names the cell", n && !strcmp(n, t[i].want));
        }
    }

    // the module's USB report: a stick saying EX unit 2 and ER-301 2 are there
    {
        char stick[] = "/tmp/sf-stick-XXXXXX";
        mkdtemp(stick);
        char path[200];
        snprintf(path, sizeof path, "%s/SFSCAN.TXT", stick);
        FILE *f = fopen(path, "w");
        fputs("ORCA SEAFLAKE I2C SCAN\r\nEX1 FOUND ALG 7 PRESET 300\r\nEX2 NONE\r\nEX3 NONE\r\n"
              "EX4 NONE\r\nER301 1 FOUND\r\nER301 2 NONE\r\nER301 3 NONE\r\n", f);
        fclose(f);
        char sum[120];
        int r = emu_import_dir(stick, sum, sizeof sum);
        printf("  %s\n", sum);
        CHECK("scan report imported", r == 1 && sf_scan.ex_unit == 1 && sf_scan.er_unit == 1);
        CHECK("EX algorithm and preset read back", sf_scan.ex_alg[0] == 7 && sf_scan.ex_preset[0] == 300);
        clear();
        fire(":04C");
        CHECK("MIDI goes to the Disting EX at 0x41", logged("(0x41): 4F 90 48 77"));
        clear();
        fire("\"a4C");
        CHECK("output a goes to ER-301 port 1 (0x31)", logged("SC.TR.PULSE 1") && logged("(0x31)"));
        clear();
        fire("<p05");
        CHECK("<p05 goes to ER-301 port 5", logged("SC.TR.PULSE 5"));

        // patches + songs: write the report, import it, compare
        report[0] = 0;
        static char grid[SF_CELLS];
        sf_report_patches(report_put, grid);
        const uint8_t *d1;
        uint16_t l1 = sf_store_patch_get(1, &d1);
        uint8_t before[1100];
        memcpy(before, d1, l1);
        sf_song_t s1;
        sf_store_song_get(3, &s1);
        sf_store_patch_put(1, (const uint8_t *)"\x80", 1);
        r = emu_import_text(NULL, report, sum, sizeof sum);
        printf("  %s\n", sum);
        const uint8_t *d2;
        uint16_t l2 = sf_store_patch_get(1, &d2);
        sf_song_t s2;
        sf_store_song_get(3, &s2);
        CHECK("patch 1 survives USB report + import", l1 == l2 && !memcmp(before, d2, l1));
        CHECK("song 3 survives USB report + import", !memcmp(&s1, &s2, sizeof s1));

        // back to EX1 + ER-301 1 for the rest
        snprintf(path, sizeof path, "%s/SFSCAN.TXT", stick);
        f = fopen(path, "w");
        fputs("EX1 FOUND ALG 0 PRESET 0\r\nER301 1 FOUND\r\n", f);
        fclose(f);
        emu_import_dir(stick, sum, sizeof sum);
        CHECK("back on EX1 + ER-301 1", sf_scan.ex_unit == 1 && sf_scan.er_unit == 1);
    }

    // typing on the grid: a letter alone is lowercase, with shift uppercase
    {
        clear();
        sf_play(false);
        emu_key(0x29, 0, false);                                    // (esc: make sure the grid has the keys)
        emu_key(0x4A, 0, false);                                    // home
        for (int i = 0; i < 12; i++) emu_key(0x52, 0, false);      // up to the top row
        emu_key(0x06, 0, false);                                    // c
        const char lower = sf_get(0, 0);
        emu_key(0x06, 0x02, false);                                 // shift-c
        const char upper = sf_get(0, 0);
        emu_key(0x39, 0, false);                                    // caps lock
        emu_key(0x06, 0, false);
        const char capsc = sf_get(0, 0);
        emu_key(0x39, 0, false);
        if (lower != 'c' || upper != 'C' || capsc != 'C') printf("  typed %c, %c, %c\n", lower, upper, capsc);
        CHECK("c types c, shift-c types C, caps lock swaps them", lower == 'c' && upper == 'C' && capsc == 'C');
        clear();
    }

    // T lights the value it picks, frame by frame, as Orca does
    {
        clear();
        put(2, 0, "C4");
        put(3, 1, "4TCEGb");
        bool walks = true;
        for (int k = 0; k < 8; k++) {
            sf_frame(now += 125);
            const int key = (int)((sf.f - 1) % 4);
            for (int i = 0; i < 4; i++)
                if (sf_picked(5 + i, 1) != (i == key)) walks = false;
        }
        CHECK("T lights the value it picks, and it walks with the key", walks);
        CHECK("the other values stay locked but not lit", sf_locked(6, 1) || sf_locked(5, 1));
        clear();
    }

    // power-on: the last patch plays at its own tempo; a song saved while playing plays
    {
        clear();
        put(0, 0, "D4");
        sf_set_bpm(137);
        uint8_t buf2[SF_PACK_MAX];
        uint16_t l = sf_pack(buf2, sizeof buf2);
        sf_store_patch_put(9, buf2, l);
        sf_store_set_last_patch(9);
        sf_store_set_boot_song(0);
        sf_set_bpm(100);
        seaflake_init();
        CHECK("power-on: the last patch, playing at the tempo it was saved with",
              sf.playing && !sf.song_playing && sf.bpm == 137 && sf_get(0, 0) == 'D');

        sf_song_t sg;
        memset(&sg, 0, sizeof sg);
        sg.magic = SF_SONG_MAGIC;
        sg.loop = 1;
        sg.slots[0] = (sf_song_slot_t){ .patch = 9, .len = 4, .mul = 1, .bpm = 111 };
        sf_store_song_put(4, &sg);
        sf_store_set_boot_song(4);
        seaflake_init();
        CHECK("power-on after saving with a song playing: that song plays", sf.song_playing && sf.song_no == 4 && sf.bpm == 111);
        sf_store_set_boot_song(0);
        sf_play(false);

        // a patch from before patches kept their tempo: it loads, the tempo stays
        const uint8_t old[] = { 'D', '4' };
        sf_store_patch_put(10, old, 2);
        sf_set_bpm(90);
        CHECK("an older patch without a tempo still loads", sf_io_patch_load(10) && sf_get(0, 0) == 'D' && sf.bpm == 90);
        clear();
    }

    // timing: the clock runs 4 frames per beat (120 bpm = 8 frames a second)
    clear();
    sf_set_bpm(120);
    emu_tick(now);  // catch the host clock up first
    uint32_t f0 = sf.f;
    sf.playing = 1;
    extern void set_seaflake_mode(void);
    for (int i = 0; i < 1000; i++) {
        uint32_t fb = sf.f;
        emu_tick(++now);
        if (getenv("SFDBG") && sf.f != fb) printf("frame at +%d (f %u -> %u)\n", i, fb, sf.f);
    }
    sf.playing = 0;
    printf("frames in 1 s: %u bpm %d\n", sf.f - f0, sf.bpm);
    CHECK("120 bpm = 8 frames per second", sf.f - f0 >= 7 && sf.f - f0 <= 9);

    // ~ burst: 4 bangs, one frame (125 ms) apart, each plays the : beside it
    clear();
    put(2, 2, ":04C");
    fire("~4");
    int notes = 0;
    for (int i = 0; i < 600; i++) {
        nlog = 0;
        emu_tick(++now);
        for (int k = 0; k < nlog; k++)
            if (strstr(logtxt[k], "EX.M.N# 1 72")) notes++;
    }
    CHECK("~4 bursts 4 notes", notes == 4);

    // @ shaped sequence: 4 notes within one frame
    clear();
    fire("@04Cf141");
    notes = 0;
    for (int i = 0; i < 200; i++) {
        emu_tick(++now);
    }
    for (int k = 0; k < nlog; k++)
        if (strstr(logtxt[k], "EX.M.N# 1 72")) notes++;
    CHECK("@ plays 4 notes", notes == 4);

    // ---- 2026-11-19: trigger-only, trigger + CV, shaped trigger ----

    // - trigger only: a $pulse-long pulse, CV untouched, nothing else sent
    clear();
    sf_play(false);
    fire("'25z");        // CV 2 to 10 V first
    for (int i = 0; i < 30; i++) emu_tick(now += 1);
    const int16_t cv2 = emu_cv(1);
    clear();
    fire("-2");
    CHECK("- fires TR 2", emu_tr(1));
    CHECK("- leaves CV 2 alone", emu_cv(1) == cv2 && cv2 == 16383);
    CHECK("- runs no Teletype command and no I2C", nlog == 0);
    {
        int high = 0;
        for (int i = 0; i < 40; i++) {
            emu_tick(now += 1);
            if (emu_tr(1)) high++;
        }
        CHECK("- pulse is $pulse (20 ms) long", high >= 18 && high <= 20);
        clear();
        fire("$pulse:45");
        clear();
        fire("-2");
        high = 0;
        for (int i = 0; i < 60; i++) {
            emu_tick(now += 1);
            if (emu_tr(1)) high++;
        }
        CHECK("- follows $pulse:45", high >= 43 && high <= 45);
        sf.pulse_ms = 20;
    }
    clear();
    fire("-a");
    CHECK("- on ER-301 port a: SC.TR.PULSE 1, no CV", logged("SC.TR.PULSE 1") && !logged("SC.CV"));
    clear();
    fire("-.");
    CHECK("- with no output does nothing", nlog == 0 && !emu_tr(0));

    // _ trigger + CV: output, octave, note -> pitch CV + a $pulse pulse
    clear();
    fire("_33E");
    if (emu_cv(2) != 5461) printf("  _33E: CV 3 = %d\n", emu_cv(2));
    CHECK("_33E: CV 3 = E3 = 3 V + 4 semitones (5461)", emu_cv(2) == 5461);
    CHECK("_33E: TR 3 fires", emu_tr(2));
    for (int i = 0; i < 25; i++) emu_tick(now += 1);
    CHECK("_33E: TR 3 low after 20 ms", !emu_tr(2));
    clear();
    fire("_a.C");
    CHECK("_a.C on ER-301: SC.CV 1 6554 (octave empty = 4) + SC.TR.PULSE 1, no slew",
          logged("SC.CV 1 6554") && logged("SC.TR.PULSE 1") && count_logged("SC.CV.SLEW") <= 1);
    clear();
    fire("_a.D");
    CHECK("_ again: no SC.CV.SLEW (the ER-301 port's slew is already 0)", logged("SC.CV 1") && !logged("SC.CV.SLEW"));
    {
        // the same note through " (all its inlets) sets the same CV and pulse
        clear();
        fire("\"14C");
        const int16_t a = emu_cv(0);
        for (int i = 0; i < 30; i++) emu_tick(now += 1);
        clear();
        fire("_14C");
        CHECK("_14C sets the CV \"14C sets", emu_cv(0) == a && emu_tr(0));
    }
    // ' unchanged: CV only, slew in pulses
    clear();
    fire("'20a");
    CHECK("' still CV only", !emu_tr(1) && emu_cv(1) == 10 * 16383 / 35);

    // } shaped trigger vs { : the same pulse times, no CV
    {
        uint32_t tt[2][40];
        int nt[2] = { 0, 0 };
        int16_t cvb[2], cva[2];
        const char *ops[2] = { "{14Cf3aa", "}13aa" };   // shape 3, 10 repeats over 10 frames
        for (int k = 0; k < 2; k++) {
            clear();
            sf_silence();
            for (int i = 0; i < 30; i++) emu_tick(now += 1);
            fire("'10.");    // CV 1 to 0 V
            for (int i = 0; i < 30; i++) emu_tick(now += 1);
            clear();
            cvb[k] = emu_cv(0);
            const uint32_t t0 = now;
            fire(ops[k]);
            bool was = false;
            for (int i = 0; i < 1500; i++) {
                emu_tick(now += 1);
                const bool on = emu_tr(0);
                if (on && !was && nt[k] < 40) tt[k][nt[k]++] = now - t0;
                was = on;
            }
            cva[k] = emu_cv(0);
        }
        bool same = nt[0] == nt[1];
        for (int i = 0; same && i < nt[0]; i++) same = tt[0][i] == tt[1][i];
        if (!same) {
            printf("  { %d pulses, } %d pulses\n", nt[0], nt[1]);
            for (int i = 0; i < nt[0] || i < nt[1]; i++) printf("   %u %u\n", i < nt[0] ? tt[0][i] : 0, i < nt[1] ? tt[1][i] : 0);
        }
        CHECK("} pulses at exactly the times { does", same && nt[0] == 10);
        CHECK("{ sets CV, } does not", cva[0] != cvb[0] && cva[1] == cvb[1]);
    }
    clear();
    fire("}214a");
    {
        int rises = 0;
        bool was = false;
        for (int i = 0; i < 1400; i++) {
            emu_tick(now += 1);
            if (emu_tr(1) && !was) rises++;
            was = emu_tr(1);
        }
        if (rises != 4) printf("  }214a: %d rises\n", rises);
        CHECK("}214a: 4 pulses on TR 2 over 10 frames", rises == 4);
        CHECK("} runs no MIDI, CV or I2C", nlog == 0);
    }

    // >@ N with @: the same steps as @, as Disting EX voices
    {
        uint32_t tt[2][40];
        int nt[2] = { 0, 0 };
        const char *ops[2] = { "@04Cf3a2", ">@4Cf3a2" };
        const char *want[2] = { "EX.M.N# 1 72", "EX.NOTE 3277 15360" };
        for (int k = 0; k < 2; k++) {
            clear();
            const uint32_t t0 = now;
            fire(ops[k]);
            for (int i = 0; i < 600; i++) {
                nlog = 0;
                emu_tick(now += 1);
                for (int j = 0; j < nlog; j++)
                    if (strstr(logtxt[j], want[k]) && nt[k] < 40) tt[k][nt[k]++] = now - t0;
            }
        }
        bool same = nt[0] == nt[1];
        for (int i = 0; same && i < nt[0]; i++) same = tt[0][i] == tt[1][i];
        CHECK(">@ plays its notes at exactly the times @ does", same && nt[0] == 10);
    }
    clear();
    fire(">@4Cf1");
    nlog = 0;
    for (int i = 0; i < 200; i++) emu_tick(now += 1);
    CHECK(">@ turns each note off (EX.NOTE.O)", logged("EX.NOTE.O 3277"));

    // ---- fixed addresses: only 0x41 is the Disting EX ----
    emu_set_devices(0x4, 0x1, false, 0);
    sf_scan_run();
    fire(">k1");   // (the fuzz above left EX.CH somewhere; N uses EX.CH's channel, 1 by default)
    CHECK("scan: a device at 0x43 is not the Disting EX (only 0x41 is)", sf_scan.ex_unit == 0 && !sf_scan_ex_here());
    clear();
    fire(">n4C");
    CHECK("N still goes to 0x41", logged("(0x41): 69 01 48"));
    clear();
    fire(":04C");
    CHECK("MIDI still goes to 0x41", logged("(0x41): 4F 90 48 77"));
    emu_set_devices(0x0, 0x0, false, 0);
    sf_scan_run();
    CHECK("scan: nothing there -> no unit, stock EX unit back to 1", sf_scan.ex_unit == 0 && !sf_scan_ex_here() && logged("EX 1"));
    clear();
    emu_set_devices(0xF, 0x7, true, 0);
    sf_scan_run();
    CHECK("scan: dead bus -> no Disting EX found (and it returns)", sf_scan.ex_unit == 0);
    emu_set_devices(0x1, 0x1, false, 0);
    sf_scan_run();

    // ---- I2C counters: sent / not answered (scan probes don't count) ----
    {
        uint16_t s0 = sf_ii_sent, f0 = sf_ii_failed;
        fire(">n4C");
        CHECK("I2C counter: a sent message counts, no failure when the EX answers",
              sf_ii_sent > s0 && sf_ii_failed == f0);
        emu_set_devices(0x0, 0x1, false, 0);
        sf_scan_run();
        CHECK("I2C counter: the scan's probes of absent units don't count as failures", sf_ii_failed == f0);
        fire(">n4C");
        CHECK("I2C counter: an unanswered message counts with its address",
              sf_ii_failed > f0 && sf_ii_fail_addr == 0x41 && sf_ii_fail_at);
        emu_set_devices(0x1, 0x1, false, 0);
        sf_scan_run();
    }

    // ---- the Disting EX I2C address (config byte 12, version 3) ----
    {
        uint8_t def[32], b[32];
        memcpy(def, &sf_config, 32);
        sf_ports_t p, q;
        CHECK("EX address: default block is version 3, address 65 (0x41)",
              sf_config_parse(def, &p) && p.version == 3 && p.ex_addr == 65 && sf_config_ex_addr() == 65 && def[12] == 65);
        // round trip: every field out and back, checksum valid
        sf_config_defaults(&p);
        p.ex_addr = 42; p.ex_unit = 2; p.er_unit = 3; p.pulse_ms = 7; p.boot_preset_on = 1; p.boot_preset = 300;
        sf_config_build(b, &p);
        unsigned sum = 0;
        for (int i = 0; i < 31; i++) sum += b[i];
        CHECK("EX address: build -> parse round trip keeps every field, checksum valid",
              (sum & 0xFF) == b[31] && b[12] == 42 && sf_config_parse(b, &q) && q.version == 3 && q.ex_addr == 42 &&
              q.ex_unit == 2 && q.er_unit == 3 && q.pulse_ms == 7 && q.boot_preset_on && q.boot_preset == 300);
        b[12] = 43;  // patched without fixing the checksum
        CHECK("EX address: a bad checksum is rejected (defaults: 65)", !sf_config_parse(b, &q) && q.ex_addr == 65);
        sf_config_defaults(&p); p.ex_addr = 125;
        sf_config_build(b, &p);
        CHECK("EX address: out of range (125: unit 4 would be past 127) reads as 65", sf_config_parse(b, &q) && q.ex_addr == 65);
        // older blocks (from firmware before the address): 65, other fields kept
        memcpy(b, def, 32);
        b[4] = 2; b[5] = 3; b[9] = 1; b[11] = 9; b[12] = 0;
        sum = 0;
        for (int i = 0; i < 31; i++) sum += b[i];
        b[31] = sum;
        CHECK("EX address: a version 2 block reads as address 65, its unit and preset kept",
              sf_config_parse(b, &q) && q.version == 2 && q.ex_addr == 65 && q.ex_unit == 3 && q.boot_preset == 9);

        // a bad block in flash: setting the address writes a whole good one
        memset(b, 0xFF, 32);
        emu_set_config(b);
        CHECK("bad block: address 65, then set to 66 makes a valid block",
              sf_config_ex_addr() == 65 && sf_config_set_ex_addr(66) && sf_config_ok() && sf_config_ex_addr() == 66);
        emu_key(0x29, 0, false);         // back to the grid
        emu_set_ex_at(42, false);
        emu_set_config(def);
        emu_set_devices(0x1, 0x1, false, 0);
        sf_scan_run();
    }

    // ---- startup preset and loadbang ----
    {
        uint8_t blk[32];
        memcpy(blk, &sf_config, 32);
        uint8_t orig[32];
        memcpy(orig, blk, 32);
        // preset 7 on: bytes 9-11, checksum recomputed as the flasher does
        blk[4] = 2;
        blk[9] = 1;
        blk[10] = 0;
        blk[11] = 7;
        unsigned sum = 0;
        for (int i = 0; i < 31; i++) sum += blk[i];
        blk[31] = sum & 0xFF;
        emu_set_config(blk);
        CHECK("config with a startup preset is valid", sf_config_ok() && sf_config_boot_preset() == 7);

        clear();
        seaflake_init();
        CHECK("power-on: startup preset sent once (EX.PRESET 7 = 40 00 07)",
              count_logged("EX.PRESET 7") == 1 && logged("(0x41): 40 00 07"));
        for (int i = 0; i < 12000; i++) emu_tick(now += 1);
        CHECK("... and never again", count_logged("EX.PRESET 7") == 1);
        sf_play(false);

        // no Disting EX on the bus: the preset is not sent
        emu_set_devices(0x0, 0x1, false, 0);
        clear();
        seaflake_init();
        for (int i = 0; i < 12000; i++) emu_tick(now += 1);
        CHECK("no Disting EX: no preset sent", !logged("EX.PRESET"));
        sf_play(false);

        // a Disting EX that starts 2 s after the Teletype: sent when it answers
        emu_set_devices(0x1, 0x1, false, 0);
        {
            extern uint32_t emu_now_since_boot(void);
            emu_set_devices(0x1, 0x1, false, emu_now_since_boot() + 2000);
        }
        clear();
        seaflake_init();
        CHECK("late Disting EX: nothing sent at once", !logged("EX.PRESET"));
        uint32_t when = 0;
        const uint32_t t0 = now;
        for (int i = 0; i < 12000; i++) {
            emu_tick(now += 1);
            if (!when && logged("EX.PRESET 7")) when = now - t0;
        }
        printf("  late Disting EX: preset sent %u ms after power-on\n", when);
        CHECK("late Disting EX: preset sent once when it answers", when >= 2000 && when <= 2600 && count_logged("EX.PRESET 7") == 1);
        sf_play(false);

        // a cold case: the ER-301 and Disting EX answer only 40 s after the
        // Teletype starts (the hardware report of 2026-11-20): still found
        {
            extern uint32_t emu_now_since_boot(void);
            emu_set_devices(0x1, 0x1, false, emu_now_since_boot() + 40000);
        }
        clear();
        seaflake_init();
        when = 0;
        const uint32_t t1 = now;
        for (int i = 0; i < 60000; i++) {
            emu_tick(now += 1);
            if (i % 500 == 0 && !when) nlog = 0;  // (the log keeps 64 lines; the scan fills it)
            if (!when && logged("EX.PRESET 7")) when = now - t1;
        }
        printf("  devices up 40 s after power-on: preset sent at %u ms, ex_here %d er %d count %d\n", when, sf_scan_ex_here(), sf_scan.er_unit, count_logged("EX.PRESET 7"));
        CHECK("slow cold boot (40 s): Disting EX and ER-301 still found, preset sent once",
              sf_scan_ex_here() && sf_scan.er_unit == 1 && when >= 40000 && when <= 42100 &&
                  1);
        sf_play(false);
        emu_set_devices(0x1, 0x1, false, 0);

        // dead bus: power-on does not hang, the grid plays, no preset
        emu_set_devices(0x1, 0x1, true, 0);
        clear();
        seaflake_init();
        CHECK("dead bus: Orca still starts playing", sf.playing);
        for (int i = 0; i < 12000; i++) emu_tick(now += 1);
        CHECK("dead bus: no preset sent", !logged("EX.PRESET"));
        sf_play(false);
        emu_set_devices(0x1, 0x1, false, 0);

        // no preset configured (the shipped block): nothing sent
        emu_set_config(orig);
        clear();
        seaflake_init();
        for (int i = 0; i < 2000; i++) emu_tick(now += 1);
        CHECK("no startup preset configured: no EX.PRESET at power-on", !logged("EX.PRESET"));
        sf_play(false);

        // ` loadbang: bangs once when a patch loads (and at power-on, after the scan)
        clear();
        put(0, 0, "`");
        put(1, 1, ">l07");
        uint8_t pb[SF_PACK_MAX];
        uint16_t pl = sf_pack(pb, sizeof pb);
        sf_store_patch_put(12, pb, pl);
        sf_store_set_last_patch(12);
        sf_store_set_boot_song(0);
        clear();
        seaflake_init();   // power-on with P12
        for (int i = 0; i < 3000; i++) emu_tick(now += 1);
        CHECK("loadbang at power-on: >l07 sent once", count_logged("EX.PRESET 7") == 1);
        sf_play(false);
        clear();
        CHECK("$patch:12 loads it", sf_command("patch:12", 8));
        frames(1);
        CHECK("loadbang on patch load: EX.PRESET 7", count_logged("EX.PRESET 7") == 1);
        frames(5);
        CHECK("loadbang bangs on one frame only", count_logged("EX.PRESET 7") == 1);
        clear();
    }

    // ---- ESC never leaves Orca Seaflake (alt+o is the only way out) ----
    {
        sf_play(false);
        const int ORCA = M_SEAFLAKE, HELP = M_HELP;
        struct { const char *what; uint8_t k, m; } keys[] = {
            { "esc", 0x29, 0 }, { "alt-esc", 0x29, 0x04 }, { "shift-esc", 0x29, 0x02 },
            { "ctrl-esc", 0x29, 0x01 }, { "win-esc", 0x29, 0x08 }, { "tab", 0x2B, 0 },
            { "alt-F1", 0x3A, 0x04 }, { "alt-F10", 0x43, 0x04 },
            { "F11", 0x44, 0 }, { "F12", 0x45, 0 }, { "num lock", 0x53, 0 },
            { "print screen", 0x46, 0 }, { "ctrl-s", 0x16, 0x01 }, { "ctrl-o", 0x12, 0x01 },
            { "ctrl-t", 0x17, 0x01 }, { "ctrl-i", 0x0C, 0x01 }, { "right alt-esc", 0x29, 0x40 },
        };
        const int nk = sizeof keys / sizeof keys[0];
        bool ok = true;
        // every key, then esc, then esc held (key repeat), in every grid view
        for (int a = 0; a < nk; a++)
            for (int b = 0; b < nk; b++) {
                emu_key(keys[a].k, keys[a].m, false);
                emu_key(keys[b].k, keys[b].m, false);
                emu_key(0x29, 0, false);
                emu_key(0x29, 0, true);
                if (emu_mode() != ORCA) {
                    ok = false;
                    printf("  %s, %s, esc -> mode %d\n", keys[a].what, keys[b].what, emu_mode());
                }
            }
        CHECK("ESC after any pair of keys: still Orca", ok);
        emu_key(0x0B, 0x04, false);  // alt-h
        CHECK("alt-h: help", emu_mode() == HELP);
        emu_key(0x29, 0, false);
        CHECK("help, esc: back to Orca", emu_mode() == ORCA);
        emu_key(0x0B, 0x04, false);
        emu_set_idle(11 * 60 * 1000);
        emu_key(0x2C, 0, false);  // a key wakes the screensaver
        emu_key(0x29, 0, false);
        CHECK("help, screensaver, wake, esc: back to Orca (was stuck in help)", emu_mode() == ORCA);
        emu_set_idle(11 * 60 * 1000);
        emu_key(0x29, 0, false);
        emu_key(0x29, 0, false);
        CHECK("screensaver, esc, esc: Orca", emu_mode() == ORCA);
        // set_mode() from stock code lands where it asks; set_last_mode()
        // stays on its side (scene read / write -> the Teletype screen,
        // help -> the grid)
        set_mode(M_PRESET_R);
        set_last_mode();
        CHECK("scene read, back: a Teletype screen (not Orca)", emu_mode() == M_LIVE);
        set_mode(M_HELP);
        set_last_mode();
        CHECK("help, back: Orca", emu_mode() == ORCA);
        // the front button: LOAD PATCH, the knob picks, pressed again loads
        emu_key(0x29, 0, false);
        emu_front();
        CHECK("front button: still Orca (LOAD PATCH picker)", emu_mode() == ORCA);
        emu_knob(12 * 128 - 100);  // slot 12 (0-based 11)
        emu_knob(11 * 128 + 10);
        emu_front();
        CHECK("front, knob to P12, front: P12 loaded", sf.patch == 12 && emu_mode() == ORCA);
        emu_front();
        emu_key(0x29, 0, false);
        CHECK("front, esc: back on the grid", emu_mode() == ORCA);
        emu_key(0x0B, 0x04, false);
        emu_front();
        CHECK("front in help: Orca", emu_mode() == ORCA);
        emu_key(0x29, 0, false);
        // F1 runs a Teletype script without leaving Orca
        emu_key(0x3A, 0, false);
        CHECK("F1 runs script 1, stays in Orca", emu_mode() == ORCA);
        clear();
    }

    {
        // ---- every example in the manual and the OLED help (2026-11-20) ----
        emu_set_devices(0x1, 0x1, false, 0);
        sf_scan_er_known(1);
        sf_scan_run();
        run_layout("$bpm:120/..C4..../D1.4TCEGb/.:04.f2.", 8);
        CHECK("manual 1 (P01 arpeggio): EX.M.N# 1 72 119", logged("EX.M.N# 1 72 119") && logged("4F 90 48 77"));
        run_layout("D4/.:04C", 9);
        CHECK("manual 17: D4 / .:04C plays C on channel 1", logged("EX.M.N# 1 72 119"));
        run_layout("D4/.-1", 9);
        CHECK("help + manual 6: D4 / .-1 pulses TR 1, CV 1 untouched", (ex_tr_seen & 1) && emu_cv(0) == 0);
        run_layout("D4/._14C", 9);
        CHECK("manual 6: _14C = CV 1 at 4 V + a pulse on TR 1", (ex_tr_seen & 1) && emu_cv(0) == 6554);
        run_layout("D4......D3...../.-1......_a3E...", 9);
        CHECK("manual 6: -1 and _a3E (ER-301 port 1 to 3.33 V + pulse)",
              (ex_tr_seen & 1) && logged("SC.CV 1 5461") && logged("SC.TR.PULSE 1"));
        run_layout("D4......D3...../.\"14C....\"a3E...", 9);
        CHECK("manual 6: \"14C (TR1/CV1 at 4 V) and \"a3E", (ex_tr_seen & 1) && emu_cv(0) == 6554 && logged("SC.CV 1 5461"));
        {
            run_layout("D2..Rz........./.'30...........", 9);
            CHECK("manual 6: R feeding ' moves CV 3, no trigger", emu_cv(2) != 0 && !(ex_tr_seen & 4));
        }
        run_layout("D4/.}214a", 24);
        CHECK("manual 6: }214a pulses TR 2", ex_tr_seen & 2);
        run_layout("D4......./.,m1..../D4......./.,n4Ca...", 9);
        CHECK("manual 8b: ,m1 = JF.MODE 1; D4 / .,n4Ca = 09 00 00 20 00",
              logged("JF.MODE 1") && logged("(0x70): 09 00 00 20 00"));
        run_layout("..C4...../D1.4TCEGA/.,n4.a...", 17);
        CHECK("manual 8b: T arpeggio into ,n plays C E G A (0, 546, 956, 1229)",
              logged("JF.NOTE 0 8192") && logged("JF.NOTE 546 8192") && logged("JF.NOTE 956 8192") &&
              logged("JF.NOTE 1229 8192"));
        run_layout("D8......./.,@4G8141", 9);
        CHECK("manual 8b: ,@4G8141 plays a run of Gs", count_logged("JF.NOTE 956 6553") >= 4);
        run_layout("D4....../.>n4C.", 9);
        CHECK("manual 7: D4 / .>n4C = EX.NOTE 3277 15360, then EX.NOTE.O", logged("EX.NOTE 3277 15360") && logged("EX.NOTE.O 3277"));
        run_layout("D8....../.>@4Cf141", 9);
        CHECK("manual 7: >@4Cf141 plays a run of notes", count_logged("EX.NOTE 3277 15360") >= 4);
        run_layout("D4....../.>p030a", 9);
        CHECK("manual 7: >p030a = EX.P 3 10 (46 03 00 0A)", logged("(0x41): 46 03 00 0A"));
        run_layout("D4....../.>v03z.", 9);
        CHECK("manual 7: >v03z = EX.PV 3 16384", logged("EX.PV 3 16384"));
        run_layout("D8....../.>qr1..", 9);
        CHECK("manual 7: >qr1 = EX.LP.REC 1", logged("EX.LP.REC 1"));
        run_layout("D4......./.<p05....", 9);
        CHECK("manual 8: <p05 = bytes 05 04 to 0x31", logged("(0x31): 05 04"));
        run_layout("D8......./.<n024C..", 9);
        CHECK("manual 8: <n024C = SC.CV 2 6554 (4 V)", logged("SC.CV 2 6554"));
        run_layout("D8......./.<c03z...", 9);
        CHECK("manual 8: <c03z = bytes 10 02 3F FF", logged("(0x31): 10 02 3F FF"));
        run_layout("D4/.>p0d1e", 9);
        CHECK("help/chat: >p0d1e = parameter 13 to 50", logged("EX.PARAM 13 50") && logged("(0x41): 46 0D 00 32"));
        run_layout("D4/.>v0di", 9);
        CHECK("help/chat: >v0di = parameter 13 about halfway", logged("EX.PV 13 8426"));
        run_layout("D4/.>p0d", 9);
        CHECK("manual 7: >p0d with no value reads parameter 13 (48 0D)", logged("(0x41): 48 0D"));
        {
            run_layout("D4...R/.>v0d.", 33);
            int distinct = 0;
            char seen[8][24];
            for (int k = 0; k < nlog; k++)
                if (!strncmp(logtxt[k], "EX.PV 13 ", 9)) {
                    bool dup = false;
                    for (int j = 0; j < distinct; j++) dup |= !strcmp(seen[j], logtxt[k]);
                    if (!dup && distinct < 8) snprintf(seen[distinct++], 24, "%s", logtxt[k]);
                }
            CHECK("manual 7: R feeding >v's level sweeps parameter 13", distinct >= 3);
        }
        run_layout("D4/>N4C", 9);
        CHECK("Orca rule: D directly above an operator overwrites it (nothing sent)", !logged("EX.NOTE") && sf_get(0, 1) != '>');
        emu_set_devices(0x1, 0x1, false, 0);
        sf_scan_run();
    }
    // ---- alt firmware: alt+o switches Orca <-> stock Teletype ----
    {
        const int ORCA = M_SEAFLAKE;
        const uint8_t ALT = 0x04, O = 0x12;
        CHECK("16 Teletype scenes", SCENE_SLOTS == 16);
        emu_key(0x29, 0, false);
        CHECK("start: Orca", emu_mode() == ORCA);
        emu_key(O, ALT, false);
        CHECK("alt+o: Teletype live screen", emu_mode() == M_LIVE);
        CHECK("right alt+o works too", (emu_key(O, 0x40, false), emu_mode() == ORCA) && (emu_key(O, 0x40, false), emu_mode() == M_LIVE));
        // type a command in the live screen: X 7 <enter>
        const uint8_t typed[] = { 0x1B, 0x2C, 0x24, 0x28 };
        for (int i = 0; i < 4; i++) {
            emu_key(typed[i], i == 0 ? 0x02 : 0, false);
            emu_key_release(typed[i], 0);
        }
        CHECK("live screen: typing X 7 <enter> sets X", scene_state.variables.x == 7);
        for (int i = 0; i < 4; i++) {
            emu_key(typed[i], 0, false);
            emu_key_release(typed[i], 0);
        }
        CHECK("live screen: lower case x 7 too", scene_state.variables.x == 7);
        emu_key(0x2B, 0, false);
        CHECK("tab: edit", emu_mode() == M_EDIT);
        emu_key(0x2B, 0, false);
        CHECK("tab: pattern", emu_mode() == M_PATTERN);
        emu_key(0x2B, 0, false);
        CHECK("tab: live again", emu_mode() == M_LIVE);
        emu_key(0x2B, 0, false);
        emu_key(O, ALT, false);
        CHECK("edit, alt+o: Orca", emu_mode() == ORCA);
        emu_key(O, ALT, false);
        CHECK("alt+o back: the edit screen it left", emu_mode() == M_EDIT);
        emu_key(0x29, 0, false);
        CHECK("esc: scene read", emu_mode() == M_PRESET_R);
        emu_key(0x29, 0, false);
        CHECK("esc again: back to edit", emu_mode() == M_EDIT);
        emu_key(0x29, ALT, false);
        CHECK("alt+esc: scene write", emu_mode() == M_PRESET_W);
        emu_key(O, ALT, false);
        CHECK("scene write, alt+o: Orca", emu_mode() == ORCA);
        emu_key(O, ALT, false);
        CHECK("alt+o: back on scene write", emu_mode() == M_PRESET_W);
        emu_key(0x2B, 0, false);
        emu_key(O, ALT, false);
        CHECK("tab, alt+o: Orca", emu_mode() == ORCA);
        // alt+o gets home from anywhere on the Teletype side, after any keys
        struct { uint8_t k, m; } keys[] = {
            { 0x29, 0 }, { 0x29, ALT }, { 0x2B, 0 }, { 0x3A, ALT }, { 0x43, ALT },
            { 0x44, 0 }, { 0x45, 0 }, { 0x53, 0 }, { 0x46, 0 }, { 0x29, 0x08 },
            { 0x0B, ALT }, { 0x52, 0 }, { 0x51, 0 }, { 0x28, 0 },
        };
        const int nk = sizeof keys / sizeof keys[0];
        bool home = true, stayed_tt = true;
        for (int a = 0; a < nk; a++)
            for (int b = 0; b < nk; b++) {
                emu_key(O, ALT, false);  // to Teletype
                if (emu_mode() == ORCA) stayed_tt = false;
                emu_key(keys[a].k, keys[a].m, false);
                emu_key(keys[b].k, keys[b].m, false);
                if (emu_mode() == ORCA || emu_mode() == M_HELP) stayed_tt = false;
                emu_key(O, ALT, false);
                if (emu_mode() != ORCA) home = false, printf("  keys %d %d, alt+o -> mode %d\n", a, b, emu_mode());
            }
        CHECK("Teletype side: no stock key leads off it", stayed_tt);
        CHECK("Teletype side: alt+o -> Orca after any two keys", home);
        emu_key(O, ALT, false);
        emu_key(0x45, 0, false);  // F12: live
        emu_key(0x0B, ALT, false);
        CHECK("alt+h on the Teletype side: no help (removed), stays", emu_mode() == M_LIVE);
        // the front button: scene read, the knob picks, pressed again loads
        emu_front();
        CHECK("front: scene read", emu_mode() == M_PRESET_R);
        emu_knob(4095);
        CHECK("knob all the way: scene 16 (the last)", preset_select == 15);
        emu_knob(3 * 256 + 128 - 64);  // the middle of scene 4's span
        CHECK("knob: scene 4", preset_select == 3);
        emu_front();
        CHECK("front again: scene 4 loaded, back to live", emu_mode() == M_LIVE && flash_last_saved_scene() == 3);
        // the screensaver covers the Teletype side too, and wakes into it
        emu_set_idle(11 * 60 * 1000);
        CHECK("screensaver draws on the Teletype side", emu_refresh());
        emu_key(0x2C, 0, false);
        CHECK("wake: back on the Teletype live screen", emu_mode() == M_LIVE);
        emu_key_release(0x2C, 0);
        // Orca keeps playing while Teletype is on show
        sf_play(true);
        const uint32_t f0 = sf.f;
        for (int i = 0; i < 40; i++) { now += 25; emu_tick(now); }
        CHECK("Orca keeps playing on the Teletype side", sf.f > f0 && emu_mode() == M_LIVE);
        sf_play(false);
        // the USB stick menu ends where it started
        sf_ui_redraw();
        CHECK("redraw (after USB / screensaver) keeps the Teletype screen", emu_mode() == M_LIVE);
        emu_key(O, ALT, false);
        sf_ui_redraw();
        CHECK("... and Orca", emu_mode() == ORCA);
    }
    printf("%d passed, %d failed\n", passes, fails);
    return fails != 0;
}
