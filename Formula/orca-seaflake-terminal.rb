class OrcaSeaflakeTerminal < Formula
  desc "Orca Seaflake live-coding grid sequencer for the terminal, with Song Mode"
  homepage "https://github.com/femifleming/orca_seaflake/tree/main/terminal"
  url "https://github.com/femifleming/orca_seaflake/releases/download/v0.10.0/orca-seaflake-terminal-0.10.0.tgz"
  sha256 "4bcd19123a0dbd2de92a6cfb5a2ae59c85759b74ac9b3d1ba33fa7c2f3e6aac2"
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
