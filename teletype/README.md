# Orca Seaflake for Teletype · Alt (beta)

> **Alt version.** `alt`+`o` (`option`+`o` on a Mac keyboard) switches between Orca Seaflake and the **stock Teletype screens** (live, edit, pattern, scene read / write), from any screen and back. To make room it keeps **16 Teletype scenes** instead of 32, and has **no stock Teletype help**. Orca keeps playing while Teletype is on show; the screensaver covers both sides. The Orca-only version is on [`main`](https://github.com/femifleming/orca_seaflake/tree/main/teletype) ([release](https://github.com/femifleming/orca_seaflake/releases/tag/teletype-v0.1.0-beta)).
>
> **Beta.** This firmware runs on real Teletypes (I²C to the Disting EX and the ER-301 is confirmed on hardware, and the stippled screensaver runs clean), but the newest parts have only been tested in the emulator so far: the four dreams, and the **Just Friends** support, which is **in beta**: it is untested on a real Just Friends (testers welcome). Back up your Teletype scenes before flashing, and keep the stock firmware at hand (see [Going back](#going-back-to-the-stock-teletype-firmware)). Reports are welcome in the [issues](https://github.com/femifleming/orca_seaflake/issues).

Firmware that turns the [monome Teletype](https://monome.org/docs/teletype/) into [Orca Seaflake](../README.md): the Orca grid, song mode and patches, running on the module itself. Notes, triggers and CV go out of the Teletype's jacks, through a **Disting EX** (MIDI and its functions), to an **ER-301**, and to **Just Friends**. One Mac app, **Orca Seaflake TT Alt**, is the emulator, the flasher and the help.

- **Manual:** [`docs/manual/Orca Seaflake TT Manual.pdf`](docs/manual/): every operator, key and screen, the flashing chapter, and how to go back to the stock firmware
- **Firmware:** [`dist/orca-seaflake-teletype.hex`](dist/) with its SHA-256 next to it
- **Low-power firmware:** [`dist/orca-seaflake-teletype-lowpower.hex`](dist/): every feature (alt+o included), for cases whose **+12 V** rail is short (the Teletype draws nothing from +5 V). The screen runs dimmer (about 40 % OLED current) and the CPU sleeps between events; ctrl+I shows LOW POWER BUILD. Use it if the module glitches in a full case but works with fewer modules. Flash it like the normal .hex (in Terminal, use its file name)
- **App:** Orca Seaflake TT Alt for macOS (Apple Silicon): emulator, flasher (`cmd`+`F`) and help (`cmd`+`/`)

## Install

You need a Mac with Apple Silicon for the app. The firmware itself can be flashed from any computer with `dfu-programmer` (see [From Terminal](#step-2-flash)).

### 1. Download

From the [Teletype alt beta release](https://github.com/femifleming/orca_seaflake/releases/tag/teletype-alt-v0.1.0-beta):

| File | What |
|---|---|
| `Orca-Seaflake-TT-Alt-macOS-arm64.zip` | the Mac app: emulator, flasher and help (the firmware is inside it) |
| `orca-seaflake-teletype-alt.hex` | the firmware on its own, for flashing from Terminal |
| `Orca-Seaflake-TT-Alt-Manual.pdf` | the manual |

Or with Homebrew: `brew install --cask femifleming/tap/orca-seaflake-tt-alt`

### 2. Open the app

1. Unzip `Orca-Seaflake-TT-Alt-macOS-arm64.zip` and drag **Orca Seaflake TT Alt** into **Applications**.
2. The app is not notarized by Apple, so the first time macOS says it can't be opened. Open **System Settings > Privacy & Security**, scroll down to the message about Orca Seaflake TT Alt and click **Open Anyway**. Or run this once in Terminal:

```sh
xattr -dr com.apple.quarantine "/Applications/Orca Seaflake TT Alt.app"
```

3. The app opens playing a patch in the emulator. `cmd`+`/` is the help.

### 3. Install dfu-programmer

The flasher uses [dfu-programmer](https://github.com/dfu-programmer/dfu-programmer). With [Homebrew](https://brew.sh): `brew install dfu-programmer`. The Flash window checks for it and offers to run this for you.

### 4. Back up, then flash the Teletype

1. Put a USB stick in the running Teletype, turn PARAM to **WRITE TO USB** and press the front-panel button: your scenes are saved as `tt00s.txt`–`tt31s.txt`. This version keeps 16 scenes: after flashing, READ FROM USB brings back `tt00s`–`tt15s`; scenes 17–32 stay on the stick only.
2. Switch the case off. Hold the Teletype's front-panel button, switch the case on, hold one more second, let go. The screen stays dark: it is in its bootloader.
3. Connect the Teletype's USB port to the Mac with a USB A-to-A cable (a C-to-A adapter is fine).
4. In the app press `cmd`+`F` and press **FLASH ORCA SEAFLAKE** once the status turns cyan. Wait for DONE (about 20 seconds).
5. Unplug the cable. On the first start the screen asks to prepare the scene memory: press the front-panel button once. The grid appears and starts playing.

The full steps, with every message you might see, are under [Flashing](#flashing) below and in the manual. On the Disting EX, set **Settings > I2C address = 65** (it leaves the factory at 49, the ER-301's address).

## What it does

- **The module is an Orca.** It boots into an 80 × 40 grid **and plays at once**: the last patch at its saved tempo, or the song that was playing when you last saved. The screen shows 21 × 7 cells and scrolls with the cursor. All Orca and Seaflake operators work as in Seaflake: the same patches give identical grids, frame by frame.
- **The bottom line names the cell under the cursor**: the operator, or the inlet of the operator that reads it (`D RATE`, `: OCTAVE`, `EX PARAM HI`, `JF VELOCITY`).
- **MIDI through the Disting EX.** `: % ! ? ( ) @` send `EX.M.N#`, `EX.M.NO#`, `EX.M.CC#` and `EX.M.PB`.
- **Triggers and CV, three plain operators:** `-` trigger only (output), `_` trigger + CV (output, octave, note), `'` CV only. Every pulse is `$pulse` long. `}` is `@`'s shaped run as pulses only; `` ` `` (loadbang) bangs once when a patch loads. Every MIDI operator also has a trigger + CV twin: `" / \ [ ] {`. Outputs `1`–`4` are the Teletype's TR/CV jacks, `a`–`z` are ER-301 ports; pitch is 1 V/octave.
- **The Disting EX** (address 0x41): **N** (`>n`: octave, note, velocity) plays its voices (EX.NOTE); **N@** (`>@`) adds `@`'s shape, repeats, duration and pulse width. `>p` sets a parameter like `EX.P`, `>v` like `EX.PV`, `>l` loads a preset; every other Disting EX function is `>` + a letter. **`<` plays the ER-301** (0x31). All 96 Disting EX and ER-301 functions Teletype has are reachable.
- **Just Friends (beta, untested on hardware):** `ctrl`+`i` shows `JUST FRIENDS 70 FOUND` when it answers on I²C. `,` + a letter, with Orca operand cells, reaches every Teletype `JF.*` op: `,n` notes (`JF.NOTE`), `,@` shaped runs, `,p` 12-voice poly over two Just Friends, `,v` voices, `,r` / `,t` triggers, `,s` shift, `,u` run, `,m` synthesis mode, `,j` just ratios, `,q` quantize, `,k` Geode clock, `,g` reads its knobs. Pitch `4C` = 0 V (Just Friends' own tuning), velocity `0`–`g` = 0–8 V. Other operators feed the cells, so `T`, `R` or `Z` play melodies and dynamics on it.
- **Orca and Teletype, `alt`+`o`:** the stock Teletype screens (live, edit, pattern, scene read / write) with their stock keys, one keystroke away. `alt`+`o` always goes back to the grid, from any Teletype screen, and back to the Teletype screen you left. 16 scenes, no Teletype help. Orca keeps playing behind Teletype; the Teletype scripts, metro and triggers run on both sides. Power-on is always the Orca grid.
- **Tempo:** `$bpm:120` and `$apm:30` anywhere on the grid.
- **32 patches and 8 songs** saved on the module (`ctrl`+`s` / `ctrl`+`o`, song view `ctrl`+`t`), each patch with its tempo; song mode as in Seaflake. WRITE TO USB saves them all to `SFPATCH.TXT`, READ FROM USB loads them back; the app imports and exports the same file.
- **I²C scan:** finds the Disting EX and the ER-301 at power-on (`ctrl`+`i` to see it, with the bus health and message counts).
- **A screensaver from four dreams:** after 5 idle minutes the screen shows one of four dreams from [femishonuga.com](https://femishonuga.com/dd.html), picked at random each time (never the same one twice in a row), drawn as sparse line art light enough for the module's OLED. `alt`+`d` starts one. Videos: [golden geometries](docs/dreams/golden_geometries.mp4) (sunlight through shattered windows), [the maintenance train](docs/dreams/tunnel_bell.mp4) (a train and its bell in a tunnel), [the street becomes a lake](docs/dreams/rain_lake.mp4) (rain, lily pads, a car on the water), [the elevator into the night](docs/dreams/night_elevator.mp4).

New operator glyphs, all on a standard keyboard and none used by Orca or Seaflake: `" / ' \ [ ] { } - _ ` > < ,`.

## Quick start

1. Back up your Teletype scenes to a USB stick (flashing erases the module).
2. Open **Orca Seaflake TT Alt**, press `cmd`+`F`, and follow the steps in the window (also below).
3. On the module: type operators, `space` to play, `alt`+`h` for help, `alt`+`o` for the Teletype screens.

```
$bpm:120
..C4....
D1.4TCEGb
.:04.f2.        an arpeggio on MIDI channel 1 through the Disting EX
```

## The app

`/Applications/Orca Seaflake TT Alt.app` runs the firmware's own C code on the Mac. The OLED is drawn pixel for pixel; around it: TR 1–4, CV 1–4, the 26 ER-301 ports, and a log of every I²C message with the Teletype command that sent it and the MIDI the Disting EX would play (also out of a virtual MIDI port **Orca Seaflake TT Alt**). It starts playing, like the module.

| Window | Key | What |
|---|---|---|
| Flash the Teletype | `cmd`+`F` | finds dfu-programmer (offers `brew install dfu-programmer`), finds the Teletype's bootloader on USB, reads the module back, shows the **ports table**, flashes Orca Seaflake or the stock firmware with live output, and carries the steps below |
| Help | `cmd`+`/` | every operator (glyph, name, inputs, outputs, what it does), every key, the flashing steps; searchable |
| Import / Export | `cmd`+`I` / `cmd`+`E` | `SFSCAN.TXT` + `SFPATCH.TXT` from / to a USB stick: the module's scan, patches and songs |

Patches and songs live in `~/Library/Application Support/Orca Seaflake Teletype/`.

## Flashing

<!-- flashing:start -->
### Flashing Orca Seaflake onto the Teletype


Orca Seaflake replaces the Teletype firmware. Flashing erases the whole chip: scenes, Orca patches and calibration. The bootloader itself can't be erased, so a failed flash is never fatal: put the module back in its bootloader and try again.


#### What you need

- The Teletype in its case, powered from the case as usual.
- A USB A-to-A cable (the Teletype's USB port is type A; on a Mac with USB-C use a C-to-A adapter). Some A-to-A cables only carry power: if nothing is found, try another cable.
- dfu-programmer on the Mac: in Terminal, run  brew install dfu-programmer (the Flash window checks for it and can run this for you).
- The firmware: orca-seaflake-teletype.hex. It is inside the app (Orca Seaflake TT Alt) and in dist/ next to its SHA-256 checksum.

#### Before you flash: back up

1. Put a USB stick in the Teletype while it is running. The USB menu appears (on the stock firmware and on Orca Seaflake alike).
2. Turn PARAM to WRITE TO USB and press the front-panel button. It saves all the scenes as tt00s.txt - tt31s.txt (this alt firmware keeps 16: tt00s - tt15s). If Orca Seaflake is already on the module it also writes SFSCAN.TXT (the I2C scan) and SFPATCH.TXT (patches + songs), which the emulator imports with cmd+I.
3. The Disting EX and ER-301 can stay on the I2C bus while you flash.

#### Step 1: put the Teletype in its bootloader

1. Switch the case off.
2. Press and hold the Teletype's front-panel button (the small SCENE RECALL button next to the USB port). Keep holding it and switch the case on. Hold for one more second, then let go.
3. Connect the Teletype's USB port to the Mac with the A-to-A cable.
4. In bootloader mode the screen stays dark. That is normal. The Mac sees an "Atmel DFU" device (USB 03eb:2ff6).

#### Step 2: flash


In the app (Orca Seaflake TT Alt, in /Applications) press cmd+F: the Flash window. It has three steps.

1. CONNECT: the status line turns from amber "Teletype not connected" to cyan "Teletype ready to flash" within two seconds of connecting the bootloader. If dfu-programmer is missing, one line says so with an INSTALL button (it runs brew install dfu-programmer).
2. SETTINGS: the addresses are fixed in the firmware: the Disting EX at 65 (0x41), the ER-301 at 0x31. On the Disting EX set Settings > I2C address = 65 (it leaves the factory at 49 = 0x31, the ER-301's address: change it first). "load Disting EX preset at power-on" (optional): tick it and type a preset number, and the module sends EX.PRESET once at every power-on, as soon as the Disting EX answers. Unticked, nothing is sent. A loadbang (`) in a patch does the same per patch.
3. FLASH ORCA SEAFLAKE: press it and confirm. A progress bar shows erase, flash and start; don't unplug anything until it says DONE (about 20 seconds).

Everything else (the pulse length, the module read-back, the firmware's size and SHA-256, the raw log) is under DETAILS, closed by default. "Restore stock Teletype..." is the way back (below).


From Terminal (what the app runs):


```sh
cd ~/Downloads     # the folder with orca-seaflake-teletype.hex (in this repository: dist/)
dfu-programmer at32uc3b0512 erase
dfu-programmer at32uc3b0512 flash orca-seaflake-teletype.hex --suppress-bootloader-mem
dfu-programmer at32uc3b0512 start
```


#### Step 3: first start

1. After START the Teletype restarts by itself. Unplug the USB cable.
2. The screen says SCENES WILL BE OVERWRITTEN / PRESS ONLY IF YOU ARE UPDATING FIRMWARE. Press the front-panel button once. That prepares the scene memory (the normal Teletype first-start step); Orca's patch memory is prepared by itself.
3. The Orca Seaflake grid appears and the bottom line shows what the I2C scan found, for example FOUND DISTING EX. ctrl+I shows the Disting EX (0x41), the ER-301 (0x31), Just Friends (0x70), the bus and the message count; enter looks again.
4. To get your Teletype scenes (their scripts run with $script:N and F1-F10) back: USB stick in, turn PARAM to READ FROM USB, press the front-panel button. The grid comes back by itself.
5. Esc never leaves the grid; alt+o (option+o) switches to the stock Teletype screens and back. On the grid the front-panel button opens LOAD PATCH (PARAM picks the slot, the button again loads it). To flash again, power off and use step 1: the bootloader is only reached by holding the button at power-on.

#### If something goes wrong

- NO TELETYPE BOOTLOADER / "no device present": the module isn't in its bootloader. Power off, hold the front-panel button while powering on, then connect the cable. Try another cable or USB port; adapters and hubs sometimes hide the device.
- "dfu-programmer: command not found": brew install dfu-programmer (Homebrew from brew.sh first).
- Flashing stops halfway: nothing is damaged. Put the module back in its bootloader and flash again.
- The screen stays dark after DONE: switch the case off and on (without holding the button).
- ctrl+I shows -- for the Disting EX: check its I2C cable (GND to GND at every end) and power, and that its I2C address (Settings) is 65, then press enter there. After a cold start give the ER-301 and the Disting EX time to boot: the module keeps looking for them for 2 minutes.
- The BUS line on the ctrl+I screen: PULLUPS OK is right. NO PULLUPS / NO SDA PULLUP / NO SCL PULLUP: nothing on the bus pulls that line up (the Teletype's own weak pull-ups are on, which is enough for a short bus; otherwise switch on a module's or the busboard's pull-ups). SDA HELD LOW / SCL HELD LOW: something holds the line: a reversed cable or a module stuck mid-message.
- The ER-301 line says -- TRY <P1: the ER-301 is not asked during the scan (it does not answer reads); send it anything (D4 with .<p01 one cell right below it) and it turns FOUND.
- Notes don't play but the Disting EX is FOUND: watch the bottom line while the patch runs. I2C 41 NACK CMD 6A = the Disting EX refused that message; I2C 41 BUSY = it timed out (the bus then restarts by itself). The last ctrl+I line counts OK and FAIL messages since power-on.

### Going back to the stock Teletype firmware

1. Download the latest release from github.com/monome/teletype/releases: the zip contains teletype.hex (in the Flash window, "Restore stock Teletype..." picks the downloaded .zip or .hex).
2. Put the Teletype in its bootloader exactly as in step 1 above.
3. Flash it the same way: in the Flash window press "Restore stock Teletype...", pick the file, then FLASH STOCK; or in Terminal:

```sh
cd ~/Downloads
dfu-programmer at32uc3b0512 erase
dfu-programmer at32uc3b0512 flash teletype.hex --suppress-bootloader-mem
dfu-programmer at32uc3b0512 start
```

4. First start: press the front-panel button when it asks, then READ FROM USB to restore your scenes from the backup. Orca patches are not kept by the stock firmware; SFPATCH.TXT on the stick keeps them for the emulator and for the next time you flash Orca Seaflake.

#### What is erased, what is kept

- Teletype scenes: erased by flashing. Back up with WRITE TO USB, restore with READ FROM USB.
- Orca patches and songs: erased by flashing. WRITE TO USB saves them to SFPATCH.TXT.
- Teletype calibration: back to the default.
- The bootloader: never touched (--suppress-bootloader-mem).

<!-- flashing:end -->

## Keys

<!-- keys:start -->
| Key | Where | Does |
|---|---|---|
| `space` | grid | play / stop (the module and the emulator start playing by themselves) |
| `arrows` | grid | move the cursor; the screen scrolls over the 80 x 40 grid |
| `alt + arrows` | grid | leap 8 cells |
| `shift + arrows` | grid | select a block |
| `page up / page down / home / end` | grid | a screen up / down, to the left / right edge |
| `any character` | grid | write it at the cursor; a letter alone is lowercase, with shift uppercase (as in Orca) |
| `caps lock` | grid | swaps that: letters alone come out uppercase (a small A by the tempo shows it) |
| `enter` | grid | insert mode on / off (the cursor moves right after each character) |
| `backspace / delete` | grid | erase the cell or the selection |
| `ctrl + c / x / v` | grid | copy / cut / paste a block (up to 1024 cells) |
| `ctrl + z` | grid | undo the last change (again = redo) |
| `ctrl + a` | grid | select the whole grid |
| `ctrl + f` | grid | step one frame while stopped |
| `ctrl + r` | grid | frame counter back to 0 |
| `ctrl + n` | grid | new, empty grid (undo brings it back) |
| `alt + [ / ]` | grid | BPM -1 / +1 |
| `ctrl + [ / ]` | grid | BPM -10 / +10 |
| `ctrl + s` | grid | SAVE PATCH: pick one of 32 slots with the arrows, enter (enter again to overwrite) |
| `ctrl + o` | grid | LOAD PATCH: pick a slot, enter; the clock keeps running, ctrl + z brings the old grid back |
| `ctrl + t` | grid | song mode (the song view) |
| `ctrl + i` | grid | I2C: the Disting EX at 0x41 (algorithm, preset), the ER-301 at 0x31, the bus (pull-ups) and the message count; enter looks again |
| `alt + h` | grid | help on the OLED: keys, every operator and selector |
| `alt + d` | grid | start the screensaver now (any key, the knob or the front button wakes it) |
| `esc` | grid | close a picker, the song view, the scan or help; clear a selection. Esc never leaves Orca Seaflake (alt + esc, tab, F11, F12 and alt + F1-F10 do nothing there) |
| `alt + o` | anywhere | switch between Orca Seaflake and the stock Teletype screens (option + o on a Mac keyboard). Back on the Teletype side you land on the screen you left; Orca keeps playing while Teletype is on show |
| `tab` | Teletype | live -> edit -> pattern -> live |
| `esc / alt + esc` | Teletype | scene read (esc again goes back) / scene write: 16 scenes |
| `alt + F1-F8 / F9 / F10` | Teletype | edit script 1-8 / metro / init |
| `F11 or num lock / F12 or print screen` | Teletype | pattern screen / live screen |
| `alt + h` | Teletype | nothing: the stock Teletype help is not in this firmware (it does not fit); Orca's help (alt + h on the grid) is |
| `front button` | module | Orca: LOAD PATCH, the picker opens; turn PARAM (or use the arrows) to pick a slot, press the button again to load it; esc closes. Teletype: scene read, PARAM picks, press again to load |
| `PARAM knob` | module | picks the slot while LOAD PATCH is open, the scene in scene read, the value in the pattern screen; always the Teletype PARAM value for scripts |
| `F1-F8 / F9 / F10` | both sides | run Teletype script 1-8 / metro / init of the scene loaded at power-on (as $script:N); ctrl + F1-F8 mute a script's trigger input, ctrl + F9 metro on / off |
| `up / down, left / right` | song view | choose the slot / the field (PAT, LEN, MUL, BPM) |
| `0-9` | song view | type a number into the field (commits when you move) |
| `+ / - (or ] / [)` | song view | field up / down |
| `enter` | song view | this slot plays the patch on the grid (save the patch first) |
| `backspace` | song view | clear the slot |
| `l` | song view | load the slot's patch and tempo onto the grid |
| `space` | song view | play / stop the song |
| `m / r` | song view | mode ORD -> REV -> RND / loop on - off |
| `ctrl + s / ctrl + o` | song view | save / load the song (8 song slots) |
| `ctrl + n` | song view | new, empty song |
| `ctrl + t / esc` | song view | back to the grid (the song keeps playing) |
| `WRITE TO USB` | USB stick in the Teletype | writes the 16 scenes plus SFSCAN.TXT (the scan) and SFPATCH.TXT (all patches and songs) to the stick |
| `READ FROM USB` | USB stick in the Teletype | reads the scenes back, and the patches and songs from SFPATCH.TXT if the stick has one |
| `cmd + F` | emulator | the Flash window: ports check, flashing, restore to stock |
| `cmd + I` | emulator | import SFSCAN.TXT + SFPATCH.TXT from a USB stick in /Volumes (the module's scan, patches and songs) |
| `cmd + E` | emulator | export SFSCAN.TXT + SFPATCH.TXT to a folder or USB stick (READ FROM USB on the module loads them) |
| `cmd + /` | emulator | this help: keys, operators, flashing |
| `cmd + D` | emulator | start the screensaver now (any key wakes it) |
| `cmd + L` | emulator | clear the I2C log |
| `cmd + P` | emulator | pause / resume the I2C log |
| `cmd + Q` | emulator | quit (the patches and songs are already saved in ~/Library/Application Support/Orca Seaflake Teletype) |

<!-- keys:end -->

## Operators

<!-- ops:start -->
### ORCA

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `A` | add | left, right | below | left + right (base-36 glyph arithmetic) |
| `B` | subtract | left, right | below | \|right - left\| |
| `C` | clock | left rate (min 1), right modulo (default 8) | below | frame / rate mod modulo |
| `D` | delay | left rate, right modulo (default 8) | bang below | bangs every rate x modulo frames |
| `E` | east | - | - | moves east one cell a frame; bangs when blocked |
| `N` | north | - | - | moves north one cell a frame; bangs when blocked |
| `S` | south | - | - | moves south one cell a frame; bangs when blocked |
| `W` | west | - | - | moves west one cell a frame; bangs when blocked |
| `F` | if | left, right | bang below | bangs when left and right are the same glyph |
| `G` | generator | left 3 = x, left 2 = y, left 1 = length; cells on the right | cells at x, y+1 | copies the cells on its right to the offset |
| `H` | halt | - | - | stops the operator below from running |
| `I` | increment | left step (default 1), right modulo | below | adds step to the value below, mod modulo |
| `J` | jumper | above | below | copies the glyph above to below |
| `Y` | jymper | left | right | copies the glyph on its left to its right |
| `K` | konkat | left = count; variable names on the right | below each name | reads that many variables, writes their values below |
| `L` | lesser | left, right | below | the smaller of left and right |
| `M` | multiply | left, right | below | left x right |
| `O` | read | left 2 = x, left 1 = y | below | the cell at that offset |
| `P` | push | left 2 = key, left 1 = length, right = value | row below | writes the value into one cell of a row below, keyed |
| `Q` | query | left 3 = x, left 2 = y, left 1 = length | cells below | copies that many cells from the offset to below |
| `R` | random | left min, right max (default 36) | below | a random value in the range |
| `T` | track | left 2 = key, left 1 = length; cells on the right | below | picks one of the cells on its right (the picked cell is lit) |
| `U` | uclid | left steps (default 1), right length (default 8) | bang below | Euclidean rhythm bangs |
| `V` | variable | left = name to write, right = value (or name to read) | below when reading | writes or reads a variable |
| `X` | write | left 2 = x, left 1 = y, right = value | cell at x, y+1 | writes the value at the offset |
| `Z` | lerp | left rate (default 1), right target | below | moves the value below toward the target |
| `*` | bang | - | the operators touching it | a one-frame bang, then disappears |
| `#` | comment | - | - | stops the rest of the row (up to the next #) |
| `a-z` | passive | - | - | a lowercase operator only runs when banged (as in Orca) |

### SEAFLAKE

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `^` | probability | right = chance (default i = 18/35) | bang below | passes a bang below at random |
| `&` | phase clock | left rate (default 1), right length (default 4), above = step (default 0) | bang below | bangs on that step of the cycle |
| `~` | burst | right: repeats (default 4), division, multiplication | bangs below | when banged, plays that many bangs one frame x div / mul apart, in real time |
| `\|` | mute block | left 2 width (default 8), left 1 height (default 4) | the block below | each bang hashes the block out with # or brings it back |
| `+` | scale | left = variable name (default 0), right 12 cells = 1 / 0 for C C# D ... B (default C major) | the variable | defines a scale for ) and ] |
| `;` | udp | cells on the right | - | Seaflake's UDP message (Pilot): kept so patches load, sends nothing on the module |
| `=` | osc | cells on the right | - | Seaflake's OSC message: kept so patches load, sends nothing on the module |

### MIDI VIA DISTING EX

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `:` | note | channel 0-f, octave 0-8, note, velocity (default f), length in frames (default 1) | EX.M.N# ... EX.M.NO# | note on through the Disting EX's MIDI out; note off when the length is up |
| `%` | mono note | same as : | EX.M.N# / EX.M.NO# | a new note on the channel cuts the old one |
| `!` | CC | channel, controller 0-z, value 0-z (scaled to 0-127) | EX.M.CC# | control change |
| `?` | pitch bend | channel, LSB 0-z, MSB 0-z (each scaled to 0-127) | EX.M.CH + EX.M.PB | pitch bend (the EX.M.CH channel is put back after) |
| `(` | just note | channel (default 0), octave (default 4), note, velocity, length | EX.M.PB + EX.M.N# | a bend to the just-intonation ratio, the note, then the bend is reset |
| `)` | scale note | channel, octave, degree (1-based), scale variable (from +), velocity, length | EX.M.N# | the note at that degree of the scale |
| `@` | shaped note | channel, octave, note, velocity, shape 1-4, repeats, duration in frames, pulse width | EX.M.N# / EX.M.NO# run | a run of notes spaced by the shape, in real time |

### TRIGGER + CV

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `"` | trig note | output 1-4 / a-z, octave, note, velocity, length in pulses (default 1 = 20 ms) | TR + CV (TT jacks) or SC.TR.P + SC.CV (ER-301) | pitch CV at 1 V/octave + a pulse |
| `/` | gate note | output, octave, note, velocity, length in frames | TR + CV / SC.TR + SC.CV | pitch CV + a gate held high for that many frames |
| `'` | CV level | output, slew in pulses, level 0-z = 0-10 V | CV / SC.CV.SLEW + SC.CV | sets a CV level only (for modulation); the trigger is not fired |
| `\` | CV bend | output, LSB, MSB (as ?) | CV / SC.CV | bends that output's pitch CV by up to +-2 semitones; CV only, no trigger |
| `[` | just trig | output (default 1), octave (default 4), note, velocity, length in pulses | TR + CV / SC.TR.P + SC.CV | pitch CV tuned to the just-intonation ratio + a pulse |
| `]` | scale trig | output, octave, degree, scale variable, velocity, length in pulses | TR + CV / SC.TR.P + SC.CV | pitch CV for that scale degree + a pulse |
| `{` | shaped trig | output, octave, note, velocity, shape, repeats, duration, pulse width | TR + CV / SC.TR.P + SC.CV | the same shaped run as @, as pulses |
| `-` | trigger | output 1-4 / a-z | TR (TT jacks) or SC.TR.TIME + SC.TR.PULSE (ER-301) | trigger only: one pulse, $pulse ms long; no CV, no other inlets. Bang it from beside: D4 then .-1 on the next row |
| `_` | trigger + CV | output, octave (default 4), note | TR + CV / SC.TR.PULSE + SC.CV | pitch CV at 1 V/octave + one pulse ($pulse ms); only the inlets needed (no velocity, length or slew) |
| `}` | shaped trigger | output (default 1), shape 1-4, repeats, duration in frames, pulse width (shape 4 spacing) | TR / SC.TR.PULSE | @'s run of steps (same timing, repeats and division) as pulses only: no CV, octave, note, velocity or channel; each pulse $pulse ms |
| ``` | loadbang | - | bang below | bangs once on the first frame after a patch loads (power-on, ctrl+o, $patch:N, a song changing patch); at power-on it waits until the Disting EX has answered (up to 30 s). Put a Disting operator on the next row one cell right, where the bang lands beside it: ` then .>l05 |

### DISTING EX

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `>` | disting ex | a function letter, then its operand cells (the >x rows) | I2C to the Disting EX the scan found (EX.* ops) | one Disting EX function when banged; >x is one operator: > then the letter. No unit or channel cells: the Disting EX is found by the I2C scan. Readers write their answer below |
| `>n` | N (note) | octave (default 4), note, velocity (default f) | EX.NOTE, then EX.NOTE.O one frame later | plays a note on the Disting EX's voices: octave, note and velocity only (typed >N or >n) |
| `>@` | N@ (note with @) | octave, note, velocity, shape 1-4, repeats, duration, pulse width | EX.NOTE / EX.NOTE.O run | N with everything @ has except the channel: a shaped run of notes at @'s exact times |
| `>p` | parameter (EX.P) | param W, value W, side | below when reading | parameter to an exact value, as EX.P (EX.PARAM / P1 / P2); value empty = read it |
| `>v` | parameter level (EX.PV) | param W, lvl, side | - | parameter to a level 0-z across its min..max, as EX.PV (PV1 / PV2) |
| `>i` | parameter min | param W, side | below | read the parameter's minimum (EX.MIN...) |
| `>j` | parameter max | param W, side | below | read the parameter's maximum (EX.MAX...) |
| `>l` | load preset | preset W, side | below when reading | load a preset (EX.PRESET / PRE1 / PRE2); preset empty = read the current one |
| `>s` | save preset | preset W, side | - | save as preset (EX.SAVE / SAVE1 / SAVE2) |
| `>r` | reset | - | - | reset the preset (EX.RESET) |
| `>a` | algorithm | n, side, n (side 3: both) | below when reading | algorithm (EX.ALG / A1 / A2 / A12); empty = read it |
| `>z` | z knob | knob 1 / 2, lvl | - | Z knob to a level (EX.Z1 / Z2); level empty = give the knob back (EX.ZO1 / ZO2) |
| `>c` | controller | n, lvl | - | I2C controller n to a level (EX.CTRL) |
| `>k` | note channel | n | - | the channel N and >o use (EX.CH; 1 until changed) |
| `>d` | note on, channel | n, N, vel | - | note on for one channel (EX.N#; empty channel = EX.NOTE) |
| `>o` | note off | n, N | - | note off (EX.NOTE.O / EX.NO#) |
| `>x` | all notes off | - | - | EX.ALLOFF |
| `>w` | voice | n, N, vel | - | play voice n (EX.VOX); velocity 0 = voice off (EX.VOX.O) |
| `>f` | voice pitch | n, N | - | set voice n's pitch only (EX.VOX.P) |
| `>t` | trigger voice | n, vel | - | trigger voice n, for SD Triggers (EX.TV / EX.T) |
| `>e` | record | n (1 / 0) | - | WAV recorder record start / stop (EX.REC) |
| `>y` | play | n (1 / 0) | - | WAV recorder play start / stop (EX.PLAY) |
| `>g` | augustus pitch | N | - | Augustus Loop pitch (EX.AL.P) |
| `>h` | augustus clock | - | - | Augustus Loop clock (EX.AL.CLK) |
| `>q` | looper | act, n | below for s / i / j | looper n: r record, p play, c clear, v reverse, d octave down; s state, i reversed?, j down? (EX.LP.*) |
| `>m` | midi out | act, W, W | - | MIDI out: p program, c clock, s start, t stop, o continue, h channel, n note on, f note off, e CC, w bend (EX.M.*) |
| `>b` | select bus | act, W, W | - | Select Bus: n note on, f note off, e CC, w bend, p program, c clock, s start, t stop, o continue, h channel (EX.SB.*) |

### ER-301

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `<` | er-301 | selector cell, then port (W) and operands (see the < selectors) | I2C to the ER-301 (SC.* ops) | runs one ER-301 function when banged |
| `<p` | pulse | port | SC.TR.PULSE | pulse the trigger |
| `<t` | trigger | port, n | SC.TR | set the trigger to 0 / 1 |
| `<g` | toggle | port | SC.TR.TOG | toggle the trigger |
| `<l` | polarity | port, n | SC.TR.POL | trigger polarity |
| `<m` | pulse ms | port, W | SC.TR.TIME | pulse length in ms (0k = 20 ms) |
| `<c` | cv level | port, lvl | SC.CV | CV level, 0-z = 0-10 V |
| `<n` | cv note | port, N | SC.CV | CV pitch from a note, 1 V/octave (4C = 4 V) |
| `<s` | cv set | port, lvl | SC.CV.SET | CV level now, without slew |
| `<o` | cv offset | port, lvl | SC.CV.OFF | CV offset |
| `<w` | cv slew | port, W | SC.CV.SLEW | CV slew in ms |

### JUST FRIENDS

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `,` | just friends | a function letter, then its operand cells (the ,x rows) | I2C to Just Friends at 0x70 (JF.* ops) | one Just Friends function when banged, as Teletype's JF.* op. Notes: octave 4 C = 0 V = Just Friends' own pitch (octave 3 a volt down). Velocity 0-g = 0-8 V, empty = a = 5 V. Channel 1-6, 0 = all six, 7-c = the second Just Friends, empty = 1 |
| `,n` | note | octave (default 4), note, velocity (default a) | JF.NOTE | a note on the next free voice (Just Friends in synthesis mode, ,m1); no note off: its envelope ends the note |
| `,@` | note with @ | octave, note, velocity, shape 1-4, repeats, duration, pulse width | JF.NOTE run | JF.NOTE as @'s shaped run of notes, in real time |
| `,p` | poly note | octave, note, velocity | JF.POLY | a note across two Just Friends (12 voices, JF.POLY); ,x starts the count again (JF.POLY.RESET) |
| `,v` | voice | channel, octave, note, velocity | JF.VOX | play one voice at a pitch |
| `,f` | voice pitch | channel, octave, note | JF.PITCH | set one voice's pitch without playing it |
| `,r` | velocity trigger | channel, velocity | JF.VTR | trigger a voice at a velocity |
| `,t` | trigger | channel, state (empty = a pulse) | JF.TR | the voice's trigger input to 1 / 0; state empty = 1, then 0 one $pulse later (a trigger) |
| `,s` | shift | octave, note (4C = 0) | JF.SHIFT | transpose the whole instrument |
| `,u` | run | level 0-z (i = 0 V, 0 = -5 V) | JF.RUN | the RUN voltage |
| `,o` | run mode | 1 / 0 | JF.RMODE | RUN mode on / off |
| `,m` | synthesis mode | 1 / 0 | JF.MODE | synthesis mode on (needed for ,n ,p ,v) / off |
| `,d` | god mode | 1 / 0 | JF.GOD | tuning: A = 432 Hz on / off |
| `,j` | tune | channel, numerator, denominator | JF.TUNE | a voice's just-intonation ratio (e.g. ,j132 = 3/2) |
| `,q` | quantize | divisions 0-w (0 = off) | JF.QT | quantize note timing to a division of the clock (Geode) |
| `,k` | tick | W: 1-48 = beats per measure, 49-255 = BPM | JF.TICK | clock Just Friends' Geode mode |
| `,a` | address | 1 / 2 | JF.ADDR | set the Just Friends' own address (1 = 0x70, 2 = 0x75) |
| `,e` | select | 1 / 2 | JF.SEL | which Just Friends the other , functions talk to |
| `,x` | poly reset | - | JF.POLY.RESET | start JF.POLY's voice count again |
| `,g` | read | a letter: s speed, t mode, r ramp, c curve, f fm, i time, o intone | below | read a setting (Just Friends firmware 4): s 0 shape / 1 sound, t 0 transient / 1 sustain / 2 cycle; knobs as 0-z (0-10 V) |

### COMMANDS

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `$` | command | the text on its right | - | runs a command when banged; $bpm: and $apm: also work without a bang |
| `$bpm:N` | tempo | 60-300 | - | tempo in BPM (4 frames per beat) |
| `$apm:N` | tempo (apm) | 15-75 | - | apm x 4 = bpm, as in Seaflake |
| `$play` | play | - | - | start the clock |
| `$stop` | stop | - | - | stop the clock |
| `$frame:N` | frame | N | - | set the frame counter ($skip:N / $rewind:N move it) |
| `$pulse:N` | pulse | 1-250 | - | trigger pulse length in ms for all trigger operators (default from the ports config, 20) |
| `$script:N` | script | 1-8, 9 = metro, 10 = init | - | run Teletype script N ($s:N) |
| `$patch:N` | patch | 1-32 | - | load patch slot N onto the grid ($p:N), for chaining patches |

### OMITTED

| Glyph | Name | Inputs | Outputs | What it does |
|---|---|---|---|---|
| `TR/CV ops` | teletype scripting | - | - | TR.*, CV.*, M, PN, etc. run inside Teletype scripts ($script:N or F1-F10), never as grid operators; there are no Teletype script screens |

<!-- ops:end -->

## Building

```sh
# firmware (needs Docker; uses the dewb/monome-build toolchain image)
find src libavr32/src module -name '*.o' -delete
docker run --rm --platform linux/amd64 --entrypoint /bin/bash -v "$PWD":/target \
  dewb/monome-build -c 'export PATH=/avr32-tools/bin:$PATH; cd /target/module && make'
cp module/teletype.hex dist/orca-seaflake-teletype.hex
(cd dist && shasum -a 256 orca-seaflake-teletype.hex > orca-seaflake-teletype.hex.sha256)

# the app (Swift + the C core; no SDL needed)
cd emu && make install        # builds "Orca Seaflake TT Alt.app", with the hex and the help tables inside, into /Applications

# manual, dist/README.md and the generated parts of this README
python3 docs/manual/make.py   # needs Google Chrome for the PDF

# tests
cd emu && make test           # 271 end-to-end checks (MIDI and I2C bytes against the stock ops, every operator incl. every JF.* op, scan, startup preset, esc, USB report, every manual example)
tests/seaflake/compare.sh     # the C engine against the original Seaflake JS engine
cd tests && make test         # Teletype's own test suite
```

Flash use: about 254 KB of the 512 KB chip; the program area below the ports block (0x80044C00) has about 28 KB free, then the 37 KB patch/song area and the Teletype's own 256 KB scene area. RAM: about 58 KB of static data and bss of the 96 KB; the rest is heap and the 8 KB stack.

## Where things are

| Path | What |
|---|---|
| `src/seaflake/sf_core.c` | the Orca Seaflake engine (C port of `orca_seaflake/terminal/src/core`) |
| `module/sf_ui.c` | the only mode logic: Orca or its help; keys, front button, knob (firmware and emulator) |
| `docs/reverse-engineering.md` | how the stock firmware, the Disting EX protocol and the ESC bug work (investigation notes) |
| `src/seaflake/sf_ii.c` | MIDI, triggers, CV, `>`, `<` and `,`: sent through the real Teletype `EX.*` / `SC.*` / `JF.*` ops; I²C scan |
| `src/seaflake/sf_config.c` | the ports configuration block at 0x80044C00 (the Flash window edits it in the .hex) |
| `src/seaflake/sf_report.c` | the USB stick files (`SFSCAN.TXT`, `SFPATCH.TXT`), writing and reading back |
| `module/seaflake_mode.c` | grid screen, keys, patch/song pickers, song tracker, scan screen |
| `module/seaflake_help.c` | the help on the OLED (`alt`+`h`); replaces Teletype's op reference to free flash |
| `module/sf_flash.c`, `module/seaflake.lds` | patch and song storage and the last scan, in their own flash area |
| `src/match_token_table.c` | replaces the 64 KB Ragel token matcher; same results, 432 bytes |
| `emu/App.swift`, `emu/Flash.swift`, `emu/tt_emu.c` | the app: emulator chrome, Flash window with the ports check, help; the C core |
| `docs/manual/` | the manual (`body.html` + `make.py` → PDF) and the tables the app's help is built from (`ops.tsv`, `keys.tsv`, `flashing.txt`) |
| `module/dream.c`, `module/dreams/` | the screensaver (includes one dream from `module/dreams/`); `tools/dream_render.c` renders a dream to video and measures its load on the OLED |
| `README-teletype.md` | monome's original Teletype README |

Based on monome/teletype (commit e390bac), GPL-2.0.
