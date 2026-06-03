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

Phases 0–5 (partial) are implemented. The full compilation pipeline is working:
`eskiuc file.esk -o file.o` → `clang file.o -o file` → runs.

**Phase 5 — COMPLETE (core):**
- `visit(StructDecl*)` — `llvm::StructType::create`; methods emitted as `Name_method(ptr self, ...)`
- `visit(MemberExpr*)` + `evaluateLValue(MemberExpr*)` — GEP for read and write; auto-deref `*T`
- `visit(IndexExpr*)` + `evaluateLValue(IndexExpr*)` — GEP for `T[N]` and `*T`
- `visit(BreakStmt*)` — `breakTarget` saved/restored around loops
- `visit(StructInitExpr*)` + `emitStructInitInto()` — named and positional struct literals
- Method calls: `p.method(args)` → `call @Type_method(ptr %p, args)`
- `emitObjectFile()` — native object via LLVM `TargetMachine`
- Float `+`/`-`/`*` now emit `fadd`/`fsub`/`fmul`

**Phase 5.5 — what remains:**
- Interface dispatch (vtable as struct of function pointers)
- Monomorphic templates (`List<T>` → `List_int`)
- `switch`/`case`

**Phase 6 — COMPLETE:**
- `alloc(T, N)` → `call @malloc(i64 N * sizeof(T))`; `AllocExpr` AST node
- `free(ptr)` → `call @free(ptr)`; auto-declared, no explicit `extern` needed
- `getOrDeclareFunc()` for lazy C runtime declarations
- DataLayout initialized early in `generateCode()` for correct `sizeof`

**Phase 5.5 templates — COMPLETE:**
- `struct Name<T, E>` declaration; `templateDecls` registry in both type checker and codegen
- `Result<int,string>` in type references → lazy instantiation → `%Result_int_string`
- `substType` for `*T`, `T[N]`, nested substitution
- `mangleTemplate("Result<int,string>")` → `"Result_int_string"`
- `varTypeStack` stores mangled name for correct `MemberExpr` resolution

**Phase 5.5 + Phase 7 (core) — COMPLETE:**
- `switch`/`case` — `SwitchStmt`, LLVM `switch`, fallthrough, `break` via `breakTarget`
- Function templates — `fn Name<T>(T x)`, `TemplateCallExpr`, `typeParamOverride`, context save/restore during nested instantiation
- `InterfaceDecl` parsed (vtable dispatch deferred — not needed for v0.1)
- `substType` now handles `Name<T,E>` nested substitution
- Parser fix: `int fn<T>(...)` at top level now detected (was silently dropped)
- Type checker fix: template bodies guarded against premature type checking
- `stdlib/result.esk` — `struct Result<T,E>` + `Ok<T,E>` / `Err<T,E>`
- `stdlib/math.esk`, `stdlib/io.esk`, `stdlib/mem.esk`

**Source locations + stdlib completion — COMPLETE:**
- `ASTNode.line/col`; parser stamps all expression/statement nodes; errors show `file.esk:8:22:`
- `&` address-of fixed — returns lvalue pointer (alloca), not loaded value
- Template pointer types (`List<T>*`) correctly resolved in member access
- `stdlib/list.esk` — `List<T>` tested end-to-end with `List_init/push/get/free`
- `stdlib/string.esk` — `String` with init/from/cstr/len/free
- Interface structural satisfaction check in type checker

**v0.1 language completeness — COMPLETE:**
- Hex literals `0xFF`, compound assignments `+=/-=/*=/`, `continue` statement
- Bitwise ops `& | ^ ~ << >>` — new parser precedence levels + codegen
- For-loop with declaration init: `for (int i = 0; ...)` — type checker scope fix
- Pointer arithmetic: `ptr + n` → `GEP(i8, ptr, n)`
- `&` address-of fixed (was returning loaded value)
- `-` unary for floats fixed (`CreateFNeg`), `!` on integers fixed (`ICmpEQ`)

**Remaining gaps — COMPLETE:**
- `import "file.esk"` — multi-file, relative paths, dedup
- Interface vtable dispatch — `%I_fat = {ptr data, ptr vtable}`; auto-boxing; indirect call
- `String.append()` / `String.concat()` — in stdlib/string.esk
- Pointer `==`/`!=` fixed (was FCmpOEQ → now ICmpEQ)
- `i1 → i32` widening fixed (ZExt not SExt)

**Compiler bugs fixed during decoder port:**
- Integer width mismatch in ICmp: ZExt narrower operand before all comparison ops
- Mixed int/float arithmetic: SIToFP promotion in +, -, *, / when types differ

**INE decoder skeleton: `ine_decoder/`**
- All five .esk files compile to .o (46KB arm64)
- Pending: zxing-cpp image loading in `qr_extract_impl.cpp`

**Language status: v0.1 COMPLETE.**

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
