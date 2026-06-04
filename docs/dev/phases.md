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

### v0.1 — Kernel on QEMU

**Goal:** Boot Eskiu code on bare metal in QEMU and print to VGA or serial — without libc, without a C runtime.

This is the classic proof-of-concept for a systems language. It requires:

1. ~~**Inline assembly**~~ — done. `asm("cli")`, extended asm with constraints and clobbers. ✅
2. ~~**Freestanding mode**~~ — done. `--freestanding` flag: `alloc`/`free` use `esk_alloc`/`esk_free`. ✅
3. ~~**`volatile`**~~ — done. `volatile let x: *T` marks all loads/stores as volatile in LLVM IR. ✅
4. ~~**Cross-compilation**~~ — done. `--target TRIPLE` (e.g. `x86_64-pc-linux-gnu`, `aarch64-unknown-none`). Both AArch64 and X86 backends included. ✅

**All compiler prerequisites are complete.** The next step is writing the kernel itself.

Total estimated effort: **3–5 days.**

### v0.2 — Language completeness

4. **Closures** — variable capture from the enclosing scope. Requires an implicit `env*` and codegen adjustments. Unblocks self-hosting.
5. **Thread primitives** — `pthread_create`/`pthread_join` + `Mutex` in stdlib.

### v1.0 — Production-ready

6. **Exception handling** — `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.
7. **Package manager** — dependency resolution, package registry, build system integration.
8. **Self-hosting** — compile `eskiuc` with Eskiu. Requires closures, freestanding mode, and a stdlib allocator.

---

Phase detail sections follow (0–8), all statuses updated to match foundation milestone completion.
