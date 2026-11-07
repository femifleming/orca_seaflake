# What Seaflake adds to Orca

Orca Seaflake is built on [Orca](https://github.com/hundredrabbits/Orca) by Hundred Rabbits. Everything in Orca works the same way here; patches made for Orca open and play the same. This page lists everything Seaflake adds or changes, with links to the full guides.

- [Operators](#operators)
- [Song Mode](#song-mode)
- [Terminal version](#terminal-version)
- [Desktop app](#desktop-app)
- [Installing](#installing)
- [Files](#files)

## Operators

### The original operators are unchanged

The original operators (`A`–`Z` and `* # : % ! ? = ; $`) behave exactly as in the released Orca app, including its defaults for empty inputs:

| You place | Orca and Seaflake do |
| --- | --- |
| `D` on its own | Bangs every 8 frames |
| `C` on its own | Counts 0 to 7 |
| `I` on its own | Counts up by 1, wrapping after `z` |
| `U` on its own | Bangs 1 of every 8 frames |
| `Z` with an empty rate | Moves by 1 toward the target |
| `R` with an empty right side | Picks a value up to `z` |

The latest Orca source code on GitHub has removed these defaults, so a lone `D` there does nothing. Seaflake keeps the behaviour of the Orca app people play with. A side-by-side test runs the released Orca engine and Seaflake's engine on hundreds of patches and checks every frame is identical.

### New operators

Each one is a symbol that Orca doesn't use, so none of them change existing patches. Full details, positions and examples are in the [operator reference](OPERATORS.md#seaflake-operators).

| Glyph | Name | What it does |
| --- | --- | --- |
| `^` | probability | Passes a bang on at random. `z` always passes, `0` never does, the default `i` about half the time. |
| `&` | phase clock | Bangs on one chosen step of a repeating cycle, like a clock divider with an offset. |
| `~` | burst | Turns one bang into a timed run of bangs: how many, and how far apart. |
| `\|` | mute | Each bang mutes or unmutes the block below it by writing `#` down both sides. Inlets set the block's width and height. |
| `+` | scale | Stores a 12-note scale (`1` = in, `0` = out) in a variable. |
| `)` | scale note | Sends a MIDI note picked by degree from a stored scale. |
| `(` | just note | Sends a MIDI note tuned to just intonation (uses pitch bend). |
| `@` | envelope | Sends a shaped run of MIDI notes: ramp up, ramp down, pulse or random. |

`$bpm:140` typed into the grid sets the tempo while it runs; `$bpm140` without the colon works too.

## Song Mode

Song Mode chains patches into a song, like a hardware tracker. It works the same in the desktop app and the terminal version, with the same keys and the same files. Full guide: [Song Mode](SONG_MODE.md).

- **32 slots.** Store the grid in a slot with `Enter`; each slot keeps its own patch, tempo and length.
- **Length = LEN × MUL frames**, and one frame is one 16th note: `LEN 16, MUL 4` is four bars.
- **Tempo per slot.** A `$bpm` inside a patch overrides the slot's tempo while it plays.
- **Order, reverse or random**, looping or playing once.
- **Song files.** `S` saves the song as a `.song` file and `O` opens one. They go in `Documents/Orca Seaflake/songs`, and open in both the desktop app and the terminal version.
- **Notes per slot** (terminal: `N`), kept in the song file.
- Open it with `Cmd+T` (`Ctrl+T` on Windows and Linux, and in the terminal version).

## Terminal version

The same operators and Song Mode in a terminal, with no window. It runs in Terminal on macOS, Windows Terminal, any Linux terminal, and over SSH on a Raspberry Pi, including 32-bit Raspberry Pi OS. Full guide: [terminal version](../terminal/README.md).

- Install with `npm` or Homebrew; run with `orca-seaflake`.
- Sends MIDI notes, and lists and picks MIDI outputs with the `midi:` command.
- Selecting, copying and pasting with your system clipboard.
- Saves `.orca` files in `Documents/Orca Seaflake`.
- Starts without MIDI if the MIDI module can't load, so you can still edit.

## Desktop app

- **Song Mode** slides in from the right of the grid with `Cmd+T`. It never covers the footer at the bottom of the window.
- **Settings** (`Cmd+Shift+P`, or **Settings…** in the app menu on macOS and the File menu on Windows and Linux): background color, background opacity (see your desktop through the window on macOS, with the grid text staying solid), and the grid font (Input Mono or any monospace font installed on your computer). Saved between launches.
- **Song commands** in the command prompt (`Cmd+K`): `song` (show or hide), `song:play`, `song:stop`, `song:mode`, `song:save`, `song:open`.
- **Drag a `.song` file** onto the window to open it.
- **Runs on** macOS (Apple Silicon and Intel), Windows, Linux, and 64-bit Raspberry Pi OS.
- Typing `$` works on Linux keyboard layouts that report `Shift+4` as `4`.

## Installing

| How | Command or file |
| --- | --- |
| Mac app with Homebrew | `brew install --cask femifleming/orca_seaflake/orca-seaflake` |
| Terminal version with Homebrew | `brew install femifleming/orca_seaflake/orca-seaflake-terminal` |
| Mac installer | `.pkg` for Apple Silicon or Intel |
| Windows | `.zip` |
| Ubuntu, Debian, Raspberry Pi OS 64-bit | `.deb` for x64 and ARM64 |
| Terminal version with npm | `npm install -g` the release's `.tgz` |

All of these are on the [releases page](https://github.com/femifleming/orca_seaflake/releases/latest), and the [README](../README.md#install--run) walks through each one.

## Files

- **`.orca` patches** are plain text and identical to Orca's; they open in Orca, Seaflake and Seaflake's terminal version. Patches that use Seaflake's new operators need Seaflake to play them.
- **`.song` files** are plain JSON and open in both Seaflake apps. Older Seaflake song files (with bars and 16ths, or `.orca-song` files from early builds) open too.
