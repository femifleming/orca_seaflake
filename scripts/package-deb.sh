#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  printf 'Usage: %s APP_DIR DEB_ARCH OUTPUT\n' "$0" >&2
  exit 2
fi

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
app_dir="$(cd "$1" && pwd)"
deb_arch="$2"
output="$3"
cd "$repo_dir"
version="$(node -p "require('./package.json').version")"
work_dir="$(mktemp -d)"
package_dir="$work_dir/orca-seaflake"
trap 'rm -rf "$work_dir"' EXIT

if [[ "$deb_arch" != amd64 && "$deb_arch" != arm64 ]]; then
  printf 'Unsupported Debian architecture: %s\n' "$deb_arch" >&2
  exit 2
fi
if [[ ! -x "$app_dir/orca-seaflake" ]]; then
  printf 'Packaged launcher not found: %s/orca-seaflake\n' "$app_dir" >&2
  exit 2
fi

install -d "$package_dir/DEBIAN" \
  "$package_dir/usr/lib/orca-seaflake" \
  "$package_dir/usr/bin" \
  "$package_dir/usr/share/applications" \
  "$package_dir/usr/share/icons/hicolor/256x256/apps"
cp -R "$app_dir/." "$package_dir/usr/lib/orca-seaflake/"
install -m 0644 icon.png "$package_dir/usr/share/icons/hicolor/256x256/apps/orca-seaflake.png"

cat > "$package_dir/DEBIAN/control" <<CONTROL
Package: orca-seaflake
Version: $version
Section: sound
Priority: optional
Architecture: $deb_arch
Maintainer: Orca Seaflake contributors
Depends: libgtk-3-0 | libgtk-3-0t64, libnss3, libxss1, libgbm1, libasound2 | libasound2t64
Description: Orca live-coding sequencer with Seaflake operators
 A desktop grid sequencer for MIDI, OSC, and UDP live coding.
CONTROL

cat > "$package_dir/usr/bin/orca-seaflake" <<'LAUNCHER'
#!/bin/sh
exec /usr/lib/orca-seaflake/orca-seaflake "$@"
LAUNCHER
chmod 0755 "$package_dir/usr/bin/orca-seaflake"

cat > "$package_dir/usr/share/applications/orca-seaflake.desktop" <<'DESKTOP'
[Desktop Entry]
Name=Orca Seaflake
Comment=Live-coding sequencer
Exec=orca-seaflake %U
Icon=orca-seaflake
Terminal=false
Type=Application
Categories=AudioVideo;Audio;Midi;
DESKTOP
chmod 0644 "$package_dir/usr/share/applications/orca-seaflake.desktop"

mkdir -p "$(dirname "$output")"
dpkg-deb --build --root-owner-group "$package_dir" "$output"
