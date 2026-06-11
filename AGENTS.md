# AGENTS.md

Guidelines for AI agents working on the Eskiu compiler.

---

## What this project is

Eskiu is a systems programming language compiler written in C++17 that emits LLVM IR and links against LLVM 17+. The pipeline is:

```
Source → Lexer → Parser → TypeChecker → CodeGen → LLVM IR → .o → (cc/clang/gcc) → executable

`eskiuc file.esk -o prog` emits the object to a temp file and links it into an
executable by invoking the system C driver (`$CC`, then `cc`/`clang`/`gcc`) —
see `linkExecutable` in `main.cpp`. A `.o` output, `-c`, or `--freestanding`
stops at the object file (no link). `-l`/`-L`/`--link-arg` pass through to the linker.
```

Every pass implements the `ASTVisitor` interface in `ast/ast.h`. When adding a new AST node you must update every visitor: `ASTPrinter`, `TypeChecker`, `CodeGen`.

## Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/eskiuc --version
```

On macOS, set `LLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm` if cmake cannot find LLVM.

## Test modes

```bash
./build/eskiuc file.esk --test-lexer            # print token stream
./build/eskiuc file.esk --test-parser           # print AST
./build/eskiuc file.esk --test-typechecker      # type check, report errors
./build/eskiuc file.esk --test-codegen          # print LLVM IR
./build/eskiuc file.esk --hover-at LINE:COL     # type at cursor position
./build/eskiuc file.esk --definition-at LINE:COL  # definition location
./build/eskiuc file.esk --target TRIPLE         # cross-compile
./build/eskiuc file.esk --freestanding          # no libc; predefines __ESKIU_FREESTANDING__ (<mem> targets esk_alloc/esk_free)
```

Use `examples/` and `tests/` as inputs. Add a `.esk` file for any feature you implement.

## Source layout

| Path | Responsibility |
|---|---|
| `lexer/` | Tokeniser. `Lexer::next_token()` only. |
| `parser/` | Recursive-descent. Returns `shared_ptr<Program>`. |
| `ast/ast.h` | All AST node types + `ASTVisitor` interface. |
| `ast/ast.cpp` | `accept()` definitions. |
| `ast/ast_printer.cpp` | Pretty-printer (`--test-parser`). |
| `sema/type_checker.cpp` | Type inference, scope, struct/interface/template registry. |
| `codegen/codegen.cpp` | LLVM IR via `IRBuilder`. |
| `main.cpp` | CLI entry point. |
| `stdlib/` | Eskiu stdlib modules (`result.esk`, `list.esk`, etc.). |
| `tests/` | Regression tests (`.esk` files). |
| `examples/` | Working demos. |
| `kernel/` | Bare-metal ARM64 kernel for QEMU (v0.1 milestone). |

## Current language status (v0.1.0)

All items below are implemented and tested end-to-end.

| Feature | Notes |
|---|---|
| Primitive types | int/8/16/32/64, uint, float, double, bool, char, string, void, `*T` |
| Structs + methods | `Struct_method(self, ...)`, named/positional initialisers |
| Interfaces | Structural typing, vtable fat-pointer dispatch |
| Templates | Monomorphic instantiation: `Result<int,string>` → `%Result_int_string`. Struct literals `Pair<int,float>{...}`; explicit `f<int>(...)` or inferred `f(3)` type args (inference only when a type param appears directly as a parameter type) |
| Enums | `enum Color { Red, Green = 5, Blue }` — members are int constants; the enum type maps to `i32`; usable in `switch`/comparisons |
| Type aliases | `type u8 = uint8;` — resolved to the underlying type in sema and codegen (`getTypeFromString`/`getExprEskiuType` expand aliases) |
| Bitfields | `struct F { uint32 a : 1; uint32 b : 3; }` — packed into storage words; masked load + read-modify-write store; signed fields sign-extend. Layout in `structLayout`; only structs with a bitfield change layout |
| Packed structs | `packed struct` or `#pragma pack(1)` → `StructDecl::isPacked` → `StructType::create(..., isPacked)`. `#pragma` survives `preprocess()`, lexes to a `PRAGMA` token, and the parser's pack stack (`currentPack`/`packStack`, `applyPragma`) tags structs declared under `pack(1)` |
| Preprocessor | object-like + function-like `#define`/`#undef` (recursive expansion, `\`-continued multi-line bodies) and `#ifdef`/`#ifndef`/`#else`/`#endif`. A text pass in the `Lexer` ctor (`preprocess()`); macro table is shared (passed into the ctor) so `#define`s cross `import`/multi-file; directives/skipped lines blank out to preserve line numbers. `#pragma` is passed through to the parser, not consumed |
| Forward declarations | Body-less `int f(int n);`; call-before-define and mutual recursion (codegen declares all prototypes before emitting bodies) |
| Compound assignment | `+= -= *= /= %=` and bitwise `&= \|= ^= <<= >>=` |
| Lambdas | `int(int x) { return x * 2; }` — anonymous functions |
| Closures | `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`; captures by value. **Escape analysis**: a non-escaping closure (only called / passed to a non-`escaping` param) keeps its env on the stack; an escaping one (returned, stored, or passed to an `escaping` param) gets a heap env. `LambdaExpr::escapes` (set in sema, default true) drives stack-vs-heap in codegen |
| `escaping` / `free_closure` | `escaping` is a parameter qualifier (Swift-style): it marks a param that *retains* the closure beyond the call. Sema enforces soundness — a non-`escaping` closure param used beyond a direct call is a compile error (`functionParamEscaping` + the `nonEscapingFnParams`/`escapedFnParams` walk in the type checker). `free_closure(f)` (a builtin, `FreeClosureExpr`) frees an escaping closure's heap env (slot 1 of the fat pointer); `free(null)` is a safe no-op for non-capturing closures |
| Function-as-value | A bare function name used as a value decays to a `fn(...)->R` via `makeFunctionPointer` (synthesizes a `__fnptr_<name>` env-ignoring thunk). `visit(CallExpr)` resolves direct named calls before `evaluateExpr` so calls don't decay |
| Predefined macros | `main.cpp` seeds the shared macro table with `__APPLE__`/`__linux__` (host OS) for `#ifdef` portability |
| `<net>` sockets | `stdlib/net.esk` — POSIX socket `extern`s + portable `packed sockaddr_in` (`#ifdef __APPLE__`) + `net_*` helpers. No compiler support needed beyond FFI |
| `thread_create` / `thread_join` | Language keywords; fat-pointer maps to `pthread_create(fn, env)` |
| `try` / `catch` / `finally` / `throw` | LLVM `invoke`/`landingpad` + `__gxx_personality_v0`; link `-lc++` |
| Inline assembly | `asm("cli")` simple; `asm("op" :: "r"(x) : "mem")` extended |
| `volatile` | `volatile let reg: *uint8 = addr;` — MMIO-safe |
| `const` | `const T x` / `const let x: T` — immutable; sema requires an initializer and rejects reassignment (`Symbol::isConst`/`isConstSymbol`). const ints resolve as array dimensions via `constInts` + `resolveArrayDim` (also accepts literals and enum members) |
| `alloc<T>` / `free` | **Stdlib, not keywords** — `import <mem>`. `alloc<T>(n)` is a generic fn wrapping `malloc`/`esk_alloc`; `free(*void)` wraps `free`/`esk_free`. `<mem>` picks the backend via `#ifdef __ESKIU_FREESTANDING__`. `<mem>` is the **only** stdlib file that names the libc symbol; everything else uses `alloc<T>`/`free` |
| `alloc_with` / `<alloc>` | `alloc_with(&a, T, n)` (`AllocWithExpr`) is still a built-in keyword — it needs the compiler to resolve `<Type>_alloc` from the allocator's static type. Lowers to `(*T)<Type>_alloc(&a, n*sizeof(T))`. `stdlib/alloc.esk` ships `Bump`/`Arena`/`Pool`/`FirstFit` over caller-provided buffers (libc-free) |
| `async` / `await` | `async` is a `FunctionDecl` modifier (`isAsync`); the call types as `*Future<T>` while the body returns inner `T`. `await E` is `AwaitExpr` — requires `E: *Future<T>`, yields `T`, only inside an async fn (`inAsyncFn`). **Lowering:** `sema/async_transform.cpp` runs after type-checking, before codegen (see `main.cpp`), rewriting each async fn into a frame struct + `__<name>_resume` (if-chain over resume state, park protocol) + a `*Future<T>` constructor — ordinary AST that normal codegen handles. The resume is a state graph — `while(true){ if(st==N){..} ... }` with branch-join and loop back-edges — so **every** control-flow construct containing an await works: `if`/`while`/C-style `for`/`switch` (C-style fall-through; break→join) and `for-in` (desugared to a counted `for` using a type-checker element-type stamp). `break`/`continue` in an await-split loop lower to state transitions (a literal one would escape the resume's own `while(true)`); a `for`'s `continue` runs through a dedicated step state. A recursive desugar normalizes `return await`/bare `await`/`x = await E`/nested awaits to let-bound awaits (and descends into switch cases); locals are hoisted to frame fields; synthesized waker lambdas set `captures` explicitly (sema already ran). Supports multi-await, `async void`, cancellation (`future_drop` cascade); **verified leak-free**. Generic combinators with a cast-free API: `spawn<T>(f)` detaches a fire-and-forget task, `select2<A,B>(a,b)`/`join2<A,B>(a,b)` are first-of-two/all-of-two — thin wrappers over one shared type-erased core (`*_hdr` over `FutureHdr*`, since a loser/unknown-type future can only be dropped through the header view), so no per-instantiation bloat. `await` normalizes the operand type and recovers `T` via the `templateInstanceArgs` reverse-map, so source-form (`*Future<int>`, plain fn) and template-mangled (`struct:Future_int`, generic call) futures resolve through one path. `<http_async>` is a non-blocking server on the accept loop. `<timer>` `timer_after(lp, ms)` is a leaf future over the event loop's timer wheel (`el_add_timer`/`el_del_timer`; `el_run` blocks until the nearest deadline) — enables `select2(read, timer_after(...))` timeouts. `<channel>` is an async message channel (`chan_recv` → `*Future<T>`; `chan_send` enqueues / hands off to a parked receiver). Runtime: `<future>`/`<executor>`/`<net_async>`/`<http_async>`/`<timer>`/`<channel>`. See `docs/dev/async-design.md` |
| `intrinsic` / `<atomic>` | `intrinsic` is a function qualifier (own `IntrinsicDecl` node, parallel to `extern`): a prototype whose calls lower to **inline IR**, not a call to a C symbol — codegen recognises the name via a small registry (`lowerIntrinsicCall`) and emits no `declare`. Gated on `intrinsicNames` (populated only when the declaring module is imported), so an un-imported user function of the same name is unaffected. `stdlib/atomic.esk` declares `atomic_load`/`atomic_store`/`atomic_swap`/`atomic_cas` → LLVM atomics (acquire/release/acq_rel) |
| Cross-compilation | `--target TRIPLE` (AArch64 and X86 backends included) |
| Freestanding | `--freestanding` predefines `__ESKIU_FREESTANDING__`; `<mem>`'s `alloc<T>`/`free` then target `esk_alloc`/`esk_free` |
| Negative literals | `-1`, `-3.14` as first-class primary expressions |
| argv / argc | `int main(int argc, string* argv)` |
| Multi-file compile | `eskiuc a.esk b.esk -o prog` — declarations from all inputs are merged into one program |
| Warnings (`-Wall`) | Unused variables/parameters/functions, assignment-in-condition; off by default |
| VS Code | Real-time errors, hover types, go-to-definition |
| stdlib | `result.esk`, `list.esk`, `string.esk`, `math.esk`, `io.esk`, `mem.esk`, `fs.esk`, `net.esk`, `alloc.esk`, `time.esk`, `env.esk`, `base64.esk`, `json.esk`, `threading.esk`, `http.esk`, `path.esk`, `eventloop.esk`, `atomic.esk`, `future.esk`, `executor.esk`, `net_async.esk`, `http_async.esk` (async runtime) |

## Roadmap (as of v0.1.0)

| Milestone | Items | Status |
|---|---|---|
| Systems milestone | Bare-metal kernel on QEMU | ✅ |
| v0.1 | Closures, threads, exceptions | ✅ |
| v0.2 | HTTP stdlib (`http.esk`) | ❌ |
| v1.0 | Package manager, self-hosting | ❌ |

## Adding a new AST node

1. Define the class in `ast/ast.h` — extend `Expr`, `Stmt`, or `Decl`.
2. Add `virtual void visit(YourNode*) = 0` to `ASTVisitor`.
3. Add `void YourNode::accept(ASTVisitor* v) { v->visit(this); }` in `ast/ast.cpp`.
4. Add `void visit(YourNode*) override` in: `ASTPrinter`, `TypeChecker`, `CodeGen`.
5. Add parse site in `parser/parser.cpp` (statement → `parseStatement`, expression → `parsePrimary` or `parseUnary`).
6. Write a test in `tests/` and verify with `--test-typechecker` and `--test-codegen`.

## Coding rules

- **C++17 only.** No C++20.
- **No new dependencies** beyond LLVM and the standard library.
- `camelCase` methods, `snake_case` locals. No trailing comments.
- AST nodes use `shared_ptr` throughout (`ExprPtr`, `StmtPtr`, `DeclPtr`). This is **load-bearing**, not incidental: struct-method bodies are co-owned by the synthesized `StructName_method` `FunctionDecl`, and template bodies are co-owned across every monomorphic instantiation (see `make_shared<FunctionDecl>(..., fd->body)` in codegen). Do **not** convert to `unique_ptr` without first adding deep-clone infrastructure for those shared bodies. To keep refcounting cheap: constructors take `Ptr` by value and `std::move` into members, and functions that only read a node take `const ExprPtr&` (never `ExprPtr` by value) — e.g. `evaluateExpr`, `getExprEskiuType`.
- Pointer types are strings ending or beginning with `*` (e.g. `"*uint8"`, `"int*"`). Use `isPointerType()` and `getPointeeType()` in both `TypeChecker` and `CodeGen`.
- Block bodies are `vector<BlockItem>` where `BlockItem = variant<DeclPtr, StmtPtr>`. Never split into two lists.

## Stdlib naming convention

Two deliberate forms — pick by **whether there is a type you operate on**:

- **`Type_method`** (PascalCase type prefix) — for a module built around a struct you instantiate and act on. The function takes the value as its first parameter (`self`) and is callable with dot syntax (`x.method()` mangles to `Type_method`). Examples: `String_append`, `List_push`, `Bump_alloc`, `Arena_save`, `Json_obj_begin`, `JsonValue_get`.
- **`module_func`** (lowercase module prefix) — for a flat namespace of free functions over primitives, buffers, or OS handles, with no central type. Examples: `base64_decode`, `fs_open`, `net_accept`, `time_now_ms`, `alloc`/`free` (in `<mem>`). A factory/entry function that *produces* a value but isn't a method also uses this form (e.g. `json_parse`, like `Ok`/`Err` in `result.esk`).

If a module has a `Foo` struct, its operations are `Foo_*`, not `foo_*`. (`<json>` originally shipped `json_*` for the `Json` builder — wrong; it is `Json_*`.) Types are always PascalCase; locals are `snake_case`.

## Type mappings

| Eskiu | LLVM |
|---|---|
| `int` / `int32` | `i32` |
| `int8` / `uint8` | `i8` |
| `int16` / `uint16` | `i16` |
| `int64` / `uint64` | `i64` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i1` |
| `char` | `i8` |
| `string` | `ptr` (i8*) |
| `*T` / `T*` | opaque `ptr` |
| `fn(T)->R` | `{ ptr fn_ptr, ptr env_ptr }` struct |

## Key codegen patterns

**Closures:** `fn(T)->R` is `{ptr, ptr}`. Lambda functions always receive `ptr env` as the first parameter. Non-capturing lambdas get `env = null`. Call sites extract `fn_ptr` and `env_ptr` from the struct and invoke `fn_ptr(env_ptr, args...)`. A `void`-returning indirect call must not be given a result name (LLVM forbids naming void values).

**Capture analysis (`TypeChecker`):** a name referenced in a lambda is captured when it resolves to a variable in a scope **below the lambda's own** — keyed off the scope index (`captureBoundary`), NOT off `functionSignatures`. Keying off the function table breaks when a param/local **shadows a same-named top-level function** (e.g. a `handler` param vs a global `handler` fn): the shadowing variable must still be captured. Also: a fn-pointer used in *callee* position (`h(x)`) is resolved by name in `visit(CallExpr)` and won't reach `visit(IdentExpr)` on its own — that branch calls `node->callee->accept(this)` so the capture still registers. **Template bodies** are skipped by the type checker (types mention unresolved `T`), so a separate `TemplateCapturePass` (file-local in `type_checker.cpp`, run from `visit(FunctionDecl)` for templates) does a type-independent lexical walk to fill `LambdaExpr::captures` with **source-form** types; codegen's `getTypeFromString` substitutes `typeParamOverride`, so a `*Future<T>` capture field becomes concrete per instantiation. Purely additive — it only writes captures that were previously empty. **`ast/ast_walk.h`** (`astwalk::forEachChildExpr`) is the single enumeration of an expression's child-expressions; the capture pass and the async transform's `rewrite`/`hasAwait` all recurse through it, so a new expression node is handled everywhere by editing one list (skipping it once miscompiled a frame var inside a struct literal).

**Exceptions:** `throw` calls `__cxa_throw` via `invoke` when inside a try body (so the local landingpad fires). `try` bodies use `invoke` for all calls. `catch` uses `landingpad { ptr, i32 } catch ptr null` (catch-all) with manual type comparison via the embedded type name in the exception object.

**Threads:** `thread_create(fn()->void)` extracts `fn_ptr` and `env_ptr` from the fat pointer and calls `pthread_create(tid, null, fn_ptr, env_ptr)` directly.

**Inline assembly:** Uses `llvm::InlineAsm::get` with `AD_ATT` dialect. Operand references use `$0`, `$1` (LLVM IR syntax, not `%0` GCC syntax). Inside try bodies, asm statements are not converted to `invoke` — asm is assumed not to throw.

**Cross-compilation:** When `targetTriple != ""` and differs from native, the CPU is set to `"generic"` to avoid host CPU features leaking into the cross-compiled object.

**Nested template instantiation (a template calling another with the param forwarded, e.g. `alloc<T>(n)` inside `List_push<T>`):** in `visit(TemplateCallExpr)`, resolve each explicit type arg through the active `typeParamOverride` before mangling/instantiating (`alloc<T>` inside `List_push<int>` must become `alloc_int`, not `alloc_T` → i32). And **save/restore** `typeParamOverride` around the inner instantiation rather than `clear()`ing it — clearing wipes the enclosing template's substitutions, so the outer body's `sizeof(T)`/param types silently revert to i32 after the inner call returns.

**Template-struct instantiation for params (`List<Point>* self`):** when `visit(FunctionDecl)` records a template-instance param type, it must `ensureTemplateInstantiated` the struct (e.g. `List_Point`) — not just mangle the name. Otherwise a `List<Point>` used only via helper functions (never a direct `let x: List<Point>` that would resolve the type) leaves the struct unregistered, and member access on `self` fails with "Unknown struct type". The struct type carries no `<`, so the on-demand path in `structBaseTypeOf` (which keys off `<`) won't catch it later.

**Pointer spellings:** both `*T` (canonical) and `T*` (trailing-star) are valid. Any pointee-stripping must handle both — `getPointeeType` used `substr(1)` (leading-star only), so `int*` deref produced the bogus pointee `nt*`. Strip a leading `*` OR a trailing `*`.

**Member access through a pointer base (`p.x` where `p: *T`):** the struct base is the *pointer value*, not the variable's storage. In both `evaluateLValue` and `visit(MemberExpr)`, compute it as `baseIsPtr ? evaluateExpr(base) : evaluateLValue(base)`, where `baseIsPtr` is detected from a leading/trailing `*` in `getExprEskiuType(base)`. Using `evaluateLValue` unconditionally GEPs from the local's `alloca` (the pointer's address) instead of the pointee, silently corrupting the variable — pointer *parameters* (e.g. `self`) happen to work because params are raw values, not allocas, which is why this only bit local `*T` vars. `getExprEskiuType` must also handle `CastExpr` (returns the cast's target type) so `(*T)x.field` and pointer-arith stride resolve correctly.

## Error format

```
error: <file>:<line>:<col>: <message>
```

Use `errorAt(node, message)` in `TypeChecker`. Codegen errors use `throw std::runtime_error(...)`.

## What agents should not do

- Do not introduce third-party libraries or new CMake targets.
- Do not rewrite visitor dispatch to use `std::variant` — virtual dispatch is intentional.
- Do not amend published commits; always create new ones.
- Do not skip `--test-typechecker` and `--test-codegen` validation before declaring a feature complete.
