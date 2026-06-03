---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors. Supersedes `docs/PHASES.md`.

Last updated: 2026-06-02. Phase 4 is COMPLETE. Phase 5 is the active work item.

---

## Phase 0 — Environment Setup

**Status: COMPLETE**

**Goal:** Establish build toolchain and CLI skeleton so all subsequent phases have a consistent entry point.

**Deliverable:** `./eskiuc --version` prints version and exits cleanly.

### Implemented
- CMake project with LLVM 17+ via `llvm-config`
- `main.cpp` CLI with `--version`, `--test-lexer`, `--test-parser`, `--test-typechecker`, `--test-codegen` dispatch
- C++17 standard enforced across all translation units

### Known Gaps
None.

### Key Files
- `main.cpp`
- `CMakeLists.txt`

---

## Phase 1 — Lexer

**Status: COMPLETE**

**Goal:** Convert raw `.esk` source text into a typed token stream with position information.

**Deliverable:** `./eskiuc <file.esk> --test-lexer` prints token type, value, line, and column for every token.

### Implemented
- All language keywords: `let`, `int`, `int8`, `int16`, `int32`, `int64`, `uint`, `uint8`, `uint16`, `uint32`, `uint64`, `float`, `double`, `bool`, `char`, `string`, `void`, `if`, `else`, `for`, `while`, `return`, `break`, `struct`, `extern`, `fn`, `true`, `false`, `null`
- Future-reserved keywords tokenized (not parsed): `thread`, `spawn`, `mutex`, `try`, `catch`, `finally`, `throw`, `alloc`, `free`, `async`, `await`
- Full operator set: arithmetic, bitwise, comparison, logical, assignment, address-of, dereference
- Delimiter set: `(`, `)`, `{`, `}`, `[`, `]`, `,`, `;`, `.`, `...`
- Literals: integer (decimal, hex `0x`), floating-point, string with escape sequences (`\n`, `\t`, `\\`, `\"`, `\0`), character, boolean, `null`
- Line and column tracking on every token
- Comments: `//` (line) and `/* */` (block, non-nested)
- Variadic sentinel `...` as a single token

### Known Gaps
None.

### Key Files
- `lexer/lexer.h`
- `lexer/lexer.cpp`

---

## Phase 2 — Parser

**Status: COMPLETE**

**Goal:** Build a typed AST from the token stream using a hand-written recursive-descent parser.

**Deliverable:** `./eskiuc <file.esk> --test-parser` prints the full AST with indentation.

### Implemented

**Declarations**
- `FunctionDecl` — return type, name, typed parameter list, body block
- `VarDecl` — both C-style (`int x = 5;`) and `let`-style (`let x: int = 5;`); optional initializer
- `StructDecl` — fields and methods, brace-delimited
- `ExternDecl` — `extern` function signatures with variadic support (`...`)

**Statements**
- `BlockStmt` — declarations and statements unified into a single ordered list
- `IfStmt` — optional `else` branch
- `ForStmt` — classic C 3-part form (init; condition; step)
- `WhileStmt`
- `ReturnStmt` — optional expression
- `BreakStmt`
- `ExprStmt`

**Expressions (precedence, lowest to highest)**

| Level | Operators |
|-------|-----------|
| Assignment | `=` |
| Logical OR | `\|\|` |
| Logical AND | `&&` |
| Equality | `==`, `!=` |
| Relational | `<`, `>`, `<=`, `>=` |
| Additive | `+`, `-` |
| Multiplicative | `*`, `/`, `%` |
| Unary | `!`, `-`, `+`, `&`, `*`, `(TYPE)` cast |
| Postfix | `f()`, `a[i]`, `a.b` |

- `CastExpr` — `(TYPE)expr` syntax
- `MemberExpr` — `.` access
- `IndexExpr` — `[]` subscript
- `CallExpr` — function call with argument list
- Error recovery: on parse error, skip tokens to the next `;` and continue

### Known Gaps
- `switch`/`case` not implemented (deferred to Phase 5)
- No struct literal initialization syntax (`Point { x: 1, y: 2 }`) — parser groundwork exists but syntax not finalized

### Key Files
- `parser/parser.h`
- `parser/parser.cpp`
- `ast/ast.h`
- `ast/ast.cpp`
- `ast/ast_printer.cpp`

---

## Phase 3 — Code Generation

**Status: COMPLETE**

**Goal:** Lower the AST to valid, verifiable LLVM IR using `IRBuilder`.

**Deliverable:** `./eskiuc <file.esk> --test-codegen` prints LLVM IR that passes `llvm::verifyModule`.

### Implemented

**Types**

| Eskiu type | LLVM type |
|------------|-----------|
| `int` / `int32` | `i32` |
| `int8` | `i8` |
| `int16` | `i16` |
| `int64` | `i64` |
| `uint` / `uint32` | `i32` (unsigned semantics) |
| `uint8` | `i8` |
| `uint16` | `i16` |
| `uint64` | `i64` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i1` |
| `char` | `i8` |
| `string` | `i8*` |
| `*T` / `T*` | pointer to T |

**Expressions**
- All arithmetic operators: `+`, `-`, `*`, `/`, `%` (integer and floating-point variants)
- All comparison operators: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical operators: `&&`, `||`, `!`
- Unary: `-`, `!`
- All literal types: integer, float, double, bool, char, string (global constant), null
- Assignment via `evaluateLValue` — correctly emits `store` to `alloca` slots rather than overwriting the loaded value
- `CastExpr` — integer truncation/extension, float↔integer conversions
- `CallExpr` — direct calls including variadic extern functions
- `IdentExpr` — load from named `alloca`

**Statements**
- `VarDecl` — `alloca` + optional initializer with type coercion
- `IfStmt` — true-block, false-block, merge block using `IRBuilder` basic-block phi structure
- `WhileStmt` — condition, body, exit (3-block pattern)
- `ForStmt` — init, condition, body, step, exit (4-block pattern)
- `ReturnStmt`
- `ExternDecl` — `llvm::Function::ExternalLinkage` with correct variadic flag

### Known Gaps (stubs emit warnings, do not abort)
- `StructDecl` — no `llvm::StructType` created; emits warning
- `MemberExpr` — no GEP emitted; returns null value
- `IndexExpr` — no GEP emitted; returns null value
- `BreakStmt` — no branch to exit block emitted; emits warning
- `emitObjectFile` — not implemented; IR-only output

### Key Files
- `codegen/codegen.h`
- `codegen/codegen.cpp`

---

## Phase 4 — Type Checker

**Status: COMPLETE**

**Goal:** Validate the AST for type correctness before codegen, reporting actionable errors.

**Deliverable:** `./eskiuc <file.esk> --test-typechecker` reports type errors with file/line context or exits cleanly.

### Implemented

**Two-pass architecture**
1. Registration pass — collect all `StructDecl` and `FunctionDecl` signatures into global registries before checking any bodies
2. Type-checking pass — walk the full AST with a scope-aware symbol table

**Type inference**
- All binary operators: integer/float promotion, comparison operators resolve to `bool`
- All unary operators
- `CastExpr` — result type is the cast target type
- `CallExpr` — return type looked up from function registry
- `MemberExpr` — field type looked up from struct registry (field name validated)
- `LiteralExpr` — typed by literal kind
- `IdentExpr` — looked up in symbol table

**Validation**
- Undefined variable references
- Undefined function calls
- Function call argument count mismatch
- Return type mismatch against declared function return type
- Struct field access on non-struct types
- Unknown struct field names
- `struct:` prefix normalization — internal struct type names are stored and matched with the `struct:` prefix consistently

**Scope management**
- Lexical scopes pushed/popped on block entry/exit
- Parameters inserted into function-body scope before checking body

### Known Gaps
- Source line/column numbers in error messages always report `0:0` — AST nodes do not carry position info yet
- Filename is hardcoded rather than taken from the CLI argument
- No implicit numeric promotion warnings (e.g. `int64` assigned to `int32` without cast)

### Key Files
- `sema/type_checker.h`
- `sema/type_checker.cpp`

---

## Phase 5 — Structs, Interfaces, Templates

**Status: NEXT**

**Goal:** First-class composite types with method dispatch and generic instantiation, sufficient to compile the INE QR decoder target types.

**Deliverable:** `QRPair`, `IneResult`, and `NoSoKeys` structs from the INE target compile and produce correct LLVM IR, including field access, fixed-size array fields, and method calls.

### What Exists
- `StructDecl` AST node with field list and method list (parser complete)
- Struct registry in type checker — struct types resolved, field access type-checked
- Method parsing — methods stored under struct in registry
- `*T` / `T*` pointer type parsing and type-checker representation

### What Needs to Be Built

**Struct codegen**
- `llvm::StructType::create` for each `StructDecl`
- `alloca` of struct type for local variables
- GEP (GetElementPtr) for field access in `MemberExpr`
- Fixed-size array fields: `uint8[858] left;` → `[858 x i8]` member type
- Struct initialization syntax — either C99-style designated initializers or positional

**Method calls**
- Method call syntax already parsed as `MemberExpr` + `CallExpr`
- Codegen must pass implicit `self` pointer as first argument

**Interface dispatch**
- Go-style implicit satisfaction: if a struct implements all method signatures of an interface, it satisfies it
- Dispatch via fat pointer: `(data_ptr, vtable_ptr)` pair
- `vtable` as a constant `llvm::StructType` of function pointers, one per interface method

**Templates**
- Monomorphic instantiation — each unique `T` argument generates a distinct `llvm::StructType` and set of functions
- Name-mangled to `StructName_T` in IR
- No partial specialization

**Parser additions**
- Struct literal initialization: `Point { x: 1, y: 2 }` or `Point { 1.0, 2.0 }`
- `switch`/`case` statement (deferred from Phase 2)

### Known Gaps to Address
- `BreakStmt` in codegen (needed for `switch`/`case`)
- `IndexExpr` codegen (needed for fixed-size array field access)

### Key Files (current + new)
- `ast/ast.h` — `StructDecl`, `MemberExpr`, `IndexExpr` already defined
- `sema/type_checker.cpp` — struct registry already present
- `codegen/codegen.cpp` — `visitStructDecl`, `visitMemberExpr`, `visitIndexExpr` need implementation
- `parser/parser.cpp` — struct literal syntax to add

---

## Phase 6 — Heap Memory + Strings

**Status: PLANNED**

**Goal:** Manual heap allocation and a mutable `String` type.

**Deliverable:** `alloc(T, N)` / `free(ptr)` lower to `malloc` / `free` calls; pointer arithmetic works; `String` provides `append`, `len`, `cstr`.

### Planned
- `alloc(T, N)` → `call i8* @malloc(i64 N * sizeof(T))` + bitcast to `T*`
- `free(ptr)` → `call void @free(i8* bitcast ptr)`
- Pointer arithmetic: `ptr + i` emits GEP, `ptr[i]` dereferences
- `String` as a stdlib struct: `{ i8* buf, i64 len, i64 cap }`

### Key Files (to create)
- `stdlib/string.esk`
- `codegen/codegen.cpp` — `alloc`/`free` builtin handling

---

## Phase 7 — Result\<T,E\> + Stdlib

**Status: PLANNED**

**Goal:** Error propagation without exceptions; core standard library modules.

**Deliverable:** `Result<T,E>` usable for function return types; `List<T>`, `math`, `io`, `mem` modules available.

### Planned
- `Result<T,E>` as a tagged union struct in stdlib; `Ok(v)` / `Err(e)` constructors
- `List<T>` — growable array backed by `alloc`/`free`
- `math` — `sqrt`, `abs`, `min`, `max`, `pow` (extern to libm)
- `io` — `read_file`, `write_file` (extern to libc)
- `mem` — `memcpy`, `memset`, `memmove` (extern to libc)

### Key Files (to create)
- `stdlib/result.esk`
- `stdlib/list.esk`
- `stdlib/math.esk`
- `stdlib/io.esk`
- `stdlib/mem.esk`

---

## Phase 8 — Lambdas + Closures

**Status: PLANNED (v0.3)**

**Goal:** Functions as first-class values with capture-by-value closures.

### Planned
- Lambda syntax: `fn(int x) { x * 2 }`
- Closure captures: synthesize a capture struct, pass as implicit first argument
- Function pointers: `int(*)(int, int)` type syntax
- Higher-order stdlib functions: `map`, `filter`, `fold` on `List<T>`

---

## Phase 9 — Threads

**Status: PLANNED (v0.2)**

**Goal:** Native POSIX thread support.

### Planned
- `spawn(fn() { ... })` → `pthread_create`
- `thread.join()` → `pthread_join`
- `Mutex` stdlib struct wrapping `pthread_mutex_t`
- Link with `-lpthread`

---

## Phase 10 — Exceptions

**Status: PLANNED (v1.0)**

**Goal:** C++-compatible exception handling via LLVM `invoke`/`landingpad`.

### Planned
- `try` / `catch (ErrorType e)` / `finally`
- `throw expr`
- LLVM EH personality function
- Base `Exception` struct in stdlib

---

## Phase 11 — Async/Await

**Status: PLANNED (v2.0)**

**Goal:** Coroutine-based async using LLVM coroutine intrinsics.

### Planned
- `async fn` declaration keyword
- `await expr` suspension point
- `Promise<T>` return type
- Event loop runtime in stdlib
- Interop with Phase 9 threads

---

## v0.1 Readiness — INE QR Decoder Target

The INE decoder requires the following. Phase column refers to when the feature lands.

| Feature | Required by | Phase | Status |
|---------|-------------|-------|--------|
| `extern` C ABI (OpenSSL, zxing-cpp) | Foreign calls | 3 | DONE |
| Integer arithmetic + comparisons | Bit manipulation | 3 | DONE |
| `if`/`else`, `for`, `while` | Control flow | 3 | DONE |
| Typed function calls | Decoder pipeline | 3 | DONE |
| Type checking — functions + vars | Correctness | 4 | DONE |
| Struct field access (`a.b`) | `QRPair`, `IneResult` | 5 | BLOCKING |
| Fixed-size array fields (`uint8[858]`) | Raw buffer types | 5 | BLOCKING |
| Struct method calls | `IneResult.decode()` | 5 | BLOCKING |
| `alloc(T, N)` / `free(ptr)` | Heap buffers | 6 | BLOCKING |
| `*uint8` pointer arithmetic | Buffer traversal | 6 | BLOCKING |
| `Result<T,E>` | Error propagation | 7 | BLOCKING |
| `String` append / cstr | Output formatting | 6/7 | BLOCKING |

Minimum Phase 5 + Phase 6 completion unblocks the first end-to-end compile of the INE decoder. Phase 7 (`Result<T,E>`) is required for idiomatic error handling but the decoder can be prototyped with raw return codes first.
