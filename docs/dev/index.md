## Eskiu Compiler — Developer Documentation

This section covers the internals of the Eskiu compiler: its pipeline, AST design, type system, and code generation layer. It is written for contributors who want to understand how the compiler works, add a new language feature, fix a bug, or extend the test suite. Familiarity with C++17 and a basic understanding of LLVM IR are assumed; no prior compiler experience is required.

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
```

Each mode exits 0 on success and prints a human-readable dump to stdout. A non-zero exit indicates a hard compiler error; diagnostic messages go to stderr.

---

## Documentation Map

| Document            | What it covers                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------- |
| `architecture.md`   | Pipeline stages, AST node hierarchy, visitor pattern, type mappings (Eskiu → LLVM)             |
| `phases.md`         | Accurate phase status (0–8+), per-phase requirements, acceptance criteria                      |
| `contributing.md`   | Branch workflow, code style, commit conventions, testing checklist                             |
| `design.md`         | Rationale for key decisions: C-style syntax, no GC, implicit interfaces, monomorphic templates |
| `debugging.md`      | How each `--test-*` mode works, error message format, common failure patterns                  |
| `../../docs/API.md` | C++ public API reference for the lexer, parser, type checker, and codegen modules              |

---

## Current Focus: Phase 5

Phase 5 covers struct codegen, implicit interfaces (Go-style), and monomorphic template instantiation.

**Already in place:**

- `StructDecl` AST node with fields and method declarations parsed and stored
- `MemberExpr` type-checked against the struct registry in `sema/type_checker.cpp`
- Struct field validation (unknown field access is caught at type-check time)
- Pointer type tracking (`*T`) through the type system

**Still to build:**

- `llvm::StructType` creation and registration in `codegen/codegen.cpp`
- GEP (GetElementPtr) emission for field reads and writes
- Method dispatch: lower `obj.method(args)` to a plain function call with `obj` as first argument
- Interface satisfaction check: verify a type implements all methods of an interface at the call site
- Template instantiation: monomorphize `T` at each unique call site, generate a typed copy of the function body

**Acceptance test — INE decoder structs:**

```eskiu
struct QRSymbol {
    uint8[858] data;
    int width;
    int height;
}
```

Compiling this struct, reading and writing fields, and passing a `*QRSymbol` across function boundaries is the Phase 5 acceptance gate.

---

## Architecture at a Glance

| Component      | Location                | Responsibility                                                        |
| -------------- | ----------------------- | --------------------------------------------------------------------- |
| Lexer          | `lexer/`                | Converts source text to a token stream with line/column tracking      |
| Parser         | `parser/`               | Recursive-descent; produces a typed AST from the token stream         |
| AST            | `ast/ast.h`             | All node types; visitor interface used by every downstream pass       |
| Type checker   | `sema/type_checker.cpp` | Scope resolution, type inference, struct registry, signature checking |
| Code generator | `codegen/codegen.cpp`   | Walks the AST via visitor, emits LLVM IR using `llvm::IRBuilder<>`    |
| Entry point    | `main.cpp`              | CLI dispatch; routes `--test-*` flags to the appropriate pass         |

**Where to look first:**

- Adding a new statement or expression type: `ast/ast.h` → `parser/` → `sema/type_checker.cpp` → `codegen/codegen.cpp`, in that order.
- Fixing a type error: start in `sema/type_checker.cpp`; the struct registry and scope stack are both local to that file.
- Wrong IR output: run with `--test-codegen` and inspect the IR; the visitor method for the relevant AST node is in `codegen/codegen.cpp`.
