#!/bin/sh
# Points Homebrew and Scoop at a published release, in the one tap femifleming/homebrew-tap:
#   Casks/orca-seaflake.rb             Orca Seaflake desktop app (macOS)
#   Formula/orca-seaflake-terminal.rb  Orca Seaflake for the terminal (macOS, Linux)
#   Casks/pilot-sealflake.rb           Pilot Sealflake app + VST3 + AU (macOS)
#   Formula/pilot-sealflake.rb         Pilot Sealflake app + VST3 + LV2 (Linux)
#   bucket/orca-seaflake.json, bucket/pilot-sealflake.json   Scoop (Windows)
# Checksums come from the release's asset digests (nothing is downloaded). The tap is the only copy.
#
#   scripts/update-homebrew.sh 0.10.0           update the tap and push it
#   scripts/update-homebrew.sh 0.10.0 --dry-run write the files into a temporary clone, don't push
set -eu

version="${1:?usage: scripts/update-homebrew.sh <version> [--dry-run]}"
repo="femifleming/orca_seaflake"
tap="femifleming/homebrew-tap"
root="$(cd "$(dirname "$0")/.." && pwd)"
pilot="$(sed -n 's/^project(PilotSealflake VERSION \([0-9.]*\).*/\1/p' "$root/pilot/CMakeLists.txt")"

assets="$(gh release view "v$version" -R "$repo" --json assets -q '.assets[] | "\(.name) \(.digest)"')"
sha () {
  d="$(printf '%s\n' "$assets" | awk -v n="$1" '$1 == n { print $2 }')"
  [ -n "$d" ] || { echo "release v$version has no $1" >&2; exit 1; }
  printf '%s\n' "${d#sha256:}"
}

arm="$(sha "Orca-Seaflake-$version-darwin-arm64.zip")"
intel="$(sha "Orca-Seaflake-$version-darwin-x64.zip")"
orcawin="$(sha "Orca-Seaflake-$version-win32-x64.zip")"
terminal="$(sha "orca-seaflake-terminal-$version.tgz")"
pmac="$(sha "Pilot-Sealflake-$pilot-macos.zip")"
pwin="$(sha "Pilot-Sealflake-$pilot-windows-x64.zip")"
plx="$(sha "Pilot-Sealflake-$pilot-linux-x64.tar.gz")"
pla="$(sha "Pilot-Sealflake-$pilot-linux-arm64.tar.gz")"
work="$(mktemp -d)"
[ "${2:-}" = "--dry-run" ] || trap 'rm -rf "$work"' EXIT
gh repo clone "$tap" "$work/tap" -- --quiet
out="$work/tap"
mkdir -p "$out/Casks" "$out/Formula" "$out/bucket"

cat > "$out/Casks/orca-seaflake.rb" <<CASK
cask "orca-seaflake" do
  arch arm: "arm64", intel: "x64"

  version "$version"
  sha256 arm:   "$arm",
         intel: "$intel"

  url "https://github.com/$repo/releases/download/v#{version}/Orca-Seaflake-#{version}-darwin-#{arch}.zip"
  name "Orca Seaflake"
  desc "Live-coding grid sequencer for MIDI, OSC and UDP"
  homepage "https://github.com/$repo"

  livecheck do
    url :url
    strategy :github_latest
  end

  depends_on :macos

  app "Orca-Seaflake.app"

  # ad-hoc signed, not notarised: clear the download quarantine so it opens
  preflight_steps do
    run "/usr/bin/xattr",
        args:         ["-dr", "com.apple.quarantine", "Orca-Seaflake.app"],
        chdir:        ".",
        must_succeed: false
  end

  zap trash: [
    "~/Library/Application Support/Orca Seaflake",
    "~/Library/Preferences/com.femifleming.orcaseaflake.plist",
  ]
end
CASK

cat > "$out/Formula/orca-seaflake-terminal.rb" <<FORMULA
class OrcaSeaflakeTerminal < Formula
  desc "Orca Seaflake live-coding grid sequencer for the terminal, with Song Mode"
  homepage "https://github.com/$repo/tree/main/terminal"
  url "https://github.com/$repo/releases/download/v$version/orca-seaflake-terminal-$version.tgz"
  sha256 "$terminal"
  license "MIT"

  livecheck do
    url :stable
    strategy :github_latest
  end

  depends_on "node"

  def install
    system "npm", "install", *std_npm_args
    bin.install_symlink libexec.glob("bin/*")
  end

  test do
    assert_equal version.to_s, shell_output("#{bin}/orca-seaflake --version").strip
  end
end
FORMULA

cat > "$out/Casks/pilot-sealflake.rb" <<CASK
cask "pilot-sealflake" do
  version "$pilot,$version"
  sha256 "$pmac"

  url "https://github.com/$repo/releases/download/v#{version.csv.second}/Pilot-Sealflake-#{version.csv.first}-macos.zip"
  name "Pilot Sealflake"
  desc "Synth for Orca with Teletype and Just Friends built in"
  homepage "https://github.com/$repo/tree/main/pilot"

  # Pilot ships inside Orca Seaflake's releases (two versions): scripts/update-homebrew.sh in the repo bumps it
  livecheck do
    skip "Released with Orca Seaflake"
  end

  depends_on :macos

  app "Pilot Sealflake #{version.csv.first}/Pilot Sealflake.app"
  audio_unit_plugin "Pilot Sealflake #{version.csv.first}/Pilot Sealflake.component"
  vst3_plugin "Pilot Sealflake #{version.csv.first}/Pilot Sealflake.vst3"

  # ad-hoc signed, not notarised: clear the download quarantine (before they're moved into place) so they open
  preflight_steps do
    run "/usr/bin/xattr",
        args:         ["-dr", "com.apple.quarantine", "."],
        chdir:        ".",
        must_succeed: false
  end

  zap trash: [
    "~/Library/Application Support/Pilot Sealflake",
    "~/Library/Application Support/Pilot Sealflake.settings",
  ]
end
CASK

cat > "$out/Formula/pilot-sealflake.rb" <<FORMULA
class PilotSealflake < Formula
  desc "Synth for Orca with Teletype and Just Friends built in (app, VST3, LV2)"
  homepage "https://github.com/$repo/tree/main/pilot"
  version "$pilot"
  license "GPL-2.0-only"

  # macOS users: brew install --cask femifleming/tap/pilot-sealflake
  depends_on :linux

  on_linux do
    on_intel do
      url "https://github.com/$repo/releases/download/v$version/Pilot-Sealflake-$pilot-linux-x64.tar.gz"
      sha256 "$plx"
    end
    on_arm do
      url "https://github.com/$repo/releases/download/v$version/Pilot-Sealflake-$pilot-linux-arm64.tar.gz"
      sha256 "$pla"
    end
  end

  def install
    libexec.install Dir["*"]
    bin.install_symlink libexec/"pilot-sealflake"
  end

  def caveats
    <<~EOS
      The plug-ins are in #{opt_libexec}. To make your DAW see them:
        mkdir -p ~/.vst3 ~/.lv2
        ln -sf "#{opt_libexec}/Pilot Sealflake.vst3" ~/.vst3/
        ln -sf "#{opt_libexec}/Pilot Sealflake.lv2" ~/.lv2/
      Pilot needs the usual desktop libraries (ALSA, FreeType, fontconfig, X11).
    EOS
  end

  test do
    assert_predicate libexec/"pilot-sealflake", :executable?
    assert_predicate libexec/"Pilot Sealflake.vst3", :directory?
    assert_predicate libexec/"Pilot Sealflake.lv2", :directory?
  end
end
FORMULA

cat > "$out/bucket/orca-seaflake.json" <<JSON
{
    "version": "$version",
    "description": "Orca Seaflake: live-coding grid sequencer for MIDI, OSC and UDP, with Song Mode",
    "homepage": "https://github.com/$repo",
    "license": "MIT",
    "architecture": {
        "64bit": {
            "url": "https://github.com/$repo/releases/download/v$version/Orca-Seaflake-$version-win32-x64.zip",
            "hash": "$orcawin",
            "extract_dir": "Orca-Seaflake-win32-x64"
        }
    },
    "bin": "orca-seaflake.exe",
    "shortcuts": [["orca-seaflake.exe", "Orca Seaflake"]],
    "checkver": { "github": "https://github.com/$repo" },
    "autoupdate": {
        "architecture": {
            "64bit": { "url": "https://github.com/$repo/releases/download/v\$version/Orca-Seaflake-\$version-win32-x64.zip" }
        }
    }
}
JSON

cat > "$out/bucket/pilot-sealflake.json" <<JSON
{
    "version": "$pilot",
    "description": "Pilot Sealflake: synth for Orca with Teletype and Just Friends built in (app + VST3)",
    "homepage": "https://github.com/$repo/tree/main/pilot",
    "license": "GPL-2.0-only",
    "architecture": {
        "64bit": {
            "url": "https://github.com/$repo/releases/download/v$version/Pilot-Sealflake-$pilot-windows-x64.zip",
            "hash": "$pwin",
            "extract_dir": "Pilot-Sealflake-$pilot-windows-x64"
        }
    },
    "bin": [["Pilot Sealflake.exe", "pilot-sealflake"]],
    "shortcuts": [["Pilot Sealflake.exe", "Pilot Sealflake"]],
    "notes": [
        "The VST3 plug-in is at \$dir\\\\Pilot Sealflake.vst3.",
        "Copy it to C:\\\\Program Files\\\\Common Files\\\\VST3 (needs admin), or add \$dir to your DAW's VST3 search paths."
    ]
}
JSON

echo "Wrote Orca Seaflake $version and Pilot Sealflake $pilot into $out"
[ "${2:-}" = "--dry-run" ] && exit 0

cd "$out"
git add Casks Formula bucket
if git diff --cached --quiet; then echo "Tap already up to date"; exit 0; fi
git commit -q -m "Orca Seaflake $version, Pilot Sealflake $pilot"
git push -q
echo "Pushed $tap"
