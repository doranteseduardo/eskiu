## Eskiu Compiler — Developer Documentation

Eskiu is a systems language built to address the fragmentation of compute-intensive services — C for performance-critical work, Go for concurrency, C++ for libraries, Python for glue. The goal is a single language that replaces that stack, starting with a solid systems foundation and eventually adding first-class support for the domain types that high-throughput services actually work with.

This section covers the internals of the compiler: its full compilation pipeline, AST design, type system, semantic analysis, and LLVM-based code generation layer. It is written for contributors who want to understand how the compiler works, add a new language feature, fix a bug, or extend the test suite. Familiarity with C++17 and a basic understanding of LLVM IR are assumed; no prior compiler experience is required.

---

## Quick Start for Contributors

```bash
# Prerequisites: clang++ (C++17), LLVM dev headers, cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run the four test modes against the canonical hello.esk
./build/eskiuc examples/hello.esk --test-lexer
./build/eskiuc examples/hello.esk --test-parser
./build/eskiuc examples/hello.esk --test-typechecker
./build/eskiuc examples/hello.esk --test-codegen

# Compile a source file to a native object and link it
./build/eskiuc examples/hello.esk -o hello.o
clang hello.o -o hello
./hello
```

Each test mode exits 0 on success and prints a human-readable dump to stdout. A non-zero exit indicates a hard compiler error; diagnostic messages go to stderr with the format `file.esk:line:col: message`.

---

## Documentation Map

| Document            | What it covers                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------- |
| `architecture.md`   | Pipeline stages, AST node hierarchy, visitor pattern, type mappings (Eskiu → LLVM)             |
| `phases.md`         | Phase status (0–8), current feature table, roadmap (v0.2 → v1.0)                              |
| `contributing.md`   | Branch workflow, code style, commit conventions, testing checklist                             |
| `design.md`         | Rationale for key decisions: C-style syntax, no GC, implicit interfaces, monomorphic templates |
| `debugging.md`      | How each `--test-*` mode works, error message format, common failure patterns                  |
| `../../docs/API.md` | C++ public API reference for the lexer, parser, type checker, and codegen modules              |

---

## Current State (v0.0.12-alpha)

All compiler phases (0–8) and editor tooling are complete and tested end-to-end. The v0.1 milestone is done: the INE credential decoder (727 lines of Eskiu) runs at 74 ms on arm64 — 2.5× faster than the reference C implementation.

Phase 8 added lambdas and anonymous functions (`int(int x) { return x * 2; }`), function pointer types (`fn(T,...)->R`), and higher-order functions. The VS Code extension provides real-time error squiggles, hover type info, and go-to-definition via two new CLI flags (`--hover-at`, `--definition-at`).

There are no components currently in active development. The compiler is stable. The next planned work is closures, inline assembly, and freestanding mode — see `phases.md` for the full roadmap.

---

## Architecture at a Glance

| Component      | Location                | Responsibility                                                                          |
| -------------- | ----------------------- | --------------------------------------------------------------------------------------- |
| Lexer          | `lexer/`                | Converts source text to a token stream with line/column tracking                        |
| Parser         | `parser/`               | Recursive-descent; produces a typed AST from the token stream                          |
| AST            | `ast/ast.h`             | All node types; visitor interface used by every downstream pass                         |
| Type checker   | `sema/type_checker.cpp` | Scope resolution, type inference, struct registry, interface satisfaction, signatures   |
| Code generator | `codegen/codegen.cpp`   | Walks the AST via visitor, emits LLVM IR using `llvm::IRBuilder<>`; handles GEP, vtable dispatch, and monomorphic template instantiation |
| Entry point    | `main.cpp`              | CLI dispatch; routes `--test-*` flags to the appropriate pass and drives object emission |

**Where to look first:**

- Adding a new statement or expression type: `ast/ast.h` → `parser/` → `sema/type_checker.cpp` → `codegen/codegen.cpp`, in that order.
- Fixing a type error: start in `sema/type_checker.cpp`; the struct registry and scope stack are both local to that file.
- Wrong IR output: run with `--test-codegen` and inspect the IR; the visitor method for the relevant AST node is in `codegen/codegen.cpp`.
- Tracing a diagnostic: error messages include file, line, and column; the source location is threaded through every AST node from the lexer onward.
