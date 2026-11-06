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

## What CI checks

`.github/workflows/build-packages.yml` runs on every pull request and every push to `main`. Besides building, it **installs and launches** each package the way a user would:

| Check | Where |
| --- | --- |
| Install the `.pkg`, launch the app, confirm it keeps running | macOS Apple Silicon, macOS Intel |
| Unzip, launch `orca-seaflake.exe`, confirm it keeps running | Windows |
| `sudo apt install ./….deb`, launch, confirm it keeps running | Ubuntu 22.04 and 24.04, x64 and ARM64 |
| Terminal: tests, `npm install -g` of the packed file, `--version`, `--midi` | macOS (both chips), Windows, Ubuntu x64 and ARM64, Node 18 to 24 |
| Terminal: the README's `apt` install steps in a clean system, with no compilers | Debian bookworm and trixie ARM64 (Raspberry Pi OS 64-bit), trixie 32-bit ARM (emulated Raspberry Pi OS 32-bit) |

## Publish a release

1. Set the same `version` in `desktop/package.json` and `terminal/package.json`, and run `npm install --package-lock-only` in both folders.
2. Commit, then push a matching tag:

   ```sh
   git tag v0.8.0
   git push origin v0.8.0
   ```

The workflow builds everything, runs every check above, and publishes a GitHub Release **only if all of them pass**. It refuses to publish if the tag doesn't match both versions.

## Signing

- **macOS:** apps get an ad-hoc signature (no Apple account needed), which keeps the bundle intact and lets Apple Silicon run it. It isn't a Developer ID signature or notarized, so macOS asks users to approve the first launch ([Apple's Open Anyway steps](https://support.apple.com/102445)). The entitlements include `com.apple.security.cs.disable-library-validation` so Electron's framework loads under the ad-hoc signature. The bundle identifier is `com.femifleming.orcaseaflake`.
- **Windows:** unsigned, so SmartScreen shows a warning on first launch.

## Homebrew

`Casks/orca-seaflake.rb` here is a copy of the cask in [femifleming/homebrew-orca_seaflake](https://github.com/femifleming/homebrew-orca_seaflake), which is what `brew install` uses. It downloads the fixed-name macOS zips from the latest release, so new releases need no cask change.

## Platform notes

- The Linux packages need a graphical desktop. The `.deb` pulls in its libraries (GTK 3, NSS, libXss, ALSA, GBM); with the `.tar.gz` you install them yourself.
- The desktop app needs a 64-bit OS. On 32-bit Raspberry Pi OS, use the terminal version.
- The terminal version uses `@julusian/midi`, which ships prebuilt MIDI binaries for macOS, Windows, Linux x64/ARM64 and 32-bit ARM, so installing needs no compiler. Linux needs the ALSA library (`alsa-utils` installs it). On other platforms it compiles, which needs a C++ toolchain.
