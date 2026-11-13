#!/usr/bin/env bash
# Packages a Release build of Pilot Sealflake for Linux:
#   Pilot-Sealflake-<version>-linux-<arch>.tar.gz   standalone + VST3 + LV2 + install.sh
#   pilot-sealflake_<version>_<debarch>.deb          /usr/bin/pilot-sealflake, /usr/lib/vst3, /usr/lib/lv2, menu entry
# usage: pilot/scripts/package-linux.sh <build dir> <output dir> <x64|arm64>
set -euo pipefail
build="$(cd "$1" && pwd)"; out="$2"; arch="$3"; mkdir -p "$out"; out="$(cd "$out" && pwd)"
here="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^project(PilotSealflake VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")"
case "$arch" in x64) debarch=amd64 ;; arm64) debarch=arm64 ;; *) echo "arch x64|arm64" >&2; exit 2 ;; esac
art="$build/PilotSealflake_artefacts/Release"
bin="$art/Standalone/Pilot Sealflake"; vst3="$art/VST3/Pilot Sealflake.vst3"; lv2="$art/LV2/Pilot Sealflake.lv2"
test -x "$bin"; test -d "$vst3"; test -d "$lv2"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

name="Pilot-Sealflake-$version-linux-$arch"
mkdir -p "$work/$name"
cp "$bin" "$work/$name/pilot-sealflake"
cp -R "$vst3" "$lv2" "$work/$name/"
cp "$here/README.md" "$here/COMMANDS.md" "$here/Resources/icon.png" "$work/$name/"
cat > "$work/$name/install.sh" <<'SH'
#!/bin/sh
# Installs for the current user: ~/.local/bin/pilot-sealflake, ~/.vst3, ~/.lv2, and a menu entry
set -e
cd "$(dirname "$0")"
mkdir -p ~/.local/bin ~/.vst3 ~/.lv2 ~/.local/share/applications ~/.local/share/icons/hicolor/256x256/apps
cp pilot-sealflake ~/.local/bin/
rm -rf ~/.vst3/"Pilot Sealflake.vst3" ~/.lv2/"Pilot Sealflake.lv2"
cp -R "Pilot Sealflake.vst3" ~/.vst3/; cp -R "Pilot Sealflake.lv2" ~/.lv2/
cp icon.png ~/.local/share/icons/hicolor/256x256/apps/pilot-sealflake.png
printf '[Desktop Entry]\nName=Pilot Sealflake\nComment=Synthesizer for Orca, with a Teletype\nExec=%s/.local/bin/pilot-sealflake\nIcon=pilot-sealflake\nTerminal=false\nType=Application\nCategories=AudioVideo;Audio;Music;\n' "$HOME" > ~/.local/share/applications/pilot-sealflake.desktop
echo "Installed. Run pilot-sealflake (make sure ~/.local/bin is on your PATH)."
SH
chmod +x "$work/$name/install.sh" "$work/$name/pilot-sealflake"
tar -czf "$out/$name.tar.gz" -C "$work" "$name"

pkg="$work/deb"
install -d "$pkg/DEBIAN" "$pkg/usr/bin" "$pkg/usr/lib/vst3" "$pkg/usr/lib/lv2" "$pkg/usr/share/applications" "$pkg/usr/share/icons/hicolor/256x256/apps" "$pkg/usr/share/doc/pilot-sealflake"
install -m 0755 "$bin" "$pkg/usr/bin/pilot-sealflake"
cp -R "$vst3" "$pkg/usr/lib/vst3/"; cp -R "$lv2" "$pkg/usr/lib/lv2/"
install -m 0644 "$here/Resources/icon.png" "$pkg/usr/share/icons/hicolor/256x256/apps/pilot-sealflake.png"
install -m 0644 "$here/README.md" "$here/COMMANDS.md" "$pkg/usr/share/doc/pilot-sealflake/"
cat > "$pkg/usr/share/applications/pilot-sealflake.desktop" <<'DESKTOP'
[Desktop Entry]
Name=Pilot Sealflake
Comment=Synthesizer for Orca, with a Teletype
Exec=pilot-sealflake
Icon=pilot-sealflake
Terminal=false
Type=Application
Categories=AudioVideo;Audio;Music;
DESKTOP
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: pilot-sealflake
Version: $version
Section: sound
Priority: optional
Architecture: $debarch
Maintainer: Orca Seaflake contributors
Depends: libasound2 | libasound2t64, libfreetype6, libfontconfig1, libx11-6, libxext6, libxrandr2, libxinerama1, libxcursor1, libgl1
Recommends: orca-seaflake
Description: Pilot Sealflake synthesizer (Orca UDP synth, Teletype, Just Friends)
 A rewrite of Hundred Rabbits' Pilot: the synth Orca plays over UDP port 49161,
 with monome Teletype built in and a Just Friends engine. Standalone, VST3 and LV2.
CONTROL
dpkg-deb --root-owner-group --build "$pkg" "$out/pilot-sealflake_${version}_${debarch}.deb"
ls -l "$out"
