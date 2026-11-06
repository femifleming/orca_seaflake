# Orca Seaflake Terminal

Orca Seaflake running directly in a terminal, on **macOS, Windows 10/11, Linux and Raspberry Pi OS**. It uses the same JavaScript operator engine as the Seaflake desktop app (including `^ & ~ ( ) + @`), with Song Mode and MIDI note output. No Electron, so it is light enough for a Pi over SSH.

## Requirements (all platforms)

- Node.js 18 or later
- A C++ build toolchain, used once to compile the `midi` native module (see your OS below)
- A MIDI destination: a hardware interface, a virtual MIDI port, or a software instrument

If the MIDI module fails to build, the app still starts as an editor and tells you on the status line. Fix the toolchain, then run `npm rebuild midi`.

## Install

### macOS

```sh
xcode-select --install          # once, if you don't have the Command Line Tools
cd terminal
npm install
npm link
orca-seaflake
```

Virtual MIDI: open **Audio MIDI Setup → Window → Show MIDI Studio → IAC Driver**, tick "Device is online".

### Windows 10 / 11

1. Install [Node.js LTS](https://nodejs.org). On the installer's "Tools for Native Modules" page, tick the box to install the build tools (or install "Desktop development with C++" from Visual Studio Build Tools).
2. Use **Windows Terminal** (cmd.exe's old console works but renders worse).
3. In the `terminal` folder:

```powershell
npm install
npm link
orca-seaflake
```

Virtual MIDI: Windows has none built in. Install [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html), create a port, then pick it in the app (see "Choosing a MIDI output").

### Linux and Raspberry Pi OS (Debian/Ubuntu based)

```sh
sudo apt update
sudo apt install -y nodejs npm build-essential python3 libasound2-dev
cd terminal
npm install
npm link        # may need: sudo npm link, or set an npm prefix in your home dir
orca-seaflake
```

Raspberry Pi OS's packaged Node can be old. If `node -v` is below 18, install a newer Node with [nvm](https://github.com/nvm-sh/nvm). Both 32-bit and 64-bit Pi OS work, since the module is compiled on the Pi itself.

List MIDI ports with `aconnect -l`. A USB MIDI interface just shows up. For a virtual port: `sudo modprobe snd-virmidi`.

Clipboard on Linux uses `wl-copy`/`wl-paste` (Wayland) or `xclip`/`xsel` (X11). Over SSH with no desktop, copy/paste use an internal clipboard that only works inside the app.

## Running

```sh
orca-seaflake                       # empty grid sized to your window
orca-seaflake path/to/patch.orca    # open a patch
```

Without `npm link` you can always run `node bin/orca-seaflake` from the `terminal` folder.

Patches save to `~/Documents/Orca Seaflake/` (created if missing). Set `ORCA_SEAFLAKE_DIR` to save somewhere else.

### Choosing a MIDI output

The first MIDI output is picked automatically. The current one is shown in the header (`MIDI: 0: IAC Driver Bus 1`). To change it:

- **Ctrl-K**, type `midi: -1`, Enter: lists every output with its number
- **Ctrl-K**, type `midi: 2`, Enter: switches to output 2

On Windows, output 0 is usually "Microsoft GS Wavetable Synth" and on Linux it is usually "Midi Through", so you'll often want to switch.

## Controls

- Arrow keys: move cursor
- Type an allowed Orca glyph: write it into the grid
- `.` or Backspace: erase
- Space: play / stop
- `+` / `-`: BPM up / down
- Type `$bpm: 120` directly into the grid to set a live BPM directive. The directive is read during playback and overrides the Song Mode slot BPM. BPM values are accepted from 60 to 300.
- Ctrl-S / Cmd-S: save patch under `~/Documents/Orca Seaflake/`
- Ctrl-N / Cmd-N: new patch
- Ctrl-K: command prompt. Commands: `bpm: 120`, `apm: 30`, `midi: 2`, `frame: 0`, `rewind: 16`, `skip: 16`
- Ctrl-Q: quit and send MIDI all-notes-off. Ctrl-C never quits the application. Escape never quits the application.

The grid runs at one Orca frame per sixteenth note. BPM is clamped to 60–300.

## Engine and feature scope

The core `Operator`, `Orca`, `library`, and transpose table files are copied directly from the supplied Seaflake v2.4 source. This retains the v2.4 glyph definitions and their operator calculations, including Seaflake's `^`, `&`, `~`, `(`, `)`, `+`, and `@` operators. MIDI note output is routed through the OS MIDI system (CoreMIDI, Windows MIDI, ALSA); `:` and the custom tuned/scale/envelope MIDI operators are wired to that backend.

This is a terminal-native port of the supplied v2.4 engine with a terminal grid, Song Mode, rectangular selection, clipboard support, and MIDI note output. MIDI input/clock sync, OSC, UDP, mono output, CC output, and some of the desktop application workflow still need dedicated terminal-side adapters. The source desktop app is not modified. The terminal keymap intentionally implements a smaller set of bindings; desktop-only actions such as module import, guide toggle, frame stepping, MIDI device cycling, cursor trigger, insert mode, case conversion, and leap movement are not claimed as available in this port.

## Notes

The `midi` package includes a native addon. If installation fails, confirm the Xcode Command Line Tools are installed and run `npm install` again. MIDI output devices are enumerated when the app starts; the first available output is selected. Create a virtual MIDI destination in Audio MIDI Setup if you want to route into a software instrument.

## Responsive grid and Song Mode

The grid now expands to the available terminal width and height. Resizing the terminal resizes the live grid while preserving cells that move temporarily outside the visible dimensions; expanding the window restores them. Start with a larger Terminal window or maximize it before launching for the largest grid.

Song Mode is available with **Ctrl-T** (and with Command-T only in terminal emulators that actually forward that key combination). macOS Terminal and iTerm normally reserve Command-T to create a new tab, so the application cannot intercept it by default. In Song Mode:

- Up / Down: select one of 32 slots
- Left / Right: select LEN, MUL, or BPM field
- Enter: store the current grid in the selected slot
- L: load the selected slot
- Space: start / stop the song
- Delete / Backspace: clear the selected slot
- M: cycle ordered, reverse, and random playback
- O: toggle looping
- E: export a `.song` file
- I: import a `.song` file
- Ctrl-T or Esc: return to the grid

Song slot duration is `LEN × MUL` Orca frames. One sixteenth note equals one frame, so `LEN 001` and `MUL 001` plays a slot for exactly one frame. Each slot retains its own base BPM. Existing version 1 `.song` files are converted from BAR/16TH into frame lengths when imported.


## Rectangular block selection and clipboard

- Press **Tab** to enter selection mode. Use the ordinary arrow keys to expand a rectangular selection from the starting cursor cell. Press **Tab** again or **F2** to exit selection mode and clear the selection. Escape does not quit the app. This works even in terminals that do not transmit Shift+Arrow modifiers.
- **Backspace / Delete** clears every cell inside the selected rectangle, leaving cells outside the selection untouched.
- **Ctrl+C** copies the selected rectangle to the system clipboard. With no active selection, it reports that there is nothing to copy. Ctrl+C never quits the application.
- **Ctrl+V** reads the system clipboard and pastes at the cursor.
- **Cmd+V** also works in terminals that support bracketed paste; pasted multi-line text is inserted as a block.
- **Ctrl+Z / Cmd+Z**: undo patch edits. **Ctrl+Shift+Z / Cmd+Shift+Z**: redo. History retains up to 200 edit steps and includes typing, block deletion, block comments, paste, and loading a Song Mode slot.
- Outside selection mode, ordinary arrow keys move the cursor and clear the selection. Shift+Arrow remains supported where the terminal transmits the modifier.

Selection includes empty cells (`.`), preserving the rectangular shape of copied code. Paste is clipped at the current grid boundary.

## Grid BPM directives

BPM directives are written directly into the grid as text. For example, type `$bpm: 120` into a row. During playback the terminal scans the grid and changes tempo when it finds a valid directive. This lets a patch override the base BPM loaded by Song Mode. Escape cancels prompts or returns from Song Mode; **Ctrl-Q** quits the application. Ctrl-C is reserved for copying and never quits. Command-key shortcuts are accepted only when the terminal emulator forwards them; macOS Terminal may reserve some Command combinations for its own actions. The terminal tracks mouse movement when the terminal supports SGR mouse reporting. Moving the pointer over an operator inlet displays the operator, inlet name, and current/default inlet value in the status line.


## Rectangular selection and block comments

- Press Tab, then use any combination of the arrow keys to extend a two-dimensional rectangular selection. Press Tab again or F2 to exit selection mode.
- The selection anchor remains fixed while you extend horizontally and vertically, so you can build a box in either order.
- `Ctrl+C` copies the selected rectangle; `Ctrl+V` pastes it.
- With a selection active, press `#` to toggle Orca-style block comments: a `#` is written at the left and right edge of every selected row. Press it again to remove the edge markers.
- The original Orca shortcut, `CmdOrCtrl+/`, is also supported where the terminal forwards it.


## Navigation, port inspection, and side-by-side Song Mode

- Hold **Shift + Arrow** to move eight cells at a time. **Ctrl + Arrow** (or **Command + Arrow** when the terminal forwards it) is an alternate fast-travel shortcut.
- In selection mode, arrows continue to extend the selection one cell at a time. Enter selection mode with **Tab** and exit with **Tab** or **F2**.
- With terminal mouse reporting enabled, left-click and drag across the grid to select a rectangular block. A click selects a single cell.
- When the cursor is on an operator, inlet, or outlet, the bottom status line identifies the operator and port name (for example, `MIDI / INLET VELOCITY`) and shows the current/default glyph value.
- **Ctrl+T** opens Song Mode as a separate full-screen tab while keeping the Orca grid visible. Press **Ctrl+T** again to close it. Each stored song slot has a **NOTES** field; select a slot and press **N** to edit its notes. Notes are included in `.song` exports.

Mouse drag selection depends on terminal mouse-reporting support. Tab-based rectangular selection remains available in terminals without it.


### Song Mode display (v0.6.4)

Song Mode opens as a separate full-screen tracker view. The Orca grid is not resized or squeezed to make room for the tracker. Press `Ctrl+T` to switch between the grid and tracker views.


### Terminal rendering fix (v0.6.4)

Mouse reporting is disabled because raw mouse escape packets could be interpreted by the terminal keypress parser as grid keystrokes. Grid cells now use distinct colors for operands (white background/dark text), inlets (cyan), outlets (magenta), and operators (white text). Keyboard navigation and Tab-based rectangular selection remain available.
