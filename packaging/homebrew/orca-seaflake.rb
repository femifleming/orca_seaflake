class OrcaSeaflake < Formula
  desc "Orca C with the Seaflake MIDI sequence operator"
  homepage "https://github.com/femifleming/orca_seaflake"
  url "https://github.com/femifleming/orca_seaflake/archive/refs/tags/v0.1.0.tar.gz"
  version "0.1.0"
  license "MIT"

  depends_on "ncurses"
  depends_on "portmidi"

  def install
    system "./tool", "build", "--portmidi", "orca"
    bin.install "build/orca" => "orca_seaflake"
  end

  test do
    assert_match "orca", shell_output("#{bin}/orca_seaflake --help 2>&1", 0)
  end
end
