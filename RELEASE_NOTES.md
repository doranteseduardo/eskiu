# Eskiu 0.6.1

A patch release. It fixes escape-sequence decoding in string and character literals
and lands a documentation-accuracy pass; no language features change, and existing code
keeps compiling unchanged.

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

## What's fixed

- **Escape sequences in string and character literals.** `\0` now decodes to a NUL byte
  instead of the character `'0'` (previously a silent trap), character literals accept
  `\r` (and `\f`, `\v`), and string and character literals now share one escape set:
  `\n \t \r \f \v \0 \\ \" \'`. An unrecognized escape still yields the character itself
  (`\q` is `q`). The fix landed in lockstep across the C++ and self-hosted lexers, so both
  decode identically.

- **Documentation accuracy** (from an internal audit): corrected AST node counts and
  documented the `defer` cleanup-stack in the internals docs; regenerated the real
  `--test-lexer` / `--test-parser` sample output; fixed the inline-asm operand syntax
  (`$0`, not `%0`) and noted that output operands are unsupported; added an `intrinsic`
  spec section and a `--safe` flag row; and corrected glossary token-type names.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in. The only behavioral change is that `\0` in a literal now produces a NUL byte, as
documented; code that relied on the previous (incorrect) behavior of `\0` yielding `'0'`
should use a literal `0` character instead.
