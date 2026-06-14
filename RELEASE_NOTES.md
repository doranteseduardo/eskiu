# Eskiu 0.2.3

Completes bounded generics, and lands a typed internal type representation that
replaces the compiler's ad-hoc type-string handling — the soundness foundation
for growing the language without the string-convention bugs that surface
otherwise. The base is the same 0.2.x: compiled to native through LLVM, you
manage memory yourself, standalone binaries.

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

**Primitives can satisfy a constraint.** Bounded generics (`<T: Iface>`, from
0.2.2) were method-based, so only structs could satisfy a constraint. Now a
*primitive* satisfies one through a **free function** named like the interface
method whose first parameter is that primitive:

```eskiu
interface Ord { int cmp(Self other); }

int cmp(int a, int b) { return a - b; }   // makes `int` satisfy Ord

T max<T: Ord>(T a, T b) {                 // now works for T = int
    if (a.cmp(b) > 0) { return a; }
    return b;
}
```

Inside a generic body, a constrained call `t.cmp(x)` on a primitive `t` lowers to
`cmp(t, x)`. So `max<int>(…)` and a constraint-bounded map over `int` keys compile
— closing the method-only seam. (The function-pointer `HashMap<K, V>` from 0.2.1
stays for when you'd rather thread `hash`/`eq` explicitly.)

**A typed internal type representation.** Under the hood, types were `std::string`
everywhere, manipulated by ad-hoc surgery in ~175 places — the kind of
convention-enforced invariant that produced a subtle constraint-checking bug in
0.2.2. This release introduces a structured `Type` IR as the manipulation form
(types still travel as canonical strings at the boundaries), migrates the
type-substitution engine and the duplicated bare-name surgery onto it (the exact
strip that caused that bug no longer exists), and adds a golden-IR oracle that
asserts the emitted code is byte-identical across the change. No behavior change
— it's a foundation, verified by the test suite, sanitizers, and the fuzzer.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.2.x — no source changes required. The new primitive-constraint
support is additive; existing code keeps compiling unchanged.
