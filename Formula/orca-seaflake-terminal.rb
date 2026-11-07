class OrcaSeaflakeTerminal < Formula
  desc "Orca Seaflake live-coding grid sequencer for the terminal, with Song Mode"
  homepage "https://github.com/femifleming/orca_seaflake/tree/main/terminal"
  url "https://github.com/femifleming/orca_seaflake/releases/download/v0.9.0/orca-seaflake-terminal-0.9.0.tgz"
  sha256 "29524d687119f9d7d0adffa3f1ec83aaa5510b2e3feab33d575aebfc838bbaf9"
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
