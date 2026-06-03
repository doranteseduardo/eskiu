# Changelog

## [Unreleased]

### Planned
- Phase 5.5 remainder: Go-style interface dispatch, `switch`/`case`
- Phase 7: `Result<T,E>` stdlib constructors (`Ok`/`Err`), stdlib base

---

## [0.0.4-alpha] — 2026-06-02

### Added — Phase 5.5 (monomorphic template structs)

- **Template struct declaration**: `struct Result<T, E> { ... }` — `StructDecl` gains `typeParams` field; template declarations are stored in a separate registry and not emitted to LLVM until first use
- **Template type references**: `Result<int, string>` parsed in `parseType()` via `IDENT < TYPE, ... >` lookahead; stored as `"Result<int,string>"` in the AST
- **Lazy instantiation in type checker**: `normalizeType("Result<int,string>")` detects `<`, looks up the template, substitutes `T→int`, `E→string` in all field types, registers `"Result_int_string"` as a concrete struct
- **Lazy instantiation in codegen**: `getTypeFromString("Result<int,string>")` calls `ensureTemplateInstantiated()` which creates `%Result_int_string = type { i32, i32, ptr }` on first use
- **Name mangling**: `Result<int,string>` → `Result_int_string` in LLVM IR
- **Type substitution**: `substType` handles `*T` → `*int`, `T*` → `int*`, `T[N]` → `int[N]`, and nested template types recursively
- **`varTypeStack` normalization**: `VarDecl` stores the mangled name so `MemberExpr` resolution finds instantiated struct fields correctly

---

## [0.0.3-alpha] — 2026-06-02

### Added — Phase 6 (heap memory)

- **`alloc(T, N)`**: new `AllocExpr` AST node; emits `call @malloc(i64 N * sizeof(T))`; `sizeof(T)` resolved via `DataLayout` initialized at the start of `generateCode()`
- **`free(ptr)`**: parsed as a regular call; `free` pre-registered in the type checker as variadic and auto-declared in the LLVM module on first use
- **`getOrDeclareFunc()`**: codegen helper that lazily declares C runtime functions (`malloc`, `free`) into the module without requiring explicit `extern` declarations

### Fixed
- `validateStructType` did not strip leading `*T` pointer prefix — caused false "undefined struct" errors on `*uint8`, `*Point`, etc.
- `inferBinaryExprType` did not handle the `=` operator for non-numeric types — assignment to pointer fields (`p.field = alloc(...)`) and struct fields now type-checks correctly
- `isValidAssignment` now allows any-pointer to any-pointer assignment for C interop

---

## [0.0.2-alpha] — 2026-06-02

### Added — Phase 5 (structs, methods, initialization)

- **Struct codegen**: `llvm::StructType::create` per `StructDecl`; `alloca %StructType` for locals; field read/write via `getelementptr`
- **Fixed-size array fields**: `uint8[858]` → `[858 x i8]`; parser now captures size (was discarded); `IndexExpr` codegen via GEP for both `T[N]` and `*T`
- **Struct literal initialization**: named (`Point { x: 1.5, y: 2.5 }`) and positional (`Point { 1.5, 2.5 }`) — fills alloca directly with per-field type coercion
- **Method calls**: methods emitted as `StructName_methodName(ptr self, ...)` mangled functions; `p.method(args)` detects and prepends implicit `self` pointer
- **`StructInitExpr` AST node**: full visitor chain — parser, type checker, codegen, AST printer
- **`BreakStmt` codegen**: `CreateBr(breakTarget)`; target saved/restored around loop bodies
- **`emitObjectFile()`**: native object file via LLVM `TargetMachine` + `legacy::PassManager`; full pipeline `eskiuc file.esk -o file.o` → linkable object
- **Float arithmetic**: `+`, `-`, `*` now emit `fadd`/`fsub`/`fmul` for floating-point operands (was always emitting integer instructions)

### Fixed
- Type checker: method bodies registered and type-checked in first pass
- Type checker: `*T` (leading-pointer) not auto-derefed on member access — now strips leading `*` before struct lookup
- Type checker: `isValidAssignment` normalizes both sides so `"Point" == "struct:Point"`
- Type checker: function parameter types were stored as parameter names (e.g. `"a"`) instead of types (e.g. `"int"`)
- Type checker: variadic functions rejected call sites with more arguments than fixed params
- Codegen: `varTypeStack` scoped alongside symbol table for correct struct type resolution across nested scopes

---

## [0.0.1-alpha] — 2026-06-02

### Added

- **Phase 0 — Build system and CLI**: CMake build with LLVM 17+ integration; `--version` flag; `--test-lexer`, `--test-parser`, `--test-typechecker`, `--test-codegen` modes; file:line:col error reporting
- **Phase 1 — Lexer**: Complete tokenizer with line/col tracking; all Eskiu keywords (`int`, `float`, `uint8`–`uint64`, `int8`–`int64`, `struct`, `interface`, `enum`, `alloc`, `free`, `extern`, `thread`, `try`/`catch`/`finally`, and more); COLON token for type annotations
- **Phase 2 — Parser**: Recursive-descent parser producing a visitor-based AST; functions, variables (`let x: int = 5` and C-style `int x = 5` both accepted), structs with fields and methods, `extern` declarations, full control flow (`if`/`else`, `for`, `while`, `break`, `return`), expressions with correct precedence, cast expressions `(TYPE)expr`
- **Phase 3 — Codegen**: LLVM IRBuilder backend; arithmetic, comparison, and logical operators; `if`/`else`, `while`, `for`; function calls; integer and float literals; type coercion on initializers; correct lvalue/rvalue split (`evaluateLValue`) so assignments emit `store` to an `alloca` rather than to a value
- **Phase 4 — Type checker**: Scope-aware analysis; type inference for all binary and unary operators; struct field validation; function signature checking; `MemberExpr` member-type resolution; parameters registered before the validation pass
- **Types**: `uint8`/`uint16`/`uint32`/`uint64` and `int8`/`int16`/`int32`/`int64` as first-class types mapped to LLVM `i8`–`i64`; `bool` → `i1`; `char` → `i8`; `string` → `i8*`
- **Pointer types**: both leading `*T` and trailing `T*` syntax accepted throughout lexer, parser, and type checker
- **Examples**: `examples/hello.esk`, `examples/test_struct.esk`, `examples/test_struct_error.esk`

### Fixed

- Lexer: COLON token was not recognized, breaking `let`-style type annotations
- Type checker: function parameters were not registered before the body validation pass, causing false "undeclared identifier" errors

### Known limitations

- Struct codegen not wired (Phase 5); `MemberExpr` type-checks but does not emit IR
- No heap allocation (`alloc`/`free`) — stack only (Phase 6)
- No interfaces or templates (Phase 5)
- No standard library or `Result<T,E>` (Phase 7)
- No lambdas, threads, or async (Phase 8+)
