# Homebrew formula for Eskiu. This is the source of truth; copy it to the tap repo
# (homebrew-eskiu/Formula/eskiu.rb) and refresh the three sha256 values (macOS arm64,
# Linux x86_64, Linux arm64) from the release's SHA256SUMS on each version bump.
# See packaging/homebrew/README.md.
class Eskiu < Formula
  desc "Self-hosting systems language with a C-style surface and an LLVM backend"
  homepage "https://eskiu-lang.org"
  version "0.9.3"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.3/eskiuc-macos-arm64.tar.gz"
      sha256 "8eb0216c7aca4d347388227479e5f1e36e1fc03e4fe88592536b4e84d266c6d0"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.3/eskiuc-linux-x86_64.tar.gz"
      sha256 "b5216cc8118a6caf06ec540b8663c2aa94efeb4a99158a74b05928f8e0d304a1"
    end
    on_arm do
      url "https://github.com/doranteseduardo/eskiu/releases/download/v0.9.3/eskiuc-linux-arm64.tar.gz"
      sha256 "0ced81adde0215673eb98852f327137598aa6c44f84a285e24a93af2643860bc"
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
