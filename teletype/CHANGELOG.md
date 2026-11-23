# Changelog

## 2026-11-23
- Just Friends in the I2C scan: one write-only byte (the JF.SPEED request, 0x8F, never read back) to 0x70 at power-on and on ctrl+I; ctrl+I shows JUST FRIENDS 70 FOUND / --, the power-on line says + JF, and every , message updates it.
- The screensaver picks one of the four new dreams at random each time it starts, never the same one twice in a row (module/dream.c; they share dream_kit's primitive list, ~8 KB more program, no more RAM). The user's request: "is there enoupgh space for it to be a random one of those 4 screensavers each time?"
- The screensaver is now the elevator into the night (dd.html, 2026-05-18), the user's pick of the four options: the car rising in its shaft, then gliding over a treeline under the stars with flashlight beams sweeping up. Sparse line art, busiest frame 593 lit pixels (the train dream: 825), 3 KB less RAM. module/dream.c includes it; the train dream moved to module/dreams/train.c.
- New operator `,` = Just Friends (I2C 0x70 / 0x75): `,` + a function letter + fixed operand cells, like `>`. Every Teletype JF.* op is reachable: ,n JF.NOTE, ,@ JF.NOTE as @'s shaped run, ,p JF.POLY, ,x JF.POLY.RESET, ,v JF.VOX, ,f JF.PITCH, ,r JF.VTR, ,t JF.TR (state empty = a $pulse trigger), ,s JF.SHIFT, ,u JF.RUN, ,o JF.RMODE, ,m JF.MODE, ,d JF.GOD, ,j JF.TUNE, ,q JF.QT, ,k JF.TICK, ,a JF.ADDR, ,e JF.SEL, ,g readers (JF.SPEED/TSC/RAMP/CURVE/FM/TIME/INTONE). Pitch: octave 4 C = 0 V (Just Friends' own tuning); velocity 0-g = 0-8 V, empty = 5 V; channels 1-6, 0 all, 7-c the second JF. Bytes are the stock ops' (tests compare each to the typed Teletype op).
- Fixed two stock Teletype bugs in src/ops/justfriends.c: JF.PITCH sent 6 bytes from a 4-byte buffer (2 bytes of stack garbage on the bus), and the 2-byte JF getters (RAMP, CURVE, FM, TIME, INTONE) read 2 bytes into a 1-byte buffer.
- The emulator has a Just Friends at 0x70 (its getters answer), and labels JF messages in the I2C log. 236 emulator tests.
- Dream screensaver options (not in the firmware yet, the user picks): module/dreams/ golden_geometries, tunnel_bell, rain_lake, night_elevator, on a shared sparse line-art kit (dream_kit.h) under the OLED limits. Videos in docs/dreams/, tools/dream_render.c renders and measures them.


## 2026-11-20
- Back to the stippled dream: the full-shade one still glitched the module's screen even with the repair. Both are needed: sparse drawing and the controller repair.
- The screen repair fixed the screensaver on the module; the dream is back in full shades (dusk to night, fog, reflections), with the repair running every frame.
- Fixed I2C addresses: Disting EX 0x41, ER-301 0x31. Removed >u, the ctrl+I address editing and the Flash window's address and pinned-unit settings. ctrl+I shows the Disting EX (algorithm, preset), the ER-301, the bus and the message count.
- Screen: the OLED controller is unlocked at start and its whole setup is re-sent before every dream frame, every ~2 s and on wake, so a corrupted controller state is repaired at once instead of lasting until power-off; OLED SPI at 10 MHz (was 20).
- Screensaver: the dream is stippled line art (ordered dots for the shading, solid ridgelines, train windows, stars and glints, at most 32 lit pixels in any row). Drawn in full shades it lit 66% of the OLED and whole rows at once, and the module's screen glitched after ~20 s, even on the first build. Now it never lights more of the screen, or of any row, than the grid does.
- Screensaver back to the first build's code (the reworks glitched on the module after ~20 s): the original dream, drawn as before; after 5 idle minutes; no black phase; alt+d starts it now.
- Screensaver starts after 5 idle minutes (was 10).
- Screensaver: no dark phase (the dream until a key). The real module showed a black screen that ignored keys: the dream now draws 2 of 8 bands per refresh, takes its time from the hardware ms counter, only one screen refresh can be queued (so refreshes can't flood the event queue and drop keys), and waking redraws every line.
- tools/keep_patches.py: flash new firmware while keeping the module's patches, songs, scenes and settings (from a DFU read-back).
- Manual + help: where the bang goes (D above-left of the operator), >p vs >v explained, I2C chapter (BUS / OK-FAIL lines, troubleshooting), new Flash window, 2-minute scan, ER-301 found by use; two broken examples fixed (second rows never banged; "a3E one cell too far). New tests run every example layout in the manual and help (212 checks). compare.sh links again (sf_io_config_write stub).
- Fix: every I2C message failed BUSY after the scan read from the ER-301 (it answered the address but never the byte; the driver then stayed 'busy' forever). The ER-301 is no longer read (found when an SC write is acknowledged; ctrl+I says `TRY <P1`), a timed-out transfer aborts and restarts the bus, and a write is retried once.
- I2C failures say why: status line `I2C 41 NACK CMD 6A` (NACK = refused, BUSY = timed out); ctrl+I last line `OK n FAIL n NACK 6A`. TWI busy wait raised to ~15 ms (libavr32, local branch). An ER-301 that ignores the scan's read is marked FOUND once an SC write to it is acknowledged.
- Power-on scan keeps looking for 2 minutes (every 0.5 s for 10 s, then every 2 s): on the real module the devices were found right after flashing but not after a cold power-cycle, because the ER-301 boots slower than the old 10 s limit. Startup preset is sent whenever the Disting EX first answers; loadbangs fire then or at 30 s.
- I2C bus init rewritten (`sf_bus_init`): SCL really 100 kHz (stock computes the divider from 12 MHz while the bus clock is 60 MHz, giving ~500 kHz), the chip's pull-ups on SDA/SCL, power-on check for external pull-ups and a held-low line, 9-clock recovery. Shown on the ctrl+I screen (`BUS 100K ...`).
- I2C diagnostics: every operator message is counted with its ACK. Status line shows `I2C NO ANSWER <addr>` for 2 s when a device doesn't answer; the ctrl+I scan screen's last line shows `I2C SENT n FAIL n <addr>` (ER-301s now on two lines).
- `-` and `_` drawn 4 pixels wide on the grid and in the help (the Teletype font's `-` was 2 pixels, `_` sat on the bottom edge). Help's trigger section rewritten with an example (`D4` above `-1`).
- Flash window simplified: connect status, Disting EX address, startup preset, one FLASH button; everything else under Details.

## Orca Seaflake for Teletype

### 2026-11-19

- **NEW**: the Disting EX I2C address is one setting (ports block version 3, byte 12; default 65 = 0x41; units 2-4 are the next three addresses). Set it in the Flash window (the number the Disting's own menu shows, hex alongside), or on the Teletype: ctrl-i, up/down (shift: 10), enter saves it in flash, no reflash. Set the same number on the Disting EX (Settings > I2C address).
- **FIX**: the firmware ignored the ports block the Flash window wrote: the compiler had folded the const block to its compiled-in defaults (e.g. `sf_config_pulse_ms` was `return 20`). It is now read through a volatile pointer; the flasher patches with the firmware's own `sf_config_build` / `sf_config_parse`.
- **NEW**: `-` trigger only (output; one `$pulse` pulse, no CV), `_` trigger + CV (output, octave, note), `}` shaped trigger (`@`'s run as pulses only), `` ` `` loadbang (bangs below when a patch loads; at power-on after the Disting EX answers). `'` unchanged.
- **NEW**: N `>n` (octave, note, velocity to the Disting EX's voices via EX.NOTE, off a frame later) and N@ `>@` (with `@`'s shape, repeats, duration, pulse width). No unit or channel cells.
- **NEW**: Disting EX startup preset in the ports block (version 2, bytes 9-11), set in the Flash window, sent once at power-on when the Disting EX answers; the power-on scan retries every 0.5 s for up to 10 s.
- **IMP**: Disting EX functions reorganised: value first, side last; `>p` = EX.P, `>v` = EX.PV; old `>n` (with channel) is `>d`; `>u` kept for racks with several units.
- **FIX**: `>n`, `>w`, `>f` sent a note number as the pitch and `>n`, `>w`, `>t` a MIDI velocity where the Disting EX expects 0..16384; now converted (pitch 0 V = note 48, note id exact for all 128 notes).
- **FIX**: esc (and the front button, alt-esc, alt-F keys, F11/F12, the USB menu) could drop the module into the stock Teletype screens with no way back to Orca. `module/sf_ui.c` now owns the modes for firmware and emulator; the stock screens are gone from the image (about 18 KB freed). The front button opens LOAD PATCH (PARAM picks, the button loads). Help could get stuck after the screensaver: fixed.
- **IMP**: the device scan is shared code (firmware and emulator differ only in the raw bus); the emulator can simulate absent, late or dead-bus devices; the Flash window's ports also apply to the emulator.
- **IMP**: SC.CV.SLEW goes to an ER-301 port only when it changes.
- **DOC**: `docs/reverse-engineering.md` (how the stock firmware, its Disting EX ops and the esc problem work).

## Teletype (upstream)

## v5.0.x:

- **FIX**: fix risk of crash/corruption in help mode reverse search

## v5.0.0

- **FIX**: fix off-by-one error in `P.ROT` understanding of pattern length
- **FIX**: fix `CROW.Q3` calls `ii.self.query2` instead of `ii.self.query3`
- **FIX**: cache currently-running commands to avoid corruption during SCENE ops.
- **FIX**: delay when opening docs
- **FIX**: `PROB 100` would only execute 99.01% of the time.
- **FIX**: some `G.FDR` configurations caused incorrect rendering in grid visualizer
- **FIX**: fix `EX.LP` not returning correct values
- **FIX**: fix `QT.B` handling of negative voltage input
- **IMP**: scene load/save code refactor, add scene load/save tests
- **IMP**: fader ops now support up to four faderbanks
- **NEW**: new Disting EX ops: dual algorithms, `EX.M.N#`, `EX.M.NO#`, `EX.M.CC#`
- **FIX**: reset M timer when changing metro rate
- **NEW**: new Drum Ops: `DR.T`, `DR.V`, `DR.P`
- **NEW**: [I2C2MIDI](https://github.com/attowatt/i2c2midi) ops
- **FIX**: fixes a transcription error  in the SD drum helper patterns
- **NEW**: Ten new patterns for `DR.V` and optimised old patterns
- **FIX**: fix BPM rounding error
- **FIX**: support all line ending types for USB load
- **FIX**: fix `STATE` not accounting for `DEVICE.FLIP`
- **FIX**: fix MIDI IN ops channel number being off by 1
- **FIX**: improve `TR.P` accuracy
- **FIX**: fix `KILL` not stopping TR pulses in progress
- **NEW**: new op: `SCALE0` / `SCL0`
- **NEW**: new ops: `$F`, `$F1`, `$F2`, `$L`, `$L1`, `$L2`, `$S`, `$S1`, `$S2`, `I1`, `I2`, `FR`
- **NEW**: new op: `CV.GET`
- **NEW**: basic menu for reading/writing scenes when a USB stick is inserted
- **NEW**: new ops: `CV.CAL` and `CV.CAL.RESET` to calibrate CV outputs
- **FIX**: N.CS scales 7 & 8 were incorrectly swapped; make them consistent with N.S and docs
- **FIX**: libavr32 update: support CDC grid size detection (e.g. zero), increase HID message buffer
- **NEW**: new Disting EX ops: `EX.CH`, `EX.#`, `EX.N#`, `EX.NO#`
- **NEW**: apply VCV Rack compatibility patches, so branches off main can be used in both hardware and software
- **FIX**: update Disting EX looper ops to work with Disting EX firmware 1.23+
- **NEW**: new dual W/ ops: `W/.SEL`, `W/S.POLY`, `W/S.POLY.RESET`, `W/1`, `W/2`
- **NEW**: split cheatsheets into separate PDFs for core ops and i2c

## v4.0.0

- **FIX**: `LAST SCRIPT` in live mode gives time since init script was run
- **FIX**: negative pattern values are properly read from USB
- **FIX**: delay when navigating to sections in docs
- **NEW**: generic i2c ops: `IIA`, `IIS..`, `IIQ..`, `IIB..`
- **NEW**: exponential delay operator `DEL.G`
- **NEW**: binary and hex format for numbers: `B...`, `X...`
- **NEW**: Disting EX ops
- **FIX**: `LAST n` is broken for script 1
- **NEW**: bitmasked delay and quantize: `DEL.B..`,  `QT.B..`, `QT.BX..`
- **NEW**: scale and chord quantize: `QT.S..`, `QT.CS..`
- **NEW**: bit toggle OP: `BTOG..`
- **NEW**: volts to semitones helper OP: `VN..`
- **IMP**: DELAY_SIZE increased to 64 from 16
- **FIX**: scale degree arguments 1-indexed: `N.S`, `N.CS`
- **NEW**: Just Friends 4.0 OPs and dual JF OPs
- **NEW**: binary scale ops `N.B` and `N.BX`
- **NEW**: reverse binary for numbers: `R...`
- **NEW**: reverse binary OP: `BREV`
- **NEW**: `ES.CV` read earthsea CV values
- **NEW**: added setter for `R`, sets R.MIN and R.MAX to same value, allowing R to be used as variable
- **NEW**: v/oct to hz/v conversion op: `HZ`
- **FIX**: W/2.0 ops added
- **NEW**: W/2.0 ops documentation
- **NEW**: `><`, `<>`, `>=<` and `<=>` OPs, checks if value is within or outside of range
- **IMP**: new powerful Q OPs
- **IMP**: Improved line editing movement (forward/backward by word skips intervening space).
- **NEW**: Delete to end of word command `alt-d` added.
- **NEW**: new op: `SCENE.P`
- **NEW**: new multi-logic OPs `AND3`, `AND4`, `OR3` and `OR4` with aliases `&&&`, `&&&&`, `|||` and `||||`
- **NEW**: alias: `EV` for `EVERY`
- **NEW**: live mode dashboard
- **NEW**: ops to control live mode: `LIVE.OFF`, `LIVE.VARS`, `LIVE.GRID`, `LIVE.DASH`, `PRINT`
- **FIX**: `PN.ROT` parameters are swapped
- **FIX**: better rendering for fine grid faders
- **FIX**: logical operators should treat all non zero values as `true`, not just positive values
- **NEW**: crow ops
- **NEW**: `TI.PRM.CALIB` alias added (was already in the docs)
- **FIX**: `SCENE` would crash if parameter was out of bounds

## v3.2.0

- **FIX**: improve DAC latency when using `CV` ops
- **NEW**: call metro / init with `SCRIPT 9` / `SCRIPT 10`
- **NEW**: forward (C-f or C-s) and reverse (C-r) search in help mode
- **NEW**: new ops: `LROT` (alias `<<<`), `RROT` (alias `>>>`)
- **NEW**: `LSH` and `RSH` shift the opposite direction when passed a negative shift amount
- **NEW**: new op: `SGN` (sign of argument)
- **NEW**: new kria remote op: `KR.DUR`
- **NEW**: new op: `NR` (binary math pattern generator)
- **NEW**: new ops: `N.S, N.C, N.CS` (use western scales and chords to get values from `N` table)
- **NEW**: new ops: `FADER.SCALE, FADER.CAL.MIN, FADER.CAL.MAX, FADER.CAL.RESET`  for scaling 16n Faderbank values (aliases `FB.S, FB.C.MIN, FB.C.MAX, FB.C.R`)
- **NEW**: new Tracker mode keybinding `alt-[ ]` semitone up, down
- **NEW**: new Tracker mode keybinding `ctrl-[ ]` fifth up, down
- **NEW**: new Tracker mode keybinding `shift-[ ]` octave up, down
- **NEW**: new Tracker mode keybinding `alt-<0-9>` `<0-9>` semitones up (0=10, 1=11)
- **NEW**: new Tracker mode keybinding `shift-alt-<0-9>` `<0-9>` semitones down (0=10, 1=11)
- **FIX**: dim M in edit mode when metro inactive
- **NEW**: new pattern ops: `P.SHUF`, `PN.SHUF`, `P.REV`, `PN.REV`, `P.ROT`, `PN.ROT`
- **NEW**: new pattern mods: `P.MAP:`, `PN.MAP x:`

## v3.1.0

- **NEW**: new op: `DEVICE.FLIP`
- **FIX**: [some keyboards losing keystrokes](https://github.com/monome/teletype/issues/156)
- **NEW**: new op: `DEL.X`
- **NEW**: new op: `DEL.R`
- **IMP**: DELAY_SIZE increased to 16 from 8
- **NEW**: new variables: `J` & `K` local script variables
- **FIX**: [metro rate not updated after `INIT.SCENE`](https://github.com/monome/teletype/issues/174)
- **NEW**: new ops: `SEED`, `R.SEED`, `TOSS.SEED`, `DRUNK.SEED`, `P.SEED`, `PROB.SEED`
- **NEW**: new op: `SCENE.G`
- **NEW**: new op: `SCRIPT.POL`, alias `$.POL`
- **NEW**: new ansible remote ops: `ANS.G`, `ANS.G.P`, `ANS.G.LED`, `ANS.A`, `ANS.A.LED`
- **NEW**: new kria remote ops: `KR.CUE`, `KR.PG`

## v3.0.0

- **NEW**: grid integration / grid visualizer / grid control mode
- **NEW**: multiline copy/paste and editing
- **NEW**: new keybindings to move by words
- **NEW**: undo in script editing
- **NEW**: i2c support for ER-301
- **NEW**: i2c support for 16n Faderbank
- **NEW**: i2c support for Matrixarchate
- **NEW**: i2c support for W/
- **NEW**: new op: `?`
- **NEW**: new ops: `P.MIN`, `PN.MIN`, `P.MAX`, `PN.MAX`, `P.RND`, `PN.RND`, `P.+`, `PN.+`, `P.-`, `PN.-`. `P.+W`, `PN.+W`, `P.-W`, `PN.-W`
- **NEW**: new Telex ops: `TO.CV.CALIB`, `TO.ENV`
- **NEW**: new Kria ops: `KR.CV`, `KR.MUTE`, `KR.TMUTE`, `KR.CLK`, `ME.CV`
- **NEW**: new aliases: `$`, `RND`, `RRND`, `WRP`, `SCL`
- **NEW**: telex, ansible, just friends, w/ added to the help screen
- **FIX**: i2c initialization delayed to account for ER-301 bootup
- **FIX**: last screen saved to flash
- **FIX**: knob jitter when loading/saving scenes reduced
- **FIX**: [duplicate commands not added to history](https://github.com/monome/teletype/issues/99)
- **FIX**: `SCALE` precision improved
- **FIX**: `PARAM` set properly when used in the init script
- **FIX**: `PARAM` and `IN` won't reset to 0 after `INIT.DATA`
- **FIX**: [`PN.HERE`, `P.POP`, `PN.POP` will update the tracker screen](https://github.com/monome/teletype/issues/151)
- **FIX**: [`P.RM` was 1-based, now 0-based](https://github.com/monome/teletype/issues/149)
- **FIX**: [`P.RM` / `PN.RM` will not change pattern length if deleting outside of length range](https://github.com/monome/teletype/issues/150)
- **FIX**: [`JI` op fixed](https://llllllll.co/t/teletype-the-ji-op/10553)
- **FIX**: [`TIME` and `LAST` are now 1ms accurate](https://github.com/monome/teletype/issues/144)
- **FIX**: [`RAND` / `RRAND` will properly work with large range values](https://github.com/monome/teletype/issues/143)
- **FIX**: [`L .. 32767` won't freeze](https://github.com/monome/teletype/issues/148)
- **FIX**: `I` now accessible to child SCRIPTS

## v2.2
- **NEW**: added a cheat sheet PDF
- **NEW**: new bitwise ops: `&`, `|`, `^`, `~`, `BSET`, `BCLR`, `BGET`
- **NEW**: new ops `PARAM.SCALE min max` and `IN.SCALE min max` to add static scaling to inputs
- **NEW**: blanking screensaver after 90 minutes of keyboard inactivity, any key to wake
- **NEW**: new op: `CHAOS` chaotic sequence generator.  Control with `CHAOS.ALG` and `CHAOS.R`
- **NEW**: new op family: `INIT`, to clear device state
- **NEW**: new ops: `R`, `R.MIN`, `R.MAX` programmable RNG
- **IMP**: profiling code (optional, dev feature)
- **IMP**: screen now redraws only lines that have changed
- **FIX**: multiply now saturates at limits, previous behaviour returned 0 at overflow
- **FIX**: entered values now saturate at int16 limits
- **FIX**: reduced flash memory consumption by not storing TEMP script
- **FIX**: `I` now carries across `DEL` commands
- **FIX**: removed TEMP script allocation in flash
- **FIX**: corrected functionality of JI op for 1volt/octave tuning and removed octave-wrapping behaviour (now returns exactly the entered ratio)
- **FIX**: reduced latency of `IN` op

## v2.1
- **BREAKING**: the `I` variable is now scoped to the `L` loop, and does not exist outside of an execution context.  Scripts using `I` as a general-purpose variable will be broken.
- **FIX**: `SCENE` will not run from `INIT` script during scene load.
- **NEW**: Tracker data entry overhaul.  Type numbers, press enter to commit.
- **NEW**: new op: `BPM` to get milliseconds per beat in given BPM
- **NEW**: script lines can be disabled / enabled with ctrl-/
- **NEW**: shift-enter in scene write mode now inserts a line
- **NEW**: new ops: `LAST x` for the last time script `x` was called
- **NEW**: `SCRIPT` with no arguments gets the current script number.
- **FIX**: `AVG` and `Q.AVG` now round up properly
- **NEW**: new op: `BREAK` to stop the remainder of the script
- **NEW**: new mod: `W [condition]: [statement]` will execute `statement` as long as `condition` is true (up to an iteration limit).
- **NEW**: new mods: `EVERY x:`, `SKIP x:`, `OTHER:` to alternately execute or not execute a command.
- **NEW**: new op: `SYNC x` will synchronize all `EVERY` and `SKIP` line to the same step.
- **NEW**: new feature: @ - the turtle.  Walks around the pattern grid.  Many ops, see documentation.
- **OLD**: ctrl-F1 to F8 mute/unmute scripts.
- **NEW**: ctrl-F9 enables/disables METRO.
- **FIX**: recursive delay fix.  Now you can `1: DEL 500: SCRIPT 1` for temporal recursion.
- **FIX**: `KILL` now clears TR output as well as disabling the METRO script.
- **FIX**: if / else conditions no longer transcend their script
- **IMP**: functional exectuion stack for `SCRIPT` operations

## v2.0.1
- **FIX**: update IRQ masking which prevents screen glitches and crashing under heavy load

## v2.0
- **BREAKING**: remove `II` op. Ops that required it will now work with out it. (e.g. `II MP.PRESET 1` will become just `MP.PRESET 1`)
- **BREAKING**: merge the `MUTE` and `UNMUTE` ops. Now `MUTE x` will return the mute status for trigger `x` (`0` is unmuted, `1` is muted), and `MUTE x y` will set the mute for trigger `x` (`y = 0` to unmute, `y = 1` to mute)
- **BREAKING**: remove unused Meadowphysics ops: `MP.SYNC`, `MP.MUTE`, `MP.UNMUTE`, `MP.FREEZE`, `MP.UNFREEZE`
- **BREAKING**: rename Ansible Meadowphysics ops to start with `ME`
- **NEW**: sub commands, use a `;` separator to run multiple commands on a single line, e.g. `X 1; Y 2`
- **NEW**: key bindings rewritten
- **NEW**: aliases: `+` for `ADD`, `-` for `SUB`, `*` for `MUL`, `/` for `DIV`, `%` for `MOD`, `<<` for `LSH`, `>>` for `RSH`, `==` for `EQ`, `!=` for `NE`, `<` for `LT`, `>` for `GT`, `<=` for `LTE`, `>=` for `GTE`, `!` for `EZ`, `&&` for `AND`, `||` for `OR`, `PRM` for `PARAM`, `TR.P` for `TR.PULSE`
- **NEW**: new ops: `LTE` (less than or equal), and `GTE` (greater than or equal)
- **NEW**: new pattern ops: `PN.L`, `PN.WRAP`, `PN.START`, `PN.END`, `PN.I`, `PN.HERE`, `PN.NEXT`, `PN.PREV`, `PN.INS`, `PN.RM`, `PN.PUSH` and `PN.POP`
- **NEW**: USB disk loading and saving works at any time
- **NEW**: `M` limited to setting the metronome speed to 25ms, added `M!` to allow setting the metronome at unsupported speeds as low as 2ms
- **NEW**: TELEX Aliases: `TO.TR.P` for `TO.TR.PULSE` (plus all sub-commands) and `TI.PRM` for `TI.PARAM` (plus all sub-commands)
- **NEW**: TELEX initialization commands: `TO.TR.INIT n`, `TO.CV.INIT n`, `TO.INIT x`, `TI.PARAM.INIT n`, `TI.IN.INIT n`, and `TI.INIT x`
- **IMP**: new Ragel parser backend
- **IMP**: script recursion enhanced, maximum recursion depth is 8, and self recursion is allowed
- **IMP**: removed the need to prefix `:` and `;` with a space, e.g. `IF X : TR.PULSE 1` becomes `IF X: TR.PULSE`
- **IMP**: `AND` and `OR` now work as boolean logic, rather than bitwise, `XOR` is an alias for `NE`
- **FIX**: divide by zero errors now explicitly return a 0 (e.g. `DIV 5 0` now returns 0 instead of -1), previously the behaviour was undefined and would crash the simulator
- **FIX**: numerous crashing bugs with text entry
- **FIX**: i2c bus crashes under high `M` times with external triggers
- **FIX**: `P.I` and `PN.I` no longer set values longer than allowed
- **FIX**: `VV` works correctly with negative values

## v1.4.1
- **NEW**: added Ansible remote commands `LV.CV` and `CY.CV`
- **NEW**: Added TELEX Modules Support for the TXi and the TXo
- **NEW**: 75 New Operators Across the Two Modules
- **NEW**: Supports all basic Teletype functions (add `TI` and `TO` to the commands you already know)
- **NEW**: Extended functionality allows for additional capabilities for existing functions
- **NEW**: Experimental input operators add capabilities such as input range mapping and quantization
- **NEW**: Experimental output operators add oscillators, envelopes, independent metronomes, pulse dividing, etc.
- **NEW**: [Full List of Methods Found and Maintained Here](https://github.com/bpcmusic/telex/blob/master/commands.md)

## v1.2.1
- **NEW**: Just Friends ops: `JF.GOD`, `JF.MODE`, `JF.NOTE`, `JF.RMODE`, `JF.RUN`, `JF.SHIFT`, `JF.TICK`, `JF.TR`, `JF.TUNE`, `JF.VOX`, `JF.VTR`

## v1.2
- **NEW**: Ansible support added to ops: `CV`, `CV.OFF`, `CV.SET`, `CV.SLEW`, `STATE`, `TR`, `TR.POL`, `TR.PULSE`, `TR.TIME`, `TR.TOG`
- **NEW**: `P.RM` will also return the value removed
- **NEW**: `ER` op
- **IMP**: a `TR.TIME` of 0 will disable the pulse
- **IMP**: `O.DIR` renamed to `O.INC`, it's the value by which `O` is *incremented* when it is accessed
- **IMP**: `IF`, `ELIF`, `ELSE` status is reset on each script run
- **IMP**: key repeat now works for all keypresses
- **FIX**: `FLIP` won't interfere with the value of `O`
- **FIX**: the `O` op now returns it's set value *before* updating itself
- **FIX**: the `DRUNK` op now returns it's set value *before* updating itself
- **FIX**: `P.START` and `P.END` were set to 1 when set with too large values, now are set to 63
- **FIX**: `CV.SLEW` is correctly initialised to 1 for all outputs
- **FIX**: several bugs where pattern length wasn't updated in track mode
- **FIX**: fixed `[` and `]` not updating values in track mode

## v1.1
- **NEW**: USB flash drive read/write
- **NEW**: `SCRIPT` op for scripted execution of other scripts!
- **NEW**: `MUTE` and `UNMUTE` ops for disabling trigger input
- **NEW**: hotkeys for `MUTE` toggle per input (meta-shift-number)
- **NEW**: screen indication in live mode for `MUTE` status
- **NEW**: `SCALE` op for scaling number from one range to another
- **NEW**: `JI` op just intonation helper
- **NEW**: `STATE` op to read current state of input triggers 1-8 (low/high = 0/1)
- **NEW**: keypad executes scripts (works for standalone USB keypads and full-sized keyboards)
- **NEW**: `KILL` op clears delays, stack, CV slews, pulses
- **NEW**: hotkey meta+ESC executes `KILL`
- **NEW**: `ABS` op absolute value, single argument
- **NEW**: `FLIP` op variable which changes state (0/1) on each read
- **NEW**: logic ops: `AND`, `OR`, `XOR`
- **NEW**: `O` ops: `O.MIN`, `O.MAX`, `O.WRAP`, `O.DIR` for counter range control
- **NEW**: `DRUNK` ops: `DRUNK.MIN`, `DRUNK.MAX`, `DRUNK.WRAP` for range control
- **NEW**: `TR.POL` specifies the polarity of `TR.PULSE`
- **NEW**: if powered down in tracker mode, will power up in tracker mode
- **IMP**: `TR.PULSE` retrigger behaviour now predictable
- **IMP**: mode switch keys more consistent (not constantly resetting to live mode)
- **FIX**: bug in command history in live mode
- **FIX**: `EXP` op now exists
- **FIX**: `P` and `PN` parse error
- **FIX**: possible crash on excess length line entry
- **FIX**: `CV` wrapping with negative `CV.OFF` values
- **FIX**: INIT script executed now on keyboardless scene recall
- **FIX**: `Q.AVG` overflow no more
- **FIX**: `P.PUSH` will fully fill a pattern
- **FIX**: `CV.SET` followed by slewed CV in one command works
- **FIX**: `DEL 0` no longer voids command

## v1.0
- Initial release
