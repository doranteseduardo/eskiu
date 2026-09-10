# Homebrew formula for Eskiu. This is the source of truth; copy it to the tap repo
# (homebrew-eskiu/Formula/eskiu.rb) and refresh the three sha256 values (macOS arm64,
# Linux x86_64, Linux arm64) from the release's SHA256SUMS on each version bump.
# See packaging/homebrew/README.md.
class Eskiu < Formula
  desc "Self-hosting systems language with a C-style surface and an LLVM backend"
  homepage "https://eskiu-lang.org"
  version "0.9.1"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.1/eskiuc-macos-arm64.tar.gz"
      sha256 "c44070f877f090bf2d8d52a9f0e9166b148a8d4d3a00acc481e98c065c5ee2d8"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.1/eskiuc-linux-x86_64.tar.gz"
      sha256 "c5d97d2bb0d9191d02e59be3239b494225ff6adb9c90fe3c983df268727a87ed"
    end
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.1/eskiuc-linux-arm64.tar.gz"
      sha256 "011244341bfc9d20d88ee21c746df6affa70d5e355719bfc943bc99ba8fc48fe"
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
