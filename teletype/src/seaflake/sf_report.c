// Orca Seaflake: the text files written to a USB stick (with the Teletype's
// WRITE TO USB) so the Mac emulator can match the module:
//
//   SFSCAN.TXT   which Disting EX units and ER-301s answered on the bus
//   SFPATCH.TXT  every patch (as grid text) and every song
//
// Shared by the firmware (module/usb_disk_mode.c) and the emulator, which
// reads the same format back (emu/tt_emu.c).

#include "sf_report.h"

#include <string.h>

#include "sf.h"
#include "sf_ii.h"
#include "sf_store.h"
#include "sf_str.h"

void sf_report_scan(sf_put_fn put) {
    char s[48], *p;
    put("ORCA SEAFLAKE I2C SCAN\r\n");
    for (uint8_t u = 0; u < 4; u++) {
        p = sf_cat(s, "EX");
        p = sf_num(p, u + 1, 1, '0');
        p = sf_cat(p, sf_scan.ex[u] ? " FOUND ALG " : " NONE");
        if (sf_scan.ex[u]) {
            p = sf_num(p, sf_scan.ex_alg[u], 1, '0');
            p = sf_cat(p, " PRESET ");
            p = sf_num(p, sf_scan.ex_preset[u], 1, '0');
        }
        sf_cat(p, "\r\n");
        put(s);
    }
    for (uint8_t n = 0; n < 3; n++) {
        p = sf_cat(s, "ER301 ");
        p = sf_num(p, n + 1, 1, '0');
        sf_cat(p, sf_scan.er[n] ? " FOUND\r\n" : " NONE\r\n");
        put(s);
    }
    sf_fmt(s, sizeof(s), "EX ADDRESS %d (0x%02X)\r\n", sf_scan.ex_addr, sf_scan.ex_addr);
    put(s);
    p = sf_cat(s, "USING EX ");
    p = sf_num(p, sf_scan.ex_unit, 1, '0');
    p = sf_cat(p, " ER301 ");
    p = sf_num(p, sf_scan.er_unit, 1, '0');
    sf_cat(p, "\r\n");
    put(s);
}

void sf_report_patches(sf_put_fn put, char *grid) {
    char s[SF_W + 4], *p;
    put("ORCA SEAFLAKE PATCHES\r\n");
    for (uint8_t n = 1; n <= SF_PATCH_SLOTS; n++) {
        const uint8_t *d;
        uint16_t len = sf_store_patch_get(n, &d);
        if (!len) continue;
        sf_unpack_to(grid, d, len);
        int w = 0, h = 0;
        for (int i = 0; i < SF_CELLS; i++)
            if (grid[i] != '.') {
                if (i % SF_W + 1 > w) w = i % SF_W + 1;
                h = i / SF_W + 1;
            }
        uint16_t start;
        const int16_t bpm = sf_patch_bpm(d, len, &start);
        p = sf_cat(s, "PATCH ");
        p = sf_num(p, n, 1, '0');
        if (bpm) {
            p = sf_cat(p, " BPM ");
            p = sf_num(p, bpm, 1, '0');
        }
        sf_cat(p, "\r\n");
        put(s);
        for (int y = 0; y < h; y++) {
            memcpy(s, grid + y * SF_W, w);
            s[w] = '\r';
            s[w + 1] = '\n';
            s[w + 2] = 0;
            put(s);
        }
        put("END\r\n");
    }
    for (uint8_t n = 1; n <= SF_SONG_SLOTS; n++) {
        sf_song_t song;
        if (!sf_store_song_get(n, &song)) continue;
        p = sf_cat(s, "SONG ");
        p = sf_num(p, n, 1, '0');
        p = sf_cat(p, " MODE ");
        p = sf_num(p, song.mode, 1, '0');
        p = sf_cat(p, " LOOP ");
        p = sf_num(p, song.loop, 1, '0');
        sf_cat(p, "\r\n");
        put(s);
        for (uint8_t i = 0; i < SF_SONG_LEN; i++) {
            sf_song_slot_t *sl = &song.slots[i];
            if (!sl->patch) continue;
            p = sf_cat(s, "SLOT ");
            p = sf_num(p, i + 1, 1, '0');
            p = sf_cat(p, " PATCH ");
            p = sf_num(p, sl->patch, 1, '0');
            p = sf_cat(p, " LEN ");
            p = sf_num(p, sl->len, 1, '0');
            p = sf_cat(p, " MUL ");
            p = sf_num(p, sl->mul, 1, '0');
            p = sf_cat(p, " BPM ");
            p = sf_num(p, sl->bpm, 1, '0');
            sf_cat(p, "\r\n");
            put(s);
        }
        put("END\r\n");
    }
}

////////////////////////////////////////////////////////////////////////////
// reading SFPATCH.TXT back

// one line (without \r \n), cut to max-1 characters. false at the end
static bool read_line(sf_getc_fn getc, sf_eof_fn eof, void *d, char *buf, uint16_t max) {
    uint16_t n = 0;
    bool any = false;
    while (!eof(d)) {
        uint16_t c = getc(d);
        any = true;
        if (c == '\n') break;
        if (c == '\r' || c == 0) continue;
        if (n < max - 1) buf[n++] = (char)c;
    }
    buf[n] = 0;
    return any;
}

static bool starts(const char *l, const char *word) {
    while (*word)
        if (*l++ != *word++) return false;
    return true;
}

// the number after the word (" PATCH 3"), -1 if the word is not there
static int16_t field(const char *l, const char *word) {
    const char *p = l;
    for (; *p; p++)
        if (starts(p, word)) {
            p += strlen(word);
            if (*p < '0' || *p > '9') return -1;
            int16_t v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            return v;
        }
    return -1;
}

int sf_import_patches(sf_getc_fn getc, sf_eof_fn eof, void *d, uint8_t *np, uint8_t *ns) {
    static char l[SF_W + 8];
    static char saved[SF_CELLS];
    static uint8_t packed[SF_PACK_MAX];
    uint8_t patches = 0, songs = 0;
    bool have = read_line(getc, eof, d, l, sizeof l);
    while (have) {
        if (starts(l, "PATCH ")) {
            int16_t n = field(l, "PATCH "), bpm = field(l, " BPM ");
            // the grid is scratch space for packing; it is put back after
            memcpy(saved, sf.grid, SF_CELLS);
            const uint16_t keep_bpm = sf.bpm;
            if (bpm >= SF_BPM_MIN && bpm <= SF_BPM_MAX) sf.bpm = (uint16_t)bpm;
            memset(sf.grid, '.', SF_CELLS);
            int16_t y = 0;
            while ((have = read_line(getc, eof, d, l, sizeof l)) && strcmp(l, "END")) {
                for (int16_t x = 0; l[x] && x < SF_W; x++)
                    if (y < SF_H) sf.grid[y * SF_W + x] = l[x];
                y++;
            }
            uint16_t len = sf_pack(packed, sizeof packed);
            memcpy(sf.grid, saved, SF_CELLS);
            sf.bpm = keep_bpm;
            if (len && n >= 1 && n <= SF_PATCH_SLOTS) {
                sf_store_patch_put((uint8_t)n, packed, len);
                patches++;
            }
        }
        else if (starts(l, "SONG ")) {
            int16_t n = field(l, "SONG "), mode = field(l, " MODE "), loop = field(l, " LOOP ");
            sf_song_t song;
            memset(&song, 0, sizeof song);
            song.magic = SF_SONG_MAGIC;
            song.mode = mode < 0 ? 0 : mode % 3;
            song.loop = loop > 0;
            while ((have = read_line(getc, eof, d, l, sizeof l)) && strcmp(l, "END")) {
                int16_t i = field(l, "SLOT "), pt = field(l, " PATCH "), len = field(l, " LEN "),
                        mul = field(l, " MUL "), bpm = field(l, " BPM ");
                if (i >= 1 && i <= SF_SONG_LEN && pt >= 0)
                    song.slots[i - 1] = (sf_song_slot_t){ .patch = pt, .len = len < 0 ? 0 : len,
                                                          .mul = mul < 0 ? 0 : mul, .bpm = bpm < 0 ? 0 : bpm };
            }
            if (n >= 1 && n <= SF_SONG_SLOTS) {
                sf_store_song_put((uint8_t)n, &song);
                songs++;
            }
        }
        have = have && read_line(getc, eof, d, l, sizeof l);
    }
    if (np) *np = patches;
    if (ns) *ns = songs;
    return patches + songs;
}
