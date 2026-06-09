---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-06-08.

---

## Vision

Eskiu addresses a specific problem: compute-intensive services currently require multiple languages — C for performance-critical work, Go for concurrency, C++ for libraries, Python for glue. Each adds toolchain complexity and interop friction.

The project follows two phases:

**Phase 1 — Systems foundation.** A language that covers everything C does: native performance, explicit memory, direct C library access. Validated against real production code. Complete.

**Phase 2 — Domain specialisation.** Once the systems foundation is stable, make the domain types that high-throughput services actually work with first-class in the language — without giving up general systems capability.

---

## Current language status

| Feature | Status |
|---------|--------|
| Primitive types (int/8/16/32/64, uint, float, double, bool, char, string, void) | ✅ |
| Pointers and pointer arithmetic | ✅ |
| Structs with methods | ✅ |
| Interfaces with vtable dispatch (fat pointer) | ✅ |
| Templates — structs and functions, monomorphic instantiation | ✅ |
| Control flow — if/else, while, for, switch/case (with type checking) | ✅ |
| Lambdas (`int(int x) { return x*2; }`) and `fn(T)->R` function pointer types | ✅ |
| Negative literals — `-1`, `-3.14` as first-class values | ✅ |
| C FFI — extern, variadic, sret on arm64 | ✅ |
| `alloc<T>(n)` / `free` via `import <mem>` (stdlib, not keywords as of v0.2.0), rich mutable `String` (push, char access, eq, index_of, substring, reverse, int conversion) | ✅ |
| List\<T\> with auto-resize | ✅ |
| for-in / for-each — `for (x in arr)` and `for (x in list)` | ✅ |
| `?` error-propagation operator for `Result<T,E>` | ✅ |
| argv/argc — `int main(int argc, string* argv)` | ✅ |
| VS Code — inline errors, hover types, go-to-definition | ✅ |
| Inline assembly — `asm(...)` | ✅ |
| Freestanding mode — compile without libc | ✅ |
| volatile — for memory-mapped I/O | ✅ |
| const — immutable typed bindings, usable as array sizes (v0.2.0) | ✅ |
| `alloc_with(&a, T, n)` + `<alloc>` (Bump/Arena/Pool/FirstFit) — caller-buffer allocators, libc-free (v0.2.0) | ✅ |
| Nested template instantiation — a template calling another with the type param forwarded (`alloc<T>(n)` inside `List_push<T>`) (v0.2.0) | ✅ |
| Closures — capturing variables from the enclosing scope | ✅ |
| Threads — `thread_create`/`thread_join` primitives | ✅ |
| Exceptions — try/catch/finally/throw | ✅ |
| sizeof(T) — compile-time size expression | ✅ |
| Typed pointer arithmetic — p + n advances by sizeof(*p) | ✅ |
| union — overlapping fields, size = largest member | ✅ |
| `import <name>` stdlib syntax — angle-bracket imports resolved from installation | ✅ |
| `<fs>` stdlib — file I/O (`fs_open`, `fs_read_all`, `fs_write_all`, etc.) | ✅ |
| `<net>` stdlib — TCP sockets (`net_tcp_listen`/`accept`/`connect`/`send`/`recv`), portable `sockaddr_in` | ✅ |
| `<time>` / `<env>` / `<base64>` / `<json>` stdlib — clock+sleep, env vars, Base64 codec, JSON builder (v0.2.0) | ✅ |
| Function-as-value — a named function decays to a `fn(...)->R` (no lambda wrapper) | ✅ |
| Predefined OS macros — `__APPLE__` / `__linux__` for `#ifdef` portability | ✅ |
| Pointer dereference as lvalue — `*ptr = value` through pointer parameters | ✅ |
| Forward declarations / call-before-define / mutual recursion | ✅ |
| Enums — `enum Color { Red, Green = 5, Blue }` | ✅ |
| Type aliases — `type u8 = uint8;` | ✅ |
| Struct bitfields — `uint32 x : 1;` | ✅ |
| Preprocessor — `#define` (object/function-like, multi-line via `\`), `#ifdef`/`#ifndef`/`#else`/`#endif` | ✅ |
| Packed structs — `packed struct` and `#pragma pack(push/pop)` | ✅ |
| Template type-argument inference — direct (`max(3, 5)`) and composite (`List_get(&nums, i)`) | ✅ |
| One-step linking — `eskiuc -o prog` invokes the system C toolchain | ✅ |
| Multi-file compilation — `eskiuc a.esk b.esk -o prog` | ✅ |
| `-Wall` warnings — unused vars/params/functions, assignment-in-condition | ✅ |
| Package manager | ❌ |
| Self-hosting | ❌ |

---

## Foundation milestone — COMPLETE

Compiler foundation proved on a real production workload: a cryptographic pipeline (AES-256-CBC + RSA-8192 decryption, QR extraction, structured output) running entirely in Eskiu.

| Stage | Eskiu | Reference C |
|-------|-------|-------------|
| QR extraction | 71.7 ms | 185.5 ms |
| Crypto (AES+RSA) | 2.8 ms | 2.9 ms |
| Output decode | < 1 ms | 0.5 ms |
| **Total** | **74.4 ms** | **188.9 ms** |

2.5× faster than the reference C implementation.

---

## Roadmap

### v0.1.0 — current release — COMPLETE

Everything in the feature table above ships in v0.1.0: the full systems language plus closures, threads, exceptions (`try`/`catch`/`finally`/`throw`), enums, unions, bitfields, type aliases, the preprocessor, one-step linking, multi-file builds, and `-Wall`. Ergonomics round it out: structural template-argument inference (including composite parameters like `List<T>*`), `for`-`in` iteration over arrays and lists, the `?` error-propagation operator for `Result<T,E>`, a rich mutable `String` type, packed structs, function-as-value decay, and a `<net>` TCP sockets module (with an HTTP server example). A bare-metal ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) on the PL011 UART — without libc or a C runtime — and the cryptographic pipeline above runs 2.5× faster than the reference C. This is the first release.

### v0.2.0 — Backend services (planned)

The theme is making Eskiu a practical language for concurrent backend services:
real async I/O, an HTTP stack, and the everyday stdlib + tooling that adoption
needs. v0.1.0 is frozen at its tag; v0.2.0 is developed as incremental commits.

Tracking checklist (checked = landed on `develop`).

The checklist is grouped into execution phases (not just categories): Phase 1
unblocks real projects, Phase 2 builds the async/HTTP stack on top of it, and
Phase 3 rounds out ergonomics and tooling.

**Phase 1 — Unblock real projects**
- [x] `const` — typed, scoped constants, usable as array sizes
- [x] `alloc_with(&allocator, T, n)` — explicit-allocator form alongside the default `alloc<T>` (built-in; `alloc`/`free` themselves moved to `<mem>`)
- [x] `<alloc>` — first-fit general-purpose allocator (after Ken Thompson's original), plus Arena, Pool, and Bump, all over `alloc_with`. A libc-free foundation for `--freestanding` (no dependency on libc `malloc`)
- [ ] `<threading>` — mutex, condvar, semaphore over pthread
- [ ] `<http>` — HTTP/1.1 parser + response builder + worker pool
- [x] `<base64>` — encode / decode
- [x] `<json>` — minimal builder
- [x] `<time>` — timestamp, sleep, monotonic clock
- [x] `<env>` — environment variables and CLI arguments

**Phase 2 — Build on Phase 1**
- [ ] `async` / `await` — keywords + state-machine codegen
- [ ] `Future<T>` in the stdlib
- [ ] `<eventloop>` — general-purpose reactor over epoll (Linux) / kqueue (macOS). A shared foundation for async/await, HTTP, and the future UI framework, designed from the start to dispatch any kind of event (sockets, frames, input), not just network I/O
- [ ] `<http_async>` — non-blocking HTTP/1.1 over the event loop
- [ ] `<string>` — `split`, `trim`, `starts_with`, `ends_with`
- [ ] `<path>` — path manipulation

**Phase 3 — Ergonomics and tooling**
- [ ] `for (i in 0..10)` — native ranges (builds on the existing `for`-`in`)
- [ ] User-defined variadic functions
- [ ] Pointer constness — distinguish a const pointer from a pointer-to-const (`const int*` vs `int* const`); `const` currently only qualifies the binding
- [ ] `__FILE__` and `__LINE__` in the preprocessor
- [ ] `#error` directive
- [ ] `#pragma pack(N)` with N > 1 (v0.1.0 honours `pack(1)` only)
- [ ] `-Wextra` — signed/unsigned mismatches, implicit conversions
- [ ] Shebang support — `#!/usr/bin/env eskiuc run`
- [ ] Package manager — dependency resolution, registry, build integration
- [ ] `eskiuc run file.esk` — compile and execute without leaving a binary
- [ ] `eskiuc fmt` — formatter
- [ ] `--asan` / `--ubsan` as first-class flags

**Documentation**
- [ ] Formal, complete BNF grammar
- [ ] Documented ABI
- [ ] `__FILE__` / `__LINE__` reference (once implemented)

### v0.3 — Self-hosting prerequisites

- LLVM C API bindings via `extern`
- Lexer rewritten in Eskiu
- Parser rewritten in Eskiu

### v1.0 — Production-ready

- `eskiuc` compiles itself (self-hosting)
- First-class types for high-throughput services

---

## Platform support (future track, not scheduled)

Eskiu targets **macOS arm64** and **Linux x86-64** today. Broadening to Windows,
Android, and iOS is a separate long-horizon track, independent of the version
milestones above and not scheduled into v0.2.0. The LLVM backends already cover
these targets, so the real work is linking, per-platform stdlib branches, and
toolchain/distribution integration, not codegen.

**Cross-cutting (shared by all platforms)**
- [ ] COFF object emission for Windows targets (Mach-O and ELF already work)
- [ ] Per-target linking: emit `.o`/`.a`/`.so` and hand off to the platform's native linker/SDK (the Unix `cc`/`clang` auto-link does not cross platforms)
- [ ] Predefined OS macros (`_WIN32`, `__ANDROID__`, iOS), extending the existing `__APPLE__`/`__linux__`
- [ ] stdlib platform branches where the API differs (sockets, threads)
- [ ] CI runners: Windows, plus NDK and Xcode for mobile

**Windows (host + target)**
- [ ] Build `eskiuc` on Windows (LLVM + CMake, MSVC or MinGW)
- [ ] Windows linker integration (`link.exe` / `lld-link`)
- [ ] `<net>` over Winsock2 (`WSAStartup`, `closesocket`, link `ws2_32`)
- [ ] `thread_create`/`thread_join` over Win32 threads (no native pthreads)

**Android (native library via JNI)**
- [ ] Cross-compile to `aarch64-linux-android` against the NDK (bionic libc)
- [ ] Build as a `.so` with JNI entry points, callable from Kotlin/Java
- [ ] APK packaging via Gradle/NDK (`<net>` and threads work as on Linux)

**iOS (static library via C ABI)**
- [ ] Cross-compile to `arm64-apple-ios` against the iOS SDK (Xcode, macOS only)
- [ ] Ship a static `.a` linked into an Xcode project, called from Swift/Obj-C by the C ABI
- [ ] Code signing and App Store constraints (AOT only, no JIT)

**Distribution model.** On mobile, Eskiu ships as the native compute core
(`.a`/`.so`) called from Swift/Kotlin over the C ABI, not as a standalone app. A
standalone Eskiu app with its own UI depends on the future UI framework (see the
`<eventloop>` note) and is out of scope for this track.
