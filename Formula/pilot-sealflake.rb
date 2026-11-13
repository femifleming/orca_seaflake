class PilotSealflake < Formula
  desc "Synth for Orca with Teletype and Just Friends built in (app, VST3, LV2)"
  homepage "https://github.com/femifleming/orca_seaflake/tree/main/pilot"
  version "1.1.0"
  license "GPL-2.0-only"

  # macOS users: brew install --cask femifleming/tap/pilot-sealflake
  depends_on :linux

  on_linux do
    on_intel do
      url "https://github.com/femifleming/orca_seaflake/releases/download/v0.10.0/Pilot-Sealflake-1.1.0-linux-x64.tar.gz"
      sha256 "1b7faa0850ae6add98cd77095cb512bac4e9f20185ff2a8408be18b6b29b81a0"
    end
    on_arm do
      url "https://github.com/femifleming/orca_seaflake/releases/download/v0.10.0/Pilot-Sealflake-1.1.0-linux-arm64.tar.gz"
      sha256 "637f2c7a984feb3d61ea4f7d733c57ae33e20bcf1e0b24ed01d68c4db30b4b17"
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
