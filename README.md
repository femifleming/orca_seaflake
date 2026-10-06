# Orca Seaflake

Orca Seaflake is a desktop grid sequencer for live coding MIDI, OSC, and UDP. This repository contains **Build 04**, the Electron desktop application source, including its Seaflake operator additions.

## Install a packaged build

Download the artifact for your system from the repository's **Actions → Build packages** workflow (or from a GitHub Release when a release is published). Extract the archive and launch Orca Seaflake. Debian packages install a desktop entry and the `orca-seaflake` launcher.

| System | Package produced |
|---|---|
| macOS Apple Silicon | `.zip` containing `Orca-Seaflake.app` (arm64) |
| macOS Intel | `.zip` containing `Orca-Seaflake.app` (x64) |
| Windows x64 | `.zip` containing the Windows app folder and `orca-seaflake.exe` |
| Linux x64 | `.tar.gz` and Debian `.deb` (amd64) |
| Linux ARM64 | `.tar.gz` and Debian `.deb` (arm64), for 64-bit Raspberry Pi OS and other ARM64 Debian/Ubuntu systems |

Raspberry Pi OS must be the 64-bit ARM64 edition. 32-bit ARM is not packaged. Linux packages require a graphical desktop and ALSA/MIDI system libraries; see [Linux notes](docs/PACKAGING.md).

## Run from source

Requirements: Node.js 18 or newer and npm.

```sh
npm ci
npm start
```

Choose a MIDI output with **MIDI → Next Output Device** (`Cmd/Ctrl+.`); refresh hardware with `Cmd/Ctrl+Shift+M`. Orca Seaflake can also send OSC and UDP; choose the destination ports from **Communication**. Press `Cmd/Ctrl+G` to show the built-in operator guide and `Cmd/Ctrl+K` to open the command prompt. See the complete [operator reference](docs/OPERATORS.md) and [packaging guide](docs/PACKAGING.md).

## Package locally

From macOS, Windows, or Linux, install dependencies with `npm ci`, then run the script for the target. macOS builds must be run on macOS. Windows and Linux app bundles may also be cross-packaged from the other supported hosts by Electron Packager.

```sh
npm run package:mac:arm64
npm run package:mac:x64
npm run package:windows:x64
npm run package:linux:x64
npm run package:linux:arm64
```

Packaged application folders are written under `dist/`. The GitHub Actions workflow creates downloadable archives and Debian packages for all listed targets when run manually or when code is pushed to `main`.

## Build 04 operator additions

- `^` probability gate, `&` phase clock, and `~` timed burst.
- `(` just-intonation MIDI notes, `)` notes selected from a stored binary scale, and `+` binary-scale definition.
- `@` shaped MIDI sequence, with eight operands for channel, octave, note, velocity, envelope, repeats, duration, and pulse width.
- `%` monophonic MIDI, `!` MIDI control change, and `?` MIDI pitch bend.

The full operand positions, ranges, defaults, examples, and triggering rules are in [docs/OPERATORS.md](docs/OPERATORS.md).

## License and upstream

This is a modified desktop build of [Hundred Rabbits' Orca](https://github.com/hundredrabbits/Orca). Upstream attribution and license terms are in [LICENSE.md](LICENSE.md).
