# Eskiu 0.9.2

A correctness and hardening release. A full-project audit, three fuzzers and eight blind
audit rounds on frozen trees found and fixed about 500 latent bugs across both compilers,
the standard library and the docs, each with a regression test and lockstep in the C++ and
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
  x64 and 32-bit ARM, callbacks through thunks, narrow integers sign- or zero-extended like
  clang, `va_list` passed to C the target's way, and bitfields in the target's C layout.
- **Language.** Interface values, `const` receivers, flow-sensitive `?*T` narrowing,
  inline methods and dot-calls on generic structs, generic async functions, `(void)expr`,
  `#if`/`#elif`, `#pragma link` with implied libraries (no more `-lm`, `-lc++` or
  `-lpthread`), octal string escapes, and `volatile` on every access.
- **Standard library.** Strict JSON, base64 and HTTP parsing; HTTP/1.1 framing per
  RFC 9112 (chunked bodies, Host rules, requests split across reads); HTTP/2 with body and
  header-list limits, malformed-request resets and Host checks; regex with RE2 semantics
  (escapes, POSIX classes, empty loops); and fixes for use-after-free, overflow and
  per-request leaks.
- **Tooling.** Three fuzzers with CI gates, `fmt` that never changes program behavior, and
  the self-hosted compiler matching the C++ one on invalid programs.

See the full list in [CHANGELOG.md](CHANGELOG.md), including the known issues planned for
0.9.3.

---

## Upgrade

Recompile. Programs that relied on something the checker now rejects get a located error
that names the rule, for example a non-constant global initializer, an implicit
conversion between unrelated pointer types, writing a captured variable inside a lambda,
`return` inside `finally`, or a conflicting generic deduction. The CHANGELOG section
"Changed" lists every such rule. Old stdlib names renamed to the `Type_method` convention
still work and are marked deprecated.

The stdlib HTTP servers do not have read or idle timeouts yet. Put them behind a reverse
proxy when they face untrusted clients.
