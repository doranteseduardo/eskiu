# Eskiu 0.5.0

A basic-C surface release. It fills the last common C constructs the language was
missing, so idiomatic C ports compile without workarounds. Every feature landed in
lockstep across the C++ `eskiuc` and the self-hosted compiler, and follows C semantics.

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

### New language constructs

- **`do`/`while` loops.** `do { ... } while (cond);` runs the body once before testing
  the condition, as in C.
- **Increment and decrement.** Prefix and postfix `++`/`--` on integer and pointer
  lvalues: postfix yields the old value, prefix the new, and a pointer steps by one
  element.
- **Array-literal initializers.** `int[N] a = { e0, e1, ... };` initializes an array in
  place. As in C, a short list zero-fills the rest and `{}` zero-fills the whole array;
  an over-long list is a compile error.
- **`static` locals.** A `static` local has a single instance that persists across calls
  (C storage). Its initializer must be a compile-time constant; `static` on a global is
  rejected.
- **Multidimensional arrays.** `T[N][M]` is N arrays of M in C order (the leftmost
  bracket is the outer dimension). `a[i]` is a row, `a[i][j]` an element, and each index
  is bounds-checked against its own dimension. Nested brace initializers
  (`int[2][3] a = { {1,2,3}, {4,5,6} }`) zero-fill at every level.
- **Ternary conditional `cond ? a : b`.** Evaluates exactly one arm; the arms take a
  common type (two numerics promote C-style, e.g. `int` and `double` yield `double`).
  Right-associative, so `a ? b : c ? d : e` chains. It coexists with the postfix `?`
  Result-propagation operator: a `?` with a matching same-level `:` ahead is a ternary,
  otherwise it is propagation (parenthesize to propagate inside a ternary arm).

### Fixed

- **`switch` on a sub-`int` subject.** A `switch` over a `char` (or other narrow integer)
  with wider case constants failed LLVM verification; the subject and case constants are
  now widened to a common type before lowering.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

This release only adds constructs; existing code compiles unchanged. `static`, `do`, and
the ternary `?:` reuse existing keywords/operators, so the one thing to know is the
disambiguation between the ternary `?` and the postfix Result-propagation `?`: a `?` with
a matching same-level `:` ahead is a ternary. To propagate inside a ternary arm,
parenthesize it: `cond ? (may_fail()?) : fallback`.
