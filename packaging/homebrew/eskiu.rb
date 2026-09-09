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
      sha256 "e4017136fab588bf5747063466fb79e2549babfe64d424bc7bbcfe52ebb32ddd"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.0/eskiuc-linux-x86_64.tar.gz"
      sha256 "9de154eb9f983a40c02b6e697d54dd13f3fd0962c94069ab2e8f1d9aca824ec9"
    end
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.0/eskiuc-linux-arm64.tar.gz"
      sha256 "3111fd3bd321f477e8f9b8c259b7c5d66345ac7d8497ce7092d2c4fe85cbc0d3"
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
