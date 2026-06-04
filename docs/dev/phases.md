---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-06-03.

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
| Closures — capturing variables from the enclosing scope | ❌ |
| Threads — pthread primitives | ❌ |
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

### v0.1 — Kernel on QEMU — COMPLETE

A bare-metal ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) and prints to the PL011 serial UART — without libc, without a C runtime.

**Result:**
- Cross-compiled with `--target aarch64-unknown-none-elf`
- UART writes via inline asm (`strb ${0:w}, [$1]`) and `volatile` MMIO pointers
- Bump allocator (`esk_alloc`/`esk_free`) implemented in freestanding mode
- Output: ASCII art banner, version string, heap allocation test
- Boots and prints correctly under QEMU

| Compiler prerequisite | Status |
|-----------------------|--------|
| Inline assembly — `asm(...)` with constraints and clobbers | ✅ |
| Freestanding mode — `--freestanding`, `esk_alloc`/`esk_free` | ✅ |
| `volatile` — MMIO loads and stores | ✅ |
| Cross-compilation — `--target TRIPLE`, both AArch64 and X86 backends | ✅ |

### v0.2 — Backend services

The primary blocker for backend service development is the absence of closures and native thread primitives. A concurrent HTTP server requires both.

1. **Closures** — variable capture from the enclosing scope. Requires an implicit `env*` and codegen adjustments. Unblocks callbacks, request handlers, and self-hosting.
2. **Thread primitives** — `pthread_create`/`pthread_join` + `Mutex` in stdlib. Unblocks concurrent servers and any work that currently needs Go or C threads.
3. **HTTP stdlib module** — minimal `http.esk` wrapping POSIX sockets: listen, accept, parse request, send response. Enough to write a real backend service in Eskiu without reaching for libcurl or libmicrohttpd.

### v1.0 — Production-ready

4. **Exception handling** — `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.
5. **Package manager** — dependency resolution, package registry, build system integration. Unblocks external adoption.
6. **Self-hosting** — compile `eskiuc` with Eskiu. Requires closures, freestanding mode, and a stdlib allocator.

---

Phase detail sections follow (0–8), all statuses updated to match foundation milestone completion.
