---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-06-03. All phases 0–8 and editor tooling are complete. A real-world cryptographic pipeline (727 lines of Eskiu) runs at 74 ms on arm64.

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
| C FFI — extern, variadic, sret on arm64 | ✅ |
| alloc/free, String with concat and append | ✅ |
| List\<T\> with auto-resize | ✅ |
| argv/argc — `int main(int argc, string* argv)` | ✅ |
| VS Code — inline errors, hover types, go-to-definition | ✅ |
| Closures — capturing variables from the enclosing scope | ❌ |
| Negative literals — `-1` as a primary expression | ❌ |
| Inline assembly — `asm(...)` | ❌ |
| Freestanding mode — compile without libc | ❌ |
| volatile — for memory-mapped I/O | ❌ |
| Threads — pthread primitives | ❌ |
| Exceptions — try/catch | ❌ |
| Package manager | ❌ |
| Self-hosting | ❌ |

---

## v0.1 Milestone — COMPLETE

**Goal:** Port a cryptographic image-processing pipeline from 3–5 seconds to under 1 second.

**Result:** 74.4 ms total — 2.5× faster than the reference C implementation.

| Stage | Eskiu | Reference C |
|-------|-------|-------------|
| QR extraction | 71.7 ms | 185.5 ms |
| Crypto (AES+RSA) | 2.8 ms | 2.9 ms |
| Output decode | < 1 ms | 0.5 ms |
| **Total** | **74.4 ms** | **188.9 ms** |

---

## Roadmap

### Near-term — v0.2

1. **Closures** — variable capture from the enclosing scope. Requires an implicit `env*` and codegen adjustments. Unblocks self-hosting.
2. **Negative literals** — `-1` as a primary expression. Works today via unary minus but fails in some initialisers. One-day parser fix.
3. **Inline assembly** — `asm("cli")`, `asm("mov %rax, %rbx")`. Required for kernel development.

### Medium-term — v0.3

4. **Freestanding mode** — compile without libc. Requires a stdlib allocator and removing the implicit dependency on `malloc`/`printf` in codegen. Prerequisite for kernel work and self-hosting.
5. **`volatile`** — non-optimisable memory access semantics for MMIO. A type qualifier in the type system.
6. **Thread primitives** — `pthread_create`/`pthread_join` + `Mutex` in stdlib.

### Long-term — v1.0

7. **Exception handling** — `try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad`.
8. **Package manager** — dependency resolution, package registry, build system integration. Unblocks external adoption.
9. **Self-hosting** — compile `eskiuc` with Eskiu. Requires closures, freestanding mode, and a stdlib allocator.

### Alternative milestone — Minimal kernel on QEMU

Boot and print to VGA/serial without libc. Requires exactly three items: **inline assembly** (3), **freestanding mode** (4), and **volatile** (5). The classic proof-of-concept for a systems language.

---

Phase sections follow (0–8), all statuses updated to match milestone completion.
