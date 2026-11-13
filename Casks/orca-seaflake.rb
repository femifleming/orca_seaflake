cask "orca-seaflake" do
  arch arm: "arm64", intel: "x64"

  version "0.10.0"
  sha256 arm:   "7a4bf446b445fb94e27916c61c5a31daecf8e542db9118fedab09901b1d35ca6",
         intel: "d5354c1f17227d90b10f507e5ceee1c5725d4f3b179c1434f9817989fefdd20a"

  url "https://github.com/femifleming/orca_seaflake/releases/download/v#{version}/Orca-Seaflake-#{version}-darwin-#{arch}.zip"
  name "Orca Seaflake"
  desc "Live-coding grid sequencer for MIDI, OSC and UDP"
  homepage "https://github.com/femifleming/orca_seaflake"

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
