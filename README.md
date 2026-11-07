# ORCΛ SEΛFLΛKE

<img src="desktop/icon.png" width="300" alt="Orca Seaflake icon"/>

Orca Seaflake is a version of [Orca](https://github.com/hundredrabbits/Orca), the [esoteric programming language](https://en.wikipedia.org/wiki/Esoteric_programming_language) designed to quickly create procedural sequencers, in which every letter of the alphabet is an operation, where lowercase letters operate on bang, uppercase letters operate each frame.

This application **is not a synthesizer, but a livecoding environment** capable of sending MIDI, OSC & UDP to your audio/visual interfaces, like Ableton, Renoise, VCV Rack or SuperCollider.

Seaflake keeps every original Orca operator exactly as it is in the Orca app, defaults included (patches made for Orca play the same), and adds:

- **New operators** for probability, phase clocks, timed bursts, muting blocks, just intonation, scales and shaped note runs.
- **Song Mode**, a 32-slot arranger that chains patches together at 16th-note resolution, saved as `.song` files.
- **A terminal version** that runs in Terminal, Windows Terminal or over SSH on a Raspberry Pi.
- **Settings** for the background color, window transparency and font.

Everything Seaflake adds to Orca is listed in [What Seaflake adds to Orca](docs/WHATS_NEW.md); release notes are in the [changelog](CHANGELOG.md).

Ways to use it:

- [Download the app](#desktop-app) for **macOS, Windows, Linux and Raspberry Pi**.
- [Use it in a terminal](#terminal-version) on **macOS, Windows, Linux and Raspberry Pi**.
- [Run from source](#run-from-source) with Node.js.

New to Orca? Watch the [overview video](https://www.youtube.com/watch?v=RaI_TuISSJE) or a [tutorial](https://www.youtube.com/watch?v=ktcWOLeWP-g), and try the [example patches](patches/).

## Install & Run

### Desktop app

Download the file for your computer, then follow the step next to it.

| Your computer | Download | Then |
| --- | --- | --- |
| Mac with Apple Silicon (M1, M2, M3, M4…) | [Orca-Seaflake-darwin-arm64.pkg](https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-darwin-arm64.pkg) | Double-click it and follow the installer. |
| Mac with an Intel processor | [Orca-Seaflake-darwin-x64.pkg](https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-darwin-x64.pkg) | Double-click it and follow the installer. |
| Windows 10 or 11 (64-bit) | [Orca-Seaflake-win32-x64.zip](https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-win32-x64.zip) | Right-click → **Extract All**, open the folder, run `orca-seaflake.exe`. |
| Ubuntu / Debian (Intel or AMD) | [orca-seaflake_amd64.deb](https://github.com/femifleming/orca_seaflake/releases/latest/download/orca-seaflake_amd64.deb) | `sudo apt install ./orca-seaflake_amd64.deb` |
| Raspberry Pi OS **64-bit** (Pi 3, 4, 5), Ubuntu / Debian on ARM | [orca-seaflake_arm64.deb](https://github.com/femifleming/orca_seaflake/releases/latest/download/orca-seaflake_arm64.deb) | `sudo apt install ./orca-seaflake_arm64.deb` |
| Other Linux (Intel/AMD or ARM64) | [x64 .tar.gz](https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-linux-x64.tar.gz) · [arm64 .tar.gz](https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-linux-arm64.tar.gz) | Extract it and run `./orca-seaflake` inside the folder. |

Then open **Orca Seaflake** from Applications (macOS), the extracted folder (Windows), or your app menu / the `orca-seaflake` command (Linux).

On a Mac you can also install with [Homebrew](https://brew.sh), and update later with `brew upgrade`:

```sh
brew install --cask femifleming/orca_seaflake/orca-seaflake
```

**Not sure which one you need?**

- **Mac:** Apple menu → **About This Mac**. "Chip: Apple M…" means Apple Silicon; "Processor: Intel" means Intel.
- **Raspberry Pi:** run `uname -m` in a terminal. `aarch64` means 64-bit; `armv7l` or `armv6l` means 32-bit. The desktop app needs 64-bit (Pi 3, 4 or 5), so on **32-bit Raspberry Pi OS use the [terminal version](#terminal-version)**.

**First launch:** the app isn't signed with a paid Apple or Microsoft certificate, so your computer may warn you once.

- **macOS** says it "cannot verify the developer": open **System Settings → Privacy & Security**, scroll down and click **Open Anyway**. Or Control-click the app, choose **Open**, then **Open** again.
- **Windows** shows "Windows protected your PC": click **More info → Run anyway**.

<img src='https://raw.githubusercontent.com/hundredrabbits/Orca/master/resources/preview.jpg' width="600"/>

### Terminal version

The terminal version runs the same operators and Song Mode, without a window. It's light enough to run on a Raspberry Pi over SSH. It sends MIDI notes; for MIDI CC, pitch bend, mono notes, OSC and UDP, use the desktop app ([details](terminal/README.md#what-the-terminal-version-sends)).

**1. Install Node.js:**

| System | What to do |
| --- | --- |
| macOS, Windows | Install Node.js LTS from [nodejs.org](https://nodejs.org). |
| Raspberry Pi OS, Ubuntu, Debian | `sudo apt update && sudo apt install -y nodejs npm alsa-utils` |

On a Mac with [Homebrew](https://brew.sh), one command installs it, Node.js included:

```sh
brew install femifleming/orca_seaflake/orca-seaflake-terminal
```

Otherwise:

**2. Install Orca Seaflake** (on Linux and Raspberry Pi, put `sudo` in front):

```sh
npm install -g https://github.com/femifleming/orca_seaflake/releases/latest/download/orca-seaflake-terminal.tgz
```

**3. Run it:**

```sh
orca-seaflake
```

To check the install, run `orca-seaflake --version`. `orca-seaflake --midi` lists your MIDI outputs. Controls and details are in the [terminal guide](terminal/README.md).

### Run from source

Install [Node.js](https://nodejs.org) 18 or newer, then:

```sh
git clone https://github.com/femifleming/orca_seaflake.git
cd orca_seaflake/desktop
npm install
npm start
```

For the terminal version from source, run `npm install` and then `npm link` in the `terminal` folder instead.

## Connect it to a synth

Orca Seaflake sends MIDI notes out; something else makes the sound. Plug in a MIDI device, or create a virtual MIDI port to reach software like Ableton, VCV Rack or GarageBand:

| System | Virtual MIDI port |
| --- | --- |
| macOS | Open **Audio MIDI Setup** → **Window → Show MIDI Studio** → double-click **IAC Driver** → tick **Device is online**. |
| Windows | Install [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) and click **+** to create a port. |
| Linux / Raspberry Pi | A USB MIDI device appears automatically. `aconnect -l` lists ports; `sudo modprobe snd-virmidi` adds virtual ones. |

Then choose the output. In the **desktop app**, press `CmdOrCtrl+.` until your device shows at the bottom of the window. In the **terminal version**, press `Ctrl+K`, type `midi: -1` and press Enter to list outputs, then use `midi: 2` to pick output 2.

## Operators

To display the list of operators inside of Orca Seaflake, use `CmdOrCtrl+G`. Inputs in *italic* sit to the left of the operator; the others sit to the right. Full details, defaults and examples are in the [operator reference](docs/OPERATORS.md).

- `A` **add**(*a* b): Outputs sum of inputs.
- `B` **subtract**(*a* b): Outputs difference of inputs.
- `C` **clock**(*rate* mod): Outputs modulo of frame.
- `D` **delay**(*rate* mod): Bangs on modulo of frame.
- `E` **east**: Moves eastward, or bangs.
- `F` **if**(*a* b): Bangs if inputs are equal.
- `G` **generator**(*x* *y* *len*): Writes operands with offset.
- `H` **halt**: Halts southward operand.
- `I` **increment**(*step* mod): Increments southward operand.
- `J` **jumper**(*val*): Outputs northward operand.
- `K` **konkat**(*len*): Reads multiple variables.
- `L` **less**(*a* b): Outputs smallest of inputs.
- `M` **multiply**(*a* b): Outputs product of inputs.
- `N` **north**: Moves northward, or bangs.
- `O` **read**(*x* *y* read): Reads operand with offset.
- `P` **push**(*key* *len* val): Writes eastward operand.
- `Q` **query**(*x* *y* *len*): Reads operands with offset.
- `R` **random**(*min* max): Outputs random value.
- `S` **south**: Moves southward, or bangs.
- `T` **track**(*key* *len* val): Reads eastward operand.
- `U` **uclid**(*step* max): Bangs on Euclidean rhythm.
- `V` **variable**(*write* read): Reads and writes variable.
- `W` **west**: Moves westward, or bangs.
- `X` **write**(*x* *y* val): Writes operand with offset.
- `Y` **jymper**(*val*): Outputs westward operand.
- `Z` **lerp**(*rate* target): Transitions operand to input.
- `*` **bang**: Bangs neighboring operands.
- `#` **comment**: Halts a line.

### Seaflake operators

- `^` **probability**(chance): Passes a bang at random. `z` always passes, `0` never does.
- `&` **phase**(*rate* length, phase above): Bangs on one chosen step of a repeating cycle.
- `~` **burst**(repeats division multiplication): Turns one bang into a timed run of bangs.
- `|` **mute**(*width* *height*): Each bang hashes out the block below it, or un-hashes it, to mute and unmute a section.
- `+` **scale**(*key* C C# D D# E F F# G G# A A# B): Stores a 12-note scale (`1` = in, `0` = out) in a variable.
- `)` **scale note**(channel octave degree scale velocity length): Sends a MIDI note picked from a stored scale.
- `(` **just note**(channel octave note velocity length): Sends a MIDI note in just intonation.
- `@` **envelope**(channel octave note velocity shape repeats duration pulsewidth): Sends a shaped run of MIDI notes.

### IO

- `:` **midi**(channel octave note velocity length): Sends a MIDI note.
- `%` **mono**(channel octave note velocity length): Sends monophonic MIDI note.
- `!` **cc**(channel knob value): Sends MIDI control change.
- `?` **pb**(channel lsb msb): Sends MIDI pitch bend.
- `;` **udp**: Sends UDP message.
- `=` **osc**(*path*): Sends OSC message.
- `$` **self**: Sends an Orca command.

For example, `:04C` plays a **C on octave 4, through MIDI channel 1** (channels count from `0`). Trigger it with a bang: `D8` above it plays the note every 8 frames.

## Song Mode

Song Mode chains patches into a song. Store a patch in a slot, give each slot a length and tempo, and Song Mode plays the slots in order, in reverse or at random.

- Each slot lasts **LEN × MUL frames**, and one frame is one 16th note. `LEN 16, MUL 1` is one bar; `LEN 16, MUL 4` is four bars.
- Each slot remembers its own **BPM**. A `$bpm:140` command inside a patch overrides it while that slot plays.
- Songs save as `.song` files in `Documents/Orca Seaflake/songs`. The desktop app and the terminal version use the same files and the same keys.

| | Desktop app | Terminal |
| --- | --- | --- |
| Open Song Mode | `CmdOrCtrl+T` | `Ctrl+T` |
| Store the grid in the selected slot | `Enter` | `Enter` |
| Play / stop the song | `Space` | `Space` |
| Order / reverse / random | `M` | `M` |
| Loop on / off | `R` | `R` |
| Save / open a `.song` file | `S` / `O` | `S` / `O` |

See the [Song Mode guide](docs/SONG_MODE.md) for everything else.

## Controls

| Action | Desktop app | Terminal |
| --- | --- | --- |
| Play / pause | `Space` | `Space` |
| Tempo up / down | `>` / `<` | `+` / `-` |
| Save | `CmdOrCtrl+S` | `Ctrl+S` |
| Open | `CmdOrCtrl+O` | `Ctrl+O` |
| New | `CmdOrCtrl+N` | `Ctrl+N` |
| Command prompt | `CmdOrCtrl+K` | `Ctrl+K` |
| Operator guide | `CmdOrCtrl+G` | — |
| Settings (background color, transparency, font) | `CmdOrCtrl+Shift+P` | — |
| Choose MIDI output | `CmdOrCtrl+.` | `Ctrl+K` then `midi: 1` |
| Undo / redo | `CmdOrCtrl+Z` / `CmdOrCtrl+Shift+Z` | `Ctrl+Z` / `Ctrl+Shift+Z` (where the terminal sends it) |
| Quit | `CmdOrCtrl+Q` or close the window | `Ctrl+Q` |

The desktop app's full list is in its menus. The command prompt accepts Orca's [commands](https://github.com/hundredrabbits/Orca#commands), such as `bpm:140` or `frame:0`.

### Settings

Open **Settings…** from the app menu, or press `CmdOrCtrl+Shift+P`, to change:

- **Background color**: any color; the rest of the theme stays the same.
- **Background opacity**: lower it to see your desktop through the window while the grid text stays solid (macOS only).
- **Font**: Input Mono (the default) or any monospace font installed on your computer. Only installed fonts appear in the list; type any other installed font's name into **Other installed font**.

Settings are saved and come back the next time you open the app. **Reset to defaults** puts all three back.

## Base36 Table

Orca operates on a base of **36 increments**. Operators using numeric values will typically also operate on letters and convert them into values as per the following table. For instance `Do` will bang every *24th frame*.

| **0** | **1** | **2** | **3** | **4** | **5** | **6** | **7** | **8** | **9** | **A** | **B** |
| :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| **C** | **D** | **E** | **F** | **G** | **H** | **I** | **J** | **K** | **L** | **M** | **N** |
| 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
| **O** | **P** | **Q** | **R** | **S** | **T** | **U** | **V** | **W** | **X** | **Y** | **Z** |
| 24 | 25 | 26 | 27 | 28 | 29 | 30 | 31 | 32 | 33 | 34 | 35 |

## Troubleshooting

- **No sound:** Orca doesn't make sound by itself. Check that a MIDI output is selected (see [Connect it to a synth](#connect-it-to-a-synth)) and that your synth or DAW is listening to that port.
- **Terminal: `orca-seaflake: command not found`:** close and reopen your terminal. If it still fails, run `npm config get prefix` and add that folder's `bin` (on Windows, the folder itself) to your PATH.
- **Terminal: install fails while building `midi`:** your computer isn't one of the common ones with a ready-made MIDI module, so it has to compile one. Install build tools (see the [terminal guide](terminal/README.md#troubleshooting)) and run the install command again.
- **Terminal: Cmd+T opens a new tab:** use `Ctrl+T` for Song Mode instead; the Terminal app keeps Cmd+T for itself.
- **Linux: the app won't start:** install the `.deb` with `apt` as shown above so its libraries come with it, rather than running the `.tar.gz` build.

## Companion Applications

- [Pilot](https://github.com/hundredrabbits/pilot), a companion synth tool.
- [Aioi](https://github.com/MAKIO135/aioi), a companion to send complex OSC messages.
- [Sonic Pi](https://sonic-pi.net/), a livecoding environment.
- [VCV Rack](https://vcvrack.com/), a virtual modular synth.

## Links

- [Original Orca](https://github.com/hundredrabbits/Orca) by Hundred Rabbits
- [Operator reference](docs/OPERATORS.md)
- [What Seaflake adds to Orca](docs/WHATS_NEW.md)
- [Changelog](CHANGELOG.md)
- [Song Mode guide](docs/SONG_MODE.md)
- [Terminal guide](terminal/README.md)
- [Building packages yourself](docs/PACKAGING.md)
- [All releases](https://github.com/femifleming/orca_seaflake/releases)

## Extras

- Orca Seaflake is derived from [Hundred Rabbits' Orca](https://github.com/hundredrabbits/Orca). Support the original through [Patreon](https://www.patreon.com/hundredrabbits).
- See the [License](LICENSE.md) file for license rights and limitations (MIT).
- Pull Requests are welcome!
