// The screensaver: each time it starts, one of four dreams from
// femishonuga.com, picked at random (never the same one twice in a row).
// Each dream is its own file in module/dreams/ on dream_kit.h; they share
// the kit's one primitive list (3 KB), since only one draws at a time.
// (The older train dream, module/dreams/train.c, is not in the set.)
//
//   golden_geometries  dd.html 2021-06-21   sunlight through shattered windows
//   tunnel_bell        lbse.html 2026-08-04 the maintenance train and its bell
//   rain_lake          dd.html 2014-09-20   the street becomes a lake
//   night_elevator     dd.html 2026-05-18   the elevator into the night

// DREAM_ONLY 0-3 builds in one dream only (the published firmware uses 3,
// the elevator); left undefined, each start picks one of the four
// #define DREAM_ONLY 3

#include <stdbool.h>
#define DK_MULTI
#include "dreams/dream_kit.h"

#define scene scene_golden
#include "dreams/golden_geometries.c"
#undef scene
#define scene scene_tunnel
#include "dreams/tunnel_bell.c"
#undef scene
#define scene scene_lake
#include "dreams/rain_lake.c"
#undef scene
#define scene scene_elevator
#include "dreams/night_elevator.c"
#undef scene

static void (*const dreams[4])(uint32_t) = { scene_golden, scene_tunnel, scene_lake, scene_elevator };

// some changing number at the moment a dream starts (the host's ms clock:
// when you stopped typing varies), so the pick is different each time
__attribute__((weak)) uint32_t dream_entropy(void) { return 0; }

static uint8_t pick = 0xFF;
static uint32_t last_ms = 0xFFFFFFFF, sessions;

uint8_t dream_current(void) { return pick; }
bool dream_random(void) {
#ifdef DREAM_ONLY
    return false;
#else
    return true;
#endif
}

void dream_band(uint32_t ms, uint8_t line, uint8_t *buf) {
    // a new screensaver: its clock starts again from 0
#ifdef DREAM_ONLY
    pick = DREAM_ONLY;
    if (ms < last_ms) dk_frame = 0xFFFFFFFF;
#else
    if (pick == 0xFF || ms < last_ms) {
        const uint32_t h = dk_hash(dream_entropy(), ++sessions, 0xD2EA);
        if (pick == 0xFF)
            pick = (uint8_t)(h % 4);
        else {
            const uint8_t p = (uint8_t)(h % 3);   // one of the other three
            pick = p >= pick ? p + 1 : p;
        }
        dk_frame = 0xFFFFFFFF;
    }
#endif
    last_ms = ms;
    if (ms != dk_frame) {
        dk_frame = ms;
        dk_n = 0;
        dk_fade = 16;
        dreams[pick](ms);
    }
    memset(buf, 0, DK_W * 8);
    const int16_t y0 = (int16_t)line * 8;
    for (uint16_t i = 0; i < dk_n; i++) dk_draw(&dk_p[i], buf, y0);
    for (uint8_t j = 0; j < 8; j++) dk_cap(&buf[j * DK_W]);
}
