# Building packages

This page is for people building Orca Seaflake themselves or publishing a release. To just use it, see the [README](../README.md#install--run).

## What gets built

| Package | Built from | For |
| --- | --- | --- |
| `Orca-Seaflake-<version>-darwin-arm64.pkg` / `.zip` | `desktop/` | Macs with Apple Silicon |
| `Orca-Seaflake-<version>-darwin-x64.pkg` / `.zip` | `desktop/` | Macs with Intel |
| `Orca-Seaflake-<version>-win32-x64.zip` | `desktop/` | Windows 10/11, 64-bit |
| `orca-seaflake_<version>_amd64.deb`, `Orca-Seaflake-<version>-linux-x64.tar.gz` | `desktop/` | Linux, Intel/AMD |
| `orca-seaflake_<version>_arm64.deb`, `Orca-Seaflake-<version>-linux-arm64.tar.gz` | `desktop/` | Raspberry Pi OS 64-bit, Linux ARM64 |
| `orca-seaflake-terminal-<version>.tgz` | `terminal/` | Terminal version, every OS (installed with `npm install -g`) |
| `Pilot-Sealflake-<pilot version>-macos.pkg` / `.zip` | `pilot/` | Pilot Sealflake app + VST3 + AU, macOS universal (Apple Silicon and Intel) |
| `Pilot-Sealflake-<pilot version>-windows-x64.zip` | `pilot/` | Pilot Sealflake app + VST3, Windows 10/11 64-bit |
| `pilot-sealflake_<pilot version>_amd64.deb`, `Pilot-Sealflake-<pilot version>-linux-x64.tar.gz` | `pilot/` | Pilot Sealflake app + VST3 + LV2, Linux Intel/AMD |
| `pilot-sealflake_<pilot version>_arm64.deb`, `Pilot-Sealflake-<pilot version>-linux-arm64.tar.gz` | `pilot/` | Pilot Sealflake app + VST3 + LV2, Linux ARM64 / Raspberry Pi OS 64-bit |

Pilot Sealflake has its own version number (`project(PilotSealflake VERSION …)` in `pilot/CMakeLists.txt`) and ships in the same release as Orca Seaflake.

Each release also carries copies without the version number in the name (for example `Orca-Seaflake-win32-x64.zip`). The README links and the Homebrew cask use those, so they always point at the newest release.

## Build the desktop app locally

You need Node.js 18 or newer. macOS packages must be built on a Mac; the others can be built on any OS.

```sh
cd desktop
npm ci
npm test                       # Song Mode tests
npm run package:mac:arm64      # or package:mac:x64, package:windows:x64, package:linux:x64, package:linux:arm64
```

The app is written to `desktop/dist/`. For a Mac app on your Desktop in one step, run `npm run app:mac`.

To make a Debian package from a Linux build:

```sh
./scripts/package-deb.sh dist/Orca-Seaflake-linux-x64 amd64 dist/orca-seaflake_amd64.deb
```

It installs the app in `/usr/lib/orca-seaflake`, adds the `orca-seaflake` command, and adds a menu entry.

## Build the terminal package locally

```sh
cd terminal
npm ci
npm test
npm pack                        # writes orca-seaflake-terminal-<version>.tgz
npm install -g ./orca-seaflake-terminal-<version>.tgz
```

## Build Pilot Sealflake locally

Pilot is C++ (JUCE 8, downloaded by CMake on the first configure unless `pilot/JUCE` exists) with monome Teletype's C firmware compiled in. You need CMake 3.22+ and Ninja.

```sh
cmake -S pilot -B pilot/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build pilot/build
pilot/build/PilotRender_artefacts/Release/PilotRender jf     # tests: jf, pi, legacy, lang, rr
```

| System | Extra setup | Packages with |
| --- | --- | --- |
| macOS | Xcode command line tools. Add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build | `pilot/scripts/package-mac.sh pilot/build dist` |
| Windows | Visual Studio 2022 with the **C++ Clang tools** component; configure from a *x64 Native Tools* prompt with `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` | `pilot/scripts/package-windows.ps1 -Build pilot/build -Out dist` |
| Linux | `sudo apt install build-essential cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev` | `pilot/scripts/package-linux.sh pilot/build dist x64` (or `arm64`) |

On macOS a build also copies the plug-ins into `~/Library/Audio/Plug-Ins` (turn off with `-DPILOT_COPY_PLUGINS=OFF`).

## What CI checks

`.github/workflows/build-packages.yml` runs on every pull request and every push to `main`. Besides building, it **installs and launches** each package the way a user would:

| Check | Where |
| --- | --- |
| Install the `.pkg`, launch the app, confirm it keeps running | macOS Apple Silicon, macOS Intel |
| Unzip, launch `orca-seaflake.exe`, confirm it keeps running | Windows |
| `sudo apt install ./….deb`, launch, confirm it keeps running | Ubuntu 22.04 and 24.04, x64 and ARM64 |
| Terminal: tests, `npm install -g` of the packed file, `--version`, `--midi` | macOS (both chips), Windows, Ubuntu x64 and ARM64, Node 18 to 24 |
| Terminal: the README's `apt` install steps in a clean system, with no compilers | Debian bookworm and trixie ARM64 (Raspberry Pi OS 64-bit), trixie 32-bit ARM (emulated Raspberry Pi OS 32-bit) |
| Pilot: build, the test suites (Just Friends, Teletype + `PI.*`, classic Pilot, language, round robin), load the VST3 in a test host | macOS universal, Windows x64, Linux x64 and ARM64 |
| Pilot: `auval` on the Audio Unit; install the `.pkg`, launch the app | macOS |
| Pilot: unzip, launch `Pilot Sealflake.exe` | Windows |
| Pilot: `sudo apt install ./….deb`, launch; the `.tar.gz`'s `install.sh` | Ubuntu 22.04 and 24.04, x64 and ARM64 |

## Publish a release

1. Set the same `version` in `desktop/package.json` and `terminal/package.json`, and run `npm install --package-lock-only` in both folders. If Pilot changed, raise its version in `pilot/CMakeLists.txt`.
2. Commit, then push a matching tag:

   ```sh
   git tag v0.9.0
   git push origin v0.9.0
   ```

The workflow builds everything, runs every check above, and publishes a GitHub Release **only if all of them pass**. It refuses to publish if the tag doesn't match both versions.

## Signing

- **macOS:** apps get an ad-hoc signature (no Apple account needed), which keeps the bundle intact and lets Apple Silicon run it. It isn't a Developer ID signature or notarized, so macOS asks users to approve the first launch ([Apple's Open Anyway steps](https://support.apple.com/102445)). The entitlements include `com.apple.security.cs.disable-library-validation` so Electron's framework loads under the ad-hoc signature. The bundle identifier is `com.femifleming.orcaseaflake`.
- **Windows:** unsigned, so SmartScreen shows a warning on first launch.

## Homebrew and Scoop

Everything installs from one tap, [femifleming/homebrew-tap](https://github.com/femifleming/homebrew-tap) (`brew tap femifleming/tap`), which is also a Scoop bucket for Windows. This repo's entries there:

| File | Installs |
| --- | --- |
| `Casks/orca-seaflake.rb` | Orca Seaflake desktop app (macOS), the `.zip` for the Mac's chip |
| `Formula/orca-seaflake-terminal.rb` | the terminal version (macOS, Linux), with Homebrew's Node.js |
| `Casks/pilot-sealflake.rb` | Pilot Sealflake app, VST3 and AU (macOS) |
| `Formula/pilot-sealflake.rb` | Pilot Sealflake app, VST3 and LV2 (Linux x64 / ARM64) |
| `bucket/orca-seaflake.json`, `bucket/pilot-sealflake.json` | Scoop manifests (Windows) |

After a release is published, update them all:

```sh
scripts/update-homebrew.sh 0.10.0
```

The script reads the release's checksums from GitHub, writes the new versions into a fresh clone of the tap and pushes it. No copies are kept in this repo.

## Platform notes

- Pilot Sealflake on Windows and Linux: the menus are on right-click, and Pilot's shortcuts inside the Teletype screen use `Ctrl+Shift` (`Ctrl+Shift+T` closes it), because `Ctrl` belongs to the Teletype there. Its files live in `%APPDATA%\Pilot Sealflake` (Windows) and `~/.config/Pilot Sealflake` (Linux). The virtual MIDI output port is macOS / Linux only; on Windows pick a MIDI output in Audio/MIDI settings.
- The Linux packages need a graphical desktop. The `.deb` pulls in its libraries (GTK 3, NSS, libXss, ALSA, GBM); with the `.tar.gz` you install them yourself.
- The desktop app needs a 64-bit OS. On 32-bit Raspberry Pi OS, use the terminal version.
- The terminal version uses `@julusian/midi`, which ships prebuilt MIDI binaries for macOS, Windows, Linux x64/ARM64 and 32-bit ARM, so installing needs no compiler. Linux needs the ALSA library (`alsa-utils` installs it). On other platforms it compiles, which needs a C++ toolchain.
