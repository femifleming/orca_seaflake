cask "orca-seaflake" do
  arch arm: "arm64", intel: "x64"

  version "0.9.0"
  sha256 arm:   "30fd187bd1fd46703d9e4e228b3552acaee419bff6e00dcdd739c986953c7f05",
         intel: "4b77c74fb9a7d6ee20237871da5039e55997ec3d03e6d5ee7c793ab2a54894ff"

  url "https://github.com/femifleming/orca_seaflake/releases/download/v#{version}/Orca-Seaflake-#{version}-darwin-#{arch}.zip"
  name "Orca Seaflake"
  desc "Live-coding grid sequencer for MIDI, OSC and UDP"
  homepage "https://github.com/femifleming/orca_seaflake"

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
