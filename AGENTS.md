# AGENTS.md

Guidelines for AI agents working on the Eskiu compiler.

---

## What this project is

Eskiu is a systems programming language compiler written in C++17 that emits LLVM IR and links against LLVM 17+. The pipeline is:

```
Source → Lexer → Parser → TypeChecker → CodeGen → LLVM IR → .o → (cc/clang/gcc) → executable

`eskiuc file.esk -o prog` emits the object to a temp file and links it into an
executable by invoking the system C driver (`$CC`, then `cc`/`clang`/`gcc`);
see `linkExecutable` in `main.cpp`. A `.o` output, `-c`, or `--freestanding`
stops at the object file (no link). `-l`/`-L`/`--link-arg` pass through to the linker.
```

Every pass implements the `ASTVisitor` interface in `ast/ast.h`. When adding a new AST node you must update every visitor: `ASTPrinter`, `TypeChecker`, `CodeGen`.

## Build

```bash
cmake -S . -B build
cmake --build build -j"$(getconf _NPROCESSORS_ONLN)"   # portable core count (macOS + Linux)
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
| `lexer/lexer.cpp` | Tokeniser (`Lexer::next_token()`). `lexer/preprocessor.cpp` is the `#define`/`#ifdef` text pass (`preprocess()`, declared in `preprocessor.h`), run from the `Lexer` ctor. |
| `parser/` | Recursive-descent. Returns `shared_ptr<Program>`. Split into `parser.cpp` (token helpers, `parseType`, entry) + `parse_{decl,stmt,expr}.cpp`; the `withPos` template lives in `parser/parser_internal.h`. |
| `ast/ast.h` | All AST node types + `ASTVisitor` interface. |
| `ast/ast.cpp` | `accept()` definitions. |
| `ast/ast_printer.cpp` | Pretty-printer (`--test-parser`). |
| `sema/` | Type checker, split into `type_checker.cpp` (core: `check`, scope, LSP, errors) + `typecheck_{decl,stmt,expr,type}.cpp` (all `TypeChecker` members sharing `type_checker.h`). `sema/type.{h,cpp}` is the structured `ty::Type` IR (parse/str/substitute/nominalName), the typed replacement for ad-hoc type-string surgery; `substType` and the sema bare-name strips delegate to it. `sema/async_transform.cpp` is the async lowering pass. |
| `codegen/` | LLVM IR via `IRBuilder`. Split into `codegen_{module,type,scope,decl,stmt}.cpp` + `codegen_{expr,call,closure,adt}.cpp` (all `CodeGen` members sharing `codegen.h`). |
| `main.cpp` | CLI entry point + phase dispatch. `main_support.cpp` (decl in `main_support.h`) holds the cl::opt-free driver utilities: filesystem/path, `fmt`, the C-linker driver + runner, and `loadProgram`. |
| `stdlib/` | Eskiu stdlib modules (`result.esk`, `list.esk`, etc.). |
| `tests/` | Regression tests (`.esk` files). |
| `examples/` | Working demos. |
| `kernel/` | Bare-metal ARM64 kernel for QEMU (v0.1 milestone). |
| `selfhost/` | The compiler reimplemented **in Eskiu**: `lexer`/`parser`/`preprocessor`/`sema`/`codegen` (+`async_lower`) + drivers (`{lex,parse,pp,tc,cg,esk}_main.esk`). Codegen emits LLVM IR as text (no LLVM lib). Validated against the C++ `eskiuc` and against itself (bootstrap fixpoint). Slice-by-slice log + design: `selfhost/BACKEND_PLAN.md`; dogfood finds: `selfhost/NOTES.md`. |

## Current language status

All items below are implemented and tested end-to-end. v0.1.0 shipped the systems
foundation (closures, threads, exceptions, the bare-metal kernel); v0.2.0 added the
backend-services stack: async/await, the full HTTP/2 + HPACK + TLS stack, sum types
with `match`, and the concurrent stdlib. v0.3.0 reimplemented the whole compiler in Eskiu
(self-hosting, a 3-stage bootstrap fixpoint, codegen feature-complete against the C++
corpus). v0.3.1 added `-O0`/`-O1`/`-O2`/`-O3` optimization levels plus correctness fixes.
v0.4-v0.7 followed (correctness/type-strictness, basic-C completion, memory safety + stdlib,
cross-compilation; see the version table). The self-hosting **promotion** has since finished
(`selfhost/PROMOTION_PLAN.md`): the Eskiu-written compiler is behaviorally equivalent to the
C++ one over the whole corpus, CI-gated, and dual-built by CMake as `eskiuc-esk`; the C++
binary stays the shipped artifact (it bundles LLVM; the self-host links via clang).

| Feature | Notes |
|---|---|
| Primitive types | int/8/16/32/64, uint, float, double, bool, char, string, void, `*T` |
| Structs + methods | `Struct_method(self, ...)`, named/positional initialisers |
| Interfaces | Structural typing, vtable fat-pointer dispatch |
| Templates | Monomorphic instantiation: `Result<int,string>` → `%Result_int_string`. Struct literals `Pair<int,float>{...}`; explicit `f<int>(...)` or inferred `f(3)` type args (inference only when a type param appears directly as a parameter type) |
| Enums | `enum Color { Red, Green = 5, Blue }`: members are int constants; the enum type maps to `i32`; usable in `switch`/comparisons. A classic enum is also `match`-able with **exhaustiveness** (payload-less arms, `_` default), lowered as a switch on the value (cases = each variant's constant). A variable/param keeps the enum's nominal name (so `match` recovers the variant set) but still behaves as `int` (every other check runs it back through `normalizeType`; the `switch` subject check normalizes too). Sema: `plainEnumDecls`; codegen: the plain-enum branch in `visit(MatchStmt)` / the `cg_is_adt==0` branch in the self-host `SK_MATCH`. Test: `enum_match` |
| Type aliases | `type u8 = uint8;`: resolved to the underlying type in sema and codegen (`getTypeFromString`/`getExprEskiuType` expand aliases) |
| Bitfields | `struct F { uint32 a : 1; uint32 b : 3; }`: packed into storage words; masked load + read-modify-write store; signed fields sign-extend. Layout in `structLayout`; only structs with a bitfield change layout |
| Packed structs | `packed struct` or `#pragma pack(1)` → `StructDecl::isPacked` → `StructType::create(..., isPacked)`. `#pragma` survives `preprocess()`, lexes to a `PRAGMA` token, and the parser's pack stack (`currentPack`/`packStack`, `applyPragma`) tags structs declared under `pack(1)` |
| Preprocessor | object-like + function-like `#define`/`#undef` (recursive expansion, `\`-continued multi-line bodies) and `#ifdef`/`#ifndef`/`#else`/`#endif`. A text pass in the `Lexer` ctor (`preprocess()`); macro table is shared (passed into the ctor) so `#define`s cross `import`/multi-file; directives/skipped lines blank out to preserve line numbers. `#pragma` is passed through to the parser, not consumed |
| Forward declarations | Body-less `int f(int n);`; call-before-define and mutual recursion (codegen declares all prototypes before emitting bodies) |
| Compound assignment | `+= -= *= /= %=` and bitwise `&= \|= ^= <<= >>=` |
| Lambdas | `int(int x) { return x * 2; }`: anonymous functions |
| Closures | `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`; captures by value. **Escape analysis**: a non-escaping closure (only called / passed to a non-`escaping` param) keeps its env on the stack; an escaping one (returned, stored, or passed to an `escaping` param) gets a heap env. `LambdaExpr::escapes` (set in sema, default true) drives stack-vs-heap in codegen |
| `escaping` / `free_closure` | `escaping` is a parameter qualifier (Swift-style): it marks a param that *retains* the closure beyond the call. Sema enforces soundness: a non-`escaping` closure param used beyond a direct call is a compile error (`functionParamEscaping` + the `nonEscapingFnParams`/`escapedFnParams` walk in the type checker). `free_closure(f)` (a builtin, `FreeClosureExpr`) frees an escaping closure's heap env (slot 1 of the fat pointer); `free(null)` is a safe no-op for non-capturing closures |
| Function-as-value | A bare function name used as a value decays to a `fn(...)->R` via `makeFunctionPointer` (synthesizes a `__fnptr_<name>` env-ignoring thunk). `visit(CallExpr)` resolves direct named calls before `evaluateExpr` so calls don't decay |
| Predefined macros | `main.cpp` seeds the shared macro table with `__APPLE__`/`__linux__` (host OS) for `#ifdef` portability |
| `<net>` sockets | `stdlib/net.esk`: POSIX socket `extern`s + portable `packed sockaddr_in` (`#ifdef __APPLE__`) + `net_*` helpers (incl. `net_accept_addr` → peer IPv4). No compiler support needed beyond FFI |
| `thread_create` / `thread_join` | Language keywords; fat-pointer maps to `pthread_create(fn, env)` |
| `try` / `catch` / `finally` / `throw` | LLVM `invoke`/`landingpad` + `__gxx_personality_v0`; link `-lc++`. `finally` runs on fall-through, exception unwind, AND early `return`/`break`/`continue`/`?` from the body (via the cleanup stack, fixed alongside `defer`) |
| `defer` / `errdefer` | `defer stmt;` (`DeferStmt`): runs at enclosing-block exit, LIFO, on fall-through/`return`/`break`/`continue`/`?`. `errdefer` (`DeferStmt.isErr`) runs ONLY on the error exit (`?`-propagation), not on success/`return`. Codegen **cleanup stack** (`cleanupScopes` of `{body,isErr}` C++ / `cleanups`+`cl_err`+`loop_cldepth` self-host): each block pushes a frame, exits emit pending frames before the branch/ret via `runCleanupsToDepth(depth, errorPath)` (errdefers skipped unless errorPath); a `try`'s `finally` registers as a cleanup for its body. Cleanup state saved/reset per function body (else a template instantiated mid-return runs the outer defers). Sema rejects control-flow escaping a defer body. NOT run on an uncaught throw past a bare defer (use `try`/`finally`) |
| Inline assembly | `asm("cli")` simple; `asm("op" :: "r"(x) : "mem")` extended |
| `volatile` | `volatile let reg: *uint8 = addr;`: MMIO-safe |
| `static` | `static int c = 0;`: a local with one instance that persists across calls (C storage). Codegen emits a private module global instead of an alloca (`VarDecl::isStatic`). Sema requires a constant initializer and rejects `static` on a global |
| `must_use` | Function qualifier (`FunctionDecl::mustUse`, self-host DeclNode `is_mustuse`, NB the field can't be named `must_use` since that's now a keyword): discarding a bare call to it is a sema error. Registered into `mustUseFuncs` in BOTH registration branches (generic fns `continue` early). Checked in `visit(ExprStmt)` on a `CallExpr`/`TemplateCallExpr`. Sema-only (no codegen). `stdlib/mem.esk` marks `alloc` must_use |
| `const` | `const T x` / `const let x: T`: immutable; sema requires an initializer and rejects reassignment (`Symbol::isConst`/`isConstSymbol`). const ints resolve as array dimensions via `constInts` + `resolveArrayDim` (also accepts literals and enum members) |
| `?*T` nullable | Checked nullable pointer (opt-in null safety). `ty::Type` `nullable` flag on Pointer (leading `?` in the type string; `str()` prepends `?`). `*T` stays nullable (C-faithful, no breakage); `?*T` can't be deref/index/member'd unchecked (`checkNullableDeref`), `if (x != null)` narrows (`narrowedNonNull` set, `visit(IfStmt)`). `*T`→`?*T` ok, `?*T`→`*T` needs a check (`assignabilityError`). Same repr as `*T`; codegen strips `?` in `getExprEskiuType` (treats `?*T`≡`*T`). Parser: leading `?` in `parseType` + decl detection. **Self-host mirror done**: `parser.esk` parses `?` in `parse_type` (+ QUESTION in `parse_block_item`), `sema.esk` has `sema_check_nullable_deref` + `narrowed` narrowing in SK_IF + the nullable assignability rule, `codegen.esk` strips `?` via `cg_strip_nullable`; gated by `tests/selfhost/nullable_parity.sh` |
| `?:` ternary | `cond ? a : b` (`TernaryExpr`): one arm evaluated (condbr + phi), arms take a common type (numeric promotion). Right-assoc, precedence just above assignment. Disambiguated from postfix `?` propagation by `ternaryColonAhead()` (a same-level `:` ahead means ternary); parenthesize to propagate inside an arm |
| arrays | `T[N]` fixed-size; brace init `{...}` with C-style zero-fill (`ArrayLitExpr` + `emitArrayInitInto`). Multidim `T[N][M]` = N arrays of M in **C order**: the *leftmost* bracket is the outer dim (`ty::Type::parse` peels the first suffix bracket; `firstArraySuffixBracket`). Indexing peels one dim per `[]`; each index bounds-checked against its own dimension |
| slices `T[]` | Empty-bracket type = `ty::Type::Kind::Slice`, lowers to a `{ ptr, i64 }` fat pointer. Constructed by `a[lo..hi]` (`IndexExpr.highIndex`, reusing `..`); aliases the backing array. `s[i]` read/write + slice construction share `indexElemAddr` (C++) / `cg_slice_elem_addr` (self-host, single-evals `lo`); `s.len` → field 1 (int64); `for (x in s)` uses `.len` (C++ only; self-host has no collection for-in). Self-host: `cg_is_slice`, hi bound in EK_INDEX `kids`. `--safe` bounds-checks slice + fixed-array index (`emitBoundsCheck` → `@llvm.trap`, in `indexElemAddr`); off by default (zero release cost). **Self-host mirror done**: `codegen.esk` `cg_bounds_check` (guarded by `g.safe`, in `cg_lval`'s EK_INDEX + `cg_slice_elem_addr`), `--safe` flag threaded through `esk_main`; gated by `tests/selfhost/safe_parity.sh` |
| operator overloading | `V3 operator +(V3 a, V3 b) {...}` (binary `+ - * / % == != < > <= >= & | ^ << >>`, unary `- ! ~`, subscript `[]`). **Static, structural, zero-cost**: the decl compiles to a normal fn under a canonical mangled name (`eskiuOpName`/`ast_op_name`: op-as-word `+`->`add`, `[]`->`index`, unary `-`->`neg`; operand types appended), and `a op b` on non-built-in operands resolves to it by operand types. C++: `operator` keyword parsed in `parseFunctionDecl`, `FunctionDecl::operatorSym`, `operatorOverloads` table + `resolveOperator` (by assignability, so numeric args coerce) stamps `BinaryExpr/UnaryExpr/IndexExpr::opFunc`, codegen synthesizes a `CallExpr`. Self-host: mangling in `ast.esk`, parsed in `parse_function`, resolved+emitted in `codegen.esk` (`cg_op_resolve`/`cg_op_call`, EXACT operand match, no numeric coercion yet), plus the comparison-operand check in `sema.esk` allows an overloaded `==`. Compound-assign (`+=`) desugars to the binary op. `&&`/`||` (short-circuit) and `*`/`&`/`=`/`.` (structural) are NOT overloadable. Test: `tests/operators.esk` |
| `alloc<T>` / `free` | **Stdlib, not keywords**: `import <mem>`. `alloc<T>(n)` is a generic fn wrapping `calloc`/`esk_alloc` (**zero-initialized**, matching C++ `new T()`); `free(*void)` wraps `free`/`esk_free`. `<mem>` picks the backend via `#ifdef __ESKIU_FREESTANDING__`; the freestanding `esk_alloc` contract is "return zeroed memory". This is the default hosted heap (like Rust's default = system malloc) |
| `alloc_with` / `<alloc>` / `<sysheap>` | `alloc_with(&a, T, n)` (`AllocWithExpr`) is a built-in keyword: the compiler resolves `<Type>_alloc` from the allocator's static type, lowering to `(*T)<Type>_alloc(&a, n*sizeof(T))`. `stdlib/alloc.esk` ships `Bump`/`Arena`/`Pool`/`FirstFit`: explicit allocators over a buffer you own (the Zig model; pure pointer math, **no externs**), NOT a `malloc` replacement. `stdlib/sysheap.esk` is the libc-`malloc`-free hosted heap: it `mmap`s OS pages and runs `FirstFit` on them (Zig `page_allocator` style), opt-in via `Heap`/`heap_alloc`; it names `mmap`/`munmap` (OS page primitives, not `malloc`) |
| `async` / `await` | `async` is a `FunctionDecl` modifier (`isAsync`); the call types as `*Future<T>` while the body returns inner `T`. `await E` is `AwaitExpr`: requires `E: *Future<T>`, yields `T`, only inside an async function (`inAsyncFn`). **Lowering:** `sema/async_transform.cpp` runs after type-checking, before codegen (see `main.cpp`), rewriting each async function into a frame struct + `__<name>_resume` (if-chain over resume state, park protocol) + a `*Future<T>` constructor, ordinary AST that normal codegen handles. The resume is a state graph (`while(true){ if(st==N){..} ... }` with branch-join and loop back-edges), so **every** control-flow construct containing an await works: `if`/`while`/C-style `for`/`switch` (C-style fall-through; break→join) and `for-in` (desugared to a counted `for` using a type-checker element-type stamp). `break`/`continue` in an await-split loop lower to state transitions (a literal one would escape the resume's own `while(true)`); a `for`'s `continue` runs through a dedicated step state. A recursive desugar normalizes `return await`/bare `await`/`x = await E`/nested awaits to let-bound awaits (and descends into switch cases); locals are hoisted to frame fields; synthesized waker lambdas set `captures` explicitly (sema already ran). Supports multi-await, `async void`, cancellation (`future_drop` cascade); **verified leak-free**. Generic combinators with a cast-free API: `spawn<T>(f)` detaches a fire-and-forget task, `select2<A,B>(a,b)`/`join2<A,B>(a,b)` are first-of-two/all-of-two, thin wrappers over one shared type-erased core (`*_hdr` over `FutureHdr*`, since a loser/unknown-type future can only be dropped through the header view), so no per-instantiation bloat. `await` normalizes the operand type and recovers `T` via the `templateInstanceArgs` reverse-map, so source-form (`*Future<int>`, plain fn) and template-mangled (`struct:Future_int`, generic call) futures resolve through one path. `<http_async>` is a non-blocking, concurrent server: the accept loop `spawn`s a detached handler per connection, joined by a `<channel>` wait-group for clean shutdown. `<timer>` `timer_after(lp, ms)` is a leaf future over the event loop's timer wheel (`el_add_timer`/`el_del_timer`; `el_run` blocks until the nearest deadline), enables `select2(read, timer_after(...))` timeouts. `<channel>` is an async message channel (`chan_recv` → `*Future<T>`; `chan_send` enqueues / hands off to a parked receiver). Runtime: `<future>`/`<executor>`/`<net_async>`/`<http_async>`/`<timer>`/`<channel>`. See `docs/dev/async-design.md` |
| `intrinsic` / `<atomic>` | `intrinsic` is a function qualifier (own `IntrinsicDecl` node, parallel to `extern`): a prototype whose calls lower to **inline IR**, not a call to a C symbol. Codegen recognises the name via a small registry (`lowerIntrinsicCall`) and emits no `declare`. Gated on `intrinsicNames` (populated only when the declaring module is imported), so an un-imported user function of the same name is unaffected. `stdlib/atomic.esk` declares `atomic_load`/`atomic_store`/`atomic_swap`/`atomic_cas` → LLVM atomics (acquire/release/acq_rel) |
| Cross-compilation | `--target TRIPLE` (AArch64 and X86 backends included) |
| Freestanding | `--freestanding` predefines `__ESKIU_FREESTANDING__`; `<mem>`'s `alloc<T>`/`free` then target `esk_alloc`/`esk_free` |
| Negative literals | `-1`, `-3.14` as first-class primary expressions |
| argv / argc | `int main(int argc, string* argv)` |
| Multi-file compile | `eskiuc a.esk b.esk -o prog`: declarations from all inputs are merged into one program |
| Warnings (`-Wall`) | Unused variables/parameters/functions, assignment-in-condition; off by default |
| VS Code | Real-time errors, hover types, go-to-definition |
| stdlib | Core: `result.esk`, `either.esk`, `list.esk`, `map.esk` (`Map<V>` string-keyed + `HashMap<K,V>` fn-pointer hash/eq), `string.esk`, `bytes.esk` (`Bytes`, binary-safe buffer), `math.esk`, `io.esk`, `mem.esk`, `path.esk`, `env.esk`, `time.esk` (clock + UTC civil calendar), `random.esk` (xoshiro256\*\* PRNG), `regex.esk` (Thompson-NFA regex with captures), `sort.esk` (generic heapsort + bsearch), `url.esk` (percent-encoding + query), `uuid.esk` (RFC 4122 v4). Memory: `alloc.esk` (Bump/Arena/Pool/FirstFit), `sysheap.esk` (mmap heap). Concurrency: `threading.esk`, `atomic.esk`. Async runtime: `eventloop.esk`, `future.esk`, `futureval.esk`, `executor.esk`, `net_async.esk`, `timer.esk`, `channel.esk`. Net/codecs: `net.esk`, `base64.esk`, `json.esk`, `multipart.esk`. HTTP: `http.esk` (String `HttpRequest` + binary-safe `HttpReq`/`http_recv`), `http_async.esk`, `http2.esk`, `http2_server.esk`, `hpack.esk`, `hpack_huffman.esk`, `tls.esk`. Files: `fs.esk` |

## Roadmap

| Milestone | Items | Status |
|---|---|---|
| Systems milestone | Bare-metal kernel on QEMU | ✅ |
| v0.1.0 | Closures, threads, exceptions, enums, unions, bitfields, preprocessor | ✅ |
| v0.2.0 | async/await, HTTP/1.1 + HTTP/2 + HPACK + TLS, sum types + `match`, allocators, the concurrent stdlib | ✅ |
| v0.2.1 | codegen split into 6 files; sanitizer (asan/ubsan) CI gate; `<bytes>`; `HashMap<K,V>`; sret-arg + fn-type-substitution fixes | ✅ |
| v0.2.2 | hardening (generative fuzzer with an O0-vs-O2 differential oracle + 4 codegen/sema fixes, CI fuzz gate); bounded generics `<T: Iface>` / `<T: A + B>` (method-based, checked at the instantiation site); compiler source modularized (type_checker / codegen_expr / parser / lexer / main split, no behavior change) | ✅ |
| v0.2.3 | bounded-generics completion (primitives satisfy a constraint via a free function); typed `ty::Type` IR foundation (`sema/type.{h,cpp}`): `substType` + the sema bare-name strips migrated to it, golden-IR oracle gating behavior-preservation. Cross-phase consolidation (codegen consuming resolved Types) deferred | ✅ |
| v0.2.4 | type unification. The type checker is the single resolver: codegen consumes its resolved expression types (re-run post-AsyncTransform) instead of re-deriving, and `getTypeFromString` dispatches on `ty::Type::parse` (one grammar interpreter across both phases). Closed the two-evaluator risk; fixed 3 latent miscompiles it surfaced (float-lit `double`, ptr-deref width, `char` zext) | ✅ |
| v0.2.5 | preprocessor correctness fix (a `//`/`/* */` comment ending in `\` no longer splices the next source line, silent code-eating footgun; found dogfooding the self-hosted lexer); **self-hosting milestone 1: the lexer in Eskiu** (`selfhost/`, byte-identical to `--test-lexer` over the preprocessor-free corpus, CI-gated); hardening (`ESKIU_RESOLVER_DEBUG` consistency oracle, backslash fuzzer generators) | ✅ |
| v0.3.0 | **Self-hosting: the whole compiler in Eskiu.** Front-end (lexer / parser + import resolution / preprocessor) and back-end (sema with all 19 error classes; codegen covering the full bootstrap subset plus floats, switch, ADT enums + `match`, closures, exceptions (Itanium ABI), atomics, generics + argument inference, async/await 19/19, unions, bitfields, interfaces) all reimplemented in Eskiu. 3-stage bootstrap fixpoint reached; codegen feature-complete against the C++ corpus (a clean `cg_parity.sh` sweep, earned and verified, not assumed from the bootstrap). All parity gates CI-wired. | ✅ |
| v0.3.1 | Correctness + optimization: `-O0`/`-O1`/`-O2`/`-O3` levels; a float-closure `-O2` miscompile fix (lambda return-type reconciliation); reject incompatible `fn`-type assignments; `*T[N]` = array of pointers; libc `size_t` externs to `int64`; a `-O0`-vs-`-O2` CI differential; parser self-host parity widened to the full corpus (51 → 121). | ✅ |
| v0.4.0 | **Correctness + type-strictness.** A four-front bug hunt fixed miscompiles (int literal width, `unsigned`→`float`, template missing-return, catch-less `try`/`finally` abort, transitive closure capture) and crash/reject-valid bugs (comparison operand typing, `void`/`string` init, float comparison promotion, const address-of), plus self-hosted-back-end fixes (`!`/`~`, hex/octal + huge literals, const array dims, exception propagation, async `for-in` over generic `List<T>`). Type system tightened: floating-point→integer needs an explicit cast; out-of-range literal, div/mod by literal 0, proven-OOB constant index, reading an uninitialized local, returning `&local`, function redefinition, and non-void fall-through are errors. Self-host sema parity for the new checks is deferred to the promotion track. | ✅ |
| v0.5.0 | **Basic-C surface completion.** Fills the last common C constructs so idiomatic C ports compile without workarounds, each landed lockstep across both compilers: `do`/`while`, prefix/postfix `++`/`--`, array-literal initializers (`{...}` with C-style zero-fill), `static` locals (persist across calls, private module global), multidimensional arrays `T[N][M]` (C order: leftmost bracket outermost) with nested initializers, and the ternary `cond ? a : b` (condbr + phi, disambiguated from postfix `?` propagation by a same-level `:` scan). Also fixes `switch` on a sub-`int` subject (widen subject + cases). | ✅ |
| v0.6.0 | **Memory safety + standard library.** Zig-flavored safety (compile-time checks + opt-in runtime guards, no borrow checker): `defer`/`errdefer` (codegen cleanup-stack, LIFO, runs on every exit incl. `?`-propagation; also fixed a latent `finally`-on-early-return bug), the slice type `T[]` (fat ptr `{ptr,i64}`, `a[lo..hi]`, `s.len`), `must_use` (discarded result is an error; `alloc` is marked), `--safe` (opt-in index bounds-checks, trap on violation, off by default), and the checked nullable pointer `?*T` (unchecked deref rejected; `if (x != null)` narrows; lowered as a bare pointer). `--safe` and `?*T` self-host mirrors are on the promotion track. Stdlib: `<random>` (xoshiro256\*\*), `<regex>` (Thompson-NFA/Pike VM with captures), `<sort>` (heapsort + bsearch), `<url>` (percent-encoding + query), `<uuid>` (v4), and a UTC civil calendar in `<time>`. | ✅ |
| v0.6.1 | **Patch.** Lexer escape-sequence fix (lockstep both compilers): `\0` decodes to NUL (not `'0'`), char literals accept `\r`/`\f`/`\v`, and string and char literals share one escape set (`\n \t \r \f \v \0 \\ \" \'`). Plus a documentation-accuracy pass from an internal audit (AST node counts, `defer` internals, real `--test-*` sample output, inline-asm `$0` operand syntax, `intrinsic` spec section, glossary token-type names). | ✅ |
| v0.6.2 | **Patch (async/CI hardening).** `el_new` now initializes every timer's `on_fire` closure (completing the v0.3.1 `on_read` fix), so the event loop holds no uninitialized function pointer (the shape of the intermittent Linux CI HTTP/2 `SIGILL`). The async transform preserves per-parameter `escaping` flags through coroutine lowering (a frame-stored escaping callback could otherwise be stack-allocated and dangle). Interface (vtable) dispatch coerces each argument to the method's declared param type, lockstep in both compilers (a widening arg previously failed LLVM verification / emitted mistyped IR). Cross-compilation: `--target` now drives the `__APPLE__`/`__linux__` platform macro (was emitting BSD symbols for a Linux target). | ✅ |
| v0.7.0 | **Minor (cross-compilation + C interop).** 32-bit ARM backend with `--mcpu` / `--mattr` / `--reloc`; a hard-float ARM triple (ending `hf`) selects the hard-float ABI, so an object carries `Tag_ABI_VFP_args` and links into a Nintendo 3DS `.3dsx` (demo verified on device). Windows x86-64 COFF emission with `_WIN32` / `_WIN64` predefined. `extern` now declares C global **variables**, not just functions (lockstep both compilers). Fixed: a `double` literal narrows to a `float` parameter at the call site (was mistyped IR); a bare-metal `none` triple predefines no host OS macro; hard-float reached the object emitter (the three code-emitting paths share one `TargetMachine` builder). | ✅ |
| v1.0 | Package manager; promote the Eskiu-written compiler to the primary build (`selfhost/PROMOTION_PLAN.md`) | ❌ |

Self-hosting codegen is **feature-complete against the C++ corpus**. A full feature sweep
(every feature-bearing `tests/*.esk` pushed through `cg_parity.sh`) is clean: each
program self-host-compiles to the same behavior as the C++ build. This was earned by fixing
11 root-cause gaps the sweep exposed (coercion/signedness, integer semantics, type aliases,
function-as-value decay, generic ADT enums, a parser cast-vs-struct-literal bug, primitive
constraint dispatch, alloc_with/thread builtins, variadics/`va_*`, packed structs, the `?`
operator). See `selfhost/BACKEND_PLAN.md` for the slice-by-slice record. **Caution that bit
us repeatedly: the bootstrap fixpoint only exercises the subset the compiler's own source
uses; it does NOT prove general feature coverage. To check whether a feature works in the
self-host, write a parity test and re-run the FULL sweep; never claim "complete" from the
bootstrap alone.** Residual non-gaps: async `for-in` types its element via a local heuristic,
not a sema stamp; the parse-parity corpus could expand to the ~70 preprocessor-touching
files. Genuinely deferred earlier: a package manager, and the tighter locals-across-await
liveness optimization (see `docs/dev/phases.md`).

## Working on the self-hosted compiler (`selfhost/`)

The C++ `eskiuc` stays the oracle; the Eskiu reimplementation is validated against it.
**Parity method, by phase:**

- Front-end (lexer/parser/preprocessor) = **byte-exact** diff vs `eskiuc --test-{lexer,parser}`
  (strip the C++ banner). Preprocessor parity runs *through* the lexer.
- Sema = **verdict + `EXPECT-ERROR` substring** (no byte dump: most programs type-check
  cleanly).
- Codegen = **behavioral**: emit `.ll` → `clang` → run → compare exit code + stdout to the
  C++-built binary (IR can't match byte-for-byte: SSA auto-numbering + constant folding).
- Bootstrap = **IR fixpoint** (cc1 and cc2 emit identical IR), NOT binary equality (Mach-O
  `LC_UUID` + ad-hoc signature differ for identical input).

Gates: `tests/selfhost/{lex,parse,pp,tc,cg}_parity.sh` + `cg_selfhost.sh` + `cg_bootstrap.sh`,
all CI-wired and honoring a `CLANG` env override (CI uses clang-22) like `ESKIUC`.

**Footguns that bite repeatedly (check these first when self-host code breaks):**

- `fn`, `in`, and `match` are **reserved keywords**: never use one as a variable / param /
  field name. The C++ parser derails silently ("Expected expression, got …").
- `alloc<T>` **zero-initializes** (hosted `calloc`; freestanding `esk_alloc` must zero): a
  fresh `*T` is null, an `int` is `0`, a `List` is a valid empty list. Reading a field before
  you set it is defined, not a segfault. (This closed a whole crash class; before, `alloc`
  wrapped `malloc` and only macOS's already-zero heap hid the garbage reads.)
- AST traversal must still be **kind-aware**: a field a node kind doesn't set is now zero, not
  garbage, but zero can still be a wrong-but-plausible value (a null `*ExprNode` where a real
  child was expected). Prefer matching the reference walker (`cg_collect_idents_e`) exactly.
- `… | head` masks `eskiuc`'s exit code → a failed build silently reuses the stale binary.

Discipline per slice: incremental → de-risk on a focused test → all gates green +
guard-malloc clean → fixpoint preserved → short commit → docs. Root fixes, never patches.

## Adding a new AST node

1. Define the class in `ast/ast.h`: extend `Expr`, `Stmt`, or `Decl`.
2. Add `virtual void visit(YourNode*) = 0` to `ASTVisitor`.
3. Add `void YourNode::accept(ASTVisitor* v) { v->visit(this); }` in `ast/ast.cpp`.
4. Add `void visit(YourNode*) override` in: `ASTPrinter`, `TypeChecker`, `CodeGen`: the `TypeChecker`/`CodeGen` definitions go in the matching split file (`typecheck_{decl,stmt,expr,type}.cpp`, `codegen_{expr,call,closure,adt}.cpp`).
5. Add parse site in the matching `parse_{decl,stmt,expr}.cpp` (statement → `parseStatement`, expression → `parsePrimary` or `parseUnary`).
6. Write a test in `tests/` and verify with `--test-typechecker` and `--test-codegen`.

## Coding rules

- **C++17 only.** No C++20.
- **No new dependencies** beyond LLVM and the standard library.
- `camelCase` methods, `snake_case` locals. No trailing comments.
- AST nodes use `shared_ptr` throughout (`ExprPtr`, `StmtPtr`, `DeclPtr`). This is **load-bearing**, not incidental: struct-method bodies are co-owned by the synthesized `StructName_method` `FunctionDecl`, and template bodies are co-owned across every monomorphic instantiation (see `make_shared<FunctionDecl>(..., fd->body)` in codegen). Do **not** convert to `unique_ptr` without first adding deep-clone infrastructure for those shared bodies. To keep refcounting cheap: constructors take `Ptr` by value and `std::move` into members, and functions that only read a node take `const ExprPtr&` (never `ExprPtr` by value), e.g. `evaluateExpr`, `getExprEskiuType`.
- Pointer types are strings ending or beginning with `*` (e.g. `"*uint8"`, `"int*"`). Use `isPointerType()` and `getPointeeType()` in both `TypeChecker` and `CodeGen`.
- Block bodies are `vector<BlockItem>` where `BlockItem = variant<DeclPtr, StmtPtr>`. Never split into two lists.

## Stdlib naming convention

Two deliberate forms. Pick by **whether there is a type you operate on**:

- **`Type_method`** (PascalCase type prefix): for a module built around a struct you instantiate and act on. The function takes the value as its first parameter (`self`) and is callable with dot syntax (`x.method()` mangles to `Type_method`). Examples: `String_append`, `List_push`, `Bump_alloc`, `Arena_save`, `Json_obj_begin`, `JsonValue_get`.
- **`module_func`** (lowercase module prefix): for a flat namespace of free functions over primitives, buffers, or OS handles, with no central type. Examples: `base64_decode`, `fs_open`, `net_accept`, `time_now_ms`, `alloc`/`free` (in `<mem>`). A factory/entry function that *produces* a value but isn't a method also uses this form (e.g. `json_parse`, like `Ok`/`Err` in `result.esk`).

If a module has a `Foo` struct, its operations are `Foo_*`, not `foo_*`. (`<json>` originally shipped `json_*` for the `Json` builder, wrong; it is `Json_*`.) Types are always PascalCase; locals are `snake_case`.

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

**Capture analysis (`TypeChecker`):** a name referenced in a lambda is captured when it resolves to a variable in a scope **below the lambda's own**, keyed off the scope index (`captureBoundary`), NOT off `functionSignatures`. Keying off the function table breaks when a param/local **shadows a same-named top-level function** (e.g. a `handler` param vs a global `handler` fn): the shadowing variable must still be captured. Also: a fn-pointer used in *callee* position (`h(x)`) is resolved by name in `visit(CallExpr)` and won't reach `visit(IdentExpr)` on its own. That branch calls `node->callee->accept(this)` so the capture still registers. **Template bodies** are skipped by the type checker (types mention unresolved `T`), so a separate `TemplateCapturePass` (file-local in `type_checker.cpp`, run from `visit(FunctionDecl)` for templates) does a type-independent lexical walk to fill `LambdaExpr::captures` with **source-form** types; codegen's `getTypeFromString` substitutes `typeParamOverride`, so a `*Future<T>` capture field becomes concrete per instantiation. Purely additive: it only writes captures that were previously empty. **`ast/ast_walk.h`** (`astwalk::forEachChildExpr`) is the single enumeration of an expression's child-expressions; the capture pass and the async transform's `rewrite`/`hasAwait` all recurse through it, so a new expression node is handled everywhere by editing one list (skipping it once miscompiled a frame var inside a struct literal).

**Exceptions:** `throw` calls `__cxa_throw` via `invoke` when inside a try body (so the local landingpad fires). `try` bodies use `invoke` for all calls. `catch` uses `landingpad { ptr, i32 } catch ptr null` (catch-all) with manual type comparison via the embedded type name in the exception object.

**Threads:** `thread_create(fn()->void)` extracts `fn_ptr` and `env_ptr` from the fat pointer and calls `pthread_create(tid, null, fn_ptr, env_ptr)` directly.

**Inline assembly:** Uses `llvm::InlineAsm::get` with `AD_ATT` dialect. Operand references use `$0`, `$1` (LLVM IR syntax, not `%0` GCC syntax). Inside try bodies, asm statements are not converted to `invoke`; asm is assumed not to throw.

**Cross-compilation:** When `targetTriple != ""` and differs from native, the CPU is set to `"generic"` to avoid host CPU features leaking into the cross-compiled object.

**Nested template instantiation (a template calling another with the param forwarded, e.g. `alloc<T>(n)` inside `List_push<T>`):** in `visit(TemplateCallExpr)`, resolve each explicit type arg through the active `typeParamOverride` before mangling/instantiating (`alloc<T>` inside `List_push<int>` must become `alloc_int`, not `alloc_T` → i32). And **save/restore** `typeParamOverride` around the inner instantiation rather than `clear()`ing it; clearing wipes the enclosing template's substitutions, so the outer body's `sizeof(T)`/param types silently revert to i32 after the inner call returns.

**Bounded generics / constraints (`<T: Iface>`):** parsed in the typeParams loops (`parser/parser.cpp`) into `FunctionDecl::constraints` / `StructDecl::constraints` (type-param name → interface names), modelled on the `paramEscaping` parallel-data precedent. Enforcement lives only in the type checker: `checkConstraints` (`type_checker.cpp`) takes the constraint map + the `subs` (T→concrete) and, for each constrained param, **normalizes the concrete type first, *then* strips `struct:`/pointers** before `structSatisfiesInterface` (normalizing after stripping re-adds `struct:` and looks up the wrong key, a false negative on satisfied types). Called from three sites: `visit(TemplateCallExpr)` (explicit args), the inferred path in `visit(CallExpr)` once `allBound`, and the not-yet-instantiated branch of `normalizeType` for template structs (node is null there → `error(0,0,…)`). Codegen is untouched for the struct path: the constraint only turns a would-be late instantiation failure into an early, located type error. **Primitives satisfy via a free function** (`structSatisfiesInterface` free-fn fallback, gated to scalar primitives in lockstep with codegen): a top-level fn named like the interface method whose first param is the primitive: `int cmp(int,int)` satisfies `Ord` for `int`. The constrained call `t.m(x)` on a primitive receiver lowers (in `codegen_call.cpp`, same scalar-primitive gate) to the free fn `m(t, x)`. fn-pointer `HashMap<K,V>` remains for explicit `hash`/`eq`.

**Template-struct instantiation for params (`List<Point>* self`):** when `visit(FunctionDecl)` records a template-instance param type, it must `ensureTemplateInstantiated` the struct (e.g. `List_Point`), not just mangle the name. Otherwise a `List<Point>` used only via helper functions (never a direct `let x: List<Point>` that would resolve the type) leaves the struct unregistered, and member access on `self` fails with "Unknown struct type". The struct type carries no `<`, so the on-demand path in `structBaseTypeOf` (which keys off `<`) won't catch it later.

**Pointer spellings:** both `*T` (canonical) and `T*` (trailing-star) are valid. Any pointee-stripping must handle both: `getPointeeType` used `substr(1)` (leading-star only), so `int*` deref produced the bogus pointee `nt*`. Strip a leading `*` OR a trailing `*`.

**Member access through a pointer base (`p.x` where `p: *T`):** the struct base is the *pointer value*, not the variable's storage. In both `evaluateLValue` and `visit(MemberExpr)`, compute it as `baseIsPtr ? evaluateExpr(base) : evaluateLValue(base)`, where `baseIsPtr` is detected from a leading/trailing `*` in `getExprEskiuType(base)`. Using `evaluateLValue` unconditionally GEPs from the local's `alloca` (the pointer's address) instead of the pointee, silently corrupting the variable; pointer *parameters* (e.g. `self`) happen to work because params are raw values, not allocas, which is why this only bit local `*T` vars. `getExprEskiuType` must also handle `CastExpr` (returns the cast's target type) so `(*T)x.field` and pointer-arith stride resolve correctly.

## Error format

```
error: <file>:<line>:<col>: <message>
```

Use `errorAt(node, message)` in `TypeChecker`. Codegen errors use `throw std::runtime_error(...)`.

## What agents should not do

- Do not introduce third-party libraries or new CMake targets.
- Do not rewrite visitor dispatch to use `std::variant`; virtual dispatch is intentional.
- Do not amend published commits; always create new ones.
- Do not skip `--test-typechecker` and `--test-codegen` validation before declaring a feature complete.
