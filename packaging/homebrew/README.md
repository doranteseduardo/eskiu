# Homebrew tap for Eskiu

Eskiu installs through a Homebrew *tap* (its own small repo of formulae). This directory
holds the source-of-truth formula; the tap repo is where users actually install from.

## One-time setup (maintainer)

1. Create a public repo named **`homebrew-eskiu`** under the `doranteseduardo` account.
   Homebrew requires the `homebrew-` prefix; the tap is then `doranteseduardo/eskiu`.
2. Add the formula at `Formula/eskiu.rb` in that repo (copy [`eskiu.rb`](eskiu.rb)).

## Per release (maintainer)

After a `vX.Y.Z` release finishes and its assets (including `SHA256SUMS`) are attached:

1. Bump `version` in the formula and both release URLs to the new tag.
2. Fill the two `sha256` values from the release's `SHA256SUMS`:

   ```sh
   curl -fsSL https://github.com/doranteseduardo/eskiu/releases/download/vX.Y.Z/SHA256SUMS
   #   <hash>  eskiuc-macos-arm64.tar.gz    -> macOS block sha256
   #   <hash>  eskiuc-linux-x86_64.tar.gz   -> Linux block sha256
   ```

3. Commit the formula to `homebrew-eskiu`. Optionally validate first:

   ```sh
   brew install --build-from-source ./Formula/eskiu.rb
   brew test eskiu
   brew audit --strict eskiu
   ```

## Install (user)

```sh
brew install doranteseduardo/eskiu/eskiu
```

`brew` handles the download, checksum, and PATH. `eskiuc` still needs `clang` at runtime to
link native output (Xcode Command Line Tools on macOS, or `brew install llvm` / a distro
clang on Linux); the formula prints this as a caveat.

## Notes

- The macOS tarball bundles its `z3`/`zstd` dylibs under `lib/deps` and references them via
  `@loader_path`, so the formula needs no `depends_on` for them.
- `eskiuc` resolves its standard library relative to its own path (`../lib/eskiu/stdlib`), so
  `prefix.install "bin", "lib"` is all that is required to keep imports working.
