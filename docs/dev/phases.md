---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-06-05.

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
| alloc/free, rich mutable `String` (push, char access, eq, index_of, substring, reverse, int conversion) | ✅ |
| List\<T\> with auto-resize | ✅ |
| for-in / for-each — `for (x in arr)` and `for (x in list)` | ✅ |
| `?` error-propagation operator for `Result<T,E>` | ✅ |
| argv/argc — `int main(int argc, string* argv)` | ✅ |
| VS Code — inline errors, hover types, go-to-definition | ✅ |
| Inline assembly — `asm(...)` | ✅ |
| Freestanding mode — compile without libc | ✅ |
| volatile — for memory-mapped I/O | ✅ |
| Closures — capturing variables from the enclosing scope | ✅ |
| Threads — `thread_create`/`thread_join` primitives | ✅ |
| Exceptions — try/catch/finally/throw | ✅ |
| sizeof(T) — compile-time size expression | ✅ |
| Typed pointer arithmetic — p + n advances by sizeof(*p) | ✅ |
| union — overlapping fields, size = largest member | ✅ |
| `import <name>` stdlib syntax — angle-bracket imports resolved from installation | ✅ |
| `<fs>` stdlib — file I/O (`fs_open`, `fs_read_all`, `fs_write_all`, etc.) | ✅ |
| `<net>` stdlib — TCP sockets (`net_tcp_listen`/`accept`/`connect`/`send`/`recv`), portable `sockaddr_in` | ✅ |
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

### v0.2 — Backend services

- HashMap stdlib module
- Higher-level HTTP on top of `<net>` — request/response parsing, routing
- A concurrent HTTP server (per-connection worker threads) built into the stdlib

### v0.3 — Self-hosting prerequisites

- LLVM C API bindings via `extern`
- Lexer rewritten in Eskiu
- Parser rewritten in Eskiu

### v1.0 — Production-ready

- `eskiuc` compiles itself (self-hosting)
- Package manager — dependency resolution, registry, build integration
- First-class types for high-throughput services
