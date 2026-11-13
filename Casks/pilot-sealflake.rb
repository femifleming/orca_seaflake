cask "pilot-sealflake" do
  version "1.1.0,0.10.0"
  sha256 "65a5aa4ce01d37fed16720c97d2e28cde2dddc61e706859554dcd2b7195984f1"

  url "https://github.com/femifleming/orca_seaflake/releases/download/v#{version.csv.second}/Pilot-Sealflake-#{version.csv.first}-macos.zip"
  name "Pilot Sealflake"
  desc "Synth for Orca with Teletype and Just Friends built in"
  homepage "https://github.com/femifleming/orca_seaflake/tree/main/pilot"

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
