# Generates COMMANDS.md. The Teletype part is the real monome firmware (teletype/), the
# PI.* ops are teletype/src/ops/pilot.c, and Pilot's own extended commands come from the
# interpreter's op table:
#   R=./build/PilotRender_artefacts/Release/PilotRender; $R ref > ref.txt; $R piparams > pi.txt
#   python3 tools/gen_commands.py ref.txt pi.txt > COMMANDS.md
import re, sys

rows = []
for l in open(sys.argv[1]).read().strip().split('\n'):
    m = re.match(r'^(.*)\|(\d+)\|([01])\|(-?\d+)\|(.*)$', l)
    name, args, ret, setargs, doc = m.groups()
    rows.append((name, int(args), ret == '1', int(setargs), doc))

ARGN = {'NOTE': 'ch n', 'NOTE.V': 'ch n vel', 'NOTE.VL': 'ch n vel ms', 'ON': 'ch n vel', 'OFF': 'ch n',
        'MIDI.RR': 'lo hi', 'CH.INIT': 'ch'}

def sig(r):
    name, args, ret, setargs, doc = r
    a = ARGN.get(name, 'ch' if name.startswith('CH.') else ' '.join(chr(97 + i) for i in range(args)))
    g = f"`{name}{' ' + a if a else ''}`"
    if setargs >= 0:
        v = 'y' if a.split()[-1:] == ['x'] else 'x'
        return g + f" · `{name}{' ' + a if a else ''} {v}`"
    return g

out = ["""# Pilot Sealflake — commands

Pilot Sealflake understands three things, in this order, on its command line and on UDP port 49161 (ORCA's `;`):

1. **Classic Pilot commands**: `04C`, `0ENV056f`, `REV8c`, `bpm140`, `renv`, alone or joined with `;` (`04C;REV8c`). Unchanged from Pilot.
2. **Teletype**: any Teletype command, run exactly like Teletype's live mode: `PI.N 0 60 100`, `DEL 500: PI.T 12`, `M 250`, `X ADD X 1`.
3. **Pilot's extended commands** that have no Teletype equivalent: `CH.VOICES 0 4`, `COM.RATIO 1.4`, `MIDI.MODE 2` (tables below).

The result of a line (or an error) shows dimmed in the command line, like Teletype's live screen.

## Teletype

The Teletype screen (Cmd+T, `TELETYPE` / `TT` in the command line, or View > Teletype) is **monome Teletype's own
firmware** (v5.0.0, in `teletype/`), compiled into Pilot: the same parser, every op and PRE, scripts 1-8 / M / I,
`DEL` `DEL.X` `DEL.R` `EVERY` `SKIP` `PROB` `L` `W` `IF`, `$` / `SCRIPT` (so `DEL RRND 200 2000: $ $` loops a script), the stack,
4×64 patterns, `N` `N.S` `N.C` `VV` `BPM` …, `TIME` / `LAST`, J and K local to each script, turtle, the grid visualizer,
scene text, mutes, the 25 ms `M` minimum (`M!` 2 ms) … everything behaves as on the module. The
[Teletype manual](https://monome.org/docs/teletype/manual/) and the in-app help (Option-H; page 18 is Pilot) are the
reference. The keys are the module's, with a Mac keyboard: **Ctrl = ctrl, Option = alt, Shift = shift, Cmd = win.**
On **Windows and Linux**: Ctrl = ctrl, Alt = alt, and Pilot's own keys below are **Ctrl+Shift** (Ctrl+Shift+T, Ctrl+Shift+V …);
Teletype's win-Esc is Ctrl+Shift+Backspace.

| Key | |
| - | - |
| Tab | live → edit → pattern |
| Esc / Option-Esc | scene read / scene write |
| Cmd-Esc | clear delays, stack and slews |
| Option-H, Ctrl-H | help (`[` `]` page, Ctrl-F / Ctrl-R search). Ctrl-H also works from Pilot's window and opens Teletype on help; it replaces Teletype's Ctrl-H = backspace |
| F1-F8, F9, F10 | run script 1-8, M, I (Option: edit it, Ctrl: mute it, Ctrl-F9: metro on/off) |
| F11 / F12 | pattern / live |
| Cmd-V | types the clipboard into Teletype (each line ends with Enter, so a whole script pastes into edit mode) |
| Cmd-T, Cmd-= / Cmd-- / Cmd-0, Cmd-R | Pilot: close Teletype, zoom it, record |

Scenes: Teletype's 32 scene slots are text files in Teletype's USB-stick format,
`Teletype/tt00s.txt … tt31s.txt` in Pilot's settings folder (`~/Library/Application Support/Pilot Sealflake` on macOS,
`%APPDATA%\\Pilot Sealflake` on Windows, `~/.config/Pilot Sealflake` on Linux), so scenes copy to and from a real
Teletype's USB stick. The scene you are working on is also saved with the standalone session and the DAW project, and
its I script runs when it loads.

There is one Teletype per app, like one module: with several Pilot plugins in a DAW they share it, and `PI.*` plays the
first Pilot opened. Hardware with nothing to connect to keeps its state: `TR`, `CV`, `IN`, `PARAM` and grid ops work and
read back, but there are no jacks.

## PI — play and change Pilot from Teletype

New Teletype ops, written like Teletype's Disting EX ops (`EX.N` → `PI.N`, `EX.VOX` → `PI.VOX`, `EX.P` → `PI.P` …), with the
Pilot channel 0-15 as the "synth".

- **note**: a MIDI note 0-127 (60 = C4 = Pilot's `04C`), **or** a Teletype pitch from `N`, `N.S`, `N.C`, `V`, `VV` …: any value over 127 is
  read as 1V/oct, so `N 60` is note 60. `PI.N 0 60 100` and `PI.N 0 N 60 100` play the same note.
- **vel**: 0-127 (or 0-16383, the Disting EX range).
- `PI.ENV` takes 1-10 per stage. `PI.FIL`, `PI.MOD` and `PI.FX` mirror Pilot's own commands and take its hex digits as numbers 0-15: `PI.FX 10 8 12` = `REV8c`.

| Op | Meaning |
| - | - |
| `PI.N ch note vel` | play a note on channel ch, released after `PI.LEN` (a 1/16 note by default), so it can sit in a `DEL x: $ $` loop |
| `PI.NT ch note vel ms` | play a note for ms milliseconds |
| `PI.N# ch note vel` | note on (held until `PI.NO#`) |
| `PI.NO# ch note` | note off |
| `PI.CH` · `PI.CH x` | default channel for `PI.NOTE` `PI.NO` `PI.ALG` `PI.P` `PI.PV` (alias `PI.#`) |
| `PI.NOTE note vel` | note on (held) on `PI.CH`, like `EX.NOTE` |
| `PI.NOTE.O note` | note off on `PI.CH` (alias `PI.NO`) |
| `PI.ALLOFF` | all notes off (alias `PI.AO`) |
| `PI.VOX ch note vel` | voice: one held note per channel, a new one replaces it (alias `PI.V`) |
| `PI.VOX.P ch note` | change the voice's note; glides with `CH.GLIDE` / `CH.LEGATO` (alias `PI.VP`) |
| `PI.VOX.O ch` | release the voice (alias `PI.VO`) |
| `PI.T ch` · `PI.TV ch vel` | trigger a drum channel (the voice's last note, or C4) |
| `PI.LEN ch` · `PI.LEN ch ms` | note length used by `PI.N`, `PI.T`, `PI.TV`; 0 = a 1/16 note at the tempo |
| `PI.ENG ch` · `PI.ENG ch x` | engine: 0 analog, 1 FM, 2 AM, 3 pluck, 4 kick, 5 snare, 6 hat, 7 tom, 8 Just Friends (= `0ENGa` … `0ENGj`) |
| `PI.ALG` · `PI.ALG x` | engine of `PI.CH`, like `EX.ALG` (alias `PI.A`) |
| `PI.ENV ch a d s r` | envelope: attack, decay, sustain, release, 1-10 each (1 = shortest / no sustain, 10 = longest / full sustain) |
| `PI.OSC ch a b` | oscillator waves: -1 off, 0-3 sine tri square saw, +4 per unison step (4-7 2×, 8-11 4×, 12-15 8×) |
| `PI.FIL ch cut res env` | filter, 0-15 each (= `0FILcre`) |
| `PI.MOD ch ratio index` | modulation, 0-15 each (= `0MODri`) |
| `PI.FX fx wet value` | effect, 0-15 each (= `REVwv`); fx 0 BIT 1 DIS 2 CHE 3 WAH 4 PHA 5 CHO 6 VIB 7 TRE 8 FEE 9 DEL 10 REV 11 STE 12 EQU 13 COM 14 VOL 15 LIM |
| `PI.RENV` · `PI.ROSC` · `PI.REFX` (or bare `RENV` `ROSC` `REFX`) | randomize envelopes / oscillators / effects (= `renv` `rosc` `refx`) |
| `PI.BPM` · `PI.BPM x` | Pilot's tempo (= `bpm140`); setting it stops following the host. Teletype's own `BPM x` still converts BPM to ms |
| `PI.P x` · `PI.P x y` | parameter x of `PI.CH` (numbers below), like `EX.P` (alias of `PI.PARAM`) |
| `PI.P# ch x` · `PI.P# ch x y` | parameter x of channel ch |
| `PI.PV x y` | set parameter x of `PI.CH` from y over 0..16384 (`PI.PV 7 PRM`, `PI.PV 7 V 5`) |
| `PI.MIN x` · `PI.MAX x` | range of parameter x |
| `PI.PRESET` · `PI.PRESET x` | load sound preset 0-31: all channels + effects (alias `PI.PRE`) |
| `PI.SAVE x` | save the current sound as preset x |
| `PI.RESET` | Pilot's default sound |
| `PI.REC x` | 1 start / 0 stop recording a WAV to `~/Music/Pilot Sealflake` |

## Just Friends — engine `j` and Teletype's JF.* ops

Any channel can be a **Just Friends** (Mannequins): `0ENGj`, `PI.ENG 0 8` or `CH.ENG 0 8`. The channel becomes JF's six
slopes IDENTITY, 2N … 6N with the module's panel, and Teletype's own **JF.\\* ops (Just Type)** play it over (emulated) i2c:
the lowest JF channel answers at JF's address 0x70 (**JF1**), the next one at 0x75 (**JF2**). With no channel in JF mode the
ops go nowhere, like an empty i2c bus. Pilot notes (UDP `04C`, MIDI, `PI.N`) on a JF channel play like `JF.NOTE`.

The Pilot screen shows a JF channel as TIME INTONE RAMP CURVE (where the envelope is) and range + mode (where the
oscillators are): `0 8800 SNSU` = TIME and INTONE at noon, sound range, sustain mode.

| Panel | Classic command | Extended command | Meaning |
| - | - | - | - |
| TIME | `0ENV8...` (1st digit) | `CH.TIME ch %` | base rate: shape 4 s … 160 Hz, sound ~10 Hz … 6.8 kHz; noon (`8`, 50) = C4 in sound |
| INTONE | 2nd digit | `CH.INTONE ch %` | noon: all together; CW toward ×1 ×2 … ×6 (harmonic), CCW toward ÷1 … ÷6 (subharmonic) |
| RAMP | 3rd digit | `CH.RAMP ch %` | saw → triangle (noon) → ramp |
| CURVE | 4th digit | `CH.CURVE ch %` | square → trapezoid → expo → linear (noon) → log → cosine |
| FM | `0MODx` (1st digit) | `CH.FM ch %` | CCW of noon: INTONE-side detune (no FM cable); CW: FLOOM index |
| range / mode | `0OSCsnsu` | `CH.RANGE ch 0/1`, `CH.MODE ch 0-2` | `sh` shape / `sn` sound; `tr` transient, `su` sustain, `cy` cycle |

`renv` randomizes a JF channel's knobs and `rosc` its mode. RUN modes (with `JF.RMODE 1`, voltage from `JF.RUN`):

| | transient | sustain | cycle |
| - | - | - | - |
| shape | SHIFT: retrigger point (−5 V always, 0 V end of rise, +5 V end of cycle) | STRATA: ARSR plateau (0 V = half way) | VOLLEY: bursts, 6 cycles at 0 V, none below −4 V, 36 at +5 V |
| sound | SPILL: IDENTITY free-runs and retriggers the others | PLUME: vactrol low-pass gate (0 V vactrol, −5 V slow, +5 V blips) | FLOOM: each slope FMs itself; RUN sets the ratio ½ … 2 |

Teletype's JF ops, unchanged (pitches are 1 V/oct: `N 0` = TIME's pitch, C4 at noon; velocity `V 5` = a normal trigger):

| Op | Meaning |
| - | - |
| `JF.TR x y` | trigger input x (1-6, 0 = all) high / low; follows MODE (transient pulse, sustain gate, cycle restart) |
| `JF.VTR x y` | trigger x with velocity y (`V 5` normal) |
| `JF.RMODE x` · `JF.RUN y` | RUN on / off (as if a cable were patched), RUN voltage −5 … +5 V (`V -5` … `V 5`) |
| `JF.SHIFT x` | transpose TIME, 1 V/oct |
| `JF.MODE x` | 1 = Just Type: **synthesis** in sound range, **Geode** in shape range; 0 = normal |
| `JF.NOTE p v` · `JF.POLY p v` | synthesis: play pitch p on the next free voice (6 voices; v 0 = note off). `JF.POLY` spills notes 7-12 onto JF2 |
| `JF.VOX x p v` | synthesis: voice x plays pitch p |
| `JF.PITCH x p` | change x's pitch without a trigger |
| `JF.TUNE x n d` | x's INTONE ratio = n / d (0 0 = back to INTONE) |
| `JF.GOD x` | 1 = A 432 Hz |
| `JF.NOTE div rep` · `JF.VOX x div rep` | Geode: `div` envelopes per bar, `rep` repeats (−1 = forever) |
| `JF.TICK x` | Geode clock: 1-48 = clock pulse with x ticks per bar, over 48 = BPM (default: Pilot's tempo) |
| `JF.QT x` | Geode: start new rhythms on 1/x of the bar (0 = at once) |
| `JF.SPEED` `JF.TSC` `JF.TIME` `JF.INTONE` `JF.RAMP` `JF.CURVE` `JF.FM` | read range (0 shape, 1 sound), mode (0-2), knobs (0-16383) |
| `JF.SEL x` · `JF1:` `JF2:` `JF0:` | address JF1 / JF2 / both |
| `JF.ADDR x` | move this JF to address 1 (0x70) or 2 (0x75); the other one swaps |

## MIDI out — I2M (i2c2midi)

Teletype's i2c2midi ops send **real MIDI** from Pilot, separately from `PI.*` (Pilot answers as the i2c2midi module):
to the virtual MIDI port **Pilot Sealflake** (any app or DAW can record it), to the plugin's MIDI output, and in the
standalone to the MIDI output picked in Audio/MIDI settings (Cmd+,).

| Op | Meaning |
| - | - |
| `I2M.CH` · `I2M.CH x` | MIDI channel 1-16 (17-32 also go out on 1-16) (alias `I2M.#`) |
| `I2M.N note vel` | note, turned off after `I2M.TIME` (alias of `I2M.NOTE`); vel 0 = note off |
| `I2M.N# ch note vel` | note on channel ch |
| `I2M.NT note vel ms` · `I2M.NT# ch note vel ms` | note with its own length |
| `I2M.NO note` · `I2M.NO# ch note` | note off (alias of `I2M.NOTE.O`) |
| `I2M.TIME` · `I2M.TIME x` · `I2M.T# ch x` | note length in ms (100 to start, 0 = no automatic note off) (alias `I2M.T`) |
| `I2M.SHIFT` · `I2M.SHIFT x` · `I2M.S# ch x` | transpose in semitones (alias `I2M.S`) |
| `I2M.MIN x y` · `I2M.MAX x y` | lowest / highest note let through |
| `I2M.CC cc x` · `I2M.CC# ch cc x` | control change, x 0-127 |
| `I2M.CCV cc x` · `I2M.CCV# ch cc x` | control change, x 0-16383 (Teletype range) |
| `I2M.CC.SET cc x` | control change without slew |
| `I2M.CC.OFF cc x` · `I2M.CC.SLEW cc ms` | offset added to a CC / slew time for a CC |
| `I2M.NRPN msb lsb x` | NRPN (CC 99, 98, 6, 38) |
| `I2M.PB x` | pitch bend -8192..8191 |
| `I2M.PRG x` | program change |
| `I2M.AT x` | channel aftertouch |
| `I2M.CLK` · `I2M.START` · `I2M.STOP` · `I2M.CONT` | MIDI clock and transport |
| `I2M.PANIC` | all notes off on every channel, reset note settings |

Not emulated (they do nothing): the chord (`I2M.C.*`), buffer (`I2M.B.*`), mute/solo and `I2M.Q.*` (MIDI in) ops. For
MIDI **in**, Teletype's own `MI.*` ops and `MI.$` scripts get the plugin's / standalone's MIDI input.
"""]

pi = [l.split('|', 5) for l in open(sys.argv[2]).read().strip().split('\n')] if len(sys.argv) > 2 else []
if pi:
    out.append("""
## PI.P parameter numbers

`PI.P x` reads parameter x, `PI.P x y` sets it, `PI.PV x y` sets it from 0..16384, `PI.MIN x` / `PI.MAX x` give the range.
0-26 act on one channel (`PI.CH`, or `PI.P# ch …`); 100+ are the global effects (100 + 10 × effect + k: k 0 = wet digit,
1 = value digit, 2.. = named parameters). Teletype numbers are whole numbers, so parameters marked ×100 are in hundredths:
`PI.P 10 140` = ratio 1.4. Teletype numbers stop at 32767, so `BIT.RATE` can be set up to 32767 Hz from Teletype.

| # | Parameter | Min | Max | Teletype value | Meaning |
| - | - | - | - | - | - |""")
    for num, name, lo, hi, scale, doc in pi:
        out.append(f"| {num} | `{name}` | {lo} | {hi} | {'×100' if scale == '100' else ''} | {doc.replace('|', '/')} |")

GROUPS = [
    ("Channels (ch = 0-15)", lambda n: n.startswith("CH.") and n not in ("CH.ATKN", "CH.DECN", "CH.SUSN", "CH.RELN", "CH.CUTN", "CH.RESN", "CH.FENVN", "CH.RATION", "CH.INDEXN")),
    ("Playing", lambda n: n in "NOTE NOTE.V NOTE.VL ON OFF STOP RESET RENV ROSC REFX".split()),
    ("MIDI routing & I/O", lambda n: n.startswith("MIDI.") or n == "PORT"),
]
out.append("""
## Pilot's extended commands

These have no Teletype equivalent. They work on the command line and over UDP (they are not Teletype ops, so they don't go in
Teletype scripts; use `PI.*` there). Values can be decimals. An op with a setter form sets when it gets one more value:
`CH.VOICES 0 4` sets, `CH.VOICES 0` reads.""")
used = set()
for title, pred in GROUPS:
    out.append(f"\n### {title}\n\n| Op | Meaning |\n| - | - |")
    for r in rows:
        n = r[0]
        if n in used or not pred(n):
            continue
        used.add(n)
        out.append(f"| {sig(r)} | {r[4].replace('|', '/')} |")

out.append("""
### Effects

Each effect has named parameters in real units: `COM.RATIO` reads, `COM.RATIO 1.4` sets.
`XXX.W` / `XXX.V` read or set Pilot's two hex digits as 0-15, and `XXX.CLR` clears the named overrides so the effect follows its
classic W/V digits again. Until set, a named parameter follows those digits (e.g. `COM` amount drives threshold + ratio).
From Teletype, use `PI.P` with the numbers above.

| Op | Unit | Meaning | Range |
| - | - | - | - |""")
FX = "BIT DIS CHE WAH PHA CHO VIB TRE FEE DEL REV STE EQU COM VOL LIM".split()
for r in rows:
    n = r[0]
    code = n.split(".")[0]
    if "." not in n or code not in FX or n.endswith((".W", ".V", ".CLR")):
        continue
    doc = r[4]
    if doc.startswith("("):
        unit = doc[1:doc.index(")")]
        rest = doc[doc.index(")") + 2:]
        meaning, rng = (rest.split("  [", 1) + [""])[:2]
        rng = rng.rstrip("]")
    else:
        unit, meaning, rng = "", doc, ""
    out.append(f"| `{n}` | {unit} | {meaning} | {rng} |")

out.append("""
## Examples

Teletype scripts (type them in edit mode, or paste with Cmd-V):

```
#1                                       script 1 re-fires itself at random intervals
DEL RRND 200 2000: $ $
PI.N 0 ADD 48 RND 24 100                 ...and plays a note each time

#M                                       metro
PI.N 0 N.S 60 1 P.NEXT 100               walk pattern 0 through C minor on channel 0
EVERY 4: PI.T 12                         a kick on channel C every 4th tick
I2M.N ADD 36 P.HERE 90                   ...and the same line as MIDI out

#I                                       init
M 125; PI.LEN 0 80; PI.ENG 12 4; I2M.CH 2
```

Live:

```
PI.CH 2; PI.ALG 1; PI.P 10 350           channel 2 = FM, ratio 3.5
PI.PV 7 PRM                              PI.CH's cutoff from the PARAM value
PI.ENV 3 1 4 5 10; PI.FX 10 8 12         envelope 1-10 each, and REV8c
I2M.CC 74 RND 127                        a random CC 74 out
PI.SAVE 4                                store the whole sound (PI.PRESET 4 recalls it)
```

Pilot's command line / UDP only:

```
COM.RATIO 1.4; COM.THR -20               compressor 1.4:1 at -20 dB
LIM.CEIL -1                              limiter ceiling -1 dBTP
MIDI.MODE 2; MIDI.RR 0 7                 plugin: round robin incoming MIDI over channels 0-7
CH.VOICES 11 1; CH.GLIDE 11 80; CH.LEGATO 11 1   channel B: mono legato bass with glide
```
""")
print("\n".join(out))
