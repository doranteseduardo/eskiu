# Changelog

All notable changes to Eskiu are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow `MAJOR.MINOR.PATCH-stage` (e.g. `0.0.9-alpha`).

---

## [0.2.0] — Unreleased (in development on `develop`)

Backend-services phase. v0.1.0 is frozen at its tag; this is the in-progress next release.

### Language
- **`alloc`/`free` are no longer keywords** — heap allocation moved to the `<mem>` stdlib as the generic functions `alloc<T>(n)` and `free(p)`. Under `--freestanding` they target `esk_alloc`/`esk_free` via the predefined `__ESKIU_FREESTANDING__` macro.
- **`const`** — immutable typed bindings, usable as array sizes; field/element mutation of a `const` value is rejected.
- **`alloc_with(&allocator, T, n)`** — explicit-allocator built-in.
- **`volatile let`** — qualifier-first form (like `const int`), in local and top-level declarations.
- Multi-argument `fn` types (`fn(A,B)->R`); function pointers usable as values, parameters, return types, and struct fields, and capturable in closures.
- **`intrinsic`** — function qualifier for compiler-lowered prototypes (distinct from `extern`): a call lowers to inline IR, not a call to a C symbol, and emits no `declare`. Gated on import, so the names stay un-reserved otherwise.
- **`escaping` + `free_closure`** — closures now escape correctly. A non-escaping closure (only called / passed to a non-`escaping` parameter) keeps its environment on the stack (zero cost); an escaping one (returned, stored, or passed to an `escaping` parameter) gets a heap environment that survives. The `escaping` parameter qualifier marks closure-retaining functions; using a non-`escaping` closure parameter beyond a direct call is a compile error. `free_closure(f)` releases an escaping closure's heap environment. (Previously, a closure that outlived its creating function read a dangling stack environment.)
- Fixed: a cast to a type imported from another file (e.g. `(FutureHdr*)p`) was misparsed; imported type names are now visible for cast detection.
- **`async` / `await`** — `async T f(...)` whose call yields `*Future<T>`, and the `await E` expression (`E: *Future<T>` → `T`), legal only inside an async fn. A pre-codegen pass lowers an async function to a resumable state machine (frame struct + resume + constructor). Single and multiple awaits run end-to-end (fast path and suspend-over-reactor), including `return await` and `async void`. Cancellation works, control flow around await (if/while/for) is supported, and the runtime is leak-free (verified with `leaks`). for-in/switch around await still error clearly.

### Standard library
- **`<alloc>`** — `Bump`, `Arena`, `Pool`, `FirstFit` allocators over caller-provided buffers (libc-free; work under `--freestanding`).
- **`<time>`** — `time_now_ms`, `time_now_s`, `time_monotonic_ms`, `sleep_ms`.
- **`<env>`** — `env_get`, `env_has`, `env_get_or`, `env_get_int`.
- **`<base64>`** — RFC 4648 `base64_encode` / `base64_decode` over buffers.
- **`<json>`** — JSON builder (`Json`) plus a recursive-descent parser (`json_parse` → `JsonValue`).
- **`<threading>`** — `Mutex`, `Cond`, `Sem` over pthread.
- **`<http>`** — HTTP/1.1 request parser, response builder, and a threaded worker pool (`http_serve`).
- **`<string>`** — `starts_with`, `ends_with`, `trim`, `split` (List + streaming token iterator).
- **`<path>`** — `path_join`, `path_basename`, `path_dirname`, `path_extension`, `path_is_absolute`.
- **`<eventloop>`** — readiness reactor over kqueue (macOS) / epoll (Linux): `el_new`, `el_add_read`, `el_del`, `el_run`, `el_stop`, `el_free`. Callbacks are `fn(EventLoop*, int)->void`. Foundation for async I/O and the HTTP stack.
- **`<atomic>`** — `atomic_load`, `atomic_store`, `atomic_swap`, `atomic_cas` over an `int` cell, lowering to LLVM atomics (acquire / release / acq_rel). Foundation for the async runtime's lock-free `Future` state handshake.
- **`<executor>` / `<net_async>`** — async runtime foundation: an `Executor` (event loop + thread-safe ready-queue + self-pipe wakeup, so wakers always resume on the home thread) and leaf futures (`net_read_async`, `net_accept_async`) over `<eventloop>`. The async/await de-risk gate validated the runtime model end-to-end (reactor read, cancel, cross-thread resume).
- **`<http_async>`** — a non-blocking HTTP/1.1 server over `<eventloop>`, written as a single async accept-loop function (awaits accept + read; serves connections sequentially). The Phase-2 async-HTTP goal, end to end.

### Compiler correctness (consistency audit)
- Unsigned integer types use unsigned div/rem/shift/compare; 64-bit integer literals no longer truncate; mixed-width ops sign/zero-extend by signedness; variadic call args get the C default-argument promotions.
- Member access on a struct-valued temporary; `(Type)`/`(Type*)`/alias/enum casts; type alias as a local pointer/array; `List<StructType>` through helper functions; `T*` (trailing-star) pointer deref; nested template close `>>`; closures no longer capture module globals by value.

---

## [0.1.0]

Eskiu is a systems language built to replace the C + Go + C++ + Python stack that compute-intensive backend services typically require. C-style syntax, structural interfaces, monomorphic templates, and explicit memory — compiled to native via LLVM with no garbage collector and no runtime.

This is the first release. It includes everything needed to write real backend services and bare-metal systems code, validated against a cryptographic pipeline running at 74 ms on arm64 — 2.5× faster than the reference C implementation — and a bare-metal ARM64 kernel booting in QEMU without libc.

### Closures and threads

- `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`; variables from the enclosing scope are captured by value automatically
- `thread_create(fn()->void)` and `thread_join(*void)` are language keywords; the fat pointer maps directly to pthread's `(start_routine, arg)` — no trampoline
- Link with `-lpthread` on Linux

```eskiu
int base = 10;
let add: fn(int)->int = int(int x) { return x + base; };  // captures base

*void t = thread_create(void() { printf("hello\n"); });
thread_join(t);
```

### Exception handling

- `try { } catch (T name) { } finally { }` with multiple catch clauses
- `throw expr` — any Eskiu value
- LLVM `invoke`/`landingpad` with `__gxx_personality_v0`; unhandled exceptions re-thrown via `resume`
- Link with `-lc++` (macOS) or `-lstdc++` (Linux)

```eskiu
try {
    throw "division by zero";
} catch (string e) {
    printf("caught: %s\n", e);
} finally {
    printf("cleanup\n");
}
```

### Language completeness

- `sizeof(T)` — compile-time `int64` constant for any type including structs
- `union` — all fields share offset 0; size = sizeof(largest field)
- Typed pointer arithmetic — `p: *int; p + 1` advances 4 bytes, not 1
- `*ptr = value` through pointer parameters works correctly

### Forward declarations and declaration order

- Declaration order no longer matters: a function may call any function defined later, and mutual recursion works
- Body-less forward declarations (`int is_odd(int n);`) parse and are honoured
- Codegen declares every function prototype in a pre-pass before emitting any body, so call-before-define resolves cleanly instead of failing

### Compound assignment

- Bitwise compound assignment operators: `&=`, `|=`, `^=`, `<<=`, `>>=` (alongside the existing `+= -= *= /= %=`)

### Template ergonomics

- Template struct literals: `Pair<int, float> { first: 7, second: 3.14 }` (named and positional)
- Type-argument inference: type parameters are inferred from argument types by structural unification — both when a parameter is the bare type parameter (`max(3, 5)`) and when it appears inside a composite type (`List_get(&nums, i)` infers `T` from the `List<T>*` parameter). Explicit `max<int>(3, 5)` still works
- Mixed-width literal arguments are coerced to the substituted parameter type at the call site (e.g. a `double` literal passed where `T = float`)

### Iteration, error propagation, and richer strings

- **`for`-`in` loops**: `for (x in iterable)` iterates over fixed-size arrays (`T[N]`) and over List-like structs (any struct with `int size` and a `data` pointer, including `List<T>`). The loop variable is a per-iteration copy; `break`/`continue` behave as in a counted loop (it desugars to one, so `continue` still advances the index)
- **`?` error-propagation operator**: postfix `expr?` on a `Result<T, E>` returns the result early if it is `Err`, otherwise evaluates to the unwrapped `T`. Permitted only inside a function that returns the same `Result` type; rejected with a clear diagnostic otherwise
- **Richer mutable `String`**: added `String_push`, `String_char_at`, `String_set`, `String_clear`, `String_index_of`, `String_eq`, `String_eq_cstr`, `String_reverse`, `String_substring`, `String_from_int`, and `String_to_int` to `stdlib/string.esk`, all maintaining the owned, NUL-terminated buffer invariant

### Compiler robustness and fixes

- The parser now reports declaration errors and returns a failure instead of silently dropping unparseable declarations and reporting success; `--test-typechecker` exits non-zero on type errors
- Fixed: writing a `float` member of a `union` stored a full `double` without truncating, so reads returned garbage
- Added a regression test suite (`tests/run.sh`) with exact-output and expected-error checks covering arithmetic, control flow, recursion, pointers, strings, structs, interfaces, templates, closures, lambdas, exceptions, forward declarations, and compound assignment

### One-step linking

- `eskiuc file.esk -o prog` now **compiles and links** in one step, producing a runnable executable — `eskiuc` invokes the system C toolchain (`$CC`, then `cc`/`clang`/`gcc`), the same way `rustc` and `clang` do internally
- `-l<lib>`, `-L<path>`, and `--link-arg=<arg>` are forwarded to the linker (e.g. `eskiuc app.esk -o app -lpthread`)
- Object-only when the `-o` output ends in `.o`, when `-c` is passed, or under `--freestanding` (bare-metal links itself); a C toolchain is the only build dependency besides LLVM

### Enums, type aliases, bitfields, and the preprocessor

- **Enums**: `enum Color { Red, Green = 5, Blue }` — members are `int` constants (implicit or explicit values), the enum type maps to `i32`, and they work in `switch`, comparisons, and as parameter types
- **Type aliases**: `type u8 = uint8;` — a name for any existing type, including pointers and templates; resolved to the underlying type everywhere
- **Bitfields**: `struct F { uint32 a : 1; uint32 b : 3; }` — packed into storage words with masked read-modify-write access; signed fields sign-extend; works with struct literals
- **Preprocessor**: object-like and function-like `#define`/`#undef` (with recursive expansion and backslash-continued **multi-line** macro bodies) plus `#ifdef`/`#ifndef`/`#else`/`#endif` conditional compilation, run as a text pass before lexing; the macro table is shared across `import`/multi-file builds, and source line numbers are preserved
- **Packed structs**: the `packed struct` qualifier removes inter-field padding (back-to-back layout) for matching on-the-wire/on-disk formats and C `__attribute__((packed))` structs. `#pragma pack(push, N)` / `pack(pop)` is honoured for C source compatibility (maintains an alignment stack; `pack(1)` packs subsequent structs); both set the same flag and compose with bitfields and `sizeof`

### Tooling

- **Multi-file compilation**: `eskiuc a.esk b.esk -o prog` compiles several files together, merging their declarations (order-independent thanks to the prototype pre-pass)
- **`-Wall`**: lint-style warnings — unused variables, parameters, and functions, plus assignment used as a condition (`if (x = 0)`); off by default

### Standard library

- `import <name>` — angle-bracket imports resolve stdlib modules from the installation; `import "path"` for local files
- `<fs>` — file I/O: `fs_open`, `fs_close`, `fs_read`, `fs_write`, `fs_seek`, `fs_tell`, `fs_size`, `fs_read_all`, `fs_write_all`, `fs_eof`, `fs_error`
- `<net>` — TCP sockets over the POSIX BSD socket API: `net_tcp_listen`, `net_accept`, `net_tcp_connect`, `net_send`/`net_recv`/`net_send_str`, `net_close`, plus the raw `extern`s and a portable `sockaddr_in`. Pure stdlib — sockets need no compiler support beyond the C FFI and `packed struct`. Includes `examples/http_server.esk` (a working HTTP/1.1 server) and `examples/tcp_echo_server.esk`

### Networking-enabling language features

- **Function-as-value**: a top-level function used as a value (not called) decays to a `fn(T,...)->R` — passable to `thread_create` and callbacks without wrapping it in a lambda. The compiler synthesizes an adapter so the function fits the `{fn_ptr, env_ptr}` closure ABI
- **Predefined OS macros**: the compiler predefines `__APPLE__` (macOS) or `__linux__` (Linux), so stdlib and user code can `#ifdef` per platform — used by `<net>` to pick the correct `sockaddr_in` layout

### Release

- GitHub Actions workflow builds static binaries for macOS arm64 and Linux x86-64 on every `v*` tag
- Tarball: `bin/eskiuc` + `lib/eskiu/stdlib/`

---

## Systems milestone

Bare-metal ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) and prints to serial without libc or a C runtime.

- Inline asm UART driver, bump allocator in `--freestanding` mode
- Cross-compiled with `--target aarch64-unknown-none-elf`
- `volatile` MMIO, `asm(...)` with GCC-compatible constraints

## [0.0.14-alpha]

### Added

**Inline assembly**
- `asm("cli");` — simple form; passes the string verbatim to the assembler with no inputs, outputs, or clobbers
- `asm("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");` — extended form with GCC-compatible constraint syntax; supports input operands, output operands, and clobber lists
- Lowers to LLVM inline asm nodes; `"memory"` clobber emits a compiler barrier

**Freestanding mode**
- `--freestanding` CLI flag — redirects `alloc` to call `esk_alloc` and `free` to call `esk_free` instead of the libc `malloc`/`free`
- User provides both symbols in their kernel or bare-metal runtime; the compiler emits `declare` stubs and the linker resolves them

**`volatile` qualifier**
- `volatile let reg: *uint8 = (uint8*) ADDR;` — marks all LLVM loads and stores through the pointer as `volatile`, preventing the optimiser from caching, reordering, or eliminating MMIO accesses

**Cross-compilation**
- `--target TRIPLE` CLI flag — sets the LLVM target triple for the output object file
- Both AArch64 and X86 LLVM backends are included in the Eskiu build
- `eskiuc file.esk --target x86_64-pc-linux-gnu` produces an ELF x86-64 object file

**v0.1 prerequisites complete**
- All compiler prerequisites for the kernel-on-QEMU milestone are implemented and tested

## [0.0.13-alpha]

### Fixed
- **Negative literals** — `-1`, `-3.14` etc. now parse as negative literal values directly, not as unary minus applied to a positive literal. Global variable initialisers with negative values (e.g. `int x = -1;`) previously compiled to 0; this is now correct. `evaluateConstantExpr` folds unary minus on numeric constants as a fallback.

---

## [0.0.12-alpha]

### Added

**Lambdas and function pointer types**
- `fn(T,...)->R` function pointer type syntax — first-class function types usable in variable declarations, struct fields, and parameter lists
- Anonymous function expressions: `int(int x) { return x * 2; }` — C-like syntax producing a function pointer value
- Lambda variables: `let double_it: fn(int)->int = int(int x) { return x * 2; };`
- Higher-order functions: lambdas can be passed as arguments to functions expecting `fn(...)` parameters
- `LambdaExpr` AST node; full visitor chain (parser, type checker, codegen, AST printer)
- Codegen: each lambda expression is lowered to a uniquely-named private `llvm::Function`; the expression value is the function pointer

**VS Code extension — real-time diagnostics, hover, and go-to-definition**
- `--hover-at LINE:COL` CLI flag — prints the inferred Eskiu type of the expression at the given source position; consumed by the VS Code extension for hover tooltips
- `--definition-at LINE:COL` CLI flag — prints the `file:line:col` of the definition of the symbol at the given position; consumed for go-to-definition
- VS Code extension (`editor/vscode/`) upgraded from syntax-only to a full language client: spawns `eskiuc --test-typechecker` on save for real-time error squiggles; hover provider calls `--hover-at`; definition provider calls `--definition-at`
- TextMate grammar already present from v0.0.11 provides syntax highlighting

**switch/case type checking**
- The type checker now validates that each `case` value is compatible with the `switch` subject expression type; mismatched case types are reported as errors at the `case` token position

## [0.0.11-alpha]

### Added

**Decoder rewritten in Eskiu (no C pipeline code)**
- `ine_decoder/crypto.esk` (541 lines) — AES-256-CBC + RSA-8192 pipeline, hex/base64 decoders, PKCS#1 stripper, 6-bit decoder, WebP reconstruction, `run_no_so_pipeline()` — all in Eskiu calling OpenSSL via `extern`
- `ine_decoder/output.esk` (186 lines) — Spanish character table, field splitter, growable JSON buffer, `decode_to_buffers()` — pure Eskiu
- `ine_decoder/crypto.c` and `output_decode.c` removed — replaced entirely by Eskiu
- Only C remaining: `qr_extract.c` shim (12 lines) + `qr_extract_impl.cpp` (CoreGraphics + zxing-cpp)
- Runtime: **80ms** on arm64, identical output to reference

**String literal adjacent concatenation**
- `"abc" "def"` on consecutive lines (or the same line) are now concatenated into a single string at parse time — enables readable multi-line constant definitions

### Fixed (compiler bugs found during Eskiu crypto port)

- **`ptr - ptr` → `int64`**: pointer subtraction for computing byte offsets in buffers
- **Integer widening in arithmetic** (`+ - * /`): `i8 - i32` now ZExts the narrower operand before emitting the instruction
- **`string[i]` → `char`**: indexing a `string` typed value now returns the correct `char` (i8) element instead of computing type `"strin"`
- **Assignment store coercion**: `arr[i] = val` where the array element type is wider than `val` now ZExts/truncates to match (e.g. `int64[0] = 0` was storing i32 into an i64 slot)
- **Return value coercion**: functions that return `int64` but the expression evaluates to `i32` now automatically extend the return value
- **GlobalVariable initializer coercion**: `uint8 X = 0x52` was creating a global with mismatched i32/i8 initializer — now casts to the declared type
- **`validateStructType` multi-level pointers**: `**char` was triggering "undefined struct '*char'" — now strips all pointer levels before checking the base type

---

## [0.0.10-alpha]

### Added

**Global variables**
- `VarDecl` at module scope now emits `llvm::GlobalVariable` instead of `alloca`
- Constant initializers folded at compile time: `int`, `float`, `double`, `bool`, `char`, `string`, `null`
- Non-constant initializers zero-initialize the global (assign in `main()` for complex expressions)
- `globalVarTypes` map tracks Eskiu type strings for globals (complement to function-scoped `varTypeStack`)
- `evaluateConstantExpr()` — folds literal expressions to `llvm::Constant*`
- `visit(IdentExpr*)` now loads from `llvm::GlobalVariable` as well as `AllocaInst`
- `IMAGE_PATH`, `OUT_JSON`, `OUT_WEBP` in `ine_decoder/main.esk` moved to module scope

**sret (large struct return)**
- `needsSret(type)` — returns true for aggregates > 16 bytes (arm64 register limit)
- `visit(FunctionDecl*)` rewrites large-return functions: prepends hidden `ptr sret.ptr` parameter, changes return type to `void`, tracks struct type in `funcSretTypes`
- `visit(ReturnStmt*)` stores result to `currentSretParam` and emits `ret void` for sret functions
- Call sites (regular + template): alloca sret buffer, prepend as arg 0, call, load result
- `currentSretParam` saved/restored across template instantiation

**Integer argument widening at call sites**
- Automatically `SExt`/`Trunc` integer arguments to match function parameter widths
- Fixes `i32 1712` passed to `int64 param` (was LLVM verification error)

### Fixed
- `*int` vs `size_t *` in `extern.esk`: `run_no_so_pipeline` and `decode_to_buffers` now use `int64` for length params to match C's `size_t` (was writing 8 bytes to a 4-byte stack slot → heap corruption)
- ine_decoder `pipeline.esk`: stage signatures updated to `int64` for all size parameters

### Planned
- `argv`/`argc` support — programs can accept CLI arguments natively

---

## [0.0.9-alpha]

### Added
- **`ine_decoder/` — INE QR decoder port** — full pipeline running at **74.4 ms** total
  (QR: 71.7 ms + crypto: 2.8 ms + output decode: <1 ms) vs. 188.9 ms reference C and 3–5 s original target; 2.5× faster than hand-written C
  - `types.esk` — `QRPair`, `NoSoKeys`, `IneResult`, `IneFields` structs
  - `extern.esk` — libc + OpenSSL EVP (AES-256-CBC / RSA-8192) + `ine_qr_extract()` declarations
  - `stage1_qr.esk` — QR extraction wrapper
  - `stage2_crypto.esk` — 3-round AES-256-CBC + RSA-8192 via OpenSSL
  - `stage3_output.esk` — pipe-delimited plaintext → JSON + WebP extraction
  - `main.esk` — orchestration, timing, output
  - `qr_extract.c` / `qr_extract_impl.cpp` — C/C++ shim using CoreGraphics + zxing-cpp 3.x
  - `Makefile`, `README.md`

### Fixed
- **Integer width mismatch in comparisons** — `uint8 == int` (e.g. `plaintext[i] == 124`) crashed LLVM with "Both operands to ICmp instruction are not of the same type"; now `ZExt`s the narrower operand to match the wider before emitting any of the six comparison operators
- **Mixed int/float arithmetic** — `i64 * double` (e.g. timing calculation `(t1 - t0) * 1000.0`) generated invalid IR; now detects int/float type mismatch in `+`, `-`, `*`, `/` and promotes the integer operand with `SIToFP` before emitting the floating-point instruction

---

## [0.0.8-alpha]

### Added
- **`import "file.esk"`** — multi-file support with paths resolved relative to the importing file's directory; recursive imports with deduplication (a file imported more than once is parsed only once); `Parser.basedir` + `importedFiles` set propagated to sub-parsers
- **Interface vtable dispatch** — `interface I { void method(); }` generates `%I_vtable = type { ptr, ... }` and `%I_fat = type { ptr data, ptr vtable }` LLVM types; structs are auto-boxed at call sites via `boxAsInterface()`; method dispatch loads the vtable pointer from the fat pointer and calls indirectly; `getExprEskiuType` extended to handle `UnaryExpr("&", ...)` and `UnaryExpr("*", ...)` for boxing detection
- **`String.append()` and `String.concat()`** — added to `stdlib/string.esk` using `memcpy` + pointer arithmetic

### Fixed
- Pointer comparison (`p == null`, `ptr1 == ptr2`) incorrectly used `FCmpOEQ` (float equality); now uses `ICmpEQ`; all six comparison operators updated to check `isFloatingPointTy()` first
- `i1 → i32` widening used `SExt` (sign-extends `true` → `-1`); now uses `ZExt` for `i1` operands so comparisons correctly store `0` or `1`

---

## [0.0.7-alpha]

### Added
- **Bitwise operators**
  - Binary: `&`, `|`, `^`, `<<`, `>>` — new precedence levels in parser (bitwiseOr → bitwiseXor → bitwiseAnd → equality → shift → comparison → additive)
  - Unary: `~` (bitwise NOT) — added to unary operator list in parser and `inferUnaryExprType`
  - Codegen: `CreateAnd`, `CreateOr`, `CreateXor`, `CreateShl`, `CreateAShr`
- **Hex literals** — `0xFF`, `0x0F`; lexer reads hex digits after `0x`/`0X` prefix; `stoll(..., 0)` for auto-base detection in codegen
- **Compound assignments** — `+=`, `-=`, `*=`, `/=`, `%=`; new tokens in lexer; desugared in `parseAssignment()` to `x = x op y`
- **`continue` statement** — `ContinueStmt` AST node; full visitor chain; `continueTarget` saved/restored around `WhileStmt` and `ForStmt` bodies (points to loop condition and step block respectively)
- **For-loop with declaration init** — `for (int i = 0; i < n; i += 1)` — parser wraps the declaration in a `BlockStmt`; type checker processes init items directly in `ForStmt` scope, avoiding the inner-scope pop that previously made `i` undefined in the condition, step, and body
- **Pointer arithmetic** — `ptr + n` → `GEP(i8, ptr, n)`, `ptr - n` → `GEP(i8, ptr, -n)` in `visit(BinaryExpr*)`

### Fixed
- `&` address-of returned a loaded value instead of the alloca pointer, breaking all pointer-argument call patterns
- Unary `-` for floats used integer `CreateNeg`; now uses `CreateFNeg` for floating-point operands
- `!` logical NOT on integers used bitwise `CreateNot` (incorrect for `i32`); now uses `CreateICmpEQ(x, 0)` for non-`i1` types
- `~` unary NOT was missing from the parser's unary operator list, causing a parse failure for `~0` and similar expressions
- `inferUnaryExprType` did not handle `~`; reported "invalid operand" for bitwise NOT on integers
- `inferBinaryExprType` did not handle bitwise or shift operators; reported "invalid operands" for `a & b`, `a << n`, etc.
- For-loop init declarations were scoped to an inner `BlockStmt`, making the loop variable undefined in the loop condition and body

---

## [0.0.6-alpha]

### Added
- **Source locations in errors** — `ASTNode` now carries `line`/`col`; parser stamps all expression and statement nodes; errors now report `file.esk:line:col:` instead of `file.esk:0:0:`; filename taken from the CLI input path
- **`&` address-of operator** — correctly returns the lvalue pointer (alloca), enabling `fn(&localVar)` patterns and template method calls via `self` pointer
- **Template member access on pointer types** — trailing `*` stripped before struct field lookup; `self: List<int>*` now correctly resolves to `structFields["List_int"]` in both `visit(MemberExpr*)` and `getExprEskiuType()`
- **`stdlib/list.esk`** — `List<T>` with `List_init`, `List_push`, `List_get`, `List_len`, `List_free` as template functions; tested end-to-end
- **`stdlib/string.esk`** — `String` struct with `String_init`, `String_from`, `String_cstr`, `String_len`, `String_free`
- **Interface structural check** — `isValidAssignment` now verifies that a struct satisfies an interface's method signatures (vtable codegen deferred to 0.0.8)
- **`isPointerType` unified** — now detects both leading `*T` and trailing `T*` conventions

### Fixed
- `&` operator returned a loaded value instead of the address, breaking all pointer-argument patterns
- Template method parameter types with pointer suffix (e.g. `List<T>*`) were incorrectly mangled to `List_int_*` instead of `List_int*`
- `getExprEskiuType` for member chains through a pointer-to-template-struct now resolves correctly

---

## [0.0.5-alpha]

### Added
- **`switch`/`case`** — `SwitchStmt` AST node with `Case { value, stmts }` list; full visitor chain; parser handles `switch (expr) { case val: stmts break; default: stmts }` with fallthrough support; codegen emits LLVM `switch` instruction with `ConstantInt` case values; `break` branches to `switch.end` via existing `breakTarget` mechanism
- **Function templates** — `fn Name<T, E>(T x) -> RetType<T,E> { ... }`; `FunctionDecl.typeParams` field; `TemplateCallExpr` AST node parsed in `parsePostfix()`; lazy instantiation via `typeParamOverride` map that intercepts all type lookups during template body emission; context save/restore preserves insert point and `currentFunction` when instantiating inside another function's body; `substType` handles `Name<T,E>` nested substitution recursively
- **`InterfaceDecl`** — `interface Speakable { void speak(); }` fully parsed and stored in `interfaceDecls`; codegen generates no IR (vtable dispatch deferred)
- **Stdlib base** (`stdlib/`)
  - `result.esk` — `struct Result<T,E>` + `Ok<T,E>` / `Err<T,E>` template constructors
  - `math.esk` — `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` (extern to libm)
  - `io.esk` — `printf`, `fprintf`, `sprintf`, `scanf`, `puts`, `getchar`, `putchar`
  - `mem.esk` — `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `memchr`

### Fixed
- Parser: `int fn<T>(T x)` at top level was silently dropped; `parseDeclaration()` now detects `<` after a name as a function template
- Type checker: template function bodies were visited with unresolved type params; now guarded with `typeParams.empty()` check
- `substType`: did not substitute type params inside `Name<T,E>` template strings; now handles nested template types recursively

---

## [0.0.4-alpha]

### Added
- **Template struct declaration** — `struct Result<T, E> { ... }`; `StructDecl` gains `typeParams` field; template declarations are stored in a separate registry and not emitted to LLVM until first use
- **Template type references** — `Result<int, string>` parsed in `parseType()` via `IDENT < TYPE, ... >` lookahead; stored as `"Result<int,string>"` in the AST
- **Lazy instantiation in type checker** — `normalizeType("Result<int,string>")` detects `<`, looks up the template, substitutes `T→int` / `E→string` in all field types, and registers `"Result_int_string"` as a concrete struct
- **Lazy instantiation in codegen** — `getTypeFromString("Result<int,string>")` calls `ensureTemplateInstantiated()` which creates `%Result_int_string = type { i32, i32, ptr }` on first use
- **Name mangling** — `Result<int,string>` → `Result_int_string` in LLVM IR
- **Type substitution** — `substType` handles `*T` → `*int`, `T*` → `int*`, `T[N]` → `int[N]`, and nested template types recursively
- **`varTypeStack` normalization** — `VarDecl` stores the mangled name so `MemberExpr` resolution finds instantiated struct fields correctly

### Fixed
- Parser: `int fn<T>(...)` at top level was silently dropped — `parseDeclaration()` now detects `<` after a function name as a template parameter list
- Type checker: template bodies were visited with unresolved type parameters — now guarded so only concrete instantiations are type-checked

---

## [0.0.3-alpha]

### Added
- **`alloc(T, N)`** — new `AllocExpr` AST node; emits `call @malloc(i64 N * sizeof(T))`; `sizeof(T)` resolved via `DataLayout` initialized at the start of `generateCode()`
- **`free(ptr)`** — parsed as a regular call; `free` pre-registered in the type checker as variadic; auto-declared in the LLVM module on first use — no explicit `extern` required
- **`getOrDeclareFunc()`** — codegen helper that lazily declares C runtime functions (`malloc`, `free`) into the module without requiring explicit `extern` declarations

### Fixed
- `validateStructType` did not strip the leading `*T` pointer prefix, causing false "undefined struct" errors on `*uint8`, `*Point`, etc.
- `inferBinaryExprType` did not handle the `=` operator for non-numeric types; assignment to pointer fields and struct fields now type-checks correctly
- `isValidAssignment` now allows any-pointer-to-any-pointer assignment for C interop

---

## [0.0.2-alpha]

### Added
- **Struct codegen** — `llvm::StructType::create` per `StructDecl`; `alloca %StructType` for locals; field read/write via `getelementptr`
- **Fixed-size array fields** — `uint8[858]` → `[858 x i8]`; parser now captures the array size (was previously discarded); `IndexExpr` codegen via GEP for both `T[N]` and `*T`
- **Struct literal initialization** — named (`Point { x: 1.5, y: 2.5 }`) and positional (`Point { 1.5, 2.5 }`); fills alloca directly with per-field type coercion
- **Method calls** — methods emitted as `StructName_methodName(ptr self, ...)` mangled functions; `p.method(args)` detects and prepends an implicit `self` pointer
- **`StructInitExpr` AST node** — full visitor chain: parser, type checker, codegen, AST printer
- **`BreakStmt` codegen** — `CreateBr(breakTarget)`; target saved/restored around loop bodies
- **`emitObjectFile()`** — native `.o` via LLVM `TargetMachine` + `legacy::PassManager`; full pipeline `eskiuc file.esk -o file.o` produces a linkable object file
- **Float arithmetic** — `+`, `-`, `*` now emit `fadd`/`fsub`/`fmul` for floating-point operands (previously always emitted integer instructions)

### Fixed
- Type checker: method bodies were not registered and type-checked in the first pass
- Type checker: `*T` (leading-pointer) was not auto-derefed on member access; now strips the leading `*` before struct field lookup
- Type checker: `isValidAssignment` normalizes both sides so `"Point" == "struct:Point"`
- Type checker: function parameter types were stored as parameter names (e.g. `"a"`) instead of type strings (e.g. `"int"`)
- Type checker: variadic functions incorrectly rejected call sites with more arguments than fixed params
- Codegen: `varTypeStack` now scoped alongside the symbol table for correct struct type resolution across nested scopes

---

## [0.0.1-alpha]

### Added
- **Phase 0 — Build system and CLI** — CMake build with LLVM 17+ integration; `--version` flag; `--test-lexer`, `--test-parser`, `--test-typechecker`, `--test-codegen` modes; `file:line:col` error reporting
- **Phase 1 — Lexer** — Complete tokenizer with line/col tracking; all Eskiu keywords (`int`, `float`, `uint8`–`uint64`, `int8`–`int64`, `struct`, `interface`, `enum`, `alloc`, `free`, `extern`, `thread`, `try`/`catch`/`finally`, and more); `COLON` token for type annotations
- **Phase 2 — Parser** — Recursive-descent parser producing a visitor-based AST; functions, variables (`let x: int = 5` and C-style `int x = 5` both accepted), structs with fields and methods, `extern` declarations, full control flow (`if`/`else`, `for`, `while`, `break`, `return`), expressions with correct precedence, cast expressions `(TYPE)expr`
- **Phase 3 — Codegen** — LLVM IRBuilder backend; arithmetic, comparison, and logical operators; `if`/`else`, `while`, `for`; function calls; integer and float literals; type coercion on initializers; correct lvalue/rvalue split (`evaluateLValue`) so assignments emit `store` to an `alloca` rather than to a value
- **Phase 4 — Type checker** — Scope-aware analysis; type inference for all binary and unary operators; struct field validation; function signature checking; `MemberExpr` member-type resolution; parameters registered before the validation pass
- **Types** — `uint8`/`uint16`/`uint32`/`uint64` and `int8`/`int16`/`int32`/`int64` as first-class types mapped to LLVM `i8`–`i64`; `bool` → `i1`; `char` → `i8`; `string` → `i8*`
- **Pointer types** — both leading `*T` and trailing `T*` syntax accepted throughout lexer, parser, and type checker
- **Examples** — `examples/hello.esk`, `examples/test_struct.esk`, `examples/test_struct_error.esk`

### Fixed
- Lexer: `COLON` token was not recognized, breaking `let`-style type annotations
- Type checker: function parameters were not registered before the body validation pass, causing false "undeclared identifier" errors

### Known limitations (resolved in later releases)
- Struct codegen not wired (Phase 5); `MemberExpr` type-checks but does not emit IR
- No heap allocation (`alloc`/`free`) — stack only (Phase 6)
- No interfaces or templates (Phase 5)
- No standard library or `Result<T,E>` (Phase 7)
- No lambdas, threads, or async (Phase 8+)
