# Homebrew formula for Eskiu. This is the source of truth; copy it to the tap repo
# (homebrew-eskiu/Formula/eskiu.rb) and refresh the three sha256 values (macOS arm64,
# Linux x86_64, Linux arm64) from the release's SHA256SUMS on each version bump.
# See packaging/homebrew/README.md.
class Eskiu < Formula
  desc "Self-hosting systems language with a C-style surface and an LLVM backend"
  homepage "https://eskiu-lang.org"
  version "0.9.0"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.0/eskiuc-macos-arm64.tar.gz"
      sha256 "b4098313f7e3548adbaaf2e5a045c700882916a0fff2f3169c57650f7c4bbf65"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.0/eskiuc-linux-x86_64.tar.gz"
      sha256 "e630371f432d0173c5f39937104b20cbb31580591ff02dea861faf02cc275904"
    end
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.0/eskiuc-linux-arm64.tar.gz"
      sha256 "fbd8871ccdeee57c16235745ba047080b39ff906e335260f2dc5ebcf8fac5bef"
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
