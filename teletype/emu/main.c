// Orca Seaflake emulator: window, keyboard and virtual MIDI port.
//
// Left: the Teletype OLED (6x). Below it: TR 1-4, CV 1-4 and ER-301 ports
// 1-26. Right: everything sent on the I2C bus, the Teletype commands that
// sent it, and the MIDI the Disting EX would play. That MIDI also goes out
// of a virtual CoreMIDI source called "Orca Seaflake TT", so any synth or
// DAW on the Mac can play it.

#include <CoreMIDI/CoreMIDI.h>
#include <SDL.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "font.h"
#include "tt_emu.h"
#include "seaflake/sf.h"

#define SCALE 6
#define PAD 24
#define LOG_W 540
#define WIN_W (PAD + EMU_W * SCALE + PAD + LOG_W + PAD)
#define WIN_H 760
#define LOG_LINES 400

static SDL_Renderer *ren;

// colours: Teletype OLED amber-white on black, Orca-ish accents
static const SDL_Color bg = { 12, 12, 14, 255 };
static const SDL_Color dim = { 90, 90, 96, 255 };
static const SDL_Color txt = { 220, 220, 220, 255 };
static const SDL_Color cyan = { 114, 222, 194, 255 };
static const SDL_Color orange = { 255, 184, 0, 255 };
static const SDL_Color pink = { 255, 110, 160, 255 };

static struct {
    int kind;
    char text[100];
} logbuf[LOG_LINES];
static int log_head, log_count;
static bool log_paused;

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static void add_log(int kind, const char *text);

////////////////////////////////////////////////////////////////////////////
// firmware flasher: finds a Teletype in its bootloader with dfu-programmer
// (the same commands as monome's update_firmware.command)

enum { FL_NO_TOOL, FL_WAITING, FL_FOUND, FL_CONFIRM, FL_FLASHING, FL_DONE, FL_FAILED };
static volatile int fl_state = FL_WAITING;
static char fl_tool[256], fl_hex[1200];

static void find_flasher(void) {
    const char *tools[] = { "/opt/homebrew/bin/dfu-programmer", "/usr/local/bin/dfu-programmer",
                            "/opt/local/bin/dfu-programmer" };
    fl_tool[0] = 0;
    for (int i = 0; i < 3; i++)
        if (access(tools[i], X_OK) == 0) {
            snprintf(fl_tool, sizeof fl_tool, "%s", tools[i]);
            break;
        }
    // the hex inside the app (Contents/Resources), or dist/ next to emu/
    char *base = SDL_GetBasePath();
    fl_hex[0] = 0;
    if (base) {
        char p[1200];
        snprintf(p, sizeof p, "%sorca-seaflake-teletype.hex", base);
        if (access(p, R_OK) == 0) snprintf(fl_hex, sizeof fl_hex, "%s", p);
        snprintf(p, sizeof p, "%s../dist/orca-seaflake-teletype.hex", base);
        if (!fl_hex[0] && access(p, R_OK) == 0) snprintf(fl_hex, sizeof fl_hex, "%s", p);
        SDL_free(base);
    }
    if (!fl_tool[0]) fl_state = FL_NO_TOOL;
}

// run a command, log its output; true if it exited 0
static bool run_logged(const char *cmd, bool quiet) {
    FILE *f = popen(cmd, "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!quiet && line[0]) add_log(3, line);
    }
    return pclose(f) == 0;
}

static void *flash_thread(void *arg) {
    (void)arg;
    char cmd[1600];
    add_log(3, "FLASHING ORCA SEAFLAKE ONTO THE TELETYPE...");
    snprintf(cmd, sizeof cmd, "'%s' at32uc3b0512 erase 2>&1", fl_tool);
    add_log(3, "1/3 ERASE");
    bool ok = run_logged(cmd, false);
    if (ok) {
        add_log(3, "2/3 FLASH");
        snprintf(cmd, sizeof cmd, "'%s' at32uc3b0512 flash '%s' --suppress-bootloader-mem 2>&1", fl_tool, fl_hex);
        ok = run_logged(cmd, false);
    }
    if (ok) {
        add_log(3, "3/3 START");
        snprintf(cmd, sizeof cmd, "'%s' at32uc3b0512 start 2>&1", fl_tool);
        run_logged(cmd, false);
        add_log(3, "DONE. THE TELETYPE RESTARTS INTO ORCA SEAFLAKE.");
        add_log(3, "FIRST START: PRESS THE FRONT BUTTON WHEN IT ASKS (CLEARS SCENE MEMORY).");
    }
    else
        add_log(3, "FLASHING FAILED - SEE THE LINES ABOVE. THE TELETYPE STAYS IN ITS BOOTLOADER; TRY AGAIN.");
    fl_state = ok ? FL_DONE : FL_FAILED;
    return NULL;
}

// every 2 s: is a Teletype bootloader on USB?
static void *watch_thread(void *arg) {
    (void)arg;
    char cmd[400];
    snprintf(cmd, sizeof cmd, "'%s' at32uc3b0512 get bootloader-version 2>&1", fl_tool);
    for (;;) {
        int st = fl_state;
        if (st == FL_WAITING || st == FL_FOUND || st == FL_DONE || st == FL_FAILED) {
            bool found = run_logged(cmd, true);
            st = fl_state;
            if (found && st == FL_WAITING) {
                fl_state = FL_FOUND;
                add_log(3, "TELETYPE BOOTLOADER FOUND ON USB. CMD-U TO FLASH ORCA SEAFLAKE.");
            }
            else if (!found && (st == FL_FOUND || st == FL_DONE || st == FL_FAILED))
                fl_state = FL_WAITING;
        }
        sleep(2);
    }
    return NULL;
}

static void add_log(int kind, const char *text) {
    pthread_mutex_lock(&log_lock);
    if (log_paused && kind != 3) {
        pthread_mutex_unlock(&log_lock);
        return;
    }
    logbuf[log_head].kind = kind;
    snprintf(logbuf[log_head].text, sizeof(logbuf[0].text), "%s", text);
    log_head = (log_head + 1) % LOG_LINES;
    if (log_count < LOG_LINES) log_count++;
    pthread_mutex_unlock(&log_lock);
}

////////////////////////////////////////////////////////////////////////////
// CoreMIDI virtual source

static MIDIClientRef midi_client;
static MIDIEndpointRef midi_src;

static void midi_init(void) {
    if (MIDIClientCreate(CFSTR("Orca Seaflake TT"), NULL, NULL, &midi_client) != noErr) return;
    MIDISourceCreate(midi_client, CFSTR("Orca Seaflake TT"), &midi_src);
}

static void midi_out(const uint8_t *b, int n) {
    if (!midi_src || n < 1 || n > 3) return;
    Byte buf[64];
    MIDIPacketList *pl = (MIDIPacketList *)buf;
    MIDIPacket *p = MIDIPacketListInit(pl);
    MIDIPacketListAdd(pl, sizeof(buf), p, 0, n, b);
    MIDIReceived(midi_src, pl);
}

////////////////////////////////////////////////////////////////////////////
// drawing with the Teletype's own font

static void color(SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
}

static void rect(int x, int y, int w, int h) {
    SDL_Rect r = { x, y, w, h };
    SDL_RenderFillRect(ren, &r);
}

static void frame(int x, int y, int w, int h) {
    SDL_Rect r = { x, y, w, h };
    SDL_RenderDrawRect(ren, &r);
}

// fixed 6 px cells, s = pixel size
static void text(int x, int y, const char *s, int px, SDL_Color c) {
    color(c);
    for (; *s; s++, x += 6 * px) {
        unsigned char ch = *s;
        if (ch < 0x21 || ch > 0x7e) continue;
        const glyph_t *g = &font_data[ch - FONT_ASCII_OFFSET];
        int w = FONT_CHARW - g->first - g->last, off = (6 - w) / 2;
        for (int i = 0; i < w; i++) {
            uint8_t bits = g->data[g->first + i];
            for (int j = 0; j < 8; j++)
                if (bits & (1 << j)) rect(x + (off + i) * px, y + j * px, px, px);
        }
    }
}

static void draw(void) {
    color(bg);
    SDL_RenderClear(ren);

    // OLED
    const uint8_t *fb = emu_framebuffer();
    int ox = PAD, oy = PAD + 28;
    text(ox, PAD, "ORCA SEAFLAKE", 2, cyan);
    static const char *const modes[] = { "TELETYPE LIVE", "TELETYPE EDIT", "TELETYPE PATTERN",
                                         "TELETYPE SCENE WRITE", "TELETYPE SCENE READ",
                                         "HELP", "GRID" };
    int m = emu_mode();
    text(ox + 186, PAD, m >= 0 && m <= 6 ? modes[m] : "", 2, m == 6 ? dim : orange);
    color((SDL_Color){ 40, 40, 44, 255 });
    frame(ox - 4, oy - 4, EMU_W * SCALE + 8, EMU_H * SCALE + 8);
    for (int y = 0; y < EMU_H; y++)
        for (int x = 0; x < EMU_W; x++) {
            uint8_t v = fb[y * EMU_W + x];
            if (!v) continue;
            // the OLED's 16 levels, warm white
            int l = v * 17;
            SDL_SetRenderDrawColor(ren, l, l * 245 / 255, l * 225 / 255, 255);
            rect(ox + x * SCALE, oy + y * SCALE, SCALE - 1, SCALE - 1);
        }

    // Teletype outputs
    int py = oy + EMU_H * SCALE + 30;
    char s[64];
    for (int i = 0; i < 4; i++) {
        int x = ox + i * 190;
        snprintf(s, sizeof(s), "TR%d", i + 1);
        text(x, py, s, 2, dim);
        color(emu_tr(i) ? orange : (SDL_Color){ 50, 50, 54, 255 });
        rect(x + 50, py, 16, 16);
        int16_t cv = emu_cv(i);
        snprintf(s, sizeof(s), "CV%d %5.2fV", i + 1, cv * 10.0 / 16384.0);
        text(x, py + 28, s, 2, dim);
        color((SDL_Color){ 40, 40, 44, 255 });
        rect(x, py + 50, 160, 8);
        color(cyan);
        if (cv > 0) rect(x, py + 50, cv * 160 / 16384, 8);
    }

    // ER-301 ports
    int ey = py + 80;
    text(ox, ey, "ER-301 PORTS (SC.TR / SC.CV)", 2, dim);
    uint32_t now = SDL_GetTicks();
    for (int p = 1; p <= 26; p++) {
        int x = ox + ((p - 1) % 13) * 59, y = ey + 26 + ((p - 1) / 13) * 46;
        bool seen = emu_er_seen(p) != 0;
        snprintf(s, sizeof(s), "%c%d", 'a' + p - 1, p);
        text(x, y, s, 1, seen ? txt : dim);
        color(emu_er_tr(p) ? orange : (SDL_Color){ 50, 50, 54, 255 });
        rect(x, y + 12, 12, 12);
        int16_t cv = emu_er_cv(p);
        color((SDL_Color){ 40, 40, 44, 255 });
        rect(x + 16, y + 12, 36, 12);
        color(pink);
        int w = cv * 36 / 16384;
        if (w > 0) rect(x + 16, y + 12, w > 36 ? 36 : w, 12);
    }
    (void)now;

    // flasher
    int fy = ey + 26 + 2 * 46 + 8;
    const char *ft = "";
    SDL_Color fc = dim;
    switch (fl_state) {
        case FL_NO_TOOL: ft = "FLASHER: INSTALL DFU-PROGRAMMER (BREW INSTALL DFU-PROGRAMMER)"; fc = orange; break;
        case FL_WAITING: ft = "FLASHER: NO TELETYPE BOOTLOADER ON USB (SEE MANUAL)"; break;
        case FL_FOUND: ft = "FLASHER: TELETYPE FOUND - CMD-U TO FLASH"; fc = cyan; break;
        case FL_CONFIRM: ft = "ERASES THE TELETYPE (SCENES + PATCHES). Y = FLASH, OTHER KEY = CANCEL"; fc = orange; break;
        case FL_FLASHING: ft = "FLASHING... DO NOT UNPLUG"; fc = orange; break;
        case FL_DONE: ft = "FLASHER: DONE - TELETYPE RESTARTED"; fc = cyan; break;
        case FL_FAILED: ft = "FLASHER: FAILED - SEE LOG, CMD-U TO RETRY"; fc = pink; break;
    }
    text(ox, fy, ft, 1, fc);
    if (fl_state == FL_CONFIRM) {
        color(orange);
        frame(ox - 6, fy - 6, EMU_W * SCALE + 12, 20);
    }

    // log
    int lx = PAD + EMU_W * SCALE + PAD;
    text(lx, PAD, log_paused ? "I2C LOG (PAUSED)" : "I2C LOG", 2, log_paused ? orange : dim);
    snprintf(s, sizeof(s), "%s %d BPM  F%u", emu_playing() ? "PLAY" : "STOP", emu_bpm(),
             emu_frame());
    text(lx + 230, PAD, s, 2, emu_playing() ? cyan : dim);
    int rows = (WIN_H - PAD * 3 - 28 - 40) / 12;
    pthread_mutex_lock(&log_lock);
    for (int r = 0; r < rows && r < log_count; r++) {
        int i = (log_head - 1 - r + LOG_LINES) % LOG_LINES;
        SDL_Color c = logbuf[i].kind == 0 ? cyan : logbuf[i].kind == 2 ? orange : dim;
        char t[91];
        snprintf(t, sizeof(t), "%s", logbuf[i].text);
        text(lx, PAD + 28 + r * 12, t, 1, c);
    }
    pthread_mutex_unlock(&log_lock);
    text(lx, WIN_H - PAD - 28, "CMD-D DREAM  CMD-I IMPORT USB  CMD-U FLASH  CMD-L CLEAR", 1, dim);
    text(lx, WIN_H - PAD - 14, "ALT-H HELP   MIDI OUT: \"ORCA SEAFLAKE TT\"", 1, dim);

    SDL_RenderPresent(ren);
}

////////////////////////////////////////////////////////////////////////////

static uint8_t hid_mod(SDL_Keymod k) {
    uint8_t m = 0;
    if (k & KMOD_LCTRL) m |= 0x01;
    if (k & KMOD_LSHIFT) m |= 0x02;
    if (k & KMOD_LALT) m |= 0x04;
    if (k & KMOD_LGUI) m |= 0x08;
    if (k & KMOD_RCTRL) m |= 0x10;
    if (k & KMOD_RSHIFT) m |= 0x20;
    if (k & KMOD_RALT) m |= 0x40;
    if (k & KMOD_RGUI) m |= 0x80;
    return m;
}

static uint32_t now_ms(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (uint32_t)(mach_absolute_time() * tb.numer / tb.denom / 1000000ULL);
}

// --shot out.bmp patch.orca ms [song|load|help|live|live:x|live:edit|live:read|live:pat]: render one frame to a file
// (used to check the screens without a person at the keyboard)
static int shot(int argc, char **argv, const char *dir, SDL_Window *win) {
    uint32_t t = 1000;
    emu_init(dir, t);
    FILE *f = fopen(argv[3], "rb");
    if (f) {
        char buf[256];
        int y = 0;
        while (y < SF_H && fgets(buf, sizeof buf, f)) {
            for (int x = 0; x < SF_W && buf[x] && buf[x] != '\n'; x++) sf_set(x, y, buf[x]);
            y++;
        }
        fclose(f);
    }
    if (! emu_playing()) emu_key(0x2C, 0, false);  // space: play (it usually already is)
    int ms = atoi(argv[4]);
    for (int i = 0; i < ms; i++) emu_tick(++t);
    if (argc > 5 && !strcmp(argv[5], "song")) emu_key(0x17, 0x01, false);  // ctrl-t
    if (argc > 5 && !strcmp(argv[5], "load")) emu_key(0x12, 0x01, false);  // ctrl-o
    if (argc > 5 && !strncmp(argv[5], "live", 4)) {  // alt-o: the Teletype side
        emu_key(0x12, 0x04, false);
        if (!strcmp(argv[5], "live:x")) {  // type X 7 in the live screen
            const uint8_t k[] = { 0x1B, 0x2C, 0x24, 0x28, 0x1B };
            for (int i = 0; i < 5; i++) { emu_key(k[i], i == 0 || i == 4 ? 0x02 : 0, false); emu_key_release(k[i], 0); }
        }
        if (!strcmp(argv[5], "live:edit")) emu_key(0x2B, 0, false);   // tab
        if (!strcmp(argv[5], "live:read")) emu_key(0x29, 0, false);   // esc
        if (!strcmp(argv[5], "live:pat")) { emu_key(0x2B, 0, false); emu_key(0x2B, 0, false); }
    }
    if (argc > 5 && !strcmp(argv[5], "help")) emu_key(0x0B, 0x04, false);  // alt-h
    if (argc > 5 && !strncmp(argv[5], "dream:", 6)) emu_set_idle (5 * 60 * 1000 + (uint32_t) atoi (argv[5] + 6));
    if (argc > 5 && !strcmp(argv[5], "scan")) emu_key(0x0C, 0x01, false);  // ctrl-i
    // cell: load P03, put the cursor on cells given as "cell:DOWN:RIGHT"
    if (argc > 5 && !strncmp(argv[5], "cell:", 5)) {
        int dn = atoi(argv[5] + 5), rt = atoi(strchr(argv[5] + 5, ':') + 1);
        emu_key(0x12, 0x01, false);
        emu_key(0x51, 0, false);
        emu_key(0x51, 0, false);
        emu_key(0x28, 0, false);
        for (int i = 0; i < 300; i++) emu_tick(++t);
        for (int i = 0; i < dn; i++) emu_key(0x51, 0, false);
        for (int i = 0; i < rt; i++) emu_key(0x4F, 0, false);
        for (int i = 0; i < 1600; i++) emu_tick(++t);  // past the LOADED message
    }
    if (argc > 5 && argv[5][0] == 'p' && argv[5][1]) {  // p2: load patch 2 and play
        emu_key(0x12, 0x01, false);
        for (int i = 1; i < atoi(argv[5] + 1); i++) emu_key(0x51, 0, false);
        emu_key(0x28, 0, false);
        for (int i = 0; i < ms; i++) emu_tick(++t);
    }
    emu_tick(++t);
    emu_refresh();
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    ren = SDL_CreateSoftwareRenderer(s);
    draw();
    SDL_SaveBMP(s, argv[2]);
    (void)win;
    return 0;
}

int main(int argc, char **argv) {
    char dir[1024];
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/Library/Application Support/Orca Seaflake Teletype",
             home ? home : ".");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *win = SDL_CreateWindow("Orca Seaflake TT (emulator)",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       WIN_W, WIN_H, SDL_WINDOW_ALLOW_HIGHDPI);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, WIN_W, WIN_H);

    if (argc > 4 && !strcmp(argv[1], "--shot")) {
        emu_set_log(add_log);
        return shot(argc, argv, argc > 6 ? argv[6] : "/tmp/sf-shot", win);
    }
    midi_init();
    find_flasher();
    if (fl_tool[0]) {
        pthread_t w;
        pthread_create(&w, NULL, watch_thread, NULL);
        pthread_detach(w);
    }
    emu_set_log(add_log);
    emu_set_midi(midi_out);
    emu_init(dir, now_ms());
    add_log(3, "ORCA SEAFLAKE READY. SPACE PLAYS, ALT-H HELP, CTRL-O LOADS A PATCH.");
    add_log(3, dir);

    bool run = true;
    uint32_t last_draw = 0;
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keymod km = e.key.keysym.mod;
                SDL_Scancode sc = e.key.keysym.scancode;
                // the flasher's confirmation takes the next key
                if (fl_state == FL_CONFIRM && !e.key.repeat) {
                    if (sc == SDL_SCANCODE_Y && fl_hex[0]) {
                        fl_state = FL_FLASHING;
                        pthread_t t;
                        pthread_create(&t, NULL, flash_thread, NULL);
                        pthread_detach(t);
                    }
                    else {
                        fl_state = FL_FOUND;
                        add_log(3, "FLASH CANCELLED");
                    }
                    continue;
                }
                // Cmd shortcuts belong to the emulator
                if ((km & KMOD_GUI) && sc == SDL_SCANCODE_Q) run = false;
                else if ((km & KMOD_GUI) && sc == SDL_SCANCODE_U) {
                    if (fl_state == FL_FOUND || fl_state == FL_FAILED) {
                        if (!fl_hex[0])
                            add_log(3, "NO FIRMWARE FILE IN THE APP (ORCA-SEAFLAKE-TELETYPE.HEX)");
                        else {
                            fl_state = FL_CONFIRM;
                            add_log(3, "FLASH? THIS ERASES THE TELETYPE: BACK UP SCENES TO USB FIRST.");
                            add_log(3, "PRESS Y TO FLASH, ANY OTHER KEY TO CANCEL.");
                        }
                    }
                    else if (fl_state == FL_NO_TOOL)
                        add_log(3, "INSTALL THE FLASHER FIRST: BREW INSTALL DFU-PROGRAMMER");
                    else
                        add_log(3, "NO TELETYPE BOOTLOADER: HOLD THE FRONT BUTTON WHILE POWERING ON, USB A-A CABLE");
                }
                else if ((km & KMOD_GUI) && sc == SDL_SCANCODE_D) {
                    emu_set_idle (5 * 60 * 1000);   // the screensaver, now (any key wakes it)
                    add_log(3, "SCREENSAVER: THE TRAIN DREAM OF 2024-10-08 (ANY KEY WAKES IT)");
                }
                else if ((km & KMOD_GUI) && sc == SDL_SCANCODE_I) {
                    char sum[160];
                    emu_import_usb(sum, sizeof sum);
                    add_log(3, sum);
                }
                else if ((km & KMOD_GUI) && sc == SDL_SCANCODE_L) log_count = 0;
                else if ((km & KMOD_GUI) && sc == SDL_SCANCODE_P) log_paused = !log_paused;
                // SDL scancodes are USB HID usage codes, as the module receives
                else if (sc < 0xE0)
                    emu_key((uint8_t)sc, hid_mod(km), e.key.repeat != 0);
            }
            if (e.type == SDL_KEYUP && e.key.keysym.scancode < 0xE0)
                emu_key_release((uint8_t)e.key.keysym.scancode, hid_mod(e.key.keysym.mod));
        }
        emu_tick(now_ms());
        uint32_t t = now_ms();
        if (t - last_draw >= 16) {
            last_draw = t;
            emu_refresh();
            draw();
        }
        SDL_Delay(1);
    }
    SDL_Quit();
    return 0;
}
