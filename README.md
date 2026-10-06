# ORCΛ SEΛFLΛKE

<img src="icon.png" width="300" alt="Orca Seaflake icon"/>

Orca Seaflake is a modified desktop build of [Orca](https://github.com/hundredrabbits/Orca), the esoteric programming language for quickly creating procedural sequencers. Each letter is an operation: lowercase operators run on bang, while uppercase operators run each frame.

This application **is not a synthesizer, but a livecoding environment** capable of sending MIDI, OSC, and UDP to audio and visual software or hardware. Build 04 adds probability, phase, burst, tuned-note, binary-scale, and shaped MIDI-sequence operators.

- [Download builds](https://github.com/femifleming/orca_seaflake/actions/workflows/build-packages.yml), available for **macOS, Windows, Linux, Debian/Ubuntu, and 64-bit Raspberry Pi OS**. Open the latest successful **Build packages** run and download its artifacts.
- [Run from source](#install--run), using Node.js and npm.
- Read the [Build 04 operator guide](docs/OPERATORS.md) for operand positions, defaults, ranges, examples, and triggering behavior.
- Read the [packaging guide](docs/PACKAGING.md) to create packages locally.

## Install & Run

### Download a packaged build

Open [Build packages](https://github.com/femifleming/orca_seaflake/actions/workflows/build-packages.yml), select the latest successful run, and download the artifact for your system. Extract it, then launch **Orca-Seaflake**. The workflow keeps artifacts for 30 days.

| System | Download |
| --- | --- |
| macOS Apple Silicon | `.zip` app bundle (`arm64`) |
| macOS Intel | `.zip` app bundle (`x64`) |
| Windows | `.zip` app folder and executable (`x64`) |
| Linux | `.tar.gz` app folder (`x64` or `arm64`) |
| Debian / Ubuntu | `.deb` installer (`amd64` or `arm64`) |
| Raspberry Pi OS | `.deb` or `.tar.gz` (`arm64`, 64-bit OS only) |

The Debian package installs an application launcher and the `orca-seaflake` command. Linux builds need a graphical desktop and MIDI/ALSA system libraries; see [Linux packaging notes](docs/PACKAGING.md#linux-notes). 32-bit Raspberry Pi OS is not packaged.

### Run from source

Install [Node.js](https://nodejs.org/) 18 or newer, then run:

```sh
git clone https://github.com/femifleming/orca_seaflake.git
cd orca_seaflake
npm ci
npm start
```

Choose a MIDI output with **MIDI → Next Output Device** (`Cmd/Ctrl+.`); refresh devices with `Cmd/Ctrl+Shift+M`. Set OSC and UDP destinations in **Communication**. Press `Cmd/Ctrl+G` to show the operator guide or `Cmd/Ctrl+K` to open the command prompt.

## Operators

The full operator list is available in the built-in guide with `Cmd/Ctrl+G`. Build 04 adds:

- `^` **probability**: passes a bang according to its probability operands.
- `&` **phase clock**: emits a phase-based clock pulse.
- `~` **timed burst**: emits a timed series of bangs.
- `(` **just intonation**: sends tuned MIDI notes.
- `)` **binary scale note**: selects notes from the active binary scale.
- `+` **binary scale**: defines the scale used by `)`.
- `@` **shaped MIDI sequence**: controls channel, octave, note, velocity, envelope, repeats, duration, and pulse width.

Existing Orca operators and the Seaflake IO/MIDI operators are documented in the [operator reference](docs/OPERATORS.md), including operand order, valid values, defaults, and working patterns.

### IO

- `:` **midi**: sends MIDI notes.
- `%` **mono**: sends monophonic MIDI notes.
- `!` **cc**: sends MIDI control change.
- `?` **pitch bend**: sends MIDI pitch-bend values.
- `=` **osc**: sends OSC messages.
- `;` **udp**: sends UDP messages.
- `$` **self**: sends an Orca command.

See the operator guide for exact inputs and examples for these operators.

## Companion Applications

Orca Seaflake can send MIDI, OSC, and UDP to compatible applications and hardware, including:

- [Pilot](https://github.com/hundredrabbits/pilot), a companion synth tool.
- [Aioi](https://github.com/MAKIO135/aioi), for complex OSC messages.
- [Sonic Pi](https://sonic-pi.net/), a livecoding environment.
- Any MIDI, OSC, or UDP receiver configured for the selected destination ports.

## Links

- [Original Orca](https://github.com/hundredrabbits/Orca)
- [Build packages and downloads](https://github.com/femifleming/orca_seaflake/actions/workflows/build-packages.yml)
- [Build 04 operator reference](docs/OPERATORS.md)
- [Packaging guide](docs/PACKAGING.md)

## Extras

- Orca Seaflake is derived from [Hundred Rabbits' Orca](https://github.com/hundredrabbits/Orca).
- See [LICENSE.md](LICENSE.md) for license rights and limitations (MIT).
- Contributions are welcome.
