cask "orca-seaflake" do
  version :latest
  sha256 :no_check

  name "Orca Seaflake"
  desc "Live-coding grid sequencer for MIDI, OSC, and UDP"
  homepage "https://github.com/femifleming/orca_seaflake"

  on_arm do
    url "https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-darwin-arm64.zip"
  end
  on_intel do
    url "https://github.com/femifleming/orca_seaflake/releases/latest/download/Orca-Seaflake-darwin-x64.zip"
  end

  auto_updates true
  app "Orca-Seaflake.app"

  caveats <<~EOS
    This app is not signed or notarized. On first launch, open Orca Seaflake from Finder
    and approve macOS's unidentified-developer prompt.
  EOS
end
