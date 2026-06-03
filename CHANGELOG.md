# Changelog

## [Unreleased]

### Planned
- Phase 5: struct codegen (method dispatch, fixed-size array fields), Go-style implicit interfaces, monomorphic template instantiation

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
