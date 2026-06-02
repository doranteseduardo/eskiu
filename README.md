# 🦬 Eskiu Lang — Systems Programming Language

A modern systems programming language compiled to LLVM IR, with C-style syntax and multiparadigm design (procedural + light functional + OOP). Manual memory management, no garbage collector.

**Target:** Compile a production INE QR decoder (cryptographic decoder for Mexican voter IDs).

## Quick Start

1. **See** [SETUP.md](SETUP.md) for environment configuration
2. **Build:**
   ```bash
   mkdir build && cd build && cmake .. && cmake --build .
   ```
3. **Run:**
   ```bash
   ./build/eskiuc --version
   # → Eskiu 0.0.1 (LLVM 17.x)
   ```

## Language Overview

### Simple Example

```eskiu
extern int printf(string fmt, ...);

int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(5, 3);
    printf("Result: %d\n", result);
    return 0;
}
```

### Key Features

| Feature | Status | Notes |
|---------|--------|-------|
| Variables & primitives | Phase 1 | `int`, `float`, `string`, pointers |
| Functions & extern | Phase 1 | C ABI interop |
| Control flow | Phase 1 | if/else, for, while, switch |
| Structs & methods | Phase 5 | No inheritance; Go-style implicit interfaces |
| Templates/Generics | Phase 5 | Monomorphic instantiation only |
| Heap memory (alloc/free) | Phase 6 | Manual memory management |
| Strings (String type) | Phase 6 | Mutable buffer wrapper |
| Error handling (Result<T,E>) | Phase 7 | Errors as values; exceptions in v1.0 |
| Lambdas & closures | Phase 8 | First-class functions, environment capture |
| Threads | Phase 9 (v0.2) | pthreads wrapper |
| Exceptions | Phase 10 (v1.0) | LLVM EH (invoke/landingpad) |
| Async/await | Phase 11 (v2.0) | LLVM coroutines |

## Development Roadmap

| Phase | Focus | Deliverable |
|-------|-------|-------------|
| 0 | **[CURRENT]** Environment setup | `eskiuc --version` works |
| 1 | Lexer | Token stream from any `.esk` file |
| 2 | Parser + AST | Abstract syntax tree pretty-printer |
| 3 | Basic codegen | Arithmetic, variables, functions → executable |
| 4 | Type checker | Type inference, error reporting |
| 5 | Structs, interfaces, templates | Composite types, polymorphism |
| 6 | Heap memory + strings | Pointer arithmetic, malloc/free |
| 7 | Result<T,E> + stdlib | Error propagation, standard library |
| 8 | Lambdas + closures | Functions as values |
| 9 | Threads (v0.2) | pthreads, mutex |
| 10 | Exceptions (v1.0) | LLVM EH, try/catch/finally |
| 11 | Async/await (v2.0) | Coroutines, event loop |

## Architecture

```
Compiler Pipeline
  ↓
[Lexer]     → Token stream
  ↓
[Parser]    → AST
  ↓
[Type Chk]  → Validated AST
  ↓
[Codegen]   → LLVM IR
  ↓
[LLVM]      → x86-64 / ARM64 / WASM / RISC-V
```

## Dependencies

- **LLVM 17+** — IR generation and optimization
- **CMake 3.20+** — Build system
- **C++17 compiler** — Clang or GCC

See [SETUP.md](SETUP.md) for installation.

## Language Specification

Full design spec: [eskiu-lang-plan.md](./docs/language-spec.md) (v0.2)

Key design principles:
1. **Performance-first** — near-metal output, no hidden costs
2. **C-style familiarity** — immediate readability
3. **Honest memory model** — stack by default, explicit heap
4. **No borrow checker** — manual alloc/free (simpler than Rust)
5. **Practical OOP** — interfaces (Go-style), not inheritance
6. **Errors as values** — Result<T,E> before exceptions

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) (placeholder).

---

**Status:** v0.0.1 (Phase 0 — Environment)  
**Last Updated:** June 2026
