# orca_seaflake

A reconstructed C build of Hundred Rabbits' Orca-c with new operators.

## Seaflake operator

`@abcdefg` is a triggered MIDI sequence operator:

- `a`: MIDI channel
- `b`: octave
- `c`: note
- `d`: velocity
- `e`: envelope shape 1-4
- `f`: repeat count
- `g`: clock division, following the timing behavior of `D`

The envelope is evaluated across the repeat cycle. `@` and `$` are accepted as grid glyphs so files containing the Seaflake command syntax are not silently converted to dots.

## Seaflake additions

### Global swing

Swing is a **global clock setting**. It changes the timing of Orca's ticks rather than changing the behavior of an individual operator.

- **50%**: straight timing
- **66%**: triplet-style swing
- **75%**: strong swing

Swing applies to the entire running grid, including the standard operators and Seaflake operators. It is not encoded in `@abcdefg`.

### `@` Seaflake MIDI sequence

`@abcdefg` triggers a MIDI sequence with seven parameters:

| Parameter | Meaning |
|---|---|
| `a` | MIDI channel |
| `b` | Octave |
| `c` | MIDI note |
| `d` | Velocity |
| `e` | Envelope shape, 1-4 |
| `f` | Repeat count |
| `g` | Clock division / timing value |

The four envelope shapes are saw, triangle, pulse, and inverse saw. The envelope is evaluated across the repeat cycle.

### Chance / probability

The Seaflake chance operator adds probabilistic control to the grid. It can be used to make an event occur only some percentage of the time, allowing generative patterns to vary from one tick to the next without manually duplicating grid logic.

### Additional MIDI operators

The Seaflake build also includes the expanded MIDI operators from the reconstructed development version. These are intended for direct MIDI event generation and sequencing alongside Orca's existing MIDI, MIDI CC, and MIDI pitch-bend operators.

The exact operator syntax is displayed in Orca's operator reference/help screen so the documentation stays synchronized with the compiled operator set.

### `# orca_seaflake

A reconstructed C build of Hundred Rabbits' Orca-c with new operators.

## Seaflake operator

`@abcdefg` is a triggered MIDI sequence operator:

- `a`: MIDI channel
- `b`: octave
- `c`: note
- `d`: velocity
- `e`: envelope shape 1-4
- `f`: repeat count
- `g`: clock division, following the timing behavior of `D`

The envelope is evaluated across the repeat cycle. `@` and `$` are accepted as grid glyphs so files containing the Seaflake command syntax are not silently converted to dots.
 grid commands

The C build accepts `# orca_seaflake

A reconstructed C build of Hundred Rabbits' Orca-c with new operators.

## Seaflake operator

`@abcdefg` is a triggered MIDI sequence operator:

- `a`: MIDI channel
- `b`: octave
- `c`: note
- `d`: velocity
- `e`: envelope shape 1-4
- `f`: repeat count
- `g`: clock division, following the timing behavior of `D`

The envelope is evaluated across the repeat cycle. `@` and `$` are accepted as grid glyphs so files containing the Seaflake command syntax are not silently converted to dots.
 as a valid grid character, preventing Seaflake grids containing dollar-sign command syntax from being replaced with `.` during input. Full command behavior is platform-dependent and remains separate from the C VM's operator system.

## Downloads

GitHub Releases are configured for macOS Apple Silicon and Intel, Linux x86_64, Debian/Raspberry Pi OS ARM64, and Windows x86_64. The Raspberry Pi target is 64-bit ARM (aarch64).

## Build from source

### macOS

`brew install ncurses portmidi`

`./tool build --portmidi orca`

### Debian / Ubuntu / Raspberry Pi OS

`sudo apt install build-essential pkg-config libncursesw5-dev libform-dev libportmidi-dev`

`./tool build --portmidi orca`

### CLI

`./tool build cli`

## Homebrew

The intended tap is `femifleming/homebrew-orca-seaflake`. Once that tap exists, install with `brew install femifleming/homebrew-orca-seaflake/orca-seaflake`.

The formula source is kept under `packaging/homebrew/`.

## Windows

The Windows build uses MinGW-w64 and PDCurses for the native console layer, with PortMidi for MIDI I/O. The release artifact is a ZIP containing the executable and runtime DLLs.

## Source

The base C implementation is derived from Hundred Rabbits' Orca-c. See LICENSE.md for the upstream license and preserve attribution when redistributing.

## Status

This repository is a reconstruction rather than a claim that the original local build directory still exists. The custom Seaflake operator is implemented directly in the VM and the build/release configuration is kept in this repository so future builds are reproducible.
