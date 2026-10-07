# Build and packaging guide

Orca Seaflake is an Electron desktop application; version 0.1.3 is the current release, following the initial 0.1.0, 0.1.1, and 0.1.2 releases. Electron Packager produces runnable application bundles; the repository workflow wraps those bundles as `.zip` archives, Linux `.tar.gz` archives, and Debian `.deb` packages.

## Requirements

- Node.js 18 or newer and npm.
- macOS packaging requires macOS. Windows and Linux targets can be packaged from supported macOS, Windows, or Linux hosts. The automated workflow uses an Intel macOS runner for x64 and an ARM64 Linux runner for the Raspberry Pi/ARM64 package.
- Debian packages require `dpkg-deb`; the GitHub Actions Linux runner already provides it.
- macOS releases are signed ad hoc to seal and verify the app bundle without an Apple account. Because they are not signed with a Developer ID or notarized, macOS may require the user to approve the first launch in Privacy & Security. v0.1.1 had an invalid resource seal, and v0.1.2 failed library validation on launch; v0.1.3 fixes both.
- Windows packages are unsigned and may show a SmartScreen warning.

Enter the desktop application folder and install exact locked dependencies:

```sh
cd desktop
npm ci
```

Run the app from source with `npm start`.

## Local app bundles

Run the matching command from `desktop/`. Electron Packager writes a directory beneath `desktop/dist/`.

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
./scripts/package-deb.sh dist/Orca-Seaflake-linux-x64 amd64 dist/orca-seaflake_0.1.3_amd64.deb
./scripts/package-deb.sh dist/Orca-Seaflake-linux-arm64 arm64 dist/orca-seaflake_0.1.3_arm64.deb
```

The package installs the app under `/usr/lib/orca-seaflake`, adds `/usr/bin/orca-seaflake`, and registers an application menu entry. The ARM64 package targets 64-bit Raspberry Pi OS and ARM64 Debian/Ubuntu. The app does not support 32-bit Raspberry Pi OS in this build.

## Automated packages

`.github/workflows/build-packages.yml` runs on pushes to `main`, version tags (`v*`), and manual dispatch. It enters `desktop/` for Electron dependency installation and packaging, then uploads downloadable workflow artifacts:

- macOS arm64 zip
- macOS x64 zip
- Windows x64 zip
- Linux x64 tar.gz and Debian amd64 package
- Linux arm64 tar.gz and Debian arm64 package

To publish versioned builds, update `version` in `desktop/package.json`, commit the change, and push a matching tag such as `v0.1.3`. The Actions run stores package artifacts for 30 days. When a version tag matching `desktop/package.json` is pushed, the workflow publishes the built packages as a GitHub Release and adds stable macOS asset names used by the Homebrew Cask in `Casks/orca-seaflake.rb`.

### macOS code signing

No Apple account or GitHub Actions secrets are needed to publish. For tagged releases, the macOS runners create an ad-hoc signature using `@electron/osx-sign`, then verify the full app bundle before archiving. The entitlements include `com.apple.security.cs.disable-library-validation` so Electron's bundled framework can load under the ad-hoc signature; this fixed the v0.1.2 launch crash. The signature does not prove the publisher's identity or satisfy Apple's notarization check, so macOS can ask users to approve the first launch. Apple documents the [Open Anyway process](https://support.apple.com/102445) for apps that are not notarized or from an identified developer. The app bundle identifier is `com.femifleming.orcaseaflake`.

## Platform notes

- Linux uses the Electron prebuilt binaries. Debian packages declare GTK, NSS, X11 screen-saver, ALSA, and GBM runtime libraries as dependencies.
- The ARM64 build uses the official Linux arm64 Electron binary. It requires a 64-bit ARM OS and a graphical desktop.
- The ad-hoc-signed macOS zip is created with `ditto`, preserving macOS bundle metadata.
- Build outputs and installed dependencies are ignored by Git (`desktop/dist/`, `outputs/`, and `desktop/node_modules/`).
- The desktop app includes MIDI, OSC, and UDP output. Actual MIDI ports depend on the operating system's MIDI services and connected devices.
