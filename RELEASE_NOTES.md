# Eskiu 0.2.2

A correctness release with one new language feature. Building a real service on
Eskiu (the INE-QR HTTP API) surfaced three compiler bugs in one session, so 0.2.2
is, by decision, exactly two things: **hardening** the compiler so it can be
trusted, and **bounded generics** — the clean path the dogfooding kept hitting.
No new stdlib, no other surface changes. The base is the same 0.2.x: compiled to
native through LLVM, you manage memory yourself, standalone binaries.

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

**Bounded generics — `<T: Iface>`.** A type parameter can now carry one or more
interface constraints, on both functions and structs:

```eskiu
interface Ord { int cmp(*Self other); }

T max<T: Ord>(T a, T b) {        // T must satisfy Ord
    if (a.cmp(&b) > 0) return a;
    return b;
}

struct Cache<K: Hashable + Eq, V> { ... }   // multiple constraints with +
```

Satisfaction reuses the existing structural `interface` match, and the constraint
is checked **at the instantiation site** — for explicit (`max<Num>(...)`),
inferred (`max(a, b)`), and template-struct uses alike — so a missing method is a
clear, located type error instead of a confusing failure deep in codegen:

```
error: type 'int' does not satisfy constraint 'Ord' (required by a bounded type parameter)
```

Satisfaction is method-based, so only `struct` types can satisfy a constraint;
for primitive keys the function-pointer `HashMap<K, V>` from 0.2.1 stays the path.

**A fuzzer, wired into CI.** `tests/fuzz/eskiu_fuzz.py` generates and mutates
Eskiu programs and uses the LLVM IR verifier and AddressSanitizer as oracles — a
finding is a compiler crash, a verifier failure, or a sanitizer abort, no
reference implementation needed. CI runs a bounded, fixed-seed fuzz job on top of
the plain + asan + ubsan suite.

**Four bug classes it found, all fixed** (each with a locked-in regression test):
- Code after a `return`/terminator inside a block was still lowered, producing
  invalid IR with instructions past a block terminator.
- Arguments to a closure/indirect call weren't coerced to the parameter types
  (off by the hidden environment pointer).
- A `switch` with duplicate `case` values now reports a type error.
- A negative array dimension is rejected instead of silently falling back.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.2.0/0.2.1 — no source changes required. Bounded generics are
additive: existing unconstrained `<T>` code keeps compiling unchanged.
