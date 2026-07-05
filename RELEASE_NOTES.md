# Eskiu 0.4.0

A correctness and type-strictness release. A four-front bug hunt across the C++ and
self-hosted compilers found a batch of miscompiles, crashes, and soundness holes; this
release fixes them and tightens the type system. The shipped compiler is the C++
`eskiuc`, which carries every fix here.

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

### Miscompiles fixed

- **Integer literals in [2^31, 2^32) were truncated** to 32 bits and sign-extended
  (`int64 a = 3000000000` gave a negative value); they are now materialized as i64.
- **`unsigned -> float` used a signed conversion**, turning a high-bit-set unsigned value
  negative; it now uses the unsigned conversion.
- **A catch-less `try`/`finally` aborted** when an exception passed through it (the
  cleanup was skipped); it now runs the `finally` and propagates.
- **A lambda nested in another lambda miscompiled** when it captured a variable two
  scopes up; captures are now threaded through every enclosing closure.

### Compiler no longer crashes or rejects valid code

- Comparison operators now type-check their operands (a pointer-vs-int or struct-vs-struct
  comparison used to reach codegen and assert or miscompile).
- A `void` or `string` value assigned to an `int` (which hung or crashed) is now a clean
  compile error.
- `float` compared against `double`/`int` now compiles (operands are promoted).
- `&x` of a `const` now yields a pointer to const, so writing through it is caught.

### Type system tightened

- **Floating-point to integer needs an explicit cast.** `int x = 3.9;` is an error; write
  `int x = (int)3.9;`. Integer-width narrowing (`int n = strlen(s);`) and float-width
  narrowing stay implicit, matching C.
- **Out-of-range integer literals** (`int8 x = 300;`) and **division/remainder by a literal
  zero** are compile errors.
- **A constant array index proven out of bounds** is a compile error.
- **Reading an uninitialized local**, **returning the address of a local** (a dangling
  pointer), **redefining a function**, and **falling off the end of a non-void function**
  are all compile errors.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

This release is stricter and may reject code that previously compiled. The likely fixes:

- `int x = 3.9;` -> `int x = (int)3.9;` (floating-point to integer now needs the cast).
- A function that falls off the end without returning, an uninitialized local read, a
  returned address of a local, or a duplicate function definition: fix the code (these
  were latent bugs).
- Integer-width narrowing (`int n = strlen(s)`) still compiles unchanged.
