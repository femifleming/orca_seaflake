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
