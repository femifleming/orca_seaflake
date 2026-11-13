# Pilot Sealflake — commands

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
`%APPDATA%\Pilot Sealflake` on Windows, `~/.config/Pilot Sealflake` on Linux), so scenes copy to and from a real
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
slopes IDENTITY, 2N … 6N with the module's panel, and Teletype's own **JF.\* ops (Just Type)** play it over (emulated) i2c:
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


## PI.P parameter numbers

`PI.P x` reads parameter x, `PI.P x y` sets it, `PI.PV x y` sets it from 0..16384, `PI.MIN x` / `PI.MAX x` give the range.
0-26 act on one channel (`PI.CH`, or `PI.P# ch …`); 100+ are the global effects (100 + 10 × effect + k: k 0 = wet digit,
1 = value digit, 2.. = named parameters). Teletype numbers are whole numbers, so parameters marked ×100 are in hundredths:
`PI.P 10 140` = ratio 1.4. Teletype numbers stop at 32767, so `BIT.RATE` can be set up to 32767 Hz from Teletype.

| # | Parameter | Min | Max | Teletype value | Meaning |
| - | - | - | - | - | - |
| 0 | `ENG` | 0 | 8 |  | ch (e): 0 analog 1 fm 2 am 3 pluck 4 kick 5 snare 6 hat 7 tom 8 just friends |
| 1 | `OSC1` | -1 | 15 |  | ch (w): -1 off, 0-15 = si tr sq sw, +4 per unison step (2/4/8) |
| 2 | `OSC2` | -1 | 15 |  | ch (w): second oscillator / modulator wave |
| 3 | `ATK` | 1.5 | 4002 |  | ch (ms): attack |
| 4 | `DEC` | 1.5 | 4002 |  | ch (ms): decay |
| 5 | `SUS` | 0 | 100 |  | ch (%): sustain |
| 6 | `REL` | 1.5 | 4002 |  | ch (ms): release |
| 7 | `CUT` | 20 | 20480 |  | ch (Hz): filter cutoff |
| 8 | `RES` | 0 | 100 |  | ch (%): resonance |
| 9 | `FENV` | 0 | 100 |  | ch (%): filter envelope amount |
| 10 | `RATIO` | 0.25 | 14 | ×100 | ch (r): FM/AM/osc2 frequency ratio 0.25-14 |
| 11 | `INDEX` | 0 | 100 |  | ch (%): FM index / AM depth / osc2 level / drum tone |
| 12 | `VOICES` | 1 | 16 |  | ch (n): polyphony 1-16 (1 = mono) |
| 13 | `GLIDE` | 0 | 10000 |  | ch (ms): portamento |
| 14 | `LEGATO` | 0 | 1 |  | ch (0/1): mono legato (no retrigger) |
| 15 | `PAN` | -100 | 100 |  | ch (-100..100): pan |
| 16 | `LEVEL` | -60 | 24 |  | ch (dB): level |
| 17 | `DETUNE` | -1 | 100 |  | ch (cents): unison spread, -1 = auto |
| 18 | `WIDTH` | 0 | 200 |  | ch (%): unison stereo width |
| 19 | `BEND` | 0 | 48 |  | ch (semis): pitch-bend range |
| 20 | `TRANS` | -48 | 48 |  | ch (semis): transpose |
| 21 | `FINE` | -100 | 100 |  | ch (cents): fine tune |
| 22 | `VSENS` | 0 | 100 |  | ch (%): velocity sensitivity |
| 23 | `KEYT` | 0 | 200 |  | ch (%): filter key tracking |
| 24 | `DRIFT` | 0 | 50 |  | ch (cents): analog pitch drift |
| 25 | `VEL` | 0 | 127 |  | ch (0-127): velocity used by NOTE |
| 26 | `LEN` | 0 | 10000 |  | ch (ms): note length for PI.N / PI.T / NOTE (0 = a 1/16 note) |
| 27 | `TIME` | 0 | 100 |  | ch (%): JF TIME (50 = noon: C4 in sound, ~6 Hz in shape) |
| 28 | `INTONE` | 0 | 100 |  | ch (%): JF INTONE (50 = unison, 100 = harmonic, 0 = subharmonic) |
| 29 | `RAMP` | 0 | 100 |  | ch (%): JF RAMP (0 saw, 50 triangle, 100 ramp) |
| 30 | `CURVE` | 0 | 100 |  | ch (%): JF CURVE (0 square, 25 expo, 50 linear, 100 cosine) |
| 31 | `FM` | 0 | 100 |  | ch (%): JF FM (below 50 = INTONE-side detune, above = FLOOM index) |
| 32 | `RANGE` | 0 | 1 |  | ch (0/1): JF range: 0 shape, 1 sound |
| 33 | `MODE` | 0 | 2 |  | ch (0-2): JF mode: 0 transient, 1 sustain, 2 cycle |
| 100 | `BIT.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 101 | `BIT.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 102 | `BIT.MIX` | 0 | 100 |  | (%) dry/wet |
| 103 | `BIT.BITS` | 1 | 24 |  | (bits) bit depth |
| 104 | `BIT.RATE` | 50 | 192000 |  | (Hz) sample-and-hold rate |
| 110 | `DIS.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 111 | `DIS.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 112 | `DIS.MIX` | 0 | 100 |  | (%) dry/wet |
| 113 | `DIS.DRIVE` | 0 | 60 |  | (dB) input drive |
| 114 | `DIS.BIAS` | 0 | 100 |  | (%) asymmetry (even harmonics) |
| 115 | `DIS.TONE` | 200 | 20000 |  | (Hz) post lowpass |
| 116 | `DIS.OUT` | -48 | 24 |  | (dB) output trim |
| 120 | `CHE.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 121 | `CHE.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 122 | `CHE.MIX` | 0 | 100 |  | (%) dry/wet |
| 123 | `CHE.ORDER` | 1 | 16 | ×100 | () harmonic order (fractional ok) |
| 124 | `CHE.DRIVE` | -24 | 36 |  | (dB) input drive |
| 130 | `WAH.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 131 | `WAH.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 132 | `WAH.MIX` | 0 | 100 |  | (%) dry/wet |
| 133 | `WAH.SENS` | 0 | 100 |  | (%) envelope sensitivity |
| 134 | `WAH.LO` | 40 | 5000 |  | (Hz) sweep bottom |
| 135 | `WAH.HI` | 200 | 16000 |  | (Hz) sweep top |
| 136 | `WAH.Q` | 0.5 | 20 | ×100 | () resonance |
| 137 | `WAH.ATK` | 0.1 | 500 |  | (ms) follower attack |
| 138 | `WAH.REL` | 1 | 3000 |  | (ms) follower release |
| 140 | `PHA.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 141 | `PHA.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 142 | `PHA.MIX` | 0 | 100 |  | (%) dry/wet (100 = deepest notches) |
| 143 | `PHA.RATE` | 0.01 | 20 | ×100 | (Hz) LFO rate |
| 144 | `PHA.DEPTH` | 0 | 8 | ×100 | (oct) sweep range |
| 145 | `PHA.FB` | -95 | 95 |  | (%) feedback |
| 146 | `PHA.STAGES` | 2 | 12 |  | () allpass stages |
| 147 | `PHA.FREQ` | 20 | 5000 |  | (Hz) sweep bottom |
| 150 | `CHO.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 151 | `CHO.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 152 | `CHO.MIX` | 0 | 100 |  | (%) dry/wet |
| 153 | `CHO.RATE` | 0.01 | 10 | ×100 | (Hz) LFO rate (voice 1) |
| 154 | `CHO.DEPTH` | 0 | 15 | ×100 | (ms) modulation depth |
| 155 | `CHO.TIME` | 1 | 30 |  | (ms) base delay |
| 160 | `VIB.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 161 | `VIB.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 162 | `VIB.DEPTH` | 0 | 4.5 | ×100 | (ms) pitch depth |
| 163 | `VIB.RATE` | 0.6 | 7.2 | ×100 | (Hz) rate |
| 170 | `TRE.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 171 | `TRE.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 172 | `TRE.DEPTH` | 0 | 100 |  | (%) depth |
| 173 | `TRE.SYNC` | 0.03125 | 16 | ×100 | (beats) period in beats (clears RATE) |
| 174 | `TRE.RATE` | 0.05 | 40 |  | (Hz) free rate (overrides SYNC) |
| 175 | `TRE.SHAPE` | 0 | 100 |  | (%) 0 sine .. 100 square |
| 180 | `FEE.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 181 | `FEE.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 182 | `FEE.MIX` | 0 | 100 |  | (%) dry/wet |
| 183 | `FEE.SYNC` | 0.0625 | 16 | ×100 | (beats) delay in beats (clears TIME) |
| 184 | `FEE.TIME` | 1 | 8000 |  | (ms) free delay (overrides SYNC) |
| 185 | `FEE.FB` | 0 | 110 |  | (%) feedback |
| 186 | `FEE.HI` | 500 | 20000 |  | (Hz) feedback lowpass |
| 187 | `FEE.LO` | 10 | 2000 |  | (Hz) feedback highpass |
| 188 | `FEE.WOW` | 0 | 500 |  | (%) tape wow |
| 190 | `DEL.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 191 | `DEL.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 192 | `DEL.MIX` | 0 | 100 |  | (%) dry/wet |
| 193 | `DEL.FB` | 5 | 90 |  | (%) feedback |
| 194 | `DEL.SYNC` | 0.0625 | 8 | ×100 | (beats) delay in beats (clears TIME) |
| 195 | `DEL.TIME` | 1 | 4000 |  | (ms) free delay (overrides SYNC) |
| 196 | `DEL.HI` | 500 | 20000 |  | (Hz) feedback lowpass |
| 197 | `DEL.LO` | 10 | 2000 |  | (Hz) feedback highpass |
| 200 | `REV.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 201 | `REV.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 202 | `REV.MIX` | 0 | 100 |  | (%) dry/wet |
| 203 | `REV.DECAY` | 0.35 | 14 | ×100 | (s) RT60 |
| 204 | `REV.PRE` | 0 | 250 |  | (ms) predelay |
| 205 | `REV.DAMP` | 500 | 20000 |  | (Hz) high damping |
| 206 | `REV.LO` | 10 | 2000 |  | (Hz) input low cut |
| 207 | `REV.MOD` | 0 | 400 |  | (%) line modulation |
| 210 | `STE.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 211 | `STE.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 212 | `STE.MIX` | 0 | 100 |  | (%) dry/wet |
| 213 | `STE.WIDTH` | 0 | 300 |  | (%) stereo width (100 = unchanged) |
| 214 | `STE.XOVER` | 20 | 1000 |  | (Hz) bass kept mono below |
| 220 | `EQU.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 221 | `EQU.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 222 | `EQU.LO` | -12.8 | 11.2 | ×100 | (dB) low shelf gain |
| 223 | `EQU.HI` | -12.8 | 11.2 | ×100 | (dB) high shelf gain |
| 224 | `EQU.MID` | -18 | 18 | ×100 | (dB) mid peak gain |
| 225 | `EQU.LOF` | 20 | 1000 |  | (Hz) low shelf freq |
| 226 | `EQU.MIDF` | 100 | 10000 |  | (Hz) mid freq |
| 227 | `EQU.HIF` | 1000 | 20000 |  | (Hz) high shelf freq |
| 228 | `EQU.Q` | 0.1 | 10 | ×100 | () mid Q |
| 230 | `COM.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 231 | `COM.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 232 | `COM.MIX` | 0 | 100 |  | (%) parallel mix |
| 233 | `COM.THR` | -60 | 0 |  | (dB) threshold |
| 234 | `COM.RATIO` | 1 | 50 |  | (:1) ratio |
| 235 | `COM.ATK` | 0.05 | 500 |  | (ms) attack |
| 236 | `COM.REL` | 5 | 5000 |  | (ms) release |
| 237 | `COM.KNEE` | 0 | 24 |  | (dB) soft knee width |
| 238 | `COM.GAIN` | -24 | 36 |  | (dB) makeup (default auto) |
| 240 | `VOL.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 241 | `VOL.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 242 | `VOL.GAIN` | -96 | 12 |  | (dB) output gain |
| 243 | `VOL.WARM` | 0 | 100 |  | (%) tape warmth |
| 250 | `LIM.W` | 0 | 15 |  | Pilot wet digit 0-15 |
| 251 | `LIM.V` | 0 | 15 |  | Pilot value digit 0-15 |
| 252 | `LIM.DRIVE` | 0 | 18 | ×100 | (dB) input drive |
| 253 | `LIM.REL` | 10 | 410 |  | (ms) release |
| 254 | `LIM.CEIL` | -24 | 0 |  | (dBTP) true-peak ceiling |
| 255 | `LIM.TP` | 0 | 1 |  | () true-peak detection on/off |

## Pilot's extended commands

These have no Teletype equivalent. They work on the command line and over UDP (they are not Teletype ops, so they don't go in
Teletype scripts; use `PI.*` there). Values can be decimals. An op with a setter form sets when it gets one more value:
`CH.VOICES 0 4` sets, `CH.VOICES 0` reads.

### Channels (ch = 0-15)

| Op | Meaning |
| - | - |
| `CH.ENG ch` · `CH.ENG ch x` | ch (e): 0 analog 1 fm 2 am 3 pluck 4 kick 5 snare 6 hat 7 tom 8 just friends |
| `CH.TIME ch` · `CH.TIME ch x` | ch (%): JF TIME (50 = noon: C4 in sound, ~6 Hz in shape) |
| `CH.INTONE ch` · `CH.INTONE ch x` | ch (%): JF INTONE (50 = unison, 100 = harmonic, 0 = subharmonic) |
| `CH.RAMP ch` · `CH.RAMP ch x` | ch (%): JF RAMP (0 saw, 50 triangle, 100 ramp) |
| `CH.CURVE ch` · `CH.CURVE ch x` | ch (%): JF CURVE (0 square, 25 expo, 50 linear, 100 cosine) |
| `CH.FM ch` · `CH.FM ch x` | ch (%): JF FM (below 50 = INTONE-side detune, above = FLOOM index) |
| `CH.RANGE ch` · `CH.RANGE ch x` | ch (0/1): JF range: 0 shape, 1 sound |
| `CH.MODE ch` · `CH.MODE ch x` | ch (0-2): JF mode: 0 transient, 1 sustain, 2 cycle |
| `CH.OSC1 ch` · `CH.OSC1 ch x` | ch (w): -1 off, 0-15 = si tr sq sw, +4 per unison step (2/4/8) |
| `CH.OSC2 ch` · `CH.OSC2 ch x` | ch (w): second oscillator / modulator wave |
| `CH.ATK ch` · `CH.ATK ch x` | ch (ms): attack |
| `CH.DEC ch` · `CH.DEC ch x` | ch (ms): decay |
| `CH.SUS ch` · `CH.SUS ch x` | ch (%): sustain |
| `CH.REL ch` · `CH.REL ch x` | ch (ms): release |
| `CH.CUT ch` · `CH.CUT ch x` | ch (Hz): filter cutoff |
| `CH.RES ch` · `CH.RES ch x` | ch (%): resonance |
| `CH.FENV ch` · `CH.FENV ch x` | ch (%): filter envelope amount |
| `CH.RATIO ch` · `CH.RATIO ch x` | ch (r): FM/AM/osc2 frequency ratio 0.25-14 |
| `CH.INDEX ch` · `CH.INDEX ch x` | ch (%): FM index / AM depth / osc2 level / drum tone |
| `CH.VOICES ch` · `CH.VOICES ch x` | ch (n): polyphony 1-16 (1 = mono) |
| `CH.GLIDE ch` · `CH.GLIDE ch x` | ch (ms): portamento |
| `CH.LEGATO ch` · `CH.LEGATO ch x` | ch (0/1): mono legato (no retrigger) |
| `CH.PAN ch` · `CH.PAN ch x` | ch (-100..100): pan |
| `CH.LEVEL ch` · `CH.LEVEL ch x` | ch (dB): level |
| `CH.DETUNE ch` · `CH.DETUNE ch x` | ch (cents): unison spread, -1 = auto |
| `CH.WIDTH ch` · `CH.WIDTH ch x` | ch (%): unison stereo width |
| `CH.BEND ch` · `CH.BEND ch x` | ch (semis): pitch-bend range |
| `CH.TRANS ch` · `CH.TRANS ch x` | ch (semis): transpose |
| `CH.FINE ch` · `CH.FINE ch x` | ch (cents): fine tune |
| `CH.VSENS ch` · `CH.VSENS ch x` | ch (%): velocity sensitivity |
| `CH.KEYT ch` · `CH.KEYT ch x` | ch (%): filter key tracking |
| `CH.DRIFT ch` · `CH.DRIFT ch x` | ch (cents): analog pitch drift |
| `CH.VEL ch` · `CH.VEL ch x` | ch (0-127): velocity used by NOTE |
| `CH.LEN ch` · `CH.LEN ch x` | ch (ms): note length for PI.N / PI.T / NOTE (0 = a 1/16 note) |
| `CH.INIT ch` | ch: reset voice settings (VOICES GLIDE PAN ...) to defaults |

### Playing

| Op | Meaning |
| - | - |
| `NOTE ch n` | ch note: play (channel's CH.VEL / CH.LEN) |
| `NOTE.V ch n vel` | ch note vel(0-127) |
| `NOTE.VL ch n vel ms` | ch note vel ms |
| `ON ch n vel` | ch note vel: note on (held) |
| `OFF ch n` | ch note: note off |
| `STOP` | silence everything |
| `RESET` | channel + effect defaults, then run script 10 |
| `RENV` | randomize envelopes |
| `ROSC` | randomize oscillators |
| `REFX` | randomize effects I-III |

### MIDI routing & I/O

| Op | Meaning |
| - | - |
| `MIDI.MODE` · `MIDI.MODE x` | 0 multi (MIDI ch = Pilot ch), 1 omni -> MIDI.CH, 2 round robin, 3 round robin (free channels first) |
| `MIDI.CH` · `MIDI.CH x` | omni target channel |
| `MIDI.RR lo hi` | lo hi: channels used by round robin |
| `MIDI.MASK` · `MIDI.MASK x` | round robin channel bitmask |
| `PORT` · `PORT x` | UDP port |

### Effects

Each effect has named parameters in real units: `COM.RATIO` reads, `COM.RATIO 1.4` sets.
`XXX.W` / `XXX.V` read or set Pilot's two hex digits as 0-15, and `XXX.CLR` clears the named overrides so the effect follows its
classic W/V digits again. Until set, a named parameter follows those digits (e.g. `COM` amount drives threshold + ratio).
From Teletype, use `PI.P` with the numbers above.

| Op | Unit | Meaning | Range |
| - | - | - | - |
| `BIT.MIX` | % | dry/wet | 0 .. 100 |
| `BIT.BITS` | bits | bit depth | 1 .. 24 |
| `BIT.RATE` | Hz | sample-and-hold rate | 50 .. 192000 |
| `DIS.MIX` | % | dry/wet | 0 .. 100 |
| `DIS.DRIVE` | dB | input drive | 0 .. 60 |
| `DIS.BIAS` | % | asymmetry (even harmonics) | 0 .. 100 |
| `DIS.TONE` | Hz | post lowpass | 200 .. 20000 |
| `DIS.OUT` | dB | output trim | -48 .. 24 |
| `CHE.MIX` | % | dry/wet | 0 .. 100 |
| `CHE.ORDER` |  | harmonic order (fractional ok) | 1 .. 16 |
| `CHE.DRIVE` | dB | input drive | -24 .. 36 |
| `WAH.MIX` | % | dry/wet | 0 .. 100 |
| `WAH.SENS` | % | envelope sensitivity | 0 .. 100 |
| `WAH.LO` | Hz | sweep bottom | 40 .. 5000 |
| `WAH.HI` | Hz | sweep top | 200 .. 16000 |
| `WAH.Q` |  | resonance | 0.5 .. 20 |
| `WAH.ATK` | ms | follower attack | 0.1 .. 500 |
| `WAH.REL` | ms | follower release | 1 .. 3000 |
| `PHA.MIX` | % | dry/wet (100 = deepest notches) | 0 .. 100 |
| `PHA.RATE` | Hz | LFO rate | 0.01 .. 20 |
| `PHA.DEPTH` | oct | sweep range | 0 .. 8 |
| `PHA.FB` | % | feedback | -95 .. 95 |
| `PHA.STAGES` |  | allpass stages | 2 .. 12 |
| `PHA.FREQ` | Hz | sweep bottom | 20 .. 5000 |
| `CHO.MIX` | % | dry/wet | 0 .. 100 |
| `CHO.RATE` | Hz | LFO rate (voice 1) | 0.01 .. 10 |
| `CHO.DEPTH` | ms | modulation depth | 0 .. 15 |
| `CHO.TIME` | ms | base delay | 1 .. 30 |
| `VIB.DEPTH` | ms | pitch depth | 0 .. 4.5 |
| `VIB.RATE` | Hz | rate | 0.6 .. 7.2 |
| `TRE.DEPTH` | % | depth | 0 .. 100 |
| `TRE.SYNC` | beats | period in beats (clears RATE) | 0.03125 .. 16 |
| `TRE.RATE` | Hz | free rate (overrides SYNC) | 0.05 .. 40 |
| `TRE.SHAPE` | % | 0 sine .. 100 square | 0 .. 100 |
| `FEE.MIX` | % | dry/wet | 0 .. 100 |
| `FEE.SYNC` | beats | delay in beats (clears TIME) | 0.0625 .. 16 |
| `FEE.TIME` | ms | free delay (overrides SYNC) | 1 .. 8000 |
| `FEE.FB` | % | feedback | 0 .. 110 |
| `FEE.HI` | Hz | feedback lowpass | 500 .. 20000 |
| `FEE.LO` | Hz | feedback highpass | 10 .. 2000 |
| `FEE.WOW` | % | tape wow | 0 .. 500 |
| `DEL.MIX` | % | dry/wet | 0 .. 100 |
| `DEL.FB` | % | feedback | 5 .. 90 |
| `DEL.SYNC` | beats | delay in beats (clears TIME) | 0.0625 .. 8 |
| `DEL.TIME` | ms | free delay (overrides SYNC) | 1 .. 4000 |
| `DEL.HI` | Hz | feedback lowpass | 500 .. 20000 |
| `DEL.LO` | Hz | feedback highpass | 10 .. 2000 |
| `REV.MIX` | % | dry/wet | 0 .. 100 |
| `REV.DECAY` | s | RT60 | 0.35 .. 14 |
| `REV.PRE` | ms | predelay | 0 .. 250 |
| `REV.DAMP` | Hz | high damping | 500 .. 20000 |
| `REV.LO` | Hz | input low cut | 10 .. 2000 |
| `REV.MOD` | % | line modulation | 0 .. 400 |
| `STE.MIX` | % | dry/wet | 0 .. 100 |
| `STE.WIDTH` | % | stereo width (100 = unchanged) | 0 .. 300 |
| `STE.XOVER` | Hz | bass kept mono below | 20 .. 1000 |
| `EQU.LO` | dB | low shelf gain | -12.8 .. 11.2 |
| `EQU.HI` | dB | high shelf gain | -12.8 .. 11.2 |
| `EQU.MID` | dB | mid peak gain | -18 .. 18 |
| `EQU.LOF` | Hz | low shelf freq | 20 .. 1000 |
| `EQU.MIDF` | Hz | mid freq | 100 .. 10000 |
| `EQU.HIF` | Hz | high shelf freq | 1000 .. 20000 |
| `EQU.Q` |  | mid Q | 0.1 .. 10 |
| `COM.MIX` | % | parallel mix | 0 .. 100 |
| `COM.THR` | dB | threshold | -60 .. 0 |
| `COM.RATIO` | :1 | ratio | 1 .. 50 |
| `COM.ATK` | ms | attack | 0.05 .. 500 |
| `COM.REL` | ms | release | 5 .. 5000 |
| `COM.KNEE` | dB | soft knee width | 0 .. 24 |
| `COM.GAIN` | dB | makeup (default auto) | -24 .. 36 |
| `VOL.GAIN` | dB | output gain | -96 .. 12 |
| `VOL.WARM` | % | tape warmth | 0 .. 100 |
| `LIM.DRIVE` | dB | input drive | 0 .. 18 |
| `LIM.REL` | ms | release | 10 .. 410 |
| `LIM.CEIL` | dBTP | true-peak ceiling | -24 .. 0 |
| `LIM.TP` |  | true-peak detection on/off | 0 .. 1 |
| `LIM.CLIPS` |  | samples the safety clamp touched (should stay 0) |  |
| `LIM.GR` |  | current limiter gain reduction dB |  |

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

