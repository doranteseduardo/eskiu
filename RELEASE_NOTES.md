# Eskiu 0.7.0

A feature release focused on cross-compilation and C interop. Eskiu now targets
32-bit ARM (with a camera-and-gyroscope demo verified on real Nintendo 3DS hardware)
and emits Windows x86-64 objects, `extern` declarations reach C global variables, and
a `double` literal passed to a `float` parameter narrows correctly at the call site.
Existing code keeps compiling unchanged.

---

## Install

**macOS (Apple Silicon)**

```bash
tar -xzf eskiuc-macos-arm64.tar.gz -C /usr/local
eskiuc --version
```

**Linux (x86-64)**

```bash
tar -xzf eskiuc-linux-x86_64.tar.gz -C /usr/local
eskiuc --version
```

Or build from source (LLVM 17+ and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## What's new

- **`extern` variables.** `extern <type> <name>;` declares a global defined in another
  translation unit, so Eskiu can read and write state shared with a C library. It emits
  an external-linkage declaration with no initializer, resolved at link time. Previously
  `extern` accepted only function prototypes. Landed in lockstep across both compilers.

- **32-bit ARM backend, cross-compile to the Nintendo 3DS.** The ARM target is now
  registered alongside AArch64 and x86, driven by three new flags: `--mcpu` (e.g.
  `mpcore` for the 3DS's ARM11), `--mattr` (LLVM feature string, e.g. `+vfp2`), and
  `--reloc` (`static` for the `.3dsx` loader). A hard-float ARM triple (ending in `hf`,
  e.g. `armv6k-none-eabihf`) selects the hard-float ABI, so the object carries the
  `Tag_ABI_VFP_args` attribute and links against hard-float libraries like libctru. An
  object built this way links cleanly into a `.3dsx` homebrew via the devkitARM toolchain.

- **Cross-compile to Windows (x86-64).** `--target x86_64-pc-windows-gnu` emits a COFF
  object with the Microsoft x64 calling convention, and the preprocessor predefines
  `_WIN32` (plus `_WIN64` on a 64-bit arch). Link it with `lld-link` and an import library
  from `llvm-dlltool` (both ship with LLVM, so no Windows SDK is needed to link).

See [`docs/dev/cross-compile.md`](docs/dev/cross-compile.md) for the flags, the platform
macros, and the 3DS and Windows recipes.

## What's fixed

- **Float arguments narrow at the call site.** A `double` literal (Eskiu's default float
  literal type) passed to a `float` parameter was left as `double`, so the argument type
  disagreed with the callee's signature and the reference compiler rejected the call at
  LLVM verification. Direct and method calls now run every argument through the shared
  numeric-coercion path, matching assignment and return.

- **Bare-metal targets no longer inherit the host platform macro.** An explicit
  non-hosted triple (OS `none`, e.g. the 3DS's `armv6k-none-eabihf`) predefines neither
  `__APPLE__` nor `__linux__`; previously it fell through to the build host's macro.

- **Hard-float ABI reached the object emitter.** The `--target …hf` hard-float ABI was
  applied when building the module and running the optimizer but not in the object-emitting
  path, so the written object dropped `Tag_ABI_VFP_args`. The three code-emitting paths now
  share one `TargetMachine` builder.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in. No breaking language or standard-library changes; recompiling picks up the fixes,
and the new targets and `extern` variables are opt-in.
