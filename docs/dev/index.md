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

# Compile and link in one step (eskiuc invokes the system C toolchain)
./build/eskiuc examples/hello.esk -o hello
./hello

# Or stop at the object file (.o output, or -c) and link it yourself
./build/eskiuc examples/hello.esk -o hello.o
clang hello.o -o hello
```

Each test mode exits 0 on success and prints a human-readable dump to stdout. A non-zero exit indicates a hard compiler error; diagnostic messages go to stderr with the format `file.esk:line:col: message`.

---

## Documentation Map

| Document            | What it covers                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------- |
| `architecture.md`   | Pipeline stages, AST node hierarchy, visitor pattern, type mappings (Eskiu → LLVM)             |
| `abi.md`            | Type lowering, calling convention (sret, varargs), fat pointers, name mangling — the C-ABI contract |
| `phases.md`         | Phase status (0–8), current feature table, roadmap (v0.2 → v1.0)                              |
| `contributing.md`   | Branch workflow, code style, commit conventions, testing checklist                             |
| `design.md`         | Rationale for key decisions: C-style syntax, no GC, implicit interfaces, monomorphic templates |
| `debugging.md`      | How each `--test-*` mode works, error message format, common failure patterns                  |
| `../../docs/API.md` | C++ public API reference for the lexer, parser, type checker, and codegen modules              |

---

## Current State (v0.2.0)

All compiler phases and editor tooling are complete and tested end-to-end. The language is feature-complete for the **v0.2.0 "backend services" release**: async/await, the full HTTP/2 stack (framing, HPACK with Huffman, streams and flow control, the multiplexed server, and TLS/ALPN), sum types with `match`, monomorphic generics, and the stdlib (allocators, threading, sockets, async runtime, JSON, and more).

**v0.1.0** (the bare-metal systems foundation) is shipped and tagged: a cryptographic pipeline running entirely in Eskiu at 74 ms on arm64 — 2.5× faster than the reference C — plus an ARM64 kernel booting in QEMU without libc. v0.2.0 builds the concurrent-backend stack on top of that foundation.

The VS Code extension provides real-time error squiggles, hover type info, and go-to-definition via two CLI flags (`--hover-at`, `--definition-at`). See `phases.md` for the full feature table and roadmap.

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
