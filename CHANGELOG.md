# Changelog

Orca Seaflake's releases. For everything Seaflake adds to Orca, see [What Seaflake adds to Orca](docs/WHATS_NEW.md).

## 0.10.0

**Pilot Sealflake joins the repository.** The synth Orca plays with `;` is now built and released here, next to Orca Seaflake, for macOS, Windows and Linux. The two still download and install separately ([Orca Seaflake](README.md#desktop-app), [Pilot Sealflake](README.md#pilot-sealflake)).

- **Pilot Sealflake 1.1.0**, new in this release:
  - **Just Friends engine** (`0ENGj`, `PI.ENG ch 8`): six slopes with TIME, INTONE, RAMP, CURVE, FM, shape/sound range, transient/sustain/cycle and every RUN mode (SHIFT, STRATA, VOLLEY, SPILL, PLUME, FLOOM), plus Just Type synthesis and Geode. Teletype's own `JF.*` ops play it over emulated i2c (first JF channel = JF1, second = JF2), and Pilot notes play it like `JF.NOTE`. See the [Just Friends reference](pilot/COMMANDS.md#just-friends--engine-j-and-teletypes-jf-ops).
  - `RENV`, `ROSC` and `REFX` work in the Teletype as well as `PI.RENV`, `PI.ROSC` and `PI.REFX`.
  - Windows and Linux builds (standalone + VST3, LV2 on Linux) next to macOS (standalone + VST3 + AU, now universal: Apple Silicon and Intel). On Windows and Linux the menus are on right-click, and Pilot's shortcuts inside the Teletype screen are `Ctrl+Shift`.
  - Fixed: Teletype's `JF.PITCH` sent two bytes past its message.
- **Homebrew moved to one tap**: `brew install --cask femifleming/tap/orca-seaflake`, `brew install femifleming/tap/orca-seaflake-terminal`, and new: `brew install --cask femifleming/tap/pilot-sealflake`.
- Includes the 0.9.1 terminal fixes below (0.9.1 was not released on its own).

## 0.9.1

Fixes for the terminal version. The desktop app is unchanged.

- **Fixed: moving the mouse typed random characters into the grid.** The terminal reported every mouse movement, and those reports were read as typed keys (`3`, `5`, `;`, `M`...). The app now reads all terminal input with its own parser: mouse reports, terminal replies and keys it doesn't know are never typed, and mouse reporting only covers clicks and drags.
- **Fixed: the terminal kept typing escape codes after the app quit or crashed.** Mouse reporting, raw mode and the hidden cursor are now always switched off on the way out, including after a crash or when the window is closed.
- **Fast travel works in every terminal.** `Shift`, `Option`/`Alt` and `Ctrl` + arrow leap 8 cells, in all the ways terminals send them (including Option+arrow in macOS Terminal and iTerm2). `Shift` + arrow no longer starts a selection as well. New: `Page Up` / `Page Down` jump a screen, `Home` / `End` jump to the ends of the row, and `Ctrl` + `Home` / `End` to the corners.
- **Patches larger than the window scroll** to follow the cursor instead of being cut off, and nothing is lost when the window is made smaller. Arrows on the frame show where there's more grid; the cursor position is shown at the top; `+` marks every 8th cell like the desktop app.
- **`+` writes the `+` operator.** Tempo is now `>` / `<`, as in the desktop app.
- Pasting with `Cmd+V` places the whole block at the cursor (it used to write every character into the same cell), and pasting into the command or file prompt works.
- iTerm2, kitty, WezTerm and foot's extended key reports (CSI u) are understood, so shortcuts work there.
- No more flicker while playing, and narrow windows no longer scroll the screen: long lines are cut to fit. The app uses the terminal's alternate screen, so your scrollback is left as it was.
- `Ctrl+A` selects the whole grid, `Ctrl+Y` redoes, `Esc` also ends a selection, and clicking without dragging just moves the cursor.
- File paths in the Open prompt accept `~` and files dragged onto the window.

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

- **The operator guide** shown at launch (and with `Cmd+G`) fits the window: no more overlapping text, and no operators cut off at the right edge. Seaflake's operator descriptions are now as short as Orca's.
- **Settings panel** (`Cmd+Shift+P`, or **Settings…** in the app menu on macOS and the File menu on Windows and Linux): background color, background opacity (macOS) and grid font.

### Install

- **Homebrew formula for the terminal version:** `brew install femifleming/orca_seaflake/orca-seaflake-terminal` (today: `brew install femifleming/tap/orca-seaflake-terminal`).
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
