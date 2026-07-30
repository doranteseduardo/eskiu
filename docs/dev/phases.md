---

# Compiler Development Phases

Authoritative status reference for Eskiu compiler contributors.

Last updated: 2026-07-04.

---

## Vision

Eskiu addresses a specific problem: compute-intensive services currently require multiple languages: C for performance-critical work, Go for concurrency, C++ for libraries, Python for glue. Each adds toolchain complexity and interop friction.

The project follows two phases:

**Phase 1: Systems foundation.** A language that covers everything C does: native performance, explicit memory, direct C library access. Validated against real production code. Complete.

**Phase 2: Domain specialisation.** Once the systems foundation is stable, make the domain types that high-throughput services actually work with first-class in the language, without giving up general systems capability.

---

## Current language status

| Feature | Status |
|---------|--------|
| Primitive types (int/8/16/32/64, uint, float, double, bool, char, string, void) | ✅ |
| Pointers and pointer arithmetic | ✅ |
| Structs with methods | ✅ |
| Interfaces with vtable dispatch (fat pointer) | ✅ |
| Templates: structs and functions, monomorphic instantiation | ✅ |
| Control flow: if/else, while, for, switch/case (with type checking) | ✅ |
| Lambdas (`int(int x) { return x*2; }`) and `fn(T)->R` function pointer types | ✅ |
| Negative literals: `-1`, `-3.14` as first-class values | ✅ |
| C FFI: extern, variadic, sret on arm64 | ✅ |
| `alloc<T>(n)` / `free` via `import <mem>` (stdlib, not keywords as of v0.2.0), rich mutable `String` (push, char access, eq, index_of, substring, reverse, int conversion) | ✅ |
| List\<T\> with auto-resize | ✅ |
| for-in / for-each: `for (x in arr)` and `for (x in list)` | ✅ |
| `?` error-propagation operator for `Result<T,E>` | ✅ |
| argv/argc: `int main(int argc, string* argv)` | ✅ |
| VS Code: inline errors, hover types, go-to-definition | ✅ |
| Inline assembly: `asm(...)` | ✅ |
| Freestanding mode: compile without libc | ✅ |
| volatile: for memory-mapped I/O | ✅ |
| const: immutable typed bindings, usable as array sizes (v0.2.0) | ✅ |
| `alloc_with(&a, T, n)` + `<alloc>` (Bump/Arena/Pool/FirstFit): explicit allocators over a buffer you own (Zig model); `<sysheap>` adds an mmap-backed heap with no libc `malloc` (v0.2.0) | ✅ |
| Nested template instantiation: a template calling another with the type param forwarded (`alloc<T>(n)` inside `List_push<T>`) (v0.2.0) | ✅ |
| Closures: capturing variables from the enclosing scope | ✅ |
| Threads: `thread_create`/`thread_join` primitives | ✅ |
| Exceptions: try/catch/finally/throw | ✅ |
| sizeof(T): compile-time size expression | ✅ |
| Typed pointer arithmetic: p + n advances by sizeof(*p) | ✅ |
| union: overlapping fields, size = largest member | ✅ |
| `import <name>` stdlib syntax: angle-bracket imports resolved from installation | ✅ |
| `<fs>` stdlib: file I/O (`fs_open`, `fs_read_all`, `fs_write_all`, etc.) | ✅ |
| `<net>` stdlib: TCP sockets (`net_tcp_listen`/`accept`/`connect`/`send`/`recv`), portable `sockaddr_in` | ✅ |
| `<time>` / `<env>` / `<base64>` / `<json>` stdlib: clock+sleep, env vars, Base64 codec, JSON builder + parser (v0.2.0) | ✅ |
| `<threading>` stdlib: `Mutex` / `Cond` / `Sem` over pthread (v0.2.0) | ✅ |
| `<http>` stdlib: HTTP/1.1 request parser, response builder, threaded worker pool (v0.2.0) | ✅ |
| Multi-argument `fn` types: `fn(A,B)->R`; fn pointers callable and capturable in closures (v0.2.0) | ✅ |
| `<string>` extras (`split`/`trim`/`starts_with`/`ends_with`) and `<path>` stdlib (v0.2.0) | ✅ |
| `List<StructType>` through helper functions; `T*` (trailing-star) pointer deref: codegen/sema fixes (v0.2.0) | ✅ |
| Consistency audit hardening (v0.2.0): unsigned div/rem/shift/compare; 64-bit int literals; sign/zero-extend by signedness; variadic arg promotion; closures don't capture globals; member access on a temporary; `(Type)`/`(Type*)`/alias/enum casts; alias as local pointer/array; `fn` return types; fn-pointer field calls; `>>` closing nested templates | ✅ |
| Function-as-value: a named function decays to a `fn(...)->R` (no lambda wrapper) | ✅ |
| Predefined OS macros: `__APPLE__` / `__linux__` for `#ifdef` portability | ✅ |
| Pointer dereference as lvalue: `*ptr = value` through pointer parameters | ✅ |
| Forward declarations / call-before-define / mutual recursion | ✅ |
| Enums: `enum Color { Red, Green = 5, Blue }` | ✅ |
| Type aliases: `type u8 = uint8;` | ✅ |
| Struct bitfields: `uint32 x : 1;` | ✅ |
| Preprocessor: `#define` (object/function-like, multi-line via `\`), `#ifdef`/`#ifndef`/`#else`/`#endif` | ✅ |
| Packed structs: `packed struct` and `#pragma pack(push/pop)` | ✅ |
| Template type-argument inference: direct (`max(3, 5)`) and composite (`List_get(&nums, i)`) | ✅ |
| One-step linking: `eskiuc -o prog` invokes the system C toolchain | ✅ |
| Multi-file compilation: `eskiuc a.esk b.esk -o prog` | ✅ |
| `-Wall` warnings: unused vars/params/functions, assignment-in-condition | ✅ |
| `<bytes>` and `HashMap<K,V>` stdlib (v0.2.1) | ✅ |
| Bounded generics: `<T: Iface>` / `<T: A + B>` constraints (v0.2.2) | ✅ |
| Primitives satisfy constraints via a free function (v0.2.3) | ✅ |
| Structured `ty::Type` IR: `sema/type.{h,cpp}`, parse/str/substitute/nominalName (v0.2.3) | ✅ |
| Single-resolver type unification: type checker resolves every expression type; codegen consumes the table (v0.2.4) | ✅ |
| Package manager | ❌ |
| Self-hosting: lexer/parser/preprocessor/sema/codegen all in Eskiu; 3-stage bootstrap fixpoint, codegen feature-complete (v0.3.0, `selfhost/`) | ✅ |
| Optimization levels: `-O0`/`-O1`/`-O2`/`-O3`; `-O1`+ run the LLVM middle-end before codegen (v0.3.1) | ✅ |
| `*T[N]` = array of pointers; pointer-to-array is `T[N]*` (v0.3.1) | ✅ |
| Incompatible `fn`-type assignments rejected; libc `size_t` externs use `int64` (v0.3.1) | ✅ |
| `-O0`-vs-`-O2` behavioral differential CI gate (`tests/opt_differential.sh`) (v0.3.1) | ✅ |

---

## Foundation milestone: COMPLETE

Compiler foundation proved on a real production workload: a cryptographic pipeline (AES-256-CBC + RSA-8192 decryption, QR extraction, structured output) running entirely in Eskiu.

| Stage | Eskiu | Reference C |
|-------|-------|-------------|
| QR extraction | 71.7 ms | 185.5 ms |
| Crypto (AES+RSA) | 2.8 ms | 2.9 ms |
| Output decode | < 1 ms | 0.5 ms |
| **Total** | **74.4 ms** | **188.9 ms** |

2.5× faster than the reference C implementation.

---

## Roadmap

### v0.1.0: systems foundation (SHIPPED)

Everything in the feature table above ships in v0.1.0: the full systems language plus closures, threads, exceptions (`try`/`catch`/`finally`/`throw`), enums, unions, bitfields, type aliases, the preprocessor, one-step linking, multi-file builds, and `-Wall`. Ergonomics round it out: structural template-argument inference (including composite parameters like `List<T>*`), `for`-`in` iteration over arrays and lists, the `?` error-propagation operator for `Result<T,E>`, a rich mutable `String` type, packed structs, function-as-value decay, and a `<net>` TCP sockets module (with an HTTP server example). A bare-metal ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) on the PL011 UART (without libc or a C runtime) and the cryptographic pipeline above runs 2.5× faster than the reference C. This is the first release.

### v0.2.0: Backend services (SHIPPED)

The theme is making Eskiu a practical language for concurrent backend services:
real async I/O, an HTTP stack, and the everyday stdlib + tooling that adoption
needs. v0.1.0 is frozen at its tag; v0.2.0 shipped the items below. The current release is **v0.6.0**: a memory-safety + stdlib release (`defer`/`errdefer`, slices `T[]`, `must_use`, `--safe`, `?*T`, plus the `<random>`/`<regex>`/`<sort>`/`<url>`/`<uuid>` modules and a UTC calendar in `<time>`) over the v0.5 basic-C surface release and the v0.3 self-hosting milestone (see the sections below).

Tracking checklist (checked = landed on `develop`).

The checklist is grouped into execution phases (not just categories): Phase 1
unblocks real projects, Phase 2 builds the async/HTTP stack on top of it, and
Phase 3 rounds out ergonomics and tooling.

**Phase 1: Unblock real projects**
- [x] `const`: typed, scoped constants, usable as array sizes
- [x] `alloc_with(&allocator, T, n)`: explicit-allocator form alongside the default `alloc<T>` (built-in; `alloc`/`free` themselves moved to `<mem>`)
- [x] `<alloc>`: a toolkit of explicit allocators (FirstFit after Ken Thompson, plus Arena, Pool, Bump) over a buffer you own, via `alloc_with` (the Zig allocator model). Pure pointer arithmetic, no externs. NOT a global `malloc` replacement: the default `alloc<T>` is still libc `malloc` in hosted mode; these are for managing a region, and are the building blocks for a freestanding heap (the kernel's `esk_alloc`)
- [x] `<sysheap>`: a general-purpose heap that sources OS pages via `mmap` and allocates with `FirstFit`: no libc `malloc` on the allocation path (the Zig `page_allocator` / jemalloc / Go-runtime approach). The libc-`malloc`-free hosted heap, opt-in
- [x] `<threading>`: mutex, condvar, semaphore over pthread
- [x] `<http>`: HTTP/1.1 parser + response builder + worker pool
- [x] `<base64>`: encode / decode
- [x] `<json>`: builder + parser
- [x] `<time>`: timestamp, sleep, monotonic clock
- [x] `<env>`: environment variables and CLI arguments

**Phase 2: Build on Phase 1**
- [x] `<eventloop>`: general-purpose reactor over epoll (Linux) / kqueue (macOS). A shared foundation for async/await, HTTP, and the future UI framework, designed from the start to dispatch any kind of event (sockets, frames, input), not just network I/O
- [x] **Atomics** (`<atomic>`): `atomic_load`/`store`/`swap`/`cas` lowering to LLVM atomics; async step 0 (the lock-free `Future` state handshake). See the `intrinsic` qualifier.
- [x] **Escaping closures**: escape analysis (non-escaping → stack, escaping → heap), the `escaping` parameter qualifier (compiler-enforced soundness), and `free_closure`. Prerequisite the de-risk gate surfaced: async wakers/callbacks are escaping closures. See `docs/dev/async-design.md`.
- [x] Async runtime foundation: `Future<T>` (`<future>`), `Executor` (`<executor>`), leaf futures (`<net_async>`). De-risk gate COMPLETE: reactor read, cancel, and cross-thread resume all validated.
- [x] `async` / `await`: `sema/async_transform.cpp` lowers an async function to a frame + N+1-state resume if-chain + constructor. Single + multiple awaits, `return await`, bare `await`, `async void`, cancellation, **all control flow around await** (`if`/`while`/C-style `for`/`switch`/`for-in`, with `break`/`continue`), and full closure-env ownership, all done and **leak-free** (verified with `leaks`). Combinators (`spawn`/`select2`/`join2`, generic + cast-free) and a `<timer>` leaf future build on it. Design in `docs/dev/async-design.md`.
- [x] `<http_async>`: non-blocking, **concurrent** HTTP/1.1 over the event loop: an async accept loop that `spawn`s a detached handler per connection (a `<channel>` wait-group joins them for clean shutdown)
- **Async polish** (builds directly on the async stack):
  - [x] **Concurrent `<http_async>`**: the accept loop `spawn`s a detached handler per connection and goes straight back to accepting, so a slow request never blocks the others. A `<channel>` wait-group makes the server future complete only after every handler finishes, so bounded shutdown stays clean. Test: `http_async_concurrent` (3 simultaneous clients).
  - [x] **Async examples**: `examples/async_combinators.esk` showcases `select2`/`join2`/`timer_after`/`spawn`.
  - [ ] **Tighter locals-across-await liveness** *(deferred: optimization, not a fix)*. Only hoist a local to the frame struct if it is live across an `await`; non-crossing locals could stay stack temporaries (smaller frames). Deliberately deferred: the current "hoist all locals" is correct and leak-free; the optimization needs a sound dataflow liveness pass over branches/loops where any error miscompiles a coroutine (a crossing local left on the stack), for a marginal frame-size win. Worth doing only with a proper liveness pass + fuzzing, not speculatively.
- [x] **Channels** (`<channel>`): an async, bounded message channel integrated with the `Future` model: `chan_recv(ch)` is a `*Future<T>` that completes when an item is available, `chan_send(ch, v)` enqueues or hands off directly to a parked receiver. v1: bounded ring, single outstanding receiver, non-blocking send; generic and leak-free. (Multi-consumer / async backpressure-send and cross-thread atomics are later additions.)
- [x] **Sum types (algebraic enums + `match`)**: payload-bearing `enum` variants make the enum a tagged union; variants are constructed by name and destructured with `match` (binds payload fields per arm). **Complete**: concrete + **generic** payload variants (`Option<T>`/`Either<A,B>`, monomorphized per instantiation; generic construction via explicit type args `Some<int>(5)`), `match` with payload binding + `_` default + **exhaustiveness** (missing-variant / duplicate-arm errors); classic int enums unchanged. Tests: `enum_adt`, `enum_generic`, `errors/match_nonexhaustive`.
- [x] **Generic value-returning `select`/`join`** (`<futureval>`): `select2v<A,B>` resolves with the winner's value as `Either<A,B>` (loser dropped); `join2v<A,B>` resolves with both as a `Pair<A,B>`. Built on generic algebraic enums; leak-free. Tests: `select_value`, `join_value`.
- [x] `<http2>`: HTTP/2 (RFC 7540) over the event loop (see `docs/dev/http2-design.md`). Complete: the 9-byte frame-header codec, frame-type + flag constants, and connection preface; the connection lifecycle (SETTINGS/ACK, PING/PONG, GOAWAY) over async reads; HPACK (`<hpack>` + `<hpack_huffman>`, RFC 7541: static + dynamic table, prefix integers, string literals, and RFC-generated Huffman); the per-stream state machine with credit-based flow control and the HEADERS/DATA/WINDOW_UPDATE/RST_STREAM codecs; the multiplexed server API (`<http2_server>`, `http2_serve_async`) reusing `<http>`'s request/response types; and TLS/ALPN (`<tls>`, libssl by FFI) selecting `h2`, in both blocking thread-per-connection and async single-thread server flavours. Verified end-to-end against `curl --http2`. Depends on `<eventloop>` for non-blocking multiplexing
- [x] `<string>`: `split` (List + streaming token iterator), `trim`, `starts_with`, `ends_with`
- [x] `<path>`: path manipulation (join, basename, dirname, extension, is_absolute)

**Phase 3: Ergonomics and tooling**
- [x] `for (i in 0..10)`: native half-open ranges `[A, B)`. Desugared at parse time to a counted `for`, so it reuses all the loop machinery (codegen, async transform, break/continue). Lexer gained a `..` (`RANGE`) token. Test: `range_for`
- [x] User-defined variadic functions: `int f(int n, ...)` read with `va_list` / `va_start` / `va_arg<T>` / `va_end` (LLVM `va_arg` instruction; works on arm64 + x86-64). Test: `variadic`
- [x] Pointer constness: `const int*` (pointer to const: pointee read-only, pointer rebindable) vs `int* const` (const pointer: binding read-only, pointee writable), composable as `const int* const`. Reading through and rebinding a `const T*` are allowed; writing through it, and any conversion that drops a const qualifier (init, assignment, argument, return), are rejected. const has no ABI effect (stripped in codegen). Tests: `pointer_const`, `errors/const_ptr_write`, `errors/const_ptr_drop`
- [x] `__FILE__` and `__LINE__` in the preprocessor: `__LINE__` refreshed per line, `__FILE__` threaded from the compiled/imported path. Test: `pp_loc`
- [x] `#error` directive: aborts compilation with the message (respects `#ifdef` branches). Test: `errors/pp_error`
- [x] `#pragma pack(N)` with N > 1: caps each field's alignment at N (manual padded layout matching the C `#pragma pack(N)` ABI; total size rounds up to the struct's alignment). Test: `pack_n`
- [x] `-Wextra`: signed/unsigned comparison mismatches (off by default; layers on top of `-Wall`)
- [x] Shebang support: a leading `#!/usr/bin/env eskiuc run` line is ignored by the preprocessor (line numbers preserved), so a `.esk` file can be `chmod +x`'d and run directly. Test: `shebang`
- [ ] Package manager: dependency resolution, registry, build integration *(deferred: revisit when a real dependency need appears)*
- [x] `eskiuc run file.esk [args...]`: compile to a temporary executable, run it (forwarding `[args...]`), and delete it; the program's exit code is propagated. Flags go before the script (`eskiuc run --asan f.esk`), program args after
- [x] `eskiuc fmt [--check] file.esk …`: conservative, comment-preserving reindenter: normalizes leading indentation (4 spaces per `{`-level), strips trailing whitespace, collapses blank-line runs, ensures a final newline; preserves each line's content (operators, spacing, comments, strings) verbatim, so it is idempotent and can never change a program's meaning. `--check` writes nothing and exits non-zero if any file would change (CI). Guarded by a formatter-idempotency pass over every test
- [x] `--asan` / `--ubsan` as first-class flags: real instrumentation via the LLVM pass manager: `--asan` runs AddressSanitizer (memory errors) and links the matching compiler-rt runtime; `--ubsan` inserts trapping bounds checks (no runtime). Both compose with `eskiuc run`

**Documentation**
- [x] Formal, complete BNF grammar: `docs/lang/grammar.md` (EBNF: lexical structure, preprocessor, declarations, types, statements, full expression precedence), derived from the parser
- [x] Documented ABI: `docs/dev/abi.md` (scalar/pointer lowering, const has no ABI effect, struct/packed/bitfield/union layout, ADT tagged unions, sret >16 B rule, varargs + `va_list`, fat pointers for closures/interfaces, template mangling)
- [x] `__FILE__` / `__LINE__` reference: expanded the predefined-macros section of spec.md §18 (per-line `__LINE__`, per-file `__FILE__`, OS + freestanding macros, shebang interaction)

### Post-0.2.0 hardening / reinforcement (v0.2.1 – v0.2.5)

With the backend-services stack shipped, the theme shifted from new features to
**reinforcing the language**: hardening, maintainability, and type soundness.
Feature freeze on the surface language; the work below is internal robustness
plus a few generics extensions.

**v0.2.1**
- [x] `<bytes>` and `HashMap<K,V>` stdlib types
- [x] Source modularization: split the monolithic codegen into `codegen/codegen_{module,type,scope,decl,stmt,expr,call,closure,adt}.cpp` (and similar splits for sema/parser/lexer/main)
- [x] **asan/ubsan CI gate**: `--asan` / `--ubsan` run on the test corpus in CI
- [x] Miscompile fixes surfaced by the new gates

**v0.2.2**
- [x] **Bounded generics**: `<T: Iface>` and multi-bound `<T: A + B>` constraints, checked at instantiation
- [x] **Generative + mutation fuzzer**: `tests/fuzz/eskiu_fuzz.py` with an **O0-vs-O2 differential oracle** (divergence between optimization levels flags a miscompile), wired into CI

**v0.2.3**
- [x] **Primitives satisfy constraints**: a primitive type can satisfy a bounded-generic constraint via a free function (not only a struct method)
- [x] **`ty::Type` IR**: a structured type representation in `sema/type.{h,cpp}` (`parse` / `str` / `substitute` / `nominalName`); behavior-preserving soundness foundation, gated by the golden-IR oracle `tests/type_zoo/snapshot.sh` + `tests/type_zoo/golden/`

**v0.2.4**
- [x] **Single-resolver type unification**: the type checker is the one type resolver: it produces a per-expression `ty::Type` table that codegen consumes (codegen no longer re-derives expression types). `getTypeFromString` dispatches on `ty::Type::parse`, the single grammar interpreter shared by both phases. This closed the two-evaluator miscompile risk and fixed three latent miscompiles (float-literal `double`, pointer-deref width, `char` zero-extension)

**v0.2.5**
- [x] **Preprocessor fix (correctness)**: a `//` (or `/* */`) comment ending in `\` no longer triggers backslash-newline line continuation, which silently spliced the next source line into the comment (eating a `return`/`else`/statement, no diagnostic). Found dogfooding the self-hosted lexer; regression test + spec note
- [x] **Self-hosting milestone 1: the lexer in Eskiu** (`selfhost/`): byte-identical to `--test-lexer` over the whole preprocessor-free corpus (114/114), gated in CI; the parser (milestone 2) is underway (expression layer done)
- [x] **Hardening**: `ESKIU_RESOLVER_DEBUG` table-vs-derivation consistency oracle (confirms the v0.2.4 single-resolver is sound across the corpus); fuzzer generators for backslash-newline in comments/strings

### v0.3.0: Self-hosting (SHIPPED)

The whole compiler is reimplemented in Eskiu, validated against the C++ `eskiuc` and
against itself. Code generation emits **textual LLVM IR** (assembled + linked by `clang`),
not LLVM-C bindings: keeps the self-host dependency-free and is the standard bootstrap path.

- [x] Lexer rewritten in Eskiu: byte-identical to `--test-lexer` over the whole
  preprocessor-free corpus (114/114), gated in CI (`selfhost/`)
- [x] Parser rewritten in Eskiu (`selfhost/parser.esk`): the full grammar
  (expressions, statements, declarations, templates/generics, lambdas/async),
  byte-identical to `--test-parser`; **follows `import`** (resolves `<stdlib>` +
  relative paths recursively, merges decls, now preprocessing each imported file) →
  corpus parity 51/51 over files with a preprocessor-free import closure, CI-gated
- [x] Preprocessor rewritten in Eskiu (`selfhost/preprocessor.esk`): `#define`/
  `#undef` (object + function), `#ifdef`/`#ifndef`/`#else`/`#endif`, `#pragma`,
  `#error`, line splicing, recursive expansion, `__FILE__`/`__LINE__`. Validated
  through the lexer: byte-identical to `--test-lexer` over the whole `tests/` +
  `stdlib/` corpus (156/156, no exclusions), CI-gated
- [x] Type checker rewritten in Eskiu (`selfhost/sema.esk`): two-pass name resolution
  + a string-based type layer catching **all 19** semantic error classes. Verdict
  matches `--test-typechecker` on 121/121 positive corpus files (0 false rejections);
  every sema negative test rejected with the right diagnostic. CI-gated (`tc_parity.sh`)
- [x] **Code generator rewritten in Eskiu** (`selfhost/codegen.esk`): textual LLVM IR.
  Behavioral oracle: emit `.ll` → `clang` → run, compared to the C++-built binary
  (`cg_parity.sh`, CI-gated). **Feature-complete against the C++ corpus**: a full feature
  sweep is clean (50/50). Beyond the bootstrap subset it covers floats, `switch`, ADT enums
  + `match` (incl. generics + payloads wider than one slot), closures, exceptions (Itanium
  ABI), atomics, generics + argument inference, async/await, unions, bitfields, interfaces
  (dynamic dispatch), type aliases, function-as-value, packed structs, user variadics +
  `va_*`, `alloc_with`/`thread_*` builtins, and the `?` operator
- [x] **Self-compilation reached (Phase D)**: the self-hosted codegen emits valid IR
  for the *entire* self-hosted compiler, and `cg_main` compiled by itself reproduces
  the C++-built codegen's IR byte-for-byte (a **bootstrap fixpoint**). All five drivers,
  self-compiled, match the C++-built ones. Gate `cg_selfhost.sh`, CI

### v0.3.1: Correctness & optimization (SHIPPED)

A hardening release over the self-hosting milestone. No new language surface.

- [x] `-O0`/`-O1`/`-O2`/`-O3` optimization levels: `-O1`+ run the LLVM middle-end (mem2reg/SROA/instcombine/inlining/GVN) before code generation; `-O0` (default) unchanged.
- [x] Fixed a float-closure `-O2` miscompile: sema reconciles a lambda's return type with its target `fn(...)->R` (a mismatched header returned `0.0` under `-O2`).
- [x] Reject incompatible `fn`-type assignments (an `fn(P)->R1` value into an `fn(P)->R2` slot was a silent ABI miscompile; now a compile error).
- [x] `*T[N]` parses as an array of pointers; a pointer to an array is `T[N]*`.
- [x] libc `size_t` externs (`memcpy`/`memset`/`memmove`/`memcmp`/`memchr` size arg, `strlen` return) use `int64`.
- [x] New CI gate `tests/opt_differential.sh`: a `-O0`-vs-`-O2` behavioral differential over the whole corpus, catching optimization-path miscompiles.
- [x] Parser self-host parity widened to the full corpus (51 → 121 files).

### v0.4.0: Correctness & type-strictness (SHIPPED)

A four-front bug hunt (behavioral differential, sema soundness, synthesized-default audit,
feature edges) across the C++ and self-hosted compilers, plus a tighter type system.

- [x] Miscompiles: integer literal in [2^31, 2^32) truncated to i32; `unsigned`→`float` used a signed conversion; a template (generic) function could fall off the end; a catch-less `try`/`finally` aborted on an in-flight exception; a lambda capturing a variable two scopes up referenced another function's value.
- [x] Crash/reject-valid: comparison operators now type-check operands; `void`/`string`→`int` init is an error (was a warning that hung/crashed); `float` vs `double`/`int` comparison compiles (operands promoted); `&const` yields a pointer to const.
- [x] Type strictness: floating-point→integer needs an explicit cast (integer/float-width narrowing stays implicit, C-style); out-of-range integer literal, division/remainder by a literal zero, and a proven-out-of-bounds constant array index are errors.
- [x] Flow analysis: reading an uninitialized scalar local, returning the address of a local (dangling), a function redefinition, and falling off the end of a non-void function are errors.
- [x] Self-hosted back-end fixes: `!`/`~` (were no-ops); hex/octal + `>2^63` literals; named-const array dims; exception propagation through catch-less `finally` and cross-function rethrow; async `for-in` over a generic `List<T>`.
- [ ] Self-host sema parity for the new checks (comparison typing, narrowing, flow analysis): deferred to the promotion track (`selfhost/PROMOTION_PLAN.md`). The shipped C++ compiler carries all checks.

### v0.5.0: Basic-C surface completion (SHIPPED)

Fills the last common C constructs the language was missing, so idiomatic C ports compile
without workarounds. Each landed in lockstep across the C++ and self-hosted compilers,
with parity gates (behavioral + byte-exact printer) green including the bootstrap fixpoint.

- [x] `do`/`while` loops (`DoWhileStmt`): body runs once before the condition is tested.
- [x] Prefix and postfix `++`/`--` (`IncDecExpr`) on integer and pointer lvalues.
- [x] Array-literal initializers `int[N] a = { ... }` (`ArrayLitExpr`): C-style zero-fill for a short list, over-long list rejected.
- [x] `static` locals: one instance persisting across calls, emitted as a private module global; initializer must be constant, `static` on a global rejected.
- [x] Multidimensional arrays `T[N][M]` in C order (leftmost bracket outermost) with nested initializers; each index bounds-checked against its own dimension.
- [x] Ternary conditional `cond ? a : b` (`TernaryExpr`): one arm evaluated, arms take a common type; disambiguated from postfix `?` propagation by a same-level `:` scan.
- [x] Fix: `switch` on a sub-`int` subject (widen subject + case constants to a common type).

### v0.6.0: Memory safety + standard library (SHIPPED)

A Zig-flavored safety pass: compile-time checks and opt-in runtime guards that fit the
C-faithful, arena-first model (no borrow checker, ownership, or GC), plus a batch of
practical stdlib modules. Each landed as granular per-layer commits.

- [x] `defer` (`DeferStmt`): run a statement at enclosing-block exit, LIFO, on every path out (fall-through, `return`, `break`, `continue`, propagated `?`). Codegen cleanup-stack mechanism (not a desugar); also fixed a latent `finally`-skipped-on-early-return bug. Both compilers.
- [x] `errdefer`: the error-only variant, runs only when the function exits through a propagated `?`. Both compilers.
- [x] Slice type `T[]` (fat pointer `{ptr, i64}`): `a[lo..hi]` construction over an array, `s[i]` read/write aliasing the backing store, `s.len`, and slice `for-in` (C++ only; self-host has no collection `for-in` yet). Remaining slices: slice-of-slice, returning slices across functions, string interop.
- [x] `must_use` function qualifier: the compiler rejects a call whose result is discarded; `stdlib` `alloc` is marked `must_use` so a forgotten allocation is a compile error. Both compilers.
- [x] `--safe` build mode (C++ only): opt-in runtime bounds check on array and slice indexing, trapping on violation; off by default so release builds pay nothing. Self-host mirror on the promotion track.
- [x] Checked nullable pointer `?*T` (C++ only): bare `*T` stays C-nullable, `?*T` cannot be dereferenced/indexed/membered until proven non-null; `if (q != null)` narrows it; `*T` widens to `?*T` but not the reverse. Lowered as a bare pointer (zero runtime cost). Self-host mirror on the promotion track.
- [x] Stdlib: `<random>` (xoshiro256\*\* PRNG), `<regex>` (Thompson-NFA / Pike VM with capture groups, linear-time), `<sort>` (generic heapsort + binary search), `<url>` (RFC 3986 percent-encoding + query parsing), `<uuid>` (RFC 4122 v4), and a UTC civil calendar in `<time>` (`DateTime`, `time_to_utc`/`time_from_utc`, ISO 8601 formatting).

### v1.0: Production-ready

- [~] `eskiuc` compiles itself (self-hosting): **3-stage bootstrap fixpoint reached.**
  The unified driver `selfhost/esk_main.esk` (pp→parse→sema→codegen) is built by the C++
  eskiuc (cc0), then by cc0 (cc1), then by cc1 (cc2); cc1 ≡ cc2 emit identical IR for the
  compiler's own source, a true self-hosting fixpoint (`cg_bootstrap.sh`, CI). The
  self-hosted codegen is also feature-complete against the C++ corpus (shipped in v0.3.0).
  Remaining for v1.0: a package manager, and promoting the Eskiu-written compiler to the
  primary build (it currently rides alongside the C++ `eskiuc`): staged, parity-gated
  plan in `selfhost/PROMOTION_PLAN.md`. Progress: the keyword-as-identifier diagnostic
  mirror (R3) and the driver stages P0 (native `-o` link via clang) and P1 (full CLI
  parity: all `--test-*` modes, `--version`, `--asan`/`--ubsan`, and multi-file input
  dispatch through `esk_main`) are done; every parity gate drives through the unified
  driver. Open: `run`/`fmt` parity (P2), whole-corpus behavioral equivalence (P3), the
  flip (P4), and the async `for-in` element-typing refactor (R1). Parse-parity corpus
  expansion shipped in v0.3.1.
- First-class types for high-throughput services

---

## Platform support (future track, not scheduled)

Eskiu runs natively on **macOS arm64** and **Linux x86-64**, and as of **v0.7.0**
cross-compiles to **32-bit ARM** (hard-float `armv6k` for the Nintendo 3DS, verified on
device) and emits **Windows x86-64** COFF objects (see
[`cross-compile.md`](cross-compile.md)). Broadening the *host* toolchain to Windows, and
reaching Android and iOS, is a separate long-horizon track, independent of the version
milestones above. The LLVM backends already cover these targets, so the real work is
linking, per-platform stdlib branches, and toolchain/distribution integration, not codegen.

**Cross-cutting (shared by all platforms)**
- [x] COFF object emission for Windows targets (shipped v0.7.0; Mach-O and ELF already worked)
- [ ] Per-target linking: emit `.o`/`.a`/`.so` and hand off to the platform's native linker/SDK (the Unix `cc`/`clang` auto-link does not cross platforms)
- [x] Predefined OS macros: `_WIN32` / `_WIN64` ship in v0.7.0 (`--target …-windows-…`); `__ANDROID__` and iOS still pending, extending the existing `__APPLE__` / `__linux__`
- [ ] stdlib platform branches where the API differs (sockets, threads)
- [ ] CI runners: Windows, plus NDK and Xcode for mobile

**Windows (host + target)**
- [ ] Build `eskiuc` on Windows (LLVM + CMake, MSVC or MinGW)
- [ ] Windows linker integration (`link.exe` / `lld-link`)
- [ ] `<net>` over Winsock2 (`WSAStartup`, `closesocket`, link `ws2_32`)
- [ ] `thread_create`/`thread_join` over Win32 threads (no native pthreads)

**Android (native library via JNI)**
- [ ] Cross-compile to `aarch64-linux-android` against the NDK (bionic libc)
- [ ] Build as a `.so` with JNI entry points, callable from Kotlin/Java
- [ ] APK packaging via Gradle/NDK (`<net>` and threads work as on Linux)

**iOS (static library via C ABI)**
- [ ] Cross-compile to `arm64-apple-ios` against the iOS SDK (Xcode, macOS only)
- [ ] Ship a static `.a` linked into an Xcode project, called from Swift/Obj-C by the C ABI
- [ ] Code signing and App Store constraints (AOT only, no JIT)

**Distribution model.** On mobile, Eskiu ships as the native compute core
(`.a`/`.so`) called from Swift/Kotlin over the C ABI, not as a standalone app. A
standalone Eskiu app with its own UI depends on the future UI framework (see the
`<eventloop>` note) and is out of scope for this track.

---

## HTTP/3 (future track, not scheduled)

HTTP/3 runs over **QUIC**, a UDP-based transport that bundles its own congestion
control, loss recovery, stream multiplexing, and mandatory TLS 1.3, plus QPACK
header compression. Implementing QUIC from scratch is a project on the scale of
the rest of the stdlib combined; no serious stack does it by hand.

- [ ] `<http3>`: bind an existing QUIC library (quiche / ngtcp2 / msquic) over the C ABI via `extern`, then layer HTTP/3 framing on top

Not scheduled, and gated on `<http2>` landing first. In practice it is rarely
needed at the application layer: a reverse proxy (nginx, Caddy, Cloudflare)
terminates HTTP/2 and HTTP/3 at the edge and speaks HTTP/1.1 (or HTTP/2) to an
Eskiu backend, so the `<http>` we have already benefits from h2/h3 on the wire.
