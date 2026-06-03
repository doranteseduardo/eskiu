# Changelog

## [Unreleased]

### Planned
- INE QR decoder port (v0.1 milestone)

---

## [0.0.8-alpha] — 2026-06-02

### Added

**`import "file.esk"`**
- Multi-file support: `import "path/to/lib.esk";` at the top level resolves relative to the importing file's directory
- Recursive imports with deduplication — a file imported multiple times is only parsed once
- `Parser.basedir` + `importedFiles` set propagated to sub-parsers

**Interface vtable dispatch**
- `interface I { void method(); }` — `visit(InterfaceDecl*)` creates `%I_vtable = type { ptr, ... }` and `%I_fat = type { ptr data, ptr vtable }` types
- Structural satisfaction already checked by type checker; codegen auto-boxes structs at call sites using `funcEskiuParamTypes` map
- `boxAsInterface()` creates a fat pointer alloca filled with `{data_ptr, vtable_constant_ptr}` and passes it as `ptr`
- Interface method dispatch: load vtable ptr from fat pointer, load fn ptr at method index, call indirectly
- `getExprEskiuType` extended to handle `UnaryExpr("&", ...)` and `UnaryExpr("*", ...)` — needed for boxing detection

**`String.append()` and `String.concat()`** — added to `stdlib/string.esk` using `memcpy` + pointer arithmetic

### Fixed
- Pointer comparison (`p == null`, `ptr1 == ptr2`) used `FCmpOEQ` (float) — now uses `ICmpEQ`; all six comparison operators updated to check `isFloatingPointTy()` first
- `i1 → i32` widening used `SExt` (sign-extends `true` → `-1`) — now uses `ZExt` for `i1` operands so comparisons correctly store `0` or `1`

---

## [0.0.7-alpha] — 2026-06-02

### Added — Language completeness for v0.1

**Bitwise operators**
- Binary: `&`, `|`, `^`, `<<`, `>>` — new precedence levels in parser (bitwiseOr → bitwiseXor → bitwiseAnd → equality, shift → comparison → additive)
- Unary: `~` (bitwise NOT) — added to unary operator list in parser + `inferUnaryExprType`
- Codegen: `CreateAnd`, `CreateOr`, `CreateXor`, `CreateShl`, `CreateAShr`

**Hex literals**
- `0xFF`, `0x0F` — lexer now reads hex digits after `0x`/`0X` prefix; `stoll(..., 0)` for auto base detection in codegen

**Compound assignments**
- `+=`, `-=`, `*=`, `/=`, `%=` — new tokens in lexer; desugared in `parseAssignment()` to `x = x op y`

**`continue` statement**
- `ContinueStmt` AST node; full visitor chain; `continueTarget` saved/restored around `WhileStmt` and `ForStmt` bodies (points to loop condition and step respectively)

**For-loop with declaration init**
- `for (int i = 0; i < n; i += 1)` — parser wraps declaration in `BlockStmt`; type checker processes init items directly in ForStmt scope (avoids inner-scope pop that made `i` undefined in condition/step/body)

**Pointer arithmetic**
- `ptr + n` → `GEP(i8, ptr, n)`, `ptr - n` → `GEP(i8, ptr, -n)` in `visit(BinaryExpr*)`

### Fixed
- `&` address-of returned a loaded value instead of the alloca pointer — breaks all pointer-argument call patterns
- Unary `-` for floats used integer `CreateNeg` — now uses `CreateFNeg` for floating-point operands
- `!` logical NOT on integers used bitwise `CreateNot` (wrong for `i32`) — now uses `CreateICmpEQ(x, 0)` for non-`i1` types
- `~` unary not in parser unary operator list — caused parse failure for `~0` and similar
- `inferUnaryExprType` did not handle `~` — reported "invalid operand" for bitwise NOT on integers
- `inferBinaryExprType` did not handle bitwise/shift operators — reported "invalid operands" for `a & b`, `a << n`
- For-loop init declarations were scoped to an inner BlockStmt, making the variable undefined in the loop condition and body

---

## [0.0.6-alpha] — 2026-06-02

### Added

- **Source locations in errors**: `ASTNode` now carries `line`/`col`; parser stamps all expression and statement nodes; errors now report `file.esk:line:col:` instead of `file.esk:0:0:`. Filename taken from CLI input path.
- **`&` address-of operator**: fixed — now correctly returns the lvalue pointer (alloca) instead of a loaded value, enabling `fn(&localVar)` patterns and template method calls via `self` pointer
- **Template member access on pointer types**: trailing `*` stripped before struct field lookup; `self: List<int>*` now correctly resolves to `structFields["List_int"]` in both `visit(MemberExpr*)` and `getExprEskiuType()`
- **`stdlib/list.esk`**: `List<T>` with `List_init`, `List_push`, `List_get`, `List_len`, `List_free` as template functions; tested end-to-end
- **`stdlib/string.esk`**: `String` struct with `String_init`, `String_from`, `String_cstr`, `String_len`, `String_free`
- **Interface structural check**: `isValidAssignment` now verifies that a struct satisfies an interface's method signatures (vtable codegen deferred)
- **`isPointerType` unified**: now detects both leading `*T` and trailing `T*` conventions

### Fixed
- `&` operator returned a loaded value instead of the address — broke all pointer-argument patterns
- Template method parameter types with pointer suffix (e.g. `List<T>*`) were incorrectly mangled to `List_int_*` instead of `List_int*`
- `getExprEskiuType` for member chains through pointer-to-template-struct now resolves correctly

---

## [0.0.5-alpha] — 2026-06-02

### Added — Phase 5.5 completion + Phase 7 stdlib base

**`switch`/`case`**
- `SwitchStmt` AST node with `Case { value, stmts }` list; full visitor chain
- Parser: `switch (expr) { case val: stmts break; default: stmts }` with fallthrough support
- Codegen: LLVM `switch` instruction with `ConstantInt` case values; `break` branches to `switch.end` via existing `breakTarget` mechanism

**Function templates**
- `int fn<T, E>(T x) -> RetType<T,E> { ... }` — `FunctionDecl.typeParams` field
- `TemplateCallExpr` AST node: `name<TypeArg,...>(args)` parsed in `parsePostfix()`
- Codegen: lazy instantiation with `typeParamOverride` map — intercepts all type lookups in `getTypeFromString`, `visit(VarDecl*)`, etc. during template body emission
- Context save/restore: insert point and `currentFunction` preserved when instantiating inside another function's body
- `substType` extended to substitute inside `Name<T,E>` template type strings recursively

**`InterfaceDecl`**
- `interface Speakable { void speak(); }` fully parsed; stored in `interfaceDecls`
- Codegen generates no IR for interfaces (vtable dispatch deferred — not required for v0.1)

**Stdlib base** (`stdlib/`)
- `result.esk` — `struct Result<T,E>` + `Ok<T,E>` / `Err<T,E>` template constructors
- `math.esk` — `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` (extern to libm)
- `io.esk` — `printf`, `fprintf`, `sprintf`, `scanf`, `puts`, `getchar`, `putchar`
- `mem.esk` — `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `memchr`

### Fixed
- Parser: `int fn<T>(T x)` at top level was silently dropped — `parseDeclaration()` now detects `<` after a name as a function template
- Type checker: template function bodies were visited with unresolved type params — now guarded with `typeParams.empty()` check
- `substType`: did not substitute type params inside `Name<T,E>` template strings — now handles nested template types recursively

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
