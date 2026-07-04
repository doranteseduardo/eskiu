# Eskiu 0.3.1

A correctness and hardening release over the 0.3.0 self-hosting milestone. It fixes
three real miscompiles (two of them exposed by the new `-O` optimization levels), adds
a permanent gate to catch that whole class, and widens self-host test coverage. No new
language surface.

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

**Fixed: `*T[N]` now parses as an array of pointers.** The type parser peeled a leading
`*` before the trailing `[N]`, so `*Node[7]` became a *pointer to* `Node[7]` and lowered
to a single opaque pointer, while indexing it assumed an array. The two disagreed and
indexing corrupted memory (and crashed the compiler outright on a module-level array).
`[N]` now binds outermost; a pointer *to* an array stays spellable as `Node[7]*`.

**Fixed: a float-returning closure no longer miscompiles under `-O2`.** A lambda whose
header return type disagreed with its target `fn(...)->R` type (for example an
`int`-returning lambda assigned to `fn(int)->float`) emitted a function whose return type
disagreed with the closure's call ABI. It was correct at `-O0` by luck and returned `0.0`
under `-O2`. The lambda's return type is now reconciled with its target.

**Fixed: incompatible function-type assignments are rejected.** Assigning a function to a
`fn(...)` slot with a different signature (say an `int`-returning function to a
`fn(int)->float`) was silently accepted and miscompiled. It is now a compile error.

**Hardening.** A new `-O0`-vs-`-O2` behavioral differential runs the whole test corpus at
both optimization levels in CI and fails on any divergence, guarding against
optimization-path miscompiles. Self-hosted parser parity was extended to the full corpus
(51 to 121 files), and the event loop now initializes every fd slot's callback defensively.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.3.0. The one source-level change is stricter: assigning a function to a
differently-typed `fn(...)` slot (an ABI mismatch that previously miscompiled) is now a
compile error. Correct programs are unaffected.
