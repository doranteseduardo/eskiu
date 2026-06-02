# Eskiu Lang — Systems Programming Language

> A performance-first systems language with C-style syntax, compiled to LLVM IR. No garbage collector, no borrow checker—just honest memory management.

**Current Status:** v0.0.1-alpha · Phases 0–4 complete · Lexer, Parser, Type Checker, Codegen working

---

## Why Eskiu?

Years of building high-performance, resource-constrained systems exposed a problem: **language fragmentation**. You need C for raw speed, Python for iteration, Go for concurrency—but switching contexts constantly costs time and introduces subtle bugs. Systems programming shouldn't require learning three type systems.

Eskiu unifies this with a single language optimized for systems work:

- **Familiar** — C-style syntax, anyone can read it immediately
- **Fast** — compiles to bare LLVM IR, zero hidden costs
- **Explicit** — stack allocation by default, heap allocation is your choice
- **Practical** — Go-style structural typing, no inheritance hierarchies

### The Proof of Fire

Eskiu's validity is measured on a concrete problem: **porting our INE QR decoder from 3–5 seconds to sub-second latency** on the same constrained hardware. Our production decoder currently uses a patchwork of languages and techniques. Eskiu's goal is to achieve the performance target *in a single, unified language*—eliminating fragmentation and proving that a systems language can be both accessible and performant.

This is not theoretical. It's a shipping product with measurable requirements.

---

## Quick Start

Get Eskiu compiling in **5 minutes**. See [QUICKSTART.md](QUICKSTART.md) for the complete guide.

**Install LLVM**, build Eskiu, then compile your first program:

```bash
mkdir build && cd build
cmake ..
make -j4

# Compile and run
./eskiu compile ../examples/hello.esk -o hello
./hello
```

For detailed setup instructions, see [BUILD.md](docs/BUILD.md).

---

## Documentation

| Guide | Purpose |
|-------|---------|
| **[QUICKSTART.md](QUICKSTART.md)** | Get your first program running in 5 minutes |
| **[GETTING_STARTED.md](docs/GETTING_STARTED.md)** | Complete walkthrough: syntax, types, memory management, debugging |
| **[LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)** | Full language specification, operators, all features |
| **[BUILD.md](docs/BUILD.md)** | Installation, build configuration, troubleshooting |
| **[DEBUGGING.md](docs/DEBUGGING.md)** | Understanding error messages, using test modes, compiler internals |
| **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** | How the compiler works (Lexer → Parser → Typechecker → Codegen) |
| **[PHASES.md](docs/PHASES.md)** | Development roadmap and implementation status |
| **[examples/](examples/)** | Real programs demonstrating language features |

---

## Language Basics

Variables with explicit types, C-style syntax, honest memory management:

```esk
fn main() -> i32 {
    let x: i32 = 42;
    let ptr: *i32 = &x;
    printf("x = %d\n", x);
    return 0;
}
```

**Full feature tour:** See [LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)  
**Learn by example:** See [examples/](examples/) and [GETTING_STARTED.md](docs/GETTING_STARTED.md)

---

## Compiler Architecture

```
┌─────────────────────────────────────────────────┐
│                  Source Code (.esk)             │
└────────────────────┬────────────────────────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   LEXER (Phase 1) ✅   │
        │  Tokenizes into tokens │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   PARSER (Phase 2) ✅  │
        │  Builds Abstract Tree  │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │ TYPE CHECKER (Phase 4) │
        │    Validates types ✅  │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   CODEGEN (Phase 3) ✅ │
        │ Emits LLVM IR          │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   LLVM Backend         │
        │  Optimizes & compiles  │
        └────────────┬───────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────┐
│  Native Binary (x86-64, ARM64, WASM, RISC-V)   │
└─────────────────────────────────────────────────┘
```

---

## Development Roadmap

| Phase | Status | Focus | Deliverable |
|-------|--------|-------|-------------|
| **0** | Complete | Setup | LLVM integration, CMake build |
| **1** | Complete | Lexer | Tokenizer for all Eskiu syntax |
| **2** | Complete | Parser | AST construction, error recovery |
| **3** | Complete | Codegen | LLVM IR generation, basic types |
| **4** | Complete | Type Checker | Type inference, validation, scope management |
| **5** | Next | Composites | Structs, interfaces, templates |
| **6** | Planned | Memory | `alloc`/`free`, String type |
| **7** | Planned | Stdlib | `Result<T,E>`, collections |
| **8** | Planned | Lambdas | Closures, first-class functions |
| **9** | Planned (v0.2) | Threads | pthreads, Mutex |
| **10** | Planned (v1.0) | Exceptions | try/catch/finally (LLVM EH) |
| **11** | Planned (v2.0) | Async | Coroutines, event loop |

---

## File Structure

```
eskiu/
├── CMakeLists.txt           ← Build configuration
├── SETUP.md                 ← Installation guide
├── README.md                ← This file
│
├── lexer/
│   ├── lexer.h              ← Token definitions
│   └── lexer.cpp            ← Tokenizer implementation
│
├── parser/
│   ├── parser.h             ← Parser interface
│   └── parser.cpp           ← Recursive descent parser
│
├── ast/
│   ├── ast.h                ← AST node types
│   ├── ast.cpp              ← Visitor implementation
│   └── ast_printer.cpp      ← Debug pretty-printer
│
├── codegen/
│   ├── codegen.h            ← LLVM IR generation
│   └── codegen.cpp          ← Visitor-based codegen
│
├── sema/                    ← (Phase 4) Type checker
├── runtime/                 ← (Phase 6) Runtime support
├── stdlib/                  ← (Phase 7) Standard library
│
└── examples/
    └── hello.esk            ← Simple test program
```

---

## Design Philosophy

### 1. Performance-First
Near-metal output, no hidden costs. What you write is what you get.

### 2. C-Style Familiarity
Familiar syntax means less mental overhead. Anyone who knows C can read Eskiu immediately.

### 3. Honest Memory Model
- **Stack allocation** is the default (fast, automatic cleanup)
- **Heap allocation** is explicit (`alloc`/`free`)
- No garbage collection overhead

### 4. No Borrow Checker
Manual memory management is simpler than fighting a borrow checker. Trade memory safety for simplicity.

### 5. Practical Polymorphism
Go-style **structural typing** (interfaces) instead of inheritance hierarchies. If it quacks, it's a duck.

### 6. Errors as Values
Use `Result<T, E>` for error handling. Exceptions come later (v1.0).

---

## Compiler Testing Modes

Inspect the compilation pipeline with these flags:

```bash
# Tokenization: see how code breaks into tokens
./eskiu compile program.esk --test-lexer

# Parsing: see the Abstract Syntax Tree
./eskiu compile program.esk --test-parser

# Type Checking: validate types and scopes
./eskiu compile program.esk --test-typechecker

# Code Generation: see generated LLVM IR
./eskiu compile program.esk --test-codegen
```

These modes are invaluable for debugging. See [DEBUGGING.md](docs/DEBUGGING.md) for detailed examples.

---

## Contributing

Contributions welcome! See [CONTRIBUTING.md](docs/CONTRIBUTING.md).

**How to add a feature:**
1. Pick a phase from [PHASES.md](docs/PHASES.md)
2. Read the relevant architecture section in [ARCHITECTURE.md](docs/ARCHITECTURE.md)
3. Write a test case first
4. Implement the feature
5. Test with `--test-*` modes
6. Commit with clear message

---

## License

MIT

---

**Status:** v0.0.1-alpha (Phases 0–4 complete)  
**Last Updated:** June 2026  
**Compiler:** LLVM 22+  
**Language:** C++17

For the full language specification, see [eskiu-lang-plan.md](./docs/language-spec.md).
