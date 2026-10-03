# Eskiu 0.9.2

A correctness and hardening release. A full-project audit, three fuzzers and eight blind
audit rounds on frozen trees found and fixed about 500 latent bugs across both compilers,
the standard library and the docs, each with a regression test and fixed in both the C++ and
self-hosted compilers. The type checker is stricter, so some programs that compiled before
are now rejected with a located error; see "Upgrade" below.

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

**Windows (x86-64, experimental)**

```powershell
Expand-Archive eskiuc-windows-x86_64.zip -DestinationPath C:\eskiu   # then add C:\eskiu\bin to PATH
eskiuc --version
```

Windows passes the full stdlib and both networking stacks on a native CI runner but is not
yet part of the release gate.

Or build from source (LLVM 21 or newer, LLVM 22 recommended, and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## Highlights

- **C semantics.** Integer promotions and the usual arithmetic conversions, `bool` as
  `!= 0`, element-counting `ptr - ptr`, constant-only global initializers, and constant
  folding with C's truncation rules.
- **C ABI.** Structs and unions by value across `extern` on AArch64, x86-64 SysV, Windows
  x64, 32-bit ARM and 32-bit x86, callbacks through thunks, narrow integers sign- or
  zero-extended like clang, `va_list` passed to C the target's way (and `va_arg` expanded
  for the AAPCS64 `va_list` on AArch64 Linux), and bitfields, `#pragma pack` structs and
  unions in the target's C layout.
- **Language.** Interface values, `const` receivers, flow-sensitive `?*T` narrowing,
  inline methods and dot-calls on generic structs, generic async functions, `(void)expr`,
  `#if`/`#elif`, `#pragma link` with implied libraries (no more `-lm`, `-lc++` or
  `-lpthread`), octal string escapes, `volatile` on every access, `await` in any position
  of an async function (inside `try`/`catch`, `match`, conditions and any expression),
  inline `asm` output operands, `null` as an interface value, `*?*T`, generic variants
  that take their type arguments from the expected type (`Opt<int64> a = Some(5)`), and
  a `-Wall` warning for a local that may be used uninitialized.
- **Standard library.** Strict JSON, base64 and HTTP parsing; HTTP/1.1 framing per
  RFC 9112 (chunked bodies, Host rules, requests split across reads); HTTP/2 with body and
  header-list limits, malformed-request resets and Host checks; read, body, idle and
  write timeouts in every HTTP server (`HttpLimits`); regex with RE2 semantics over UTF-8
  (`\p{..}` classes, `(?i)`, `(?:...)`, named groups, POSIX classes); and fixes for
  use-after-free, overflow and per-request leaks.
- **Tooling.** Three fuzzers with CI gates, `fmt` that never changes program behavior,
  the self-hosted compiler matching the C++ one on invalid programs (same messages and
  line:col), native builds on the target's baseline CPU as in clang, and
  `tests/linux_docker.sh`, a Linux check of the release from a Mac.

See the full list in [CHANGELOG.md](CHANGELOG.md), including the few known issues that
remain.

---

## Upgrade

Recompile. Programs that relied on something the checker now rejects get a located error
that names the rule, for example a non-constant global initializer, an implicit
conversion between unrelated pointer types, writing a captured variable inside a lambda,
`return` inside `finally`, or a conflicting generic deduction. The CHANGELOG section
"Changed" lists every such rule. Old stdlib names renamed to the `Type_method` convention
still work and are marked deprecated.

A few valid programs now behave differently, each to match C and clang:

- Structs mixing bitfield types, structs under `#pragma pack(N)` nested in another type,
  and unions under `#pragma pack(N)` take the target C compiler's layout, so their size
  can change (a C ABI change for code that shares them with C).
- Enum values, array dimensions and `case` labels fold with C's 32-bit `int` rules
  (`2147483647 + 1` wraps to INT_MIN; unsigned operands compare as unsigned).
- A string escape `\NNN` is octal (`"\012"` is a newline), and an unknown escape is an
  error.
- A cancelled future runs the `defer` and `finally` blocks pending at its `await`.
- A native build targets the target's baseline CPU (`apple-m1` on arm64 Apple, `generic`
  elsewhere) unless `--mcpu` is given; pass a CPU name such as `--mcpu x86-64-v3` to tune
  for a specific machine.

The stdlib HTTP servers now time out clients that send nothing, trickle their bytes or
stop reading (a 10 s request head, 60 s body and HTTP/2 idle, 30 s write), and the async
servers hold at most 1024 connections at once. Pass an `HttpLimits` to the `*_with`
variants to change these limits.
