# Pilot Sealflake

A ground-up rewrite of [Pilot](https://github.com/hundredrabbits/pilot), the UDP synthesizer companion for ORCA.
It speaks the **same command language** on UDP port **49161**, so existing ORCA patches using `;` keep working.
It ships as a standalone app plus **VST3 / AU / LV2 polyphonic instrument** plug-ins for **macOS, Windows and Linux**, all
built from one C++/JUCE codebase. It lives in the [Orca Seaflake](../README.md) repository and is released with it, but
downloads and installs on its own.

## Install

| System | Download | What you get |
| - | - | - |
| macOS (Apple Silicon and Intel) | [Pilot-Sealflake-macos.pkg](https://github.com/femifleming/orca_seaflake/releases/latest/download/Pilot-Sealflake-macos.pkg) (installer) or [.zip](https://github.com/femifleming/orca_seaflake/releases/latest/download/Pilot-Sealflake-macos.zip) | app, VST3, AU |
| Windows 10 / 11 (64-bit) | [Pilot-Sealflake-windows-x64.zip](https://github.com/femifleming/orca_seaflake/releases/latest/download/Pilot-Sealflake-windows-x64.zip) | `Pilot Sealflake.exe`, VST3 |
| Ubuntu / Debian x64 | [pilot-sealflake_amd64.deb](https://github.com/femifleming/orca_seaflake/releases/latest/download/pilot-sealflake_amd64.deb): `sudo apt install ./pilot-sealflake_amd64.deb` | `pilot-sealflake`, VST3, LV2 |
| Raspberry Pi OS 64-bit, Linux ARM64 | [pilot-sealflake_arm64.deb](https://github.com/femifleming/orca_seaflake/releases/latest/download/pilot-sealflake_arm64.deb) | `pilot-sealflake`, VST3, LV2 |
| Other Linux | [x64](https://github.com/femifleming/orca_seaflake/releases/latest/download/Pilot-Sealflake-linux-x64.tar.gz) / [arm64](https://github.com/femifleming/orca_seaflake/releases/latest/download/Pilot-Sealflake-linux-arm64.tar.gz) `.tar.gz`: extract, run `./install.sh` | `~/.local/bin/pilot-sealflake`, `~/.vst3`, `~/.lv2` |

Package managers, from the one tap [femifleming/homebrew-tap](https://github.com/femifleming/homebrew-tap):

```sh
brew install --cask femifleming/tap/pilot-sealflake       # macOS: app + VST3 + AU
brew install --formula femifleming/tap/pilot-sealflake    # Linux (Homebrew on Linux)
scoop bucket add femifleming https://github.com/femifleming/homebrew-tap
scoop install femifleming/pilot-sealflake                 # Windows
```

| Where things go | macOS | Windows | Linux |
| - | - | - | - |
| App | `/Applications/Pilot Sealflake.app` | wherever you unzip it | `/usr/bin/pilot-sealflake` (.deb), `~/.local/bin` (install.sh) |
| VST3 | `/Library/Audio/Plug-Ins/VST3` (.pkg) or `~/Library/Audio/Plug-Ins/VST3` | `C:\Program Files\Common Files\VST3` (copy it there) | `/usr/lib/vst3` or `~/.vst3` |
| AU / LV2 | `…/Plug-Ins/Components` | — | `/usr/lib/lv2` or `~/.lv2` |
| Settings, Teletype scenes, presets | `~/Library/Application Support/Pilot Sealflake` | `%APPDATA%\Pilot Sealflake` | `~/.config/Pilot Sealflake` |
| Recordings (`Cmd/Ctrl+R`) | `~/Music/Pilot Sealflake` | `Music\Pilot Sealflake` | `~/Music/Pilot Sealflake` |

The builds aren't signed with a paid certificate: on macOS approve the first launch in **System Settings → Privacy &
Security → Open Anyway**; on Windows click **More info → Run anyway**.

**With Orca:** start Pilot Sealflake, then use Orca's `;` operator (Orca Seaflake or the original Orca), e.g. `;04C`. Pilot
listens on UDP port 49161, like the original Pilot.

## Commands

Pilot Sealflake takes classic Pilot commands, **real Teletype commands** and a few extended Pilot commands, on its command
line and over UDP (ORCA's `;`). **The full reference is in [COMMANDS.md](COMMANDS.md).**

```
04C;REV8c   0ENV056f   bpm140                         classic Pilot (always first)
PI.N 0 60 100   DEL 500: PI.T 12   I2M.CC 74 64       Teletype: play Pilot (PI.*), MIDI out (I2M.*)
COM.RATIO 1.4   CH.VOICES 0 1; CH.GLIDE 0 80          extended Pilot commands
```

Classic Pilot commands:

| Command | Example | Meaning |
| - | - | - |
| Play | `04C`, `04Cf`, `04Cff` | channel 0-F, octave, note (`C c D d E F f G g A a B`, H-Z wrap like Pilot), velocity 0-F, length 0-F (1/16 bar … 1 bar) |
| Envelope | `0ENV056f` | attack, decay, sustain, release |
| Oscillators | `1OSCsisq` | `si tr sq sw`; prefix `2/4/8` = detuned stereo unison (`8w` = 8-voice supersaw); `--` = off |
| Engine | `0ENGa` | `a` analog, `f` FM, `m` AM, `p` pluck (Karplus-Strong), `k` kick, `s` snare, `h` hat, `t` tom, `j` Just Friends |
| Filter | `0FIL7a8` | cutoff, resonance, envelope amount (4-pole ZDF ladder) |
| Mod | `6MOD36` | ratio, index: FM ratio/index, AM ratio/depth, analog osc2 interval/level, drum sweep/noise |
| Effects | `REV8c` | `XXX` + wet + value. `BIT DIS CHE WAH PHA CHO VIB TRE FEE DEL REV STE EQU COM VOL LIM` |
| Special | `bpm140` `renv` `rosc` `refx` `reset` `stop` `port49161` `midi` `midi4` `midirr0-7` `midirf` | |

### Effects

| Code | Effect | wet / value |
| - | - | - |
| BIT | Bitcrusher (bits + fractional decimation) | mix / crush |
| DIS | 4x-oversampled asymmetric tube saturation | mix / drive |
| CHE | 4x-oversampled Chebyshev harmonics | mix / order |
| WAH | Envelope-following resonant wah | mix / sensitivity |
| PHA | 8-stage stereo phaser with feedback | mix / depth |
| CHO | 3-voice stereo ensemble chorus | mix / depth |
| VIB | True pitch vibrato | depth / rate |
| TRE | Tempo-synced stereo tremolo | depth / rate |
| FEE | Tempo-synced tape delay (wow, filtered, saturated feedback) | mix / time |
| DEL | Dotted-1/8 ping-pong delay | mix / feedback |
| REV | 16-line modulated FDN reverb, 0.35–14 s | mix / size |
| STE | M/S widener, mono-safe bass + decorrelation | mix / width |
| EQU | Low shelf 110 Hz / high shelf 7.5 kHz, `8` = flat | low / high |
| COM | Soft-knee compressor, program-dependent release | parallel mix / amount |
| VOL | Output gain (`c` ≈ 0 dB) + tape warmth | gain / warmth |
| LIM | True-peak lookahead limiter (8x inter-sample detection), −0.3 dBTP, always on — the output never clips | drive / release |

## Teletype

Cmd+T (or `TELETYPE` in the command line, or View > Teletype) opens **monome Teletype's own firmware** (v5.0.0, compiled
in from [monome/teletype](https://github.com/monome/teletype), `teletype/`) under Pilot, in the same window: its live,
edit, pattern, scene and help screens, its 128×64 pixel display drawn in Pilot's theme colours, and its keys
(Option = alt, Cmd = win). Scripts, `DEL`, metro, patterns, `EVERY`, the stack, turtle … all behave exactly as on the module.

New Teletype ops, named after Teletype's Disting EX ops:

- `PI.*` play and change Pilot: `PI.N ch note vel`, `PI.N#`, `PI.NT`, `PI.VOX`, `PI.T`, `PI.ENG`, `PI.ENV`, `PI.FIL`, `PI.FX`,
  `PI.P x y` (every Pilot parameter), `PI.PRESET` …
- `I2M.*` (Teletype's i2c2midi ops) send real MIDI notes and CCs out of Pilot, on the virtual port **Pilot Sealflake**, the
  plugin's MIDI output and the standalone's MIDI output (Cmd+,). Teletype's `MI.*` ops read MIDI in.

Scenes are `Teletype/tt00s.txt … tt31s.txt` in Pilot's settings folder (table above), in Teletype's USB stick format.
See COMMANDS.md for every new op, the `PI.P` parameter numbers and the keys. Teletype is GPL-2.0 (`teletype/LICENSE`).

## Just Friends

`0ENGj` (or `PI.ENG 0 8`) turns a channel into **Just Friends**: six slopes with TIME, INTONE, RAMP, CURVE, FM, shape/sound
range, transient/sustain/cycle mode and every RUN mode (SHIFT, STRATA, VOLLEY, SPILL, PLUME, FLOOM). Teletype's own `JF.*`
ops (Just Type: `JF.TR`, `JF.VTR`, `JF.NOTE`, `JF.VOX`, `JF.MODE`, `JF.RUN`, Geode, `JF.TUNE`, `JF.GOD`, the getters, `JF1:` / `JF2:` …)
reach it over emulated i2c: the first JF channel is JF1 (0x70), the second JF2 (0x75). Pilot notes on a JF channel play it like
`JF.NOTE`. On screen a JF row reads TIME INTONE RAMP CURVE and range + mode (`8800 SNSU`); `0ENV`, `0OSCsncy`, `0MOD` set them.
Details in [COMMANDS.md](COMMANDS.md#just-friends--engine-j-and-teletypes-jf-ops). Test: `PilotRender jf`.

## Interface

On **Windows and Linux**, read `Cmd` as `Ctrl` below; the menus (File, View, Theme, Audio/MIDI settings) open on right-click,
and inside the Teletype screen Pilot's own shortcuts are `Ctrl+Shift` (`Ctrl+Shift+T` closes it, `Ctrl+Shift+V` pastes)
because `Ctrl` is the Teletype's ctrl key there.

It's identical to Pilot: a frameless black window (445×210), Input Mono 11px, uppercase hex readouts, per-channel waveforms with activity dots, the BPM readout that blinks while recording, and the command input.
Pilot's shortcuts are kept: `Cmd+R` record (WAV to `~/Music/Pilot Sealflake`), `Esc` stop recording, `Cmd+Backspace` reset, `Cmd+=`/`Cmd+-`/`Cmd+0` zoom, `` ` `` toggle animations, `Cmd+Enter` fullscreen, `Cmd+Shift+O` open an [Ecosystem theme](https://github.com/hundredrabbits/Themes) (or drag a theme `.svg` onto the window), `Cmd+Shift+Backspace` reset theme.
New: `Cmd+,` opens Audio/MIDI settings (standalone). Drag the window by its background.
Invisible extras (they don't change the look): drag or scroll an effect's hex digits to change them (double-click resets), and click a channel number to audition it.
App commands: `port49161` changes the UDP port; `midi` = multi-timbral MIDI, `midi0`–`midif` = all MIDI to one channel.

## Plugin notes

- MIDI routing (`MIDI.MODE`): 0 multi (MIDI ch 1-16 → Pilot 0-F, default), 1 omni (all MIDI → `MIDI.CH`, polyphonic within it),
  2 round robin (each new note → next channel in `MIDI.RR lo hi`), 3 round robin skipping channels that still hold a note.
- Each channel has 1-16 voices (`CH.VOICES`, default 8; 1 = mono with `CH.GLIDE` / `CH.LEGATO`), click-free stealing, pitch bend (`CH.BEND` range) and sustain pedal.
- All 32 effect controls are host-automatable. Effect commands sent over UDP update those parameters, so the host can record them as automation.
- Tempo follows the host unless a `bpm` command is received.
- Only one instance can own a UDP port. If the standalone app is already running, give the plugin another port with a `port49162` command.

## Build

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE 8.0.10 is downloaded on the first configure (or put a JUCE checkout in `JUCE/`). Per-platform requirements
(Visual Studio's clang-cl on Windows, the ALSA / X11 / FreeType dev packages on Linux) and the packaging scripts in
`scripts/` are described in [Building packages](../docs/PACKAGING.md#build-pilot-sealflake-locally).

`PilotRender` (offline level / alias / CPU measurements) and `PilotHostTest` (loads the VST3 like a DAW) are dev tools.
Tests: `PilotRender jf` (Just Friends + JF.* i2c), `PilotRender pi` (Teletype + PI.* + I2M MIDI out), `PilotRender lang`, `PilotRender legacy`, `PilotRender rr`;
`PilotRender ttshot <dir>` renders the Teletype screens to PNGs. After editing `teletype/src/match_token.rl`, run
`tools/regen_teletype.sh` (needs `brew install ragel`).

## License

Pilot Sealflake is GPL-2.0 (it compiles in monome Teletype's GPL-2.0 firmware); see [LICENSE.md](LICENSE.md).
Based on [Pilot](https://github.com/hundredrabbits/pilot) by Hundred Rabbits (MIT).
