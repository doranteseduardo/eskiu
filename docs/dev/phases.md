---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-06-04.

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
| alloc/free, String with concat and append | ✅ |
| List\<T\> with auto-resize | ✅ |
| argv/argc — `int main(int argc, string* argv)` | ✅ |
| VS Code — inline errors, hover types, go-to-definition | ✅ |
| Inline assembly — `asm(...)` | ✅ |
| Freestanding mode — compile without libc | ✅ |
| volatile — for memory-mapped I/O | ✅ |
| Closures — capturing variables from the enclosing scope | ✅ |
| Threads — `thread_create`/`thread_join` primitives | ✅ |
| Exceptions — try/catch | ❌ |
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

### Systems milestone — COMPLETE

A bare-metal ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) and prints to the PL011 serial UART — without libc, without a C runtime.

- Cross-compiled with `--target aarch64-unknown-none-elf`
- UART via inline asm, bump allocator via `--freestanding`, `volatile` MMIO
- Proves the systems foundation is solid

### v0.1 — First productive release — COMPLETE

Closures and threads are now implemented, completing the v0.1 milestone. Eskiu can now express concurrent, callback-driven, and higher-order programmes with no C glue required.

1. **Closures** — `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`. Variables from the enclosing scope are captured by value at lambda creation time. Non-capturing lambdas get `env_ptr = null`. The type annotation is unchanged — the representation is transparent to the user.
2. **Thread primitives** — `thread_create` and `thread_join` are language keywords. The fat-pointer representation maps directly to pthread's `(start_routine, arg)` pattern; no trampoline is needed. Link with `-lpthread` on Linux.

### v0.2 — Backend services

With closures and threads in place, the missing piece is stdlib support for network services.

3. **HTTP stdlib module** — minimal `http.esk` wrapping POSIX sockets: listen, accept, parse request, send response.

### v1.0 — Production-ready

4. **Exception handling** — `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.
5. **Package manager** — dependency resolution, package registry, build system integration.
6. **Self-hosting** — compile `eskiuc` with Eskiu. Requires closures, freestanding mode, and a stdlib allocator.

---

Phase detail sections follow (0–8), all statuses updated to match foundation milestone completion.
