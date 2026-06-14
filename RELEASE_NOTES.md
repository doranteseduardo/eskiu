# Eskiu 0.2.4

An internal soundness release. The compiler had two independent type resolvers —
the type checker and codegen each interpreted the type grammar and derived
expression types on their own. This release makes the type checker the **single
resolver** and codegen a consumer of its results, closing a latent class of
"the two disagree" bugs. No new language surface; the only user-visible effect is
three latent miscompiles the unification surfaced and fixed.

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

**One type resolver.** The type checker is re-run on the AST after the async
transform, and its resolved per-expression types are handed to codegen — which now
consumes them instead of re-deriving its own. Plus `getTypeFromString` now
dispatches on the same `ty::Type` parser the checker uses, so the type-string
grammar is interpreted in exactly one place across both phases. This eliminates the
structural condition behind an earlier constraint-checking bug.

**Three latent miscompiles fixed** (surfaced by the unification — they were benign
only because the affected values happened to be small):

- **Float literals** are now `double` in the type checker too (they always lowered
  to a `double` constant), so generic inference like `max(1.5, 2.5)` monomorphizes
  to `double` to match the emitted value.
- **Pointer-arithmetic dereference** `*(arr + n)` for `arr: *int` could load `i8`
  (one byte of a four-byte int) on some paths; now correctly loads `i32`.
- **`char` widening** to `int` used a signed extend on some paths; `char` is
  unsigned in Eskiu, so it is now a zero extend.

Everything else is behavior-preserving, verified by the test suite, sanitizers, a
golden-IR snapshot oracle, and the O0-vs-O2 differential fuzzer.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.2.x. The three fixes only change code that was already relying on
(benign) latent miscompiles; correct programs are unaffected.
