## Eskiu Compiler — Developer Documentation

This section covers the internals of the Eskiu compiler: its full compilation pipeline, AST design, type system, semantic analysis, and LLVM-based code generation layer. It is written for contributors who want to understand how the compiler works, add a new language feature, fix a bug, or extend the test suite. Familiarity with C++17 and a basic understanding of LLVM IR are assumed; no prior compiler experience is required.

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
| `phases.md`         | Phase status (0–7 and Phase 5.5), per-phase requirements, acceptance criteria                  |
| `contributing.md`   | Branch workflow, code style, commit conventions, testing checklist                             |
| `design.md`         | Rationale for key decisions: C-style syntax, no GC, implicit interfaces, monomorphic templates |
| `debugging.md`      | How each `--test-*` mode works, error message format, common failure patterns                  |
| `../../docs/API.md` | C++ public API reference for the lexer, parser, type checker, and codegen modules              |

---

## Current State (v0.0.11-alpha)

All compiler phases (0–7) and Phase 5.5 are complete and tested end-to-end. The language feature set is v0.1 ready: every type, operator, control flow construct, struct feature, template, interface, and multi-file mechanism described in the language spec compiles to correct native arm64 and x86-64 object files.

The immediate next milestone is porting the INE QR decoder as a real-world integration test of the full pipeline — particularly fixed-size array fields, pointer arithmetic, and multi-file imports.

There are no components currently in active development. The compiler is stable.

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
