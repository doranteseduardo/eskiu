# Eskiu 0.9.1

A correctness release. A multi-front bug hunt turned up a set of latent miscompiles and
type-rule gaps that only bit specific patterns; all are now fixed, lockstep across the C++
and self-hosted compilers. No language or standard-library changes, so recompiling picks up
the fixes.

---

## Install

**macOS (Apple Silicon)**

```bash
tar -xzf eskiuc-macos-arm64.tar.gz -C /usr/local
eskiuc --version
```

**Linux (x86-64 / arm64)**

```bash
tar -xzf eskiuc-linux-x86_64.tar.gz -C /usr/local   # or eskiuc-linux-arm64.tar.gz
eskiuc --version
```

Or build from source (LLVM 17+ and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## Fixed

- **Global and `static`-local constant initializers.** A 64-bit literal was truncated to 32
  bits, a `struct` global came out zeroed, and a `const`/`enum`/`sizeof`/`~`/`!` value folded
  to `0`; a `string` global was `null` and a hex literal produced invalid IR in the
  self-host. All of these now fold correctly. `static` locals also accept those constant
  forms (not just a bare literal), and an over-full global array literal is rejected.
- **Signed/unsigned integer semantics now follow C.** `float`→unsigned casts use `fptoui` (a
  value above the signed max no longer saturates); a right shift's kind follows the value
  shifted, not the count's signedness; and a mixed-rank signed/unsigned op is unsigned only
  when the unsigned operand's rank is at least the signed one's.
- **`switch` `break` no longer runs enclosing `defer`s twice**, and a **bitfield write
  through a `*Struct`** now reaches the pointee instead of the pointer's own slot.
- **Scientific-notation float literals** (`3.4e38`, `1e6`, `2.5e-3`) now lex and compile.
- **`--safe` slice construction** bounds-checks `0 <= lo <= hi <= len`: a valid empty
  end-slice no longer traps, and an out-of-range upper bound is caught.
- **Async (self-host):** awaiting a future from a method call, a variable, or a template call
  resolves to the right value type; and an `await` in a condition or larger expression is a
  clean compile error instead of a crash or silent miscompile.

See the full log in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in. No breaking changes; recompiling picks up the fixes.
