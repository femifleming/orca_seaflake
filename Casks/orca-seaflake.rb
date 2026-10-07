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
    On first launch, macOS may say it cannot verify the developer. To open the app,
    Control-click Orca-Seaflake in Applications, choose Open, then confirm Open.
    The release has an ad-hoc code signature to verify app integrity; it is not
    signed with an Apple Developer ID or notarized.
  EOS
end
