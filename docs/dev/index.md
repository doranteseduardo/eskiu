# Eskiu Compiler: Developer Documentation

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
| [`architecture.md`](architecture.md)   | Pipeline stages, AST node hierarchy, visitor pattern, type mappings (Eskiu → LLVM)             |
| [`abi.md`](abi.md)            | Type lowering, calling convention (sret, varargs), fat pointers, name mangling: the C-ABI contract |
| [`phases.md`](phases.md)         | Current language status, feature table, and roadmap (v0.1 → v0.2.x hardening → v0.3.x self-hosting → v0.4 correctness → v0.5 basic-C → v0.6 memory-safety → v1.0)  |
| [`self-hosting.md`](self-hosting.md) | How the compiler is written in Eskiu (`selfhost/`) and how parity/bootstrap keep it honest |
| [`cross-compile.md`](cross-compile.md) | Cross-compiling with `--target`/`--mcpu`/`--mattr`/`--reloc`; hard-float ARM + 3DS `.3dsx`, and Windows x86-64 COFF/PE |
| [`contributing.md`](contributing.md)   | Branch workflow, code style, commit conventions, testing checklist                             |
| [`design.md`](design.md)         | Rationale for key decisions: C-style syntax, no GC, implicit interfaces, monomorphic templates |
| [`async-design.md`](async-design.md) | The async/await lowering: `Future`, the event loop, and the coroutine state machine |
| [`http2-design.md`](http2-design.md) | The HTTP/2 stack: framing, HPACK, streams and flow control, the multiplexed server |
| [`debugging.md`](debugging.md)      | How each `--test-*` mode works, error message format, common failure patterns                  |
| [`../../docs/API.md`](../../docs/API.md) | C++ public API reference for the lexer, parser, type checker, and codegen modules              |
| [`../../kernel/README.md`](../../kernel/README.md) | The freestanding bare-metal ARM64 kernel demo (no libc; boots in QEMU), a worked example of `--freestanding` + inline asm |

---

## Current State

All compiler phases and editor tooling are complete and tested end-to-end. The language covers async/await, the full HTTP/2 stack (framing, HPACK with Huffman, streams and flow control, the multiplexed server, and TLS/ALPN), sum types with `match`, monomorphic and bounded generics (`<T: Iface>` / `<T: A + B>`), and a broad stdlib (allocators, threading, sockets, the async runtime, JSON, and more). The type checker is the single type resolver, and codegen consumes its resolved expression types.

As of v0.3.0 the compiler is also **self-hosted**: the whole pipeline is reimplemented in Eskiu under `selfhost/`, reaching a 3-stage bootstrap fixpoint with a code generator feature-complete against the C++ corpus (see [`self-hosting.md`](self-hosting.md)). v0.3.1 added `-O0`/`-O1`/`-O2`/`-O3` optimization levels and a `-O0`-vs-`-O2` CI differential. v0.4.0 was a correctness bug-hunt that also tightened the type system; v0.5.0 filled the last basic-C constructs (`do`/`while`, `++`/`--`, array initializers, `static` locals, multidimensional arrays, the ternary `?:`); and v0.6.0 added opt-in memory safety (`defer`/`errdefer`, slices `T[]`, `must_use`, `--safe`, checked nullable pointers `?*T`) plus the `<random>`/`<regex>`/`<sort>`/`<url>`/`<uuid>` stdlib modules and a UTC civil calendar in `<time>`. v0.7.0 added cross-compilation (32-bit ARM / Nintendo 3DS, Windows COFF) and `extern` variables. The self-hosting **promotion** is now complete (`selfhost/PROMOTION_PLAN.md`): the Eskiu-written compiler is behaviorally equivalent to the C++ one over the whole corpus (CI-gated) and dual-built as `eskiuc-esk`, with the C++ binary staying the shipped artifact.

The VS Code extension provides real-time error squiggles, hover type info, and go-to-definition via two CLI flags (`--hover-at`, `--definition-at`). See `phases.md` for the full feature table and roadmap.

---

## Architecture at a Glance

| Component      | Location                | Responsibility                                                                          |
| -------------- | ----------------------- | --------------------------------------------------------------------------------------- |
| Lexer          | `lexer/lexer.cpp`, `lexer/preprocessor.cpp` | Converts source text to a token stream with line/column tracking; runs the preprocessor pass first |
| Parser         | `parser/parser.cpp` + `parse_{decl,stmt,expr}.cpp` (`parser_internal.h`) | Recursive-descent; produces a typed AST from the token stream |
| AST            | `ast/ast.h`             | All node types; visitor interface used by every downstream pass                         |
| Type checker   | `sema/type_checker.cpp` + `typecheck_{decl,stmt,expr,type}.cpp`; the `ty::Type` IR in `sema/type.{h,cpp}` | Scope resolution, type inference, struct registry, interface satisfaction, signatures; the **single type resolver** that produces a per-expression `ty::Type` table that codegen consumes |
| Async transform | `sema/async_transform.cpp` | Rewrites each `async fn` into a frame struct + resume function + `*Future<T>` constructor (ordinary AST that normal codegen handles) |
| Code generator | `codegen/codegen_{module,type,scope,decl,stmt,expr,call,closure,adt}.cpp`, `codegen.h` (no single `codegen/codegen.cpp`) | Walks the AST via visitor, emits LLVM IR using `llvm::IRBuilder<>`; handles GEP, vtable dispatch, and monomorphic template instantiation; consumes the type checker's resolved type table |
| Entry point    | `main.cpp` + `main_support.cpp` | `main.cpp`: CLI dispatch, routing `--test-*` flags to the right pass and driving object emission. `main_support.cpp`: the surrounding machinery, stdlib-path resolution (from the executable's own path), linking objects into an executable, `eskiuc run` (compile to a temp binary, exec, clean up), and `eskiuc fmt` (the source formatter) |

**Pipeline.** lexer → parser → type checker → async transform → **type checker RE-RUN on the transformed AST (the single resolver, producing a per-expression `ty::Type` table)** → codegen (consumes that table; `getTypeFromString` dispatches on `ty::Type::parse`, the one grammar interpreter; codegen does not re-derive expression types).

**Where to look first:**

- Adding a new statement or expression type: `ast/ast.h` → `parser/` → `sema/` (`type_checker.cpp` + the `typecheck_*.cpp` split) → `codegen/`, in that order.
- Fixing a type error: start in `sema/type_checker.cpp` and the `typecheck_*.cpp` split; the struct registry and scope stack are both local to those files.
- Wrong IR output: run with `--test-codegen` and inspect the IR; the visitor method for the relevant AST node is in the relevant `codegen/codegen_*.cpp`.
- Tracing a diagnostic: error messages include file, line, and column; the source location is threaded through every AST node from the lexer onward.
