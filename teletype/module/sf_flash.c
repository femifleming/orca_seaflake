// Orca Seaflake patches and songs in the Teletype's flash.
//
// They live in their own section (.flash_sf, see seaflake.lds) placed just
// below the Teletype scene storage, so installing this firmware does not
// move or overwrite the 32 Teletype scenes.

#include <string.h>

#include "flashc.h"
#include "seaflake/sf_store.h"
#include "seaflake/sf_ii.h"

#define SF_FLASH_MAGIC 0x5EAF1A01UL
#define PAGE 512

typedef struct {
    uint32_t magic;
    uint8_t last_patch;
    uint8_t boot_song;   // (in what was padding: 0 on modules formatted before it existed)
    // the last I2C scan, for the flasher (offset 6; 0xA5 = valid)
    uint8_t scan_ok;
    uint8_t scan_ex;        // bit u-1: Disting EX unit u answered
    uint8_t scan_er;        // bit n-1: ER-301 n answered
    uint8_t scan_ex_unit;   // the unit Orca sends to
    uint8_t scan_er_unit;
    uint8_t scan_alg[4];    // each EX unit's algorithm
    uint8_t pad[PAGE - 15];
} sf_head_t;

typedef struct {
    uint16_t len;  // 0 = empty
    uint16_t sum;
    uint8_t data[2 * PAGE - 4];
} sf_patch_flash_t;

typedef struct {
    sf_song_t song;
    uint8_t pad[PAGE - sizeof(sf_song_t)];
} sf_song_flash_t;

typedef struct {
    sf_head_t head;
    sf_patch_flash_t patch[SF_PATCH_SLOTS];
    sf_song_flash_t song[SF_SONG_SLOTS];
} sf_nvram_t;

static __attribute__((__section__(".flash_sf"))) sf_nvram_t n;

static uint16_t sum(const uint8_t *d, uint16_t len) {
    uint16_t s = 0x5EAF;
    for (uint16_t i = 0; i < len; i++) s = (s << 1 | s >> 15) ^ d[i];
    return s;
}

void sf_store_init(void) {
    if (n.head.magic == SF_FLASH_MAGIC) return;
    // first start with this firmware: the area holds whatever was there
    // before (old program code), so empty every slot
    for (int i = 0; i < SF_PATCH_SLOTS; i++)
        flashc_memset32((void *)&n.patch[i], 0, 4, true);
    for (int i = 0; i < SF_SONG_SLOTS; i++)
        flashc_memset32((void *)&n.song[i], 0, 4, true);
    sf_head_t h;
    memset(&h, 0, sizeof(h));
    h.magic = SF_FLASH_MAGIC;
    flashc_memcpy((void *)&n.head, &h, sizeof(h), true);
}

uint16_t sf_store_patch_get(uint8_t i, const uint8_t **data) {
    if (i < 1 || i > SF_PATCH_SLOTS) return 0;
    const sf_patch_flash_t *p = &n.patch[i - 1];
    if (!p->len || p->len > sizeof(p->data) || sum(p->data, p->len) != p->sum)
        return 0;
    *data = p->data;
    return p->len;
}

void sf_store_patch_put(uint8_t i, const uint8_t *data, uint16_t len) {
    if (i < 1 || i > SF_PATCH_SLOTS || len > sizeof(n.patch[0].data)) return;
    static sf_patch_flash_t p;
    memset(&p, 0, sizeof(p));
    p.len = len;
    memcpy(p.data, data, len);
    p.sum = sum(p.data, len);
    // only the pages the patch uses need writing
    flashc_memcpy((void *)&n.patch[i - 1], &p, len + 4, true);
}

bool sf_store_song_get(uint8_t i, sf_song_t *out) {
    if (i < 1 || i > SF_SONG_SLOTS) return false;
    const sf_song_t *s = &n.song[i - 1].song;
    if (s->magic != SF_SONG_MAGIC) return false;
    *out = *s;
    if (out->mode > 2) out->mode = 0;
    return true;
}

void sf_store_song_put(uint8_t i, const sf_song_t *song) {
    if (i < 1 || i > SF_SONG_SLOTS) return;
    flashc_memcpy((void *)&n.song[i - 1].song, song, sizeof(*song), true);
}

uint8_t sf_store_last_patch(void) {
    return n.head.last_patch;
}

uint8_t sf_store_boot_song(void) {
    return n.head.boot_song <= SF_SONG_SLOTS ? n.head.boot_song : 0;
}

void sf_store_set_boot_song(uint8_t i) {
    if (n.head.boot_song == i) return;
    flashc_memset8((void *)&n.head.boot_song, i, 1, true);
}

void sf_store_set_last_patch(uint8_t i) {
    if (n.head.last_patch == i) return;
    flashc_memset8((void *)&n.head.last_patch, i, 1, true);
}

void sf_store_set_scan(const struct sf_scan *sc) {
    uint8_t v[9];
    v[0] = 0xA5;
    v[1] = v[2] = 0;
    for (uint8_t u = 0; u < 4; u++)
        if (sc->ex[u]) v[1] |= 1 << u;
    for (uint8_t i = 0; i < 3; i++)
        if (sc->er[i]) v[2] |= 1 << i;
    v[3] = sc->ex_unit;
    v[4] = sc->er_unit;
    for (uint8_t u = 0; u < 4; u++) v[5 + u] = sc->ex_alg[u] < 0 ? 0 : sc->ex_alg[u] > 255 ? 255 : sc->ex_alg[u];
    if (memcmp((const void *)&n.head.scan_ok, v, sizeof(v)) == 0) return;
    flashc_memcpy((void *)&n.head.scan_ok, v, sizeof(v), true);
}

// the ports configuration block (src/seaflake/sf_config.h), rewritten when
// the Disting EX address is changed on the scan screen. It sits alone in its
// 512-byte flash page (0x80044C00; the patch area's head starts at
// 0x80044E00), so erasing that page touches nothing else.
#include "seaflake/sf_config.h"
void sf_io_config_write(const uint8_t block[32]) {
    flashc_memcpy((void *)&sf_config, block, sizeof(sf_config_t), true);
}
