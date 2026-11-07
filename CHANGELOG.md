# Changelog

Orca Seaflake's releases. For everything Seaflake adds to Orca, see [What Seaflake adds to Orca](docs/WHATS_NEW.md).

## 0.9.0

### Operators

- **Classic defaults are back** for the original operators, as in the released Orca app: a lone `D` bangs every 8 frames, `C` counts 0–7, `I` counts up by 1, `U` plays 1 of 8, `Z` moves by 1 and `R` picks up to `z`. `L` outputs nothing when a side is empty, and `J` and `Y` chain as in Orca. Version 0.8.0 had followed the latest Orca source, which removed these defaults.
- **New `|` mute operator.** Each bang mutes or unmutes the block below it by writing `#` down both of its sides; the inlets set the block's width and height. Muting and unmuting take effect on the frame of the bang.

### Song Mode

- **Songs are saved only as `.song` files.** `S` saves and `O` opens, in both the desktop app and the terminal version. Files go in `Documents/Orca Seaflake/songs`, and saving again offers the file you last used.
- **The desktop app's 16 in-app song banks (`W`/`R`, `[`/`]`) are gone.** On first launch, any bank holding a song is saved to the songs folder as `bank-01.song`, `bank-02.song` and so on.
- **Loop moved to `R`**, since `O` now opens a song.
- The panel's header shows the song's name, and its key help is laid out in grid-aligned columns. The terminal version's Song Mode screen matches.
- Slot notes written in the terminal version are kept when the song is opened and saved in the desktop app.
- New slots in the terminal version default to `LEN 16`, as in the desktop app.
- The footer at the bottom of the window stays visible when a patch or song slot is taller than the window (it used to be pushed off-screen when a slot loaded). The Song Mode panel never covers it.
- The panel's edge is a thin 2-pixel line instead of a full-width bar.
- `Cmd+T` alone opens Song Mode; `Cmd+M` minimizes the window again.
- `.orca-song` files can be dragged onto the window, and a dropped song no longer loads twice.
- Fixed: the `song:mode`, `song:save` and `song:open` commands did nothing.
- Terminal version: the file name you type after `S` or `O` is now shown on the Song Mode screen.

### Desktop app

- **Settings panel** (`Cmd+Shift+P`, or **Settings…** in the app menu on macOS and the File menu on Windows and Linux): background color, background opacity (macOS) and grid font.

### Install

- **Homebrew formula for the terminal version:** `brew install femifleming/orca_seaflake/orca-seaflake-terminal`.
- The Homebrew cask for the app now names its version, so `brew upgrade` picks up new releases.

### Docs

- New: [What Seaflake adds to Orca](docs/WHATS_NEW.md) and this changelog.
- The operator reference lists every default and describes `Y` correctly (it outputs to its right).

## 0.8.0

Not published as a release; its changes are included in 0.9.0.

- Song Mode in the desktop app.
- The terminal version joins this repository, for macOS, Windows, Linux and Raspberry Pi.
- Every install is tested on each platform before a release is published.
- Fixed a crash at launch on Ubuntu 22.04, and a terminal crash when a MIDI output can't be opened.

## 0.1.0 – 0.1.4

First releases of the desktop app: Seaflake's operators `^ & ~ + ) ( @`, macOS, Windows and Linux packages, macOS installer packages and the Homebrew cask.
