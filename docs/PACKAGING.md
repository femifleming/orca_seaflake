# Build and packaging guide

Orca Seaflake is an Electron desktop application; version 0.1.0 is its first public release. Electron Packager produces runnable application bundles; the repository workflow wraps those bundles as `.zip` archives, Linux `.tar.gz` archives, and Debian `.deb` packages.

## Requirements

- Node.js 18 or newer and npm.
- macOS packaging requires macOS. Windows and Linux targets can be packaged from supported macOS, Windows, or Linux hosts. The automated workflow uses an Intel macOS runner for x64 and an ARM64 Linux runner for the Raspberry Pi/ARM64 package.
- Debian packages require `dpkg-deb`; the GitHub Actions Linux runner already provides it.
- App signing/notarization is not configured. macOS Gatekeeper or Windows SmartScreen may display warnings for unsigned builds.

Install exact locked dependencies:

```sh
npm ci
```

Run the app from source with `npm start`.

## Local app bundles

Run the matching command on the repository root. Electron Packager writes a directory beneath `dist/`.

| Target | Command |
|---|---|
| macOS Apple Silicon | `npm run package:mac:arm64` |
| macOS Intel | `npm run package:mac:x64` |
| Windows x64 | `npm run package:windows:x64` |
| Linux x64 | `npm run package:linux:x64` |
| Linux ARM64 | `npm run package:linux:arm64` |

For macOS, zip the generated `.app` bundle while preserving its directory structure. For Windows, zip the complete generated app folder. For Linux, tar the complete generated app folder; it can be launched with `./orca-seaflake`.

## Debian packages

On Debian or Ubuntu, first build the Linux bundle, then run:

```sh
./scripts/package-deb.sh dist/Orca-Seaflake-linux-x64 amd64 dist/orca-seaflake_0.1.0_amd64.deb
./scripts/package-deb.sh dist/Orca-Seaflake-linux-arm64 arm64 dist/orca-seaflake_0.1.0_arm64.deb
```

The package installs the app under `/usr/lib/orca-seaflake`, adds `/usr/bin/orca-seaflake`, and registers an application menu entry. The ARM64 package targets 64-bit Raspberry Pi OS and ARM64 Debian/Ubuntu. The app does not support 32-bit Raspberry Pi OS in this build.

## Automated packages

`.github/workflows/build-packages.yml` runs on pushes to `main`, version tags (`v*`), and manual dispatch. It uploads five downloadable workflow artifacts:

- macOS arm64 zip
- macOS x64 zip
- Windows x64 zip
- Linux x64 tar.gz and Debian amd64 package
- Linux arm64 tar.gz and Debian arm64 package

To publish versioned builds, update `version` in `package.json`, commit the change, and push a matching tag such as `v0.1.1`. The Actions run stores the package artifacts for download; the workflow does not create or publish a GitHub Release automatically.

## Platform notes

- Linux uses the Electron prebuilt binaries. Debian packages declare GTK, NSS, X11 screen-saver, ALSA, and GBM runtime libraries as dependencies.
- The ARM64 build uses the official Linux arm64 Electron binary. It requires a 64-bit ARM OS and a graphical desktop.
- Build outputs and installed dependencies are ignored by Git (`dist/`, `outputs/`, and `node_modules/`).
- The desktop app includes MIDI, OSC, and UDP output. Actual MIDI ports depend on the operating system's MIDI services and connected devices.
