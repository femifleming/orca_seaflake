// Orca Seaflake help (alt-h). Replaces Teletype's help_mode.c in this
// firmware: the module is an Orca, and the Teletype op reference took 37 KB
// of flash that Seaflake needs. Same interface (help_mode.h).

#include <string.h>

#include "font.h"
#include "globals.h"
#include "help_mode.h"
#include "keyboard_helper.h"
#include "region.h"

static const char *const help[] = {
    "ORCA SEAFLAKE HELP",
    "UP/DOWN PGUP/PGDN  ESC BACK",
    "",
    "-- KEYS",
    "SPACE     PLAY / STOP",
    "ARROWS    MOVE  ALT LEAP 8",
    "SHIFT+ARR SELECT",
    "ENTER     INSERT MODE",
    "BKSP/DEL  ERASE",
    "CTRL C X V Z  COPY CUT",
    "          PASTE UNDO",
    "CTRL A    SELECT ALL",
    "CTRL F    STEP 1 FRAME",
    "CTRL R    FRAME 0",
    "CTRL N    NEW GRID",
    "ALT [ ]   BPM -1 +1",
    "CTRL [ ]  BPM -10 +10",
    "CTRL S/O  SAVE/LOAD PATCH",
    "CTRL T    SONG MODE",
    "CTRL I    I2C: EX, ER-301, BUS",
    "BOTTOM LINE: WHAT THE",
    "  CELL UNDER THE CURSOR IS",
    "ALT H     THIS HELP",
    "FRONT BTN LOAD PATCH, KNOB",
    "  PICKS, BUTTON AGAIN LOADS",
    "F1-F10    RUN TT SCRIPT",
    "ESC NEVER LEAVES ORCA",
    "",
    "-- ORCA  (L=LEFT R=RIGHT)",
    "A  ADD L+R",
    "B  SUBTRACT R-L",
    "C  CLOCK  RATE C MOD(8)",
    "D  DELAY  RATE D MOD(8)",
    "E  EAST   N NORTH",
    "S  SOUTH  W WEST",
    "F  IF L=R BANG",
    "G  GEN X Y LEN G IN..",
    "H  HALT BELOW",
    "I  INC STEP I MOD",
    "J  JUMP ABOVE>BELOW",
    "K  KONKAT LEN K VARS",
    "L  LESSER OF L R",
    "M  MULTIPLY L*R",
    "O  READ X Y O",
    "P  PUSH KEY LEN P VAL",
    "Q  QUERY X Y LEN Q",
    "R  RANDOM MIN R MAX",
    "T  TRACK KEY LEN T ..",
    "U  EUCLID STEP U LEN",
    "V  VARIABLE W V R",
    "X  WRITE X Y X VAL",
    "Y  JYMP LEFT>RIGHT",
    "Z  LERP RATE Z TARGET",
    "*  BANG   # COMMENT",
    "LOWERCASE: ON BANG ONLY",
    "",
    "-- SEAFLAKE",
    "^  PROB ^CHANCE",
    "&  PHASE RATE&LEN",
    "~  BURST ~REP DIV MUL",
    "|  MUTE W H|",
    "+  SCALE KEY+101011010101",
    "$  CMD $BPM:120 $APM:30",
    "   $PULSE:20 $PATCH:1",
    "   $PLAY $STOP $FRAME:0",
    "",
    "-- MIDI (VIA DISTING EX)",
    ":  NOTE CH OCT NOTE VEL LEN",
    "%  MONO CH OCT NOTE VEL LEN",
    "!  CC CH NUM VAL",
    "?  BEND CH LSB MSB",
    "(  JI CH OCT NOTE VEL LEN",
    ")  SCALE CH OCT DEG KEY..",
    "@  SHAPE CH OCT NOTE VEL",
    "   SHAPE REP DUR PW",
    "",
    "-- TRIGGER + CV",
    "OUT CELL: 1-4 = TT TR 1-4",
    "  A-Z = ER-301 PORT 1-26",
    "THEY FIRE WHEN BANGED.",
    "D BANGS THE CELL BELOW IT,",
    "SO PUT THE OP ONE RIGHT:",
    "  D4",
    "  .-1  <- PULSES TR 1",
    "(D RIGHT ABOVE AN OP",
    " OVERWRITES IT)",
    "-  JUST A PULSE: -1 OR -A",
    "   ($PULSE MS, DEFAULT 20)",
    "_  PULSE + PITCH: _14C",
    "   OUT OCTAVE NOTE",
    "'  CV ONLY  'OUT SLEW LEVEL",
    "}  SHAPED PULSES (AS @)",
    "   OUT SHAPE REP DUR PW",
    "`  LOADBANG: BANG BELOW",
    "   WHEN THE PATCH LOADS",
    "\"  TRIG OUT OCT NOTE VEL LEN",
    "/  GATE OUT OCT NOTE VEL FR",
    "\\  CV BEND OUT LSB MSB",
    "[  TR JI   ]  TR SCALE",
    "{  TR SHAPE",
    "",
    "-- DISTING EX  >F ...",
    "AT 0X41 (ER-301 AT 0X31)",
    "SET THE EX: SETTINGS>I2C 65",
    "W=2 CELLS 00-ZZ  N=OCT NOTE",
    "SIDE (LAST): . SINGLE 1 L 2 R",
    ">N N VEL  NOTE (N)",
    ">@ N VEL SHAPE REP DUR PW",
    ">P PARAM W VALUE W SIDE",
    "   EXACT VALUE (EX.P)",
    "   NO VALUE = READ IT",
    "   >P0D1E: P13 = 50",
    ">V PARAM W LEVEL SIDE (PV)",
    "   LEVEL 0-Z = MIN..MAX",
    "   >V0DI: P13 HALFWAY",
    ">I >J  MIN MAX  W SIDE",
    ">L LOAD PRESET W SIDE",
    "   (NO PRESET = READ)",
    ">S SAVE PRESET W SIDE",
    ">R RESET PRESET",
    ">A ALG A SIDE A2",
    ">C CTRL N LEVEL",
    ">Z Z KNOB 1/2 LEVEL",
    ">K NOTE CHANNEL N",
    ">D NOTE ON CH N VEL",
    ">O NOTE OFF CH N",
    ">X ALL NOTES OFF",
    ">W VOICE V N VEL",
    ">F VOICE PITCH V N",
    ">T TRIGGER V VEL",
    ">E >Y  REC PLAY 1/0",
    ">G >H  AUGUSTUS PITCH CLK",
    ">Q LOOP ACT N",
    "  R P C V D  S I J",
    ">M MIDI ACT W W",
    "  P C S T O H N F E W",
    ">B SEL BUS ACT W W",
    "  N F E W P C S T O H",
    "",
    "-- < ER-301  <SEL PORT(W)",
    "T TR PORT 0/1",
    "P PULSE PORT",
    "G TOGGLE PORT",
    "L POLARITY PORT 0/1",
    "M PULSE MS PORT W",
    "C CV PORT LEVEL",
    "N CV NOTE PORT OCT NOTE",
    "S CV SET PORT LEVEL",
    "O CV OFFSET PORT LEVEL",
    "W SLEW MS PORT W",
    "",
    "-- , JUST FRIENDS ,SEL ...",
    "AT 0X70. 4C = 0V (JF PITCH)",
    "VEL 0-G = 0-8V, . = A (5V)",
    "CH 1-6, 0 ALL, 7-C 2ND JF",
    ",N N VEL  NOTE (,M1 FIRST)",
    ",@ N VEL SHAPE REP DUR PW",
    ",P N VEL  POLY (2 JF)",
    ",V CH N VEL  VOICE",
    ",F CH N  VOICE PITCH",
    ",R CH VEL  TRIGGER",
    ",T CH STATE (. = PULSE)",
    ",S N  SHIFT  ,U LVL  RUN",
    ",O RUN MODE  ,M SYNTH 1/0",
    ",D GOD 1/0  ,Q QUANT 0-W",
    ",J CH NUM DEN  TUNE",
    ",K W  TICK  ,X POLY RESET",
    ",A ADDR 1/2  ,E SELECT 1/2",
    ",G READ S T R C F I O",
};
#define HELP_LINES (sizeof(help) / sizeof(help[0]))

static uint8_t top;
static bool dirty;

void set_help_mode(void) {
    dirty = true;
}

void process_help_keys(uint8_t k, uint8_t m, bool is_held_key) {
    (void)is_held_key;
    if (k == HID_UP && top > 0) top--;
    else if (k == HID_DOWN && top + 8 < HELP_LINES) top++;
    else if (k == HID_PAGEUP) top = top > 8 ? top - 8 : 0;
    else if (k == HID_PAGEDOWN) top = top + 16 < HELP_LINES ? top + 8 : HELP_LINES - 8;
    else if (k == HID_HOME) top = 0;
    else if (match_no_mod(m, k, HID_ESCAPE)) set_last_mode();
    dirty = true;
}

uint8_t screen_refresh_help(void) {
    if (!dirty) return 0;
    dirty = false;
    for (uint8_t i = 0; i < 8; i++) {
        region_fill(&line[i], 0);
        if (top + i >= HELP_LINES) continue;
        const char *s = help[top + i];
        bool head = (s[0] == '-' && s[1] == '-') || (top + i) == 0;
        const uint8_t fg = head ? 0xF : 0xA;
        const uint8_t w = line[i].w;
        if (!head && (s[0] == '`' || s[0] == '-' || s[0] == '_') && s[1] == ' ') {
            // operator glyphs the Teletype font draws badly: ` is a pound sign,
            // - is 2 pixels, _ sits on the bottom edge. Draw them, then the rest
            if (s[0] == '`')
                for (uint8_t k = 0; k < 3; k++) line[i].data[(1 + k) * w + 2 + k] = fg;
            else
                for (uint8_t k = 0; k < 4; k++)
                    line[i].data[(s[0] == '-' ? 4 : 6) * w + 2 + k] = fg;
            font_string_region_clip(&line[i], s + 1, 2 + 6, 0, fg, 0);
        }
        else
            font_string_region_clip(&line[i], s, 2, 0, fg, 0);
    }
    return 0xFF;
}
