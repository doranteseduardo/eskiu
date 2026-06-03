---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-06-03. All phases 0–8 and editor tooling are COMPLETE. Decoder fully ported to Eskiu (727 lines), running at 74ms on arm64.

---

## Estado actual del lenguaje

| Categoría | Estado |
|-----------|--------|
| Tipos primitivos (int/8/16/32/64, uint, float, double, bool, char, string, void) | ✅ |
| Punteros y aritmética de punteros | ✅ |
| Structs + métodos (`Struct_method`) | ✅ |
| Interfaces con vtable dispatch (fat pointer) | ✅ |
| Templates (structs y funciones, instanciación monomorfa) | ✅ |
| Control de flujo: if/else, while, for, switch/case (con type checking) | ✅ |
| Lambdas (`int(int x) { return x*2; }`) + tipo `fn(T)->R` + HOF | ✅ |
| FFI C (extern, variadic, sret arm64) | ✅ |
| alloc/free, String con concat y append | ✅ |
| List\<T\> con auto-resize | ✅ |
| argv/argc (`int main(int argc, string* argv)`) | ✅ |
| VS Code: errores inline, hover types, go-to-definition | ✅ |
| Closures (captura de variables del scope externo) | ❌ |
| Negative literals (`-1` como primario) | ❌ |
| Inline assembly (`asm(...)`) | ❌ |
| Freestanding mode (sin libc) | ❌ |
| volatile (para MMIO) | ❌ |
| Threads (pthread) | ❌ |
| Exceptions (try/catch) | ❌ |
| Self-hosting | ❌ |

---

## v0.1 Milestone — COMPLETE

**Goal:** Port the INE credential image-processing pipeline from 3–5 seconds to under 1 second.

**Result:** 74.4 ms total — 2.5x faster than reference C, 40–70x faster than the original target.

| Stage | Eskiu | Reference C |
|-------|-------|-------------|
| QR extraction | 71.7 ms | 185.5 ms |
| Crypto (AES+RSA) | 2.8 ms | 2.9 ms |
| Output decode | <1 ms | 0.5 ms |
| **TOTAL** | **74.4 ms** | **188.9 ms** |

---

## Roadmap

### Corto plazo — v0.2

1. **Closures** — captura de variables del scope externo en lambdas. Requiere `env*` implícito y ajuste en codegen. Desbloquea self-hosting.
2. **Negative literals** — `-1` como literal primario (hoy funciona vía unary minus pero falla en inicializadores). Fix de parser de un día.
3. **Inline assembly** — `asm("cli")`, `asm("mov %rax, %rbx")`. Imprescindible para desarrollo de kernel.

### Medio plazo — v0.3

4. **Freestanding mode** — compilar sin libc. Requiere: allocator propio en stdlib, eliminar dependencia de `malloc/printf` del codegen. Requisito para kernel y para self-hosting.
5. **`volatile`** — semántica de acceso no optimizable para MMIO. Un qualifier en el type system.
6. **Thread primitives** — `pthread_create/join` + `Mutex` en stdlib.

### Largo plazo — v1.0

7. **Exception handling** — `try/catch/finally/throw` via LLVM `invoke/landingpad`.
8. **Self-hosting** — compilar `eskiuc` con Eskiu. Requiere closures + freestanding + allocator propio.

### Milestone alternativo — Kernel mínimo en QEMU

Boot + print en VGA/serial sin libc. Requiere únicamente: **inline assembly** (3) + **freestanding mode** (4) + **volatile** (5). Es la prueba de fuego clásica para un lenguaje de sistemas.

---

Phase sections follow (0–8), all statuses updated to match milestone completion.
