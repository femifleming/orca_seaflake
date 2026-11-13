#!/usr/bin/env bash
# Packages a Release build of Pilot Sealflake for macOS (universal: Apple Silicon + Intel):
#   Pilot-Sealflake-<version>-macos.zip   app + VST3 + AU, to drag into place
#   Pilot-Sealflake-<version>-macos.pkg   installer: app -> /Applications, VST3 / AU -> /Library/Audio/Plug-Ins
# usage: pilot/scripts/package-mac.sh <build dir> <output dir>
set -euo pipefail
build="$(cd "$1" && pwd)"; out="$2"; mkdir -p "$out"; out="$(cd "$out" && pwd)"
here="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^project(PilotSealflake VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")"
art="$build/PilotSealflake_artefacts/Release"
app="$art/Standalone/Pilot Sealflake.app"; vst3="$art/VST3/Pilot Sealflake.vst3"; au="$art/AU/Pilot Sealflake.component"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

for b in "$app" "$vst3" "$au"; do
  test -d "$b"
  codesign --force --deep --sign - "$b"          # ad-hoc signature (no paid certificate)
  codesign --verify --deep --strict "$b"
done
lipo -info "$app/Contents/MacOS/Pilot Sealflake"

mkdir -p "$work/zip/Pilot Sealflake $version"
dst="$work/zip/Pilot Sealflake $version"
ditto "$app" "$dst/Pilot Sealflake.app"; ditto "$vst3" "$dst/Pilot Sealflake.vst3"; ditto "$au" "$dst/Pilot Sealflake.component"
cp "$here/README.md" "$here/COMMANDS.md" "$dst/"
cat > "$dst/INSTALL.txt" <<TXT
Pilot Sealflake $version for macOS (Apple Silicon and Intel)

App:     drag "Pilot Sealflake.app" to Applications.
VST3:    copy "Pilot Sealflake.vst3" to ~/Library/Audio/Plug-Ins/VST3
AU:      copy "Pilot Sealflake.component" to ~/Library/Audio/Plug-Ins/Components

Or use the .pkg installer, or Homebrew:  brew install --cask femifleming/tap/pilot-sealflake
First launch: macOS may say it cannot verify the developer. Open System Settings > Privacy & Security
and click Open Anyway (or Control-click the app, Open, Open).
TXT
(cd "$work/zip" && ditto -c -k --sequesterRsrc --keepParent "Pilot Sealflake $version" "$out/Pilot-Sealflake-$version-macos.zip")

# installer: three components, one package
mkdir -p "$work/root/Applications" "$work/root/Library/Audio/Plug-Ins/VST3" "$work/root/Library/Audio/Plug-Ins/Components"
ditto "$app" "$work/root/Applications/Pilot Sealflake.app"
ditto "$vst3" "$work/root/Library/Audio/Plug-Ins/VST3/Pilot Sealflake.vst3"
ditto "$au" "$work/root/Library/Audio/Plug-Ins/Components/Pilot Sealflake.component"
pkgbuild --root "$work/root" --install-location / --identifier com.sealflake.pilot --version "$version" "$out/Pilot-Sealflake-$version-macos.pkg"
pkgutil --payload-files "$out/Pilot-Sealflake-$version-macos.pkg" | grep -Fq './Applications/Pilot Sealflake.app/Contents/Info.plist'
ls -l "$out"
