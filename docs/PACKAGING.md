# Build and packaging guide

Orca Seaflake is an Electron desktop application; version 0.1.1 is the current release, following the initial 0.1.0 release. Electron Packager produces runnable application bundles; the repository workflow wraps those bundles as `.zip` archives, Linux `.tar.gz` archives, and Debian `.deb` packages.

## Requirements

- Node.js 18 or newer and npm.
- macOS packaging requires macOS. Windows and Linux targets can be packaged from supported macOS, Windows, or Linux hosts. The automated workflow uses an Intel macOS runner for x64 and an ARM64 Linux runner for the Raspberry Pi/ARM64 package.
- Debian packages require `dpkg-deb`; the GitHub Actions Linux runner already provides it.
- Versioned macOS releases must be Developer ID signed and notarized. The current v0.1.1 macOS bundle has an invalid signature and may be rejected by Gatekeeper as damaged. The release workflow now refuses to publish a version tag unless signing and notarization complete successfully.
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
./scripts/package-deb.sh dist/Orca-Seaflake-linux-x64 amd64 dist/orca-seaflake_0.1.1_amd64.deb
./scripts/package-deb.sh dist/Orca-Seaflake-linux-arm64 arm64 dist/orca-seaflake_0.1.1_arm64.deb
```

The package installs the app under `/usr/lib/orca-seaflake`, adds `/usr/bin/orca-seaflake`, and registers an application menu entry. The ARM64 package targets 64-bit Raspberry Pi OS and ARM64 Debian/Ubuntu. The app does not support 32-bit Raspberry Pi OS in this build.

## Automated packages

`.github/workflows/build-packages.yml` runs on pushes to `main`, version tags (`v*`), and manual dispatch. It enters `desktop/` for Electron dependency installation and packaging, then uploads downloadable workflow artifacts:

- macOS arm64 zip
- macOS x64 zip
- Windows x64 zip
- Linux x64 tar.gz and Debian amd64 package
- Linux arm64 tar.gz and Debian arm64 package

To publish versioned builds, update `version` in `desktop/package.json`, commit the change, and push a matching tag such as `v0.1.2`. The Actions run stores package artifacts for 30 days. When a version tag matching `desktop/package.json` is pushed (for example, `v0.1.1`), the workflow publishes the built packages as a GitHub Release and adds stable macOS asset names used by the Homebrew Cask in `Casks/orca-seaflake.rb`.

### Configure macOS release signing

macOS release tags fail unless these GitHub Actions secrets are present in **Settings → Secrets and variables → Actions** for the repository:

Apple recommends signing direct-download Mac apps with a **Developer ID Application** certificate and notarizing the signed build. See [Apple's signing overview](https://developer.apple.com/developer-id/) and [notarization guide](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution).

| Secret | Value |
|---|---|
| `APPLE_CERTIFICATE_P12_BASE64` | Base64-encoded `.p12` export of a **Developer ID Application** certificate, including its private key. On macOS, copy it with `base64 -i DeveloperIDApplication.p12 | tr -d '\n' | pbcopy`. |
| `APPLE_CERTIFICATE_PASSWORD` | Password used when exporting that `.p12` file. |
| `APPLE_DEVELOPER_IDENTITY` | Exact signing identity, for example `Developer ID Application: Your Name (ABCDE12345)`. |
| `APPLE_ID` | Apple Developer account email address. |
| `APPLE_APP_SPECIFIC_PASSWORD` | App-specific password generated for notarization; do not use the normal Apple ID password. |
| `APPLE_TEAM_ID` | Team ID associated with the Developer ID certificate. |

Create the Developer ID Application certificate in the Apple Developer account, export it from Keychain Access together with its private key, and generate an app-specific password from the Apple ID account page. Do not commit the certificate or put its password in source files. The workflow imports the certificate into a temporary keychain, signs the app with hardened runtime and the entitlements required by this Electron 11 build, submits it to Apple's notarization service, staples the ticket, and checks the signature and Gatekeeper assessment before creating the release zip. The app bundle identifier is `com.femifleming.orcaseaflake`.

Pull request, branch, and manual workflow builds remain unsigned and are for testing only. A version tag will fail with a list of missing secrets rather than publish an unsigned macOS release.

## Platform notes

- Linux uses the Electron prebuilt binaries. Debian packages declare GTK, NSS, X11 screen-saver, ALSA, and GBM runtime libraries as dependencies.
- The ARM64 build uses the official Linux arm64 Electron binary. It requires a 64-bit ARM OS and a graphical desktop.
- The signed and notarized macOS zip is created with `ditto` after stapling, preserving macOS bundle metadata.
- Build outputs and installed dependencies are ignored by Git (`desktop/dist/`, `outputs/`, and `desktop/node_modules/`).
- The desktop app includes MIDI, OSC, and UDP output. Actual MIDI ports depend on the operating system's MIDI services and connected devices.
