#!/bin/sh
# Points the Homebrew cask (desktop app) and formula (terminal version) at a
# published release: writes the version and checksums into Casks/ and Formula/
# here, then copies them to the tap and pushes it.
#
#   scripts/update-homebrew.sh 0.9.0           update here and push the tap
#   scripts/update-homebrew.sh 0.9.0 --local   update the files here only
set -eu

version="${1:?usage: scripts/update-homebrew.sh <version> [--local]}"
repo="femifleming/orca_seaflake"
tap="femifleming/homebrew-orca_seaflake"
root="$(cd "$(dirname "$0")/.." && pwd)"
base="https://github.com/$repo/releases/download/v$version"

sha () {
  curl -fsSL "$1" | shasum -a 256 | cut -d' ' -f1
}

echo "Downloading v$version release files to checksum them..."
arm="$(sha "$base/Orca-Seaflake-$version-darwin-arm64.zip")"
intel="$(sha "$base/Orca-Seaflake-$version-darwin-x64.zip")"
terminal="$(sha "$base/orca-seaflake-terminal-$version.tgz")"

cat > "$root/Casks/orca-seaflake.rb" <<CASK
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

  app "Orca-Seaflake.app"

  zap trash: [
    "~/Library/Application Support/Orca Seaflake",
    "~/Library/Preferences/com.femifleming.orcaseaflake.plist",
  ]

  caveats <<~EOS
    On first launch, macOS may say it cannot verify the developer. Open
    System Settings > Privacy & Security and click Open Anyway, or
    Control-click Orca-Seaflake in Applications, choose Open, then Open again.
  EOS
end
CASK

cat > "$root/Formula/orca-seaflake-terminal.rb" <<FORMULA
class OrcaSeaflakeTerminal < Formula
  desc "Orca Seaflake live-coding grid sequencer for the terminal, with Song Mode"
  homepage "https://github.com/$repo/tree/main/terminal"
  url "https://github.com/$repo/releases/download/v$version/orca-seaflake-terminal-$version.tgz"
  sha256 "$terminal"
  license "MIT"

  livecheck do
    url :homepage
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

echo "Updated Casks/orca-seaflake.rb and Formula/orca-seaflake-terminal.rb for $version"
[ "${2:-}" = "--local" ] && exit 0

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
gh repo clone "$tap" "$work/tap" -- --quiet
mkdir -p "$work/tap/Casks" "$work/tap/Formula"
cp "$root/Casks/orca-seaflake.rb" "$work/tap/Casks/"
cp "$root/Formula/orca-seaflake-terminal.rb" "$work/tap/Formula/"
cd "$work/tap"
git add Casks Formula
if git diff --cached --quiet; then echo "Tap already up to date"; exit 0; fi
git commit -q -m "Orca Seaflake $version"
git push -q
echo "Pushed $tap"
