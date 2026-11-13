# Orca Seaflake: terminal version

Orca Seaflake in a terminal, on **macOS, Windows, Linux and Raspberry Pi OS** (32-bit and 64-bit). It runs the same engine as the desktop app, so patches behave the same, and it includes Song Mode. There's no window and no Electron, so it's light enough for a Raspberry Pi over SSH.

## Install

### 1. Install Node.js

You only do this once.

**macOS and Windows:** install Node.js LTS from [nodejs.org](https://nodejs.org). On Windows, use **Windows Terminal** to run Orca Seaflake (it's built into Windows 11; on Windows 10 get it from the Microsoft Store).

**Raspberry Pi OS, Ubuntu, Debian:**

```sh
sudo apt update
sudo apt install -y nodejs npm alsa-utils
```

No compilers or developer tools are needed: the MIDI module comes ready-made for macOS, Windows, Linux and Raspberry Pi.

Raspberry Pi: tested on 32-bit and 64-bit Raspberry Pi OS for the Pi 2, 3, 4, 5 and Zero 2 W. The Pi 1 and the original Pi Zero use an older processor that isn't tested and may not work.

### 2. Install Orca Seaflake

On a Mac with [Homebrew](https://brew.sh), this one command installs Orca Seaflake and Node.js, so you can skip step 1:

```sh
brew install femifleming/tap/orca-seaflake-terminal
```

Otherwise, on macOS and Windows:

```sh
npm install -g https://github.com/femifleming/orca_seaflake/releases/latest/download/orca-seaflake-terminal.tgz
```

On Raspberry Pi OS, Ubuntu and Debian, add `sudo`:

```sh
sudo npm install -g https://github.com/femifleming/orca_seaflake/releases/latest/download/orca-seaflake-terminal.tgz
```

### 3. Check it worked

```sh
orca-seaflake --version
orca-seaflake --midi
```

The first prints the version number. The second lists your MIDI outputs, or explains how to create one.

To update later, run the step 2 command again. To uninstall, run `npm uninstall -g orca-seaflake-terminal`.

## Run

```sh
orca-seaflake                      # empty grid, sized to your window
orca-seaflake my-patch.orca        # open a patch
orca-seaflake --help               # list options
```

Make the terminal window big before you start: the grid fills whatever space it has.

Patches save to `Documents/Orca Seaflake` in your home folder. To save somewhere else, set the `ORCA_SEAFLAKE_DIR` environment variable.

### Choose a MIDI output

The first MIDI output is picked when the app starts. The one in use shows at the top, for example `MIDI: 0: IAC Driver Bus 1`.

To pick another, press `Ctrl+K`, type `midi: -1` and press Enter to list them all, then press `Ctrl+K` again and type the number you want, for example `midi: 2`.

The first output is often not the one you want. On Windows it's usually "Microsoft GS Wavetable Synth", and on Linux "Midi Through".

To create a virtual MIDI port that reaches other software:

- **macOS:** Audio MIDI Setup → Window → Show MIDI Studio → double-click **IAC Driver** → tick **Device is online**.
- **Windows:** install [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) and click **+**.
- **Linux / Raspberry Pi:** USB MIDI devices appear automatically. `aconnect -l` lists ports; `sudo modprobe snd-virmidi` adds virtual ones.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move the cursor |
| `Shift`, `Option`/`Alt` or `Ctrl` + arrow | Leap 8 cells |
| `Page Up` / `Page Down` | Jump a screen up / down (`fn` + `↑`/`↓` on a Mac laptop) |
| `Home` / `End` | Jump to the start / end of the row (`fn` + `←`/`→` on a Mac laptop) |
| `Ctrl` + `Home` / `End` | Jump to the top-left / bottom-right corner |
| Any operator or value | Write it at the cursor |
| `.` or `Backspace` | Erase |
| `Space` | Play / stop |
| `>` / `<` | Tempo up / down (60–300 BPM) |
| `Ctrl+S` | Save |
| `Ctrl+O` | Open a file (type its path) |
| `Ctrl+N` | New, empty grid |
| `Ctrl+K` | Command prompt (see below) |
| `Ctrl+T` | Song Mode |
| `Ctrl+Z` | Undo (200 steps) |
| `Ctrl+Y` | Redo (`Ctrl+Shift+Z` also works in terminals that send it) |
| `Ctrl+Q` | Quit (also silences all MIDI notes) |

`Ctrl+C` copies and never quits; `Esc` never quits either. The grid runs one frame per 16th note.

The grid starts at the size of the window and grows if you make the window bigger. A patch larger than the window scrolls to follow the cursor; arrows on the frame (`▲ ▼ ◀ ▶`) show there's more grid in that direction. The cursor position is shown at the top. `+` marks every 8th cell, so leaps line up with the marks.

**Leaping on a Mac:** `Shift` + arrow works in every terminal. macOS uses `Ctrl` + arrow for Mission Control, so that only works if you turn those shortcuts off in System Settings → Keyboard → Keyboard Shortcuts → Mission Control. `Option` + arrow works in Terminal and iTerm2.

### Selecting, copying and pasting

| Key | Action |
| --- | --- |
| `Tab` | Start selecting; arrows (and leaps and jumps) grow the box. `Tab`, `Esc` or `F2` stops. |
| `Ctrl+A` | Select the whole grid |
| Mouse drag | Select a box, in terminals with mouse support |
| `Ctrl+C` | Copy the box (to the system clipboard where available) |
| `Ctrl+V` or `Cmd+V` | Paste at the cursor |
| `Backspace` | Clear the box |
| `#` | Comment / uncomment the box |

### Commands (`Ctrl+K`)

| Command | Does |
| --- | --- |
| `bpm: 120` | Set the tempo |
| `apm: 30` | Set the tempo in quarter notes (`30` = 120 BPM) |
| `midi: -1` / `midi: 2` | List MIDI outputs / use output 2 |
| `frame: 0` | Jump to frame 0 |
| `skip: 16` / `rewind: 16` | Move forward / back 16 frames |

You can also type `$bpm: 140` straight into the grid. While playing, it sets the tempo, and in Song Mode it overrides the slot's BPM.

## Song Mode

Press `Ctrl+T` to switch between the grid and Song Mode.

| Key | Action |
| --- | --- |
| `↑` / `↓` | Choose a slot (32 in total); `Shift` + arrow, `Page Up` / `Page Down` move 8 |
| `←` / `→` | Choose the LEN, MUL or BPM column |
| `0`–`9`, `+` / `-` | Change the value |
| `Enter` | Store the current grid in the slot |
| `L` | Load the slot onto the grid |
| `Backspace` | Clear the slot |
| `N` | Write a note for the slot |
| `Space` | Play / stop the song |
| `M` | Order: forward, reverse, random |
| `R` | Loop on / off |
| `S` / `O` | Save / open a `.song` file (in `Documents/Orca Seaflake/songs`) |
| `Ctrl+T` or `Esc` | Back to the grid |

A slot plays for **LEN × MUL frames**, and one frame is one 16th note: `LEN 16, MUL 1` is one bar. `.song` files open in both the terminal and the desktop app.

## What the terminal version sends

The terminal version sends **MIDI notes**: `:` and Seaflake's `(`, `)` and `@`. These other operators are accepted in the grid but **don't send anything yet** in the terminal; use the desktop app for them:

- `%` mono notes
- `!` control change and `?` pitch bend
- `=` OSC and `;` UDP
- `$` commands (except `$bpm`, which works)

## Troubleshooting

- **`orca-seaflake: command not found`:** close and reopen the terminal. If that doesn't fix it, run `npm config get prefix` and add that folder's `bin` subfolder to your PATH (on Windows, add the folder itself).
- **The install fails with errors about `midi`, `gyp` or `make`:** there's no ready-made MIDI module for your computer (for example a Pi 1 or original Pi Zero), so it has to compile one. Install build tools, then run the install command again:
  - macOS: `xcode-select --install`
  - Windows: [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) with **Desktop development with C++**
  - Linux / Raspberry Pi: `sudo apt install -y build-essential python3 libasound2-dev`
- **The status line says `NO MIDI`:** the app still works for editing. Run `orca-seaflake --midi` to see why, and create a MIDI port as described above.
- **Linux: `npm install -g` says permission denied:** put `sudo` in front.
- **`Cmd+T` opens a new terminal tab:** use `Ctrl+T`. macOS terminals keep the Cmd shortcuts for themselves.
- **Letters and numbers appear in the terminal when you move the mouse after quitting:** an older version crashed or was closed without switching mouse reporting off. Run `orca-seaflake` once and quit with `Ctrl+Q`, or run `reset`. Version 0.9.1 and later always switch it off, even when they crash.
- **Copy and paste only work inside the app on Linux:** install `wl-clipboard` (Wayland) or `xclip` (X11). Over SSH there's no system clipboard, so the app uses its own.

## Run from source

```sh
git clone https://github.com/femifleming/orca_seaflake.git
cd orca_seaflake/terminal
npm install
npm link          # adds the orca-seaflake command (Linux: sudo npm link)
npm test          # optional: runs the test suite
```
