# Homebrew formula for Eskiu. This is the source of truth; copy it to the tap repo
# (homebrew-eskiu/Formula/eskiu.rb) and refresh the three sha256 values (macOS arm64,
# Linux x86_64, Linux arm64) from the release's SHA256SUMS on each version bump.
# See packaging/homebrew/README.md.
class Eskiu < Formula
  desc "Self-hosting systems language with a C-style surface and an LLVM backend"
  homepage "https://eskiu-lang.org"
  version "0.9.2"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.2/eskiuc-macos-arm64.tar.gz"
      sha256 "2a8a5c128b0b9a0a5fe22d74cf5e9ab474959226c70937692cfc11bf90b07cc5"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.2/eskiuc-linux-x86_64.tar.gz"
      sha256 "46316ce1553489045aec1cf9fbc432f551cda4850b90d200a7b4c73bc835e8ec"
    end
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.2/eskiuc-linux-arm64.tar.gz"
      sha256 "0df564e04c9f36631a6e3c2da7cb033b67db3ddfdc7f235076d729b1a16f44cd"
    end
  end

  def install
    # Tarball layout: bin/eskiuc + lib/eskiu/stdlib (+ lib/deps bundled dylibs on macOS).
    # eskiuc finds its stdlib relative to its own path, so keep bin/ and lib/ siblings.
    prefix.install "bin", "lib"
  end

  def caveats
    <<~EOS
      eskiuc compiles programs by shelling out to `clang` to link native output.
      Make sure a C toolchain is installed:
        macOS:  xcode-select --install
        Linux:  brew install llvm   (or your distro's clang / gcc)
    EOS
  end

  test do
    assert_match "Eskiu #{version}", shell_output("#{bin}/eskiuc --version")
  end
end
