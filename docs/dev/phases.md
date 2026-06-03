---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors. Supersedes `docs/PHASES.md`.

Last updated: 2026-06-02. All phases 0–7 and Phase 5.5 are COMPLETE. Language is v0.1 ready. Next: INE decoder port.

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
- `switch`/`case` now implemented (see Phase 5.5)

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

**Object file emission**
- `emitObjectFile(filename)` — initializes native target, creates `TargetMachine`, emits via `legacy::PassManager`; produces a linkable `.o` file
- Full pipeline: `eskiuc file.esk -o file.o` → `clang file.o -o file` → runnable binary

### Known Gaps
- Float literal assigned to `float` field emits `double` constant — store width mismatch (no impact on integer/uint types used by INE decoder target)
- `BreakStmt` inside `switch`/`case` works correctly via `breakTarget`
- `emitObjectFile` on Windows not tested

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
- ~~Source line/column always `0:0`~~ — fixed: `ASTNode` carries `line`/`col`, parser stamps all expression/statement nodes
- Filename is hardcoded rather than taken from the CLI argument
- No implicit numeric promotion warnings (e.g. `int64` assigned to `int32` without cast)

### Key Files
- `sema/type_checker.h`
- `sema/type_checker.cpp`

---

## Phase 5 — Structs, Interfaces, Templates

**Status: COMPLETE (core) — interfaces and templates deferred to Phase 5.5**

**Goal:** First-class composite types with method dispatch and initialization, sufficient to compile the INE QR decoder target types.

**Deliverable:** Structs with field access, fixed-size array fields, named/positional initialization, and method calls compile and produce correct native code.

### Implemented

**Struct codegen**
- `visit(StructDecl*)` — `llvm::StructType::create`; registered in `structTypes` and `structFields`
- `visit(MemberExpr*)` — `getelementptr inbounds` for field read; field index resolved by name
- `evaluateLValue(MemberExpr*)` — GEP for field write; enables `p.x = val`
- `visit(VarDecl*)` — `alloca %StructType`; `varTypeStack` scoped alongside symbol table

**Struct initialization**
- Named: `Point { x: 1.5, y: 2.5 }` — fills fields by name in any order
- Positional: `Point { 1.5, 2.5 }` — fills fields in declaration order
- `emitStructInitInto` fills dest alloca directly (no temporary for `VarDecl` init)
- Per-field type coercion (float↔double, int width truncation/extension)
- `StructInitExpr` AST node with full visitor chain: parser → type checker → codegen → printer

**Method calls**
- Methods emitted as `StructName_methodName(ptr self, ...)` mangled functions
- `p.method(args)` → `call @Point_method(ptr %p, args)` — implicit self pointer
- Type checker: methods registered in first pass; call sites validated including arg types
- `self` in method body is the pointer to the receiver struct

**Fixed-size array fields**
- `uint8[858]` → `[858 x i8]`; parser captures size (was discarded)
- `visit(IndexExpr*)` — GEP `[0, i]` for `T[N]`; offset GEP for `*T`
- `evaluateLValue(IndexExpr*)` — enables `arr[i] = val`

**Control flow**
- `visit(BreakStmt*)` — `CreateBr(breakTarget)`; target saved/restored around loops

**Object file emission**
- `emitObjectFile(filename)` — LLVM `TargetMachine` + `legacy::PassManager`
- `eskiuc file.esk -o file.o && clang file.o -o file` produces a running binary

**Bug fixes**
- `+`/`-`/`*` now emit `fadd`/`fsub`/`fmul` for floating-point (was always integer)
- `*T` leading pointer auto-derefed on member access
- `isValidAssignment` normalizes `"Point" == "struct:Point"`
- Param types stored as types, not names, in function signature registry
- Variadic functions accept ≥N fixed args

### Verified

```
%Point = type { float, float }

define float @Point_sum(ptr %self) {
  %x = getelementptr inbounds nuw %Point, ptr %self, i32 0, i32 0
  %0 = load float, ptr %x
  %y = getelementptr inbounds nuw %Point, ptr %self, i32 0, i32 1
  %1 = load float, ptr %y
  %2 = fadd float %0, %1
  ret float %2
}
```

`eskiuc file.esk -o file.o && clang file.o -o file && ./file` → correct output.

### Phase 5.5 — Monomorphic Templates

**Status: COMPLETE (struct templates) — interface dispatch and switch/case remain**

#### Implemented

- **Template declaration**: `struct Name<T, E>` — `StructDecl.typeParams` holds param names; stored in `templateDecls` registry, not emitted until first use
- **Template type parsing**: `Result<int, string>` in `parseType()` via `IDENT <` lookahead; stored as `"Result<int,string>"` in AST
- **Lazy instantiation — type checker**: `normalizeType` detects `<`, substitutes type params, registers concrete `StructInfo` under mangled name (e.g. `"Result_int_string"`)
- **Lazy instantiation — codegen**: `ensureTemplateInstantiated` creates `llvm::StructType` on first use; `getTypeFromString` triggers it
- **Name mangling**: `Result<int,string>` → `Result_int_string`; no angle brackets in LLVM IR
- **`substType`**: recursive substitution handles `*T`, `T*`, `T[N]`, nested templates

#### Verified

```eskiu
struct Result<T, E> { int ok; T value; E error; }

let r: Result<int, string>;
r.ok = 1; r.value = 42;
```

```llvm
%Result_int_string = type { i32, i32, ptr }
%r = alloca %Result_int_string
getelementptr inbounds nuw %Result_int_string, ptr %r, i32 0, i32 1
store i32 42, ptr %1
```

#### Also completed (function templates + switch)

- **Function templates**: `int fn<T>(T x)` — `FunctionDecl.typeParams`; `TemplateCallExpr` AST node; lazy instantiation via `typeParamOverride`; save/restore insert point during nested instantiation
- **`substType` extended** to substitute inside `Name<T,E>` template type strings
- **`switch`/`case`**: `SwitchStmt`; LLVM `switch` instruction; fallthrough; `break` via `breakTarget`
- **`InterfaceDecl`** parsed and stored; vtable generation deferred (not needed for v0.1)
- **Stdlib base**: `stdlib/result.esk`, `stdlib/math.esk`, `stdlib/io.esk`, `stdlib/mem.esk`

#### Phase 5.5 — COMPLETE

Interface vtable dispatch implemented:
- `%I_vtable = type { ptr, ... }` per interface, `%I_fat = type { ptr, ptr }` fat pointer
- Structural satisfaction check in type checker; auto-boxing at call sites in codegen
- Indirect dispatch: load vtable ptr → load fn ptr → call with data ptr as self
- `import "file.esk"` — multi-file support with relative path resolution and dedup

### Key Files
- `ast/ast.h` — `StructDecl.typeParams`, `StructInitExpr`, `AllocExpr`
- `parser/parser.cpp` — `parseStructInit()`, `parseStructDecl()` with `<T>`, `parseType()` with template refs, array size capture
- `sema/type_checker.cpp` — method registration, `visit(StructInitExpr*)`, `visit(AllocExpr*)`, template lazy instantiation in `normalizeType`
- `codegen/codegen.cpp` — `visit(StructDecl*)`, `visit(MemberExpr*)`, `visit(IndexExpr*)`, `visit(StructInitExpr*)`, `visit(AllocExpr*)`, `ensureTemplateInstantiated()`, `emitObjectFile()`

---

## Phase 6 — Heap Memory + Strings

**Status: COMPLETE (alloc/free) — mutable String deferred to Phase 7**

**Goal:** Manual heap allocation via `alloc`/`free`; pointer types usable for dynamic buffers.

**Deliverable:** `alloc(T, N)` / `free(ptr)` compile and run correctly; pointer fields on structs can hold heap-allocated data.

### Implemented

- **`alloc(T, N)`** — `AllocExpr` AST node; emits `call ptr @malloc(i64 N * sizeof(T))` where `sizeof(T)` is resolved from `DataLayout` at compile time; `malloc` auto-declared in module
- **`free(ptr)`** — parsed as regular `CallExpr`; `free` pre-registered in type checker (variadic, no strict arg checking) and auto-declared in module on first use
- **`getOrDeclareFunc()`** — codegen helper for lazy C runtime function declaration; eliminates need for explicit `extern malloc`/`extern free`
- **DataLayout early init** — `generateCode()` sets target triple + data layout before visiting the AST so `sizeof` queries are correct even in `--test-codegen` mode

### Verified

```eskiu
let buf: *uint8 = alloc(uint8, 1024);
buf[0] = 42;
free(buf);
```

```eskiu
struct IneResult { int ok; *uint8 json; int json_len; }
let r: IneResult;
r.json = alloc(uint8, 256);
r.json[0] = 123;
free(r.json);
```

Both compile, link, and produce correct output.

### Remaining (Phase 6.5 / Phase 7)
- `String` mutable struct (`{ *char data; int len; int cap }`) with `append`, `len`, `cstr` methods — deferred; depends on Phase 5.5 templates or a fixed non-generic implementation
- Pointer arithmetic: `ptr + i` as an expression (currently `ptr[i]` indexing works via GEP)

### Key Files
- `ast/ast.h` — `AllocExpr`
- `parser/parser.cpp` — `ALLOC`/`FREE` in `parsePrimary()`
- `sema/type_checker.cpp` — `visit(AllocExpr*)`, `free` pre-registration, pointer-ptr compat
- `codegen/codegen.cpp` — `visit(AllocExpr*)`, `getOrDeclareFunc()`, early DataLayout init

---

## Phase 7 — Result\<T,E\> + Stdlib

**Status: COMPLETE (core)**

**Goal:** Error propagation without exceptions; core standard library modules.

### Implemented

- **`stdlib/result.esk`** — `struct Result<T,E>` + `Ok<T,E>` / `Err<T,E>` template constructors
- **`stdlib/list.esk`** — `List<T>` with `List_init`, `List_push`, `List_get`, `List_len`, `List_free`; tested end-to-end
- **`stdlib/string.esk`** — `String` with `String_init`, `String_from`, `String_cstr`, `String_len`, `String_free`
- **`stdlib/math.esk`** — `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` (extern to libm)
- **`stdlib/io.esk`** — `printf`, `fprintf`, `sprintf`, `scanf`, `puts`, `getchar`, `putchar`
- **`stdlib/mem.esk`** — `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `memchr`

### Remaining
- `String.append()` — needs dynamic realloc
- Higher-order collection functions — Phase 8

### Key Files
- `stdlib/result.esk`, `stdlib/list.esk`, `stdlib/string.esk`
- `stdlib/math.esk`, `stdlib/io.esk`, `stdlib/mem.esk`

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
