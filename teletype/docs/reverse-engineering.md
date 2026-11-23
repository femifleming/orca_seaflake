# Orca Seaflake for Teletype: how the firmware works (investigation notes)

Written 2026-11-19 for the trigger / CV / Disting EX rework and the ESC fix.

**Method.** The request asked for REA (the `rea-agents` reverse-engineering CLI). It was not
installed or approved on this Mac, so it was not used. The stock Teletype firmware is open
source and is the tree this repository is forked from (monome/teletype at e390bac, with
monome/libavr32 as the `libavr32` submodule), so the investigation was done by reading and
tracing that source, by Expert Sleepers' published Disting EX documentation (user manual 1.26,
the Teletype wiki's "Disting EX I2C specification"), and at binary level with the AVR32
toolchain's own `size` / `nm` in the `dewb/monome-build` Docker image and with the emulator.
Nothing here was observed on a real Teletype or Disting EX: no module is connected to this Mac.

## 1. File map

| Path | What it is |
|---|---|
| `src/teletype.c`, `src/command.c` | Teletype's interpreter: `parse()` (text to `tele_command_t`), `process_command()` (runs it on a stack) |
| `src/scanner.rl` → `src/scanner.c`, `src/match_token_table.c` | tokenizer; the table replaces Ragel's 64 KB `match_token.c` in this fork |
| `src/ops/op.c`, `src/ops/op_enum.h` | the op table `tele_ops[]`: every op as a `tele_op_t` (name, get, set, arg count, returns-value) |
| `src/ops/hardware.c` | `TR`, `TR.P`, `TR.TIME`, `TR.POL`, `CV`, `CV.SLEW`... for the module's own jacks (and the Ansible/TXo addresses above 4) |
| `src/ops/disting.c` | every `EX.*` op (Disting EX over I2C) |
| `src/ops/er301.c` | every `SC.*` op (ER-301 over I2C) |
| `src/state.c`, `src/state.h` | `scene_state_t`: variables (`tr`, `tr_pol`, `tr_time`, `cv`, `cv_slew`...), scripts, patterns |
| `src/teletype_io.h` | what the interpreter needs from the host: `tele_tr`, `tele_tr_pulse`, `tele_cv`, `tele_cv_slew`, `tele_ii_tx`, `tele_ii_rx`... |
| `module/main.c` | the hardware host: init, event loop, soft timers, TR pulse timers, CV/DAC, I2C, keyboard, front button, knob, screensaver |
| `module/live_mode.c`, `edit_mode.c`, `pattern_mode.c`, `preset_r_mode.c`, `preset_w_mode.c`, `help_mode.c` | the stock screens (help replaced here by `seaflake_help.c`) |
| `module/usb_disk_mode.c` | the USB stick menu (WRITE / READ), entered when a stick is plugged in |
| `module/flash.c`, `module/sf_flash.c`, `module/seaflake.lds` | scene storage (stock) and Orca patch / song / scan storage (`.flash_sf`), the ports block (`.sf_config`) |
| `libavr32/src/i2c.c`, `libavr32/asf/avr32/drivers/twi/twi.c` | I2C leader: `i2c_leader_tx/rx` over the ASF TWI driver |
| `libavr32/src/init_teletype.c`, `init_common.c`, `conf/teletype/conf_board.h` | GPIO (TR pins B08-B11, front button on NMI), ADC (IN, PARAM), SPI DAC, TWI at 100 kHz |
| `src/seaflake/sf_core.c` | the Orca Seaflake engine (C port of orca_seaflake's JS core), every operator |
| `src/seaflake/sf_ii.c` | everything the grid sends: built as real Teletype ops and run through `process_command()` |
| `src/seaflake/sf_config.c` | the 32-byte ports configuration block at 0x80044C00 |
| `module/seaflake_mode.c` | the grid screen, pickers, song tracker, scan screen, power-on |
| `module/sf_ui.c` | (new) which screen is shown and where keys / front button / knob go; shared by firmware and emulator |
| `emu/tt_emu.c` | the emulator's host: replaces `module/main.c` + `flash.c`, runs everything else unchanged |
| `emu/App.swift`, `emu/Flash.swift` | the Mac app: emulator window, Flash window (dfu-programmer), help |

## 2. Operators: registration and parsing

**Teletype.** An op is a `tele_op_t` made with `MAKE_GET_OP` / `MAKE_GET_SET_OP` / `MAKE_ALIAS_OP`
and listed in `tele_ops[]` (`op.c`); `op_enum.h` gives each an `E_OP_*` index. `parse()` turns a
line into tokens (`OP`, `NUMBER`, `MOD`, separators) and `process_command()` evaluates them
right to left on a `command_state_t` stack, calling the op's `get` (or `set` when it is the left
side of an assignment) with the arguments popped in typing order.

**Orca Seaflake.** The grid has no text parser: each glyph is an operator by itself.
`sf_frame()` snapshots the grid, then visits every cell in row order; an operator runs if it is
uppercase or a symbol, or lowercase and banged (`has_bang`: a `*` beside it). `run_op()` is one
`switch` on the glyph. Inlets are fixed cells relative to the operator (`LV(x+k, y, default,
min, max)` reads a cell as base 36 with a default for `.`), and every inlet cell is locked
(`L()`) so it is not itself run as an operator that frame. Outputs are written with `W()`, bangs
with `bang_out()`.

The status line's inlet names come from `port_of()` / `dev_port()` (`sf_cell_name()`), one
table per operator, so a `N` inside a Disting operator reads `EX NOTE`, not `NORTH`.

The Disting EX and ER-301 operators `>` and `<` take a **function letter** in the cell to their
right; `sf_dev_format()` gives that function's operand layout (`n` 1 cell, `w` 2 cells base 36,
`N` octave + note, `v` velocity, `l` level, `s` side, `a` action letter), and `sf_io_dev()`
(sf_ii.c) decodes the cells and runs the matching stock op.

**Every output goes through a real Teletype op.** `sf_ii.c` builds a `tele_command_t` (OP +
NUMBER tokens) and runs `process_command()` on the module's own `scene_state`, so the bytes on
the bus are the ones the stock op sends when typed in a script. The emulator logs the command
text (`print_command`) and the bytes.

## 3. Inlets and value handling

- Orca cells are base 36: `0`-`9`, `a`-`z` (case-insensitive as values), `.` / `*` = empty (the
  inlet's default).
- Notes: `sf_note_id(letter, octave)` = Orca's transpose table, uppercase natural, lowercase
  sharp, `H`-`Z` continue the scale upward; result `octave * 12 + semitone + 24` (so `4C` = 72,
  as Orca's MIDI does).
- Velocity: Orca `0`-`g` (0-16). MIDI: `(v * 127 + 8) / 16` (f = 119). Disting EX voices:
  `v * 1024`, g = 16384 (the Disting's maximum).
- CV (Teletype jacks and ER-301): 0-16383 = 0-10 V. Pitch is 1 V/octave with octave 0 C = 0 V:
  `(semitones-from-24 * 16384 + 60) / 120`.

## 4. Pulses, `$pulse` and timing

- **Stock `TR.P n`** (`hardware.c`): sets `tr[n] = tr_pol[n]`, `tele_tr()` drives GPIO pin
  `B08 + n` (reversed with `DEVICE.FLIP`), then `tele_tr_pulse(n, tr_time[n])` arms a 1 ms soft
  timer (`trPulseTimer[n]`, `main.c`); its callback calls `tele_tr_pulse_end()`, which puts the
  pin back. Default `TR.TIME` is 100 ms. A new pulse while one is running re-arms the timer:
  two pulses closer than the pulse length merge into one longer high.
- **`$pulse:N`** (`sf_command`, sf_core.c) sets `sf.pulse_ms` (1-250). Its power-on value is
  the ports block's `pulse_ms` (20 by default, settable in the Flash window). Every Orca
  trigger uses it: `pulse(out, ms)` → `sf_io_trig()`, which does what `TR.P` does but with its
  own length, so the scene's `TR.TIME` is left alone. On an ER-301 port it sends
  `SC.TR.TIME port ms` only when the length changed, then `SC.TR.PULSE port`.
- **Frames**: a 1 ms timer (`seaflakeTimer`) posts an event when a frame, a timed step or a
  power-on scan is due; the event loop runs `seaflake_tick()`. `sf_frame_us() = 15 000 000 / bpm`
  (4 frames per beat). Shaped runs (`@ { } >@`) and bursts (`~`) are timed in milliseconds
  inside a frame by `sf_ms()`; step times come from `seq_time()` (shapes 1-4, pulse width =
  the spread of shape 4), identical for every one of them.
- **CV**: `tele_cv()` sets a target; `cvTimer` (every 6 ms, `RATE_CV`) steps slews and writes
  the two DAC chips over SPI. Slew resolution is therefore 6 ms.

## 5. Trigger and CV output paths down to the hardware

| Orca | Teletype op path | Hardware |
|---|---|---|
| output `1`-`4`, trigger | `sf_io_trig` → `tele_tr` + `tele_tr_pulse` (as `TR.P`) | GPIO B08-B11, soft timer ends the pulse |
| output `1`-`4`, CV | `sf_io_cv` → `tele_cv_slew` (this move only) + `tele_cv` (as `CV`) | DAC over SPI, every 6 ms |
| output `a`-`z`, trigger | `SC.TR.TIME` (when changed) + `SC.TR.PULSE` | I2C to the ER-301 at 0x31 (+1, +2 for 2nd/3rd) |
| output `a`-`z`, CV | `SC.CV.SLEW` (when changed) + `SC.CV` | I2C to the ER-301 |
| MIDI ops `: % ! ? ( ) @` | `EX.M.N#`, `EX.M.NO#`, `EX.M.CC#`, `EX.M.CH` + `EX.M.PB` | I2C 0x4F + raw MIDI to the Disting EX, out of its MIDI breakout |
| N `>n`, N@ `>@` | `EX.NOTE` / `EX.NOTE.O` | I2C 0x6A / 0x68 / 0x69 to the Disting EX's voices |

## 6. The Disting EX over I2C (as `src/ops/disting.c` implements it)

- **Addresses**: units 1-4 = `disting_ex_base` + 0-3, 0x41-0x44 by default. In this firmware `disting_ex_base` is the Disting EX address from the ports block (byte 12, set in the Flash window or with ctrl+I on the module). The current unit is a static in
  `disting.c`, set by `EX n` (or for one command by `EX1:`...`EX4:`). The Disting EX's own
  factory I2C address is **49 = 0x31**, the ER-301's address: it must be set to 65 (0x41) in
  the Disting's Settings for any of this to work, and that also avoids the clash.
- **Bus**: Teletype is the only leader; TWI at 100 kHz. `tele_ii_tx` = `i2c_leader_tx`; the
  stock ops ignore the result. Reads are a write of the getter byte, then a separate read
  (`tele_ii_rx`).
- **Messages used here** (bytes after the address):

| Teletype op | Bytes | Reply |
|---|---|---|
| `EX.PRESET p` / getter | `40 pH pL` / `43` | getter: 2 bytes |
| `EX.SAVE p` / `EX.RESET` | `41 pH pL` / `42` | - |
| `EX.ALG a` / getter | `44 a` / `45` | getter: 1 byte |
| `EX.CTRL c v` | `11 c vH vL` | - |
| `EX.P p v` (`EX.PARAM`) / getter | `46 p vH vL` / `48 p` | getter: 2 bytes |
| `EX.PV p v` (0..16384 over min..max) | `47 p vH vL` | - |
| `EX.MIN p` / `EX.MAX p` | `49 p` / `4A p` | 2 bytes |
| dual: `EX.P1/P2`, `EX.PV1/PV2`, `EX.MIN1`... | `5A`-`5E` with `(side << 4) | param` | getters: 1 byte |
| `EX.NOTE pitch vel` | `6A ch n`, `68 ch n pH pL`, `69 ch n vH vL` | - |
| `EX.NOTE.O pitch` | `6A ch n` | - |
| `EX.N# ch pitch vel` / `EX.NO#` | the same with the given channel | - |
| `EX.VOX v pitch vel` / `EX.VOX.O` | `51 v pH pL`, `52 v vH vL` / `53 v` (`57` = all) | - |
| `EX.ALLOFF` | `57` | - |
| `EX.M.*` | `4F status d1 d2` (raw MIDI) | - |
| `EX.SB.*` | `50 status d1 d2` (Select Bus) | - |

- **Notes (`EX.NOTE`)**: the pitch is 14-bit 1 V/octave (16384 = 10 V, a semitone = 136.53),
  with **0 V = MIDI note 48** (the Disting EX manual: "0 Volts corresponding to MIDI note 48").
  The op derives the note id from the pitch with `calculate_note()` and sends a note-off for
  that id first. The channel is the static `note_channel` (1 until `EX.CH` changes it).
  `calculate_note()` truncates toward zero, so below 0 V a pitch exactly on a semitone can come
  out one note high; `sf_ex_pitch()` nudges the pitch by up to a few units (under a cent) so the
  id is always the note (tested for all 128 notes).
- **Velocity** for `EX.NOTE` / `EX.VOX` / `EX.TV`: 0..16384.
- **Which algorithms answer**: the note messages (0x54-0x56, 0x68-0x6A) are played by the
  algorithms whose "I2C support" section in the manual lists them (SD Multisample, the poly
  synths, Macro Oscillator 2, etc.); some need their I2C / MIDI mode parameter turned on. The
  channel selects the timbre in multi-timbral algorithms (SD Multisample: channels 1-3).
- **Replies**: only the getters (0x43, 0x45, 0x48, 0x49, 0x4A, 0x59, 0x5A-0x5C, 0x5F, 0x61, 0x66)
  return data. Those are the only way to find out whether a Disting EX is there.
- **Bugs found in the old Orca `>` notes** (fixed): `>n`, `>w`, `>f` passed the MIDI note
  number as the pitch (so `>n.4C` sent pitch 72 = note 49, not 72) and `>n`, `>w`, `>t` sent
  MIDI velocity 0-127 where the Disting expects 0..16384 (barely audible).

**Parameter control (`EX.P` family).** `EX.P p v` sets parameter p to its own value (what the
Disting shows), `EX.PV p v` to a 0..16384 position across min..max, `EX.P p` reads it,
`EX.MIN` / `EX.MAX` read the range; the `1` / `2` forms address one side of a dual algorithm.
Limits: an Orca value is at most two base-36 cells (0-1295) and never negative, so a negative
parameter value (an attenuverter at -100 %) cannot be set with `>p`; use `>v` (a level across
the range) for those. Values read back are written below as one glyph, clamped to 0-35.

**Preset loading and startup.** Stock Teletype has no power-on preset: its only power-on hook is
the scene's init script (`run_script(INIT_SCRIPT)` in `initialize_module()`), after a fixed
`delay_ms(1500)` "to allow for any i2c devices to fully initialise". A Disting EX or ER-301 can
take longer than that; a message sent before a follower is up is simply not acknowledged and
lost.

## 7. ER-301 and other followers on the same bus

ER-301 units are at 0x31-0x33 (`ER301_1 + output / 100`, port `output % 100`). `SC.TR`,
`SC.TR.TOG`, `SC.TR.TIME`, `SC.TR.PULSE`, `SC.CV`, `SC.CV.SET`, `SC.CV.SLEW`, `SC.CV.OFF` are
plain writes (`cmd port vH vL`); the ER-301 answers reads of its address with an ACK. Everything
else on the bus (TXo/TXi, Ansible, JF, W/, crow...) keeps its own address; Orca only sends to
0x41-0x44 and 0x31-0x33 (and the stock scripts send wherever they send). The Disting EX's
default 0x31 is the one real clash.

## 8. Boot, modes, and what ESC did

**Boot** (`initialize_module()`): clocks, GPIO, timers, USB host, OLED, MIDI; `delay_ms(1500)`;
I2C leader; scene state; the first-start "SCENES WILL BE OVERWRITTEN" screen if the scene flash
is fresh (front button to continue); calibration; last scene; timers; init script. Then
`seaflake_init()` (patch / song, scan) and the Orca grid.

**The bootloader** is the Atmel DFU bootloader at the bottom of flash. It checks the front
button at reset: held at power-on = stay in the bootloader (USB 03eb:2ff6). The firmware has no
path into it: no soft reset, watchdog or ISP-request code is linked (`nm`: none). A running
module can only reach the bootloader by being power-cycled with the button held. There is one
firmware image on the chip.

**Modes.** Stock Teletype is a mode machine in `main.c`: `set_mode()` / `set_last_mode()`,
global keys in `process_global_keys()`: ESC = SCENE READ (`M_PRESET_R`) or back to the last mode,
alt-ESC = SCENE WRITE, tab = live → edit → pattern, F11 / num lock = pattern, F12 / print
screen = live, alt-F1..F10 = edit a script; the front button = SCENE READ (hold to load a
scene); a plugged-in USB stick = the USB menu, which returns with `set_mode(M_LIVE)`.

Orca Seaflake had been added as one more mode, `M_SEAFLAKE`, reached at boot and with alt-o.
In the grid it caught a plain ESC itself, but every other stock route stayed live, and
`set_last_mode()` only remembers one step. So:

- after the USB stick menu (the backup step of the flashing guide) the module landed in the
  stock **live** screen;
- the **front button** opened the stock SCENE READ screen; alt-ESC, alt-F1..F10, F11, F12 and
  alt-o opened other stock screens;
- from any stock screen, ESC toggles SCENE READ and the previous stock screen: it never comes
  back to Orca. On the module this looks exactly like having booted the stock firmware.
- after the screensaver, `exit_screensaver()` re-set the current mode, making `last_mode` equal
  to it: help could then not be left with ESC or alt-h (stuck in help), and alt-o did nothing.

The emulator (`tt_emu.c`) had a copy of the same mode code and reproduced all of these (probe
run before the fix: alt-ESC → SCENE WRITE, then ESC → SCENE READ → LIVE → SCENE READ...).

**The fix** is structural: `module/sf_ui.c` owns the mode for both the firmware and the
emulator. `set_mode()` only knows Orca and its help: any request for a stock screen (from the
USB menu, the grid visualizer, scene read) lands on the grid; `set_last_mode()` is the grid.
Keys go to Orca only: ESC closes Orca's pickers / views, alt-ESC / tab / F11 / F12 / alt-F do
nothing, alt-h toggles help; F1-F10 still run the scene's scripts (`$script:N` does the same).
The front button opens LOAD PATCH (PARAM picks the slot, the button again loads it), the Orca
counterpart of stock scene recall. With nothing referencing them, the linker drops the stock
screens' code (`set_live_mode`, `process_live_keys`, `screen_refresh_live`, the edit, pattern
and scene read / write equivalents are gone from `teletype.sym`): the program shrank from
0x3E8AA to 0x3A132 bytes of text (about 18 KB freed) while gaining the new operators.

## 9. Emulator and flasher

- `emu/tt_emu.c` replaces only `module/main.c` and `module/flash.c`: timers, TR/CV state, the
  scene files, the I2C log. The interpreter, every op, the Orca engine, `sf_ii.c`, the screens
  and (now) `sf_ui.c` are the firmware's own files, compiled for the Mac into
  `libseaflake-emu.a`. The device scan is shared too: the firmware and the emulator only provide
  the raw bus (`sf_io_ii_tx/rx`); the emulator acknowledges the addresses of the devices it
  pretends are there (EX1 + ER-301 1, or an imported `SFSCAN.TXT`; tests can make the bus dead
  or the devices late).
- The Flash window (`Flash.swift`) runs `dfu-programmer` (erase / flash / start), detects the
  bootloader through IOKit (USB 03eb:2ff6), reads the module back, and shows the **ports table**
  from the 32-byte block at 0x80044C00 (`SFCF`, version, EX unit, ER-301 unit, pulse,
  MIDI-via-EX, and since version 2 the startup preset), patches it in the .hex (checksum = low
  byte of the sum of the first 31 bytes) before flashing, and now also hands the same block to
  the running emulator.

## 10. Limits found

- I2C at 100 kHz: an EX.NOTE is three messages (about 1.5 ms of bus time); a shaped run of
  many steps per frame on a busy bus can make steps late.
- A follower that is absent costs a NACK; a dead bus (SDA held low) costs the TWI driver's
  bounded busy wait (`TWI_BUSY_COUNTER`, 10 000 polls) per message, then it gives up. The power-on scan is bounded and
  runs from the event loop, so the grid keeps playing either way.
- Trigger pulses closer together than `$pulse` merge into one long high (the timer re-arms).
- CV slew resolution 6 ms; TR timer resolution 1 ms.
- Disting EX: must be at 0x41-0x44 (factory 0x31 clashes with the ER-301); parameter values are
  sent as unsigned from Orca (no negative values with `>p`); presets are numbered as in
  `EX.PRESET`.
- Flash: 512 KB chip; program area up to 0x80044C00 (275 KB), then the ports block, the
  37 KB Orca patch / song area, and Teletype's 256 KB scene area. RAM: about 57 KB of 96 KB is
  static data and bss.
