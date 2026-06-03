# AGENTS.md

Guidelines for AI agents working on the Eskiu compiler.

## What this project is

Eskiu is a systems programming language compiler written in C++17 that emits LLVM IR. The pipeline is:

```
Source → Lexer → Parser → Type Checker → Codegen → LLVM IR
```

Every pass implements the `ASTVisitor` interface defined in `ast/ast.h`. When adding a new AST node you must update every visitor (type checker, codegen, AST printer).

## Build

```bash
mkdir -p build && cd build
cmake .. && make -j4
```

Requires LLVM 17+. The binary is `build/eskiuc`.

## Test modes

Always validate with the relevant test flag before moving to the next phase:

```bash
./build/eskiuc <file.esk> --test-lexer        # print token stream
./build/eskiuc <file.esk> --test-parser       # print AST
./build/eskiuc <file.esk> --test-typechecker  # run sema, report errors
./build/eskiuc <file.esk> --test-codegen      # print LLVM IR
```

Use files in `examples/` and `test/` as inputs. Add a new `.esk` file for any feature you implement.

## Source layout

| Path | Responsibility |
|---|---|
| `lexer/` | Tokenizer. `Lexer::next_token()` is the only public API. |
| `parser/` | Recursive-descent parser. Produces a `std::shared_ptr<Program>`. |
| `ast/ast.h` | All AST node types + `ASTVisitor` interface. |
| `ast/ast_printer.cpp` | Pretty-printer (used by `--test-parser`). |
| `sema/type_checker.cpp` | Type inference, scope management, struct registry. |
| `codegen/codegen.cpp` | LLVM IR emission via `IRBuilder`. |
| `main.cpp` | CLI entry point. Wires the pipeline and test modes. |

## Coding rules

- **C++17 only.** No C++20 features.
- **No new dependencies.** Only LLVM and the standard library.
- **Match the existing style:** `camelCase` methods, `snake_case` local variables, no trailing comments.
- **No comments** unless the code would genuinely confuse a reader without them.
- AST nodes use `std::shared_ptr` throughout (`ExprPtr`, `StmtPtr`, `DeclPtr`). Keep this consistent.
- Pointer types are represented as strings ending with `*` (e.g. `"i8*"`, `"*Point"`). The helpers `isPointerType()` and `getPointeeType()` in both `TypeChecker` and `CodeGen` handle these.
- Block bodies use `std::vector<BlockItem>` where `BlockItem = std::variant<DeclPtr, StmtPtr>`. Never split these back into two lists.

## Type mappings

| Eskiu | LLVM |
|---|---|
| `int` | `i32` |
| `int64` | `i64` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i1` |
| `char` | `i8` |
| `string` | `i8*` |
| `*T` | pointer to T |

## Adding a new AST node

1. Define the class in `ast/ast.h` (follow existing patterns).
2. Add `virtual void visit(YourNode*)` to `ASTVisitor`.
3. Implement `accept()` in `ast/ast.cpp`.
4. Add `visit(YourNode*)` in: `ASTPrinter`, `TypeChecker`, `CodeGen`.
5. Add a parse site in `parser/parser.cpp`.
6. Write a `.esk` test file under `test/` or `examples/`.

## Current phase boundary

Phases 0–4 are complete. Phase 5 (structs + interfaces) is next. The AST (`StructDecl`, `MemberExpr`) and type checker (`StructInfo` registry, field validation) already have groundwork. What is missing:

- `CodeGen::visit(StructDecl*)` — emit `llvm::StructType`
- `CodeGen::visit(MemberExpr*)` — emit `getelementptr`
- Struct literal initialization syntax in the parser

Do not touch memory management (Phase 6) or stdlib (Phase 7) until struct codegen is working end-to-end and validated with `--test-codegen`.

## Error format

All compiler errors must follow:

```
error: <file>:<line>:<col>: <message>
```

Use the `error(line, col, message)` helper in `TypeChecker`. Codegen errors should `llvm::errs()` with the same format.

## What agents should not do

- Do not add heap allocation (`new`/`malloc`) to the language until Phase 6.
- Do not introduce third-party libraries or new CMake targets.
- Do not rewrite the visitor dispatch to use `std::variant` visiting — the current virtual-dispatch pattern is intentional.
- Do not amend published commits; always create new ones.
