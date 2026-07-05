# Changelog

All notable changes to Eskiu are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow `MAJOR.MINOR.PATCH-stage` (e.g. `0.0.9-alpha`).

---

## [Unreleased]

### Fixed
- **Comparison operators did not type-check their operands.** `==`, `!=`, `<`, `>`,
  `<=`, `>=` were accepted for any pair of types (pointer vs int, struct vs struct,
  string vs int), so the checker reported success and codegen emitted a malformed
  comparison (an assertion under an assertions build, a miscompile otherwise). They
  now require mutually comparable operands: both numeric, both pointer-like (including
  `null`), or the same non-aggregate type. Enum, pointer, and `null` comparisons are
  unaffected.
- **Incompatible non-numeric variable initializers were only a warning.** `int x =
  f();` where `f` returns `void` let a non-value into codegen and hung the back-end;
  `int x = "hello";` crashed at run time. Both are now compile errors, matching the
  assignment path.
- **Taking the address of a `const` produced a mutable pointer.** `const int N; *int
  p = &N; *p = 99;` silently mutated `N`. `&` of a const location now yields a pointer
  to const, so writing through it (or assigning it to a plain pointer) is rejected.
- **`float` compared against `double` or `int` failed to compile.** The comparison
  operators did not promote a mixed float/int (or float/double) pair to a common
  float type, so LLVM rejected the `fcmp`. They now promote like the arithmetic
  operators do.
- **Integer literals in [2^31, 2^32) were truncated to i32 and sign-extended.** A
  decimal literal such as `3000000000` assigned to an `int64` materialized as a
  32-bit constant (high bit set), then sign-extended to a negative value
  (`-1294967296`). The codegen now widens any literal that does not fit a *signed*
  i32 to i64 (matching the self-host), so large literals keep their value.
- **`unsigned -> float` conversions used signed `SIToFP`.** A high-bit-set unsigned
  value (for example `uint32 4000000000`) converted to a negative float. Integer to
  float conversion now selects `UIToFP` vs `SIToFP` by the source's signedness, in
  both back-ends (routed through a single `intToFloat` helper on the C++ side).
- **A generic (template) function could fall off the end without returning.** The
  missing-return analysis skipped template bodies, so a generic like
  `T pick<T>(T a, int c) { if (c > 0) { return a; } }` compiled (returning a synthesized
  zero under the C++ back-end, crashing under the self-host). The definite-return check
  now runs on template bodies too.

### Changed
- **Falling off the end of a non-void function is now a compile error.** A non-void
  function that returned on no path still compiled before: the C++ back-end
  synthesized an implicit `ret 0`/`ret null`, so `int add(int a, int b) { int r = a
  + b; }` silently returned 0 rather than the computed value, and the last
  expression was never the result. The self-hosted back-end emitted `unreachable`
  for the same code, so the two compilers disagreed and the self-host build crashed
  (SIGILL) on a program the C++ build ran cleanly. Both now reject it in sema with
  `missing return in non-void function`, via a definite-return analysis. `void`
  functions may still fall off the end, `async` functions complete their future
  implicitly, and a body that provably cannot fall through needs no trailing
  `return` (an `if`/`else` where both branches return, an exhaustive `switch`/`match`
  where every arm returns, or an infinite `while (1)` with no `break`).

---

## [0.3.1], 2026-07-01

### Fixed
- **`*T[N]` now parses as an array of pointers, not a pointer to an array.** The
  type-string parser (`ty::Type::parse`) peeled a leading `*` before the trailing
  `[N]`, so `*Node[7]` became a *pointer to* `Node[7]` and lowered to a single
  opaque `ptr`, while codegen's `IndexExpr` lowering assumed the array reading.
  The two disagreed: indexing such a value emitted an invalid two-index GEP into a
  scalar pointer, silently corrupting locals and crashing the compiler outright on
  a module-level array (a constant-folded GEP tripped an LLVM assertion). The
  trailing `[N]` now binds outermost, so `*Node[7]` is an array of 7 pointers
  consistently across the type checker and codegen; a pointer *to* an array stays
  spellable with a trailing star (`Node[7]*`). Found porting a C program whose
  central data structure was a module-level `Actividad *agenda[7]`.
- **`<eventloop>`: initialize the `on_read` closure of every fd slot.** `el_new`
  zeroed `active`/`gen`/`isWrite` but left the `on_read` fat pointer as `alloc`
  garbage. Dispatch is guarded by `active`, so this was latent, but any stray read
  would invoke a garbage function pointer, a SIGILL on Linux/x86-64 (fresh
  allocations aren't zero there as they happen to be on macOS). Now defaulted to a
  no-op. Defensive hardening for the intermittent HTTP/2-test SIGILL under
  investigation (see the `project-flaky-http2` note).

- **A lambda's return type is reconciled with its target `fn(...)->R` type.** Assigning
  a lambda whose header return type disagrees with the declared closure type (e.g.
  `let f: fn(int)->float = int(int x) { return (float)x * k; }`) emitted a function
  returning the *header* type (`int`): an `fptosi` truncation plus an int/float return-
  register mismatch against the closure's call ABI. It was correct at `-O0` by luck but a
  silent `0.0` miscompile under `-O2`. Sema now sets the lambda's return type to the
  target's `R` so its `return` coerces through the normal path. Surfaced by an `-O0`-vs-`-O2`
  differential over the whole test corpus (`closures.esk` was the only divergence).
- **Incompatible function-type assignments are now rejected.** Assigning a function
  value to a `fn(...)` slot with a different signature (e.g. an `int`-returning function
  to a `fn(int)->float`) was silently accepted: a fn value has a fixed call ABI (param
  and return registers), so there is no implicit adapter, and reinterpreting it
  miscompiled (wrong even at `-O0`, `0.0` under `-O2`). `isValidAssignment` now requires
  fn types to match exactly, and a mismatched fn initializer is a hard error rather than a
  warning. Lambda literals in a `let x: fn(...)->R = ...` still coerce (their return type
  is malleable); only non-adaptable fn *values* are rejected. Test `errors/fn_return_mismatch`.

### Changed
- **New CI gate: `-O0`-vs-`-O2` behavioral differential** (`tests/opt_differential.sh`).
  Compiles the whole corpus at both levels and fails on any exit/stdout divergence,
  guarding against optimization-path miscompiles now that `-O` exists (it caught the
  float-closure bug above). The intermittent HTTP/2 async tests are excluded to keep the
  gate deterministic.
- **Self-hosted parser parity now covers the full corpus (51 → 121).** `parse_main`
  preprocesses the top-level file (matching how the C++ `--test-parser` folds
  preprocessing into the lexer), so `parse_parity.sh --full` no longer excludes
  files whose import closure touches the preprocessor (`#ifdef`/`#define`/`__FILE__`/
  `__LINE__`/shebang). A prerequisite for promoting the Eskiu-written compiler
  (see `selfhost/PROMOTION_PLAN.md`, R2).

---

## [0.3.0], 2026-06-29

The self-hosting milestone: the whole compiler (lexer, preprocessor, parser, semantic
analyzer, and code generator) is reimplemented in Eskiu (`selfhost/`), reaches a 3-stage
bootstrap fixpoint, and the self-hosted codegen is **feature-complete against the C++
corpus** (a full feature sweep is clean). All parity/self-host/bootstrap gates are CI-wired.

### Fixed
- **`&&` and `||` now short-circuit.** Code generation evaluated both operands eagerly
  and emitted a logical-and/or, so the right-hand side always ran; a guarded
  dereference like `p != null && p.field` could fault. They now lower to a conditional
  branch + PHI, evaluating the RHS only when the LHS doesn't decide the result. Found
  dogfooding the self-hosted type checker. Regression test `tests/short_circuit.esk`.
- **`--test-parser` no longer crashes on a forward-declared function.** The AST
  printer dereferenced a null `FunctionDecl::body` (a prototype like `int f(int);`),
  segfaulting `eskiuc --test-parser`. It now omits the `Body:` section for a
  body-less function (as `ReturnStmt` already does for a null value). Found
  dogfooding the self-hosted parser. Affected only the debug printer, not codegen.
- **C `size_t` externs now use `int64`, not `int`.** The `<mem>`/`<string>` declarations
  for `memcpy`/`memset`/`memmove`/`memcmp`/`memchr` (size argument) and `strlen` (return)
  were `int` (i32). That is an incorrect ABI on LP64 targets and truncates sizes above
  4 GB. They are now `int64`, matching `size_t`. Behavior is unchanged for the common
  sub-4 GB case (the value zero-extends into the argument register either way).

### Added
- **`-O` optimization levels.** `eskiuc -O1`/`-O2`/`-O3` now runs the LLVM middle-end
  pipeline (mem2reg/SROA/instcombine/inlining/GVN/...) over the module before code
  generation. `-O0` (the default) keeps the prior behavior: naive IR straight to the
  backend. On real code this collapses the per-local stack traffic the front-end emits
  (the `ine_decoder` demo drops from 514 allocas to 25 at `-O2`); its Makefile now builds
  with `-O2`.
- **Clearer diagnostic for a keyword used as a name.** Using a reserved word (`fn`, `in`,
  `match`, a type name, ...) as a variable, parameter, or field name now reports
  `expected a name, found keyword 'fn'` at the cause, instead of a misleading downstream
  error (`Expected ';'`, `Expected expression`). This was the most recurring self-host
  papercut. The self-hosted parser mirror is tracked in `selfhost/PROMOTION_PLAN.md` (R3).
- **`String_free` clears `data`.** It sets `self.data = null` after `free`, so a reused or
  doubly-freed `String` can no longer hand a dangling pointer to `free`.
- **`String` length/capacity are now `int64`.** `%String` went from `{ ptr, i32, i32 }` to
  `{ ptr, i64, i64 }`; the length/index/capacity arithmetic inside `<string>` is `int64`
  throughout. Matches `size_t`, removes the 2 GB object-size cap, and avoids width casts at
  the libc boundary. Public accessors that return a length (`String_len`, `String_index_of`)
  now return `int64`; callers that store into an `int` truncate exactly as before.
- **Self-hosted codegen is now feature-complete against the C++ corpus.** A systematic feature
  sweep (every feature-bearing `tests/*.esk` through `cg_parity.sh`) is clean: each program,
  compiled by the Eskiu-written codegen, runs identically to the C++ build. Closing it required
  11 root-cause fixes the sweep exposed (the bootstrap fixpoint had only ever exercised the
  subset the compiler's own source uses): var-decl/struct-init coercion + signedness-aware
  integer widening; full integer semantics (binary-op operand-width unification, unsigned
  `udiv`/`lshr`/`icmp`, >32-bit literals as `i64`, vararg small-int promotion); type-alias
  resolution; function-as-value decay (a `__fnptr` env-dropping thunk + `{thunk,null}` closure,
  or a bare `@fn` for a pointer cast); generic ADT enum monomorphization; a self-host *parser*
  fix (a parenthesized struct literal `(P{…})` was mis-parsed as a cast); primitive constraint
  dispatch (`a.m(b)` on a primitive → `m(a,b)`); the `alloc_with`/`thread_create`/`thread_join`/
  `free_closure` builtins; user-defined variadic functions + `va_list`/`va_start`/`va_arg<T>`/
  `va_end`; packed structs (`packed` / `#pragma pack(N)`); and the `?` error-propagation
  operator. All parity/self-host/bootstrap gates stay green throughout.
- **Self-hosted codegen: ADT payloads wider than one slot.** An ADT enum's tagged-union
  payload area (`{ i32, [N x i64] }`) is now sized by **bytes with field alignment**
  (`cg_layout_size`, mirroring the C++ DataLayout), not by field count, so a variant
  carrying a struct-by-value larger than 8 bytes (e.g. `Line(Vec3)` where `Vec3` is 12B)
  fits instead of overflowing its slot. Construction + `match` extraction already viewed
  the payload as the variant's `{ fields }` struct, so only the area sizing needed the fix.
  Test `adt_big_payload`. cg_parity 56/56. (A subsequent feature sweep found this was NOT
  the last gap; see below.)
- **Self-hosting feature coverage audited + closed out.** Verified the async combinators
  `select2`/`join2`/`spawn` all work through the self-hosted codegen (added `async_select`/
  `async_spawn` to the parity corpus). The self-hosted drivers (`esk_main`/`cg_main`) now
  route diagnostics to **stderr** (an `eprint` helper over `write(2, …)` + `sprintf`) so an
  error can never contaminate the `.ll` text on stdout. cg_parity 55/55, cg_selfhost 68/68,
  bootstrap fixpoint. (NOTE: a later systematic feature sweep, pushing the C++ test corpus
  through the parity oracle, showed self-host codegen is NOT yet feature-complete; ~8
  root-cause gaps remain, tracked in `selfhost/BACKEND_PLAN.md`. The bootstrap only exercises
  the subset the compiler's own source uses, so it missed them.)
- **Self-hosted codegen: more of the language.** Beyond the bootstrap subset, the
  self-hosted code generator (`selfhost/codegen.esk`) now also lowers: **floating point**
  (`fadd`/`fsub`/`fmul`/`fdiv`, `fcmp`, `fneg`, and int↔float casts via `sitofp`/`fptosi`/
  `fpext`/`fptrunc`, with mixed int/float promotion); **`switch`** (a real LLVM `switch`
  with C-style fall-through + `break`); and **ADT enums + `match`** (the tagged-union
  layout `{ i32 tag, [N x i64] payload }`, variant construction, and `match` lowered to a
  tag switch with payload bindings). Each is behaviorally parity-tested (`cg_parity.sh`).
  Return values are now coerced to the function's return type (e.g. a `bool` result
  zero-extended to `int`). It also lowers **closures/lambdas** (free-variable capture into
  a stack env + a fat pointer `{ fn, env }`, higher-order functions, indirect calls) and
  **exceptions** (the Itanium ABI: `invoke`/`landingpad`, `__cxa_allocate_exception`/
  `__cxa_throw`/`__cxa_begin_catch`, type-name `strcmp` catch matching, `finally`; programs
  with `throw`/`try` link `-lc++abi`) and **atomics** (`atomic_load`/`store`/`swap`/`cas` →
  `load atomic`/`store atomic`/`atomicrmw xchg`/`cmpxchg`). The self-hosted compiler
  reproduces its own IR throughout (bootstrap fixpoint stays green).
- **Self-hosted codegen: unions, bitfields, and dynamic trait dispatch.** The
  self-hosted code generator now also lowers: **unions** (every member overlaps at
  offset 0; the type is `{ [N x i8] }` sized to the largest member, member access GEPs
  to the shared storage); **bitfields** (declared widths packed into `i32` words:
  reads `lshr`+`and`+optional `sext`, writes read-modify-write the word with a cleared
  mask, struct-init fills bit slots, and a normal field in a bitfield struct uses its
  real word index); and **interfaces / dynamic trait dispatch** (an interface value is a
  fat pointer `{ data, vtable }`; passing a struct pointer where an interface is expected
  **boxes** it, building a per-`(interface, struct)` vtable global of the struct's method
  implementations, and a call through an interface value dispatches by loading the method
  pointer from its vtable slot and calling it with `data` as the implicit receiver). Each
  is behaviorally parity-tested; the bootstrap fixpoint stays green.
- **Self-hosting async: all 19 async tests pass.** `for-in` over an await now lowers too
  (desugared to a counted `for`: array `T[N]` indexes `xs[i]`/length N; a list-like struct
  uses `xs.data[i]`/`xs.size`), completing the async transform. (Was 18/19.)
- **Self-hosting async: 18 of 19 async tests pass.** Closures' environments are
  **heap-allocated** (an escaping closure, e.g. an event-loop callback or a future combinator,
  called after its creating frame returns, no longer dangles its captured variables); struct
  literals work as **rvalues** (`x = P{…}`, not just var-decls); and the async constructor emits
  the **`on_drop` cancellation closure** (`future_drop` on a suspended task cascade-drops the
  awaited future). Together these unblock the event-loop / socket / timer / combinator
  (`select`/`join`/`spawn`) / cancellation async tests. (Only async `for-in` remains: it needs
  iterable element-type resolution.)
- **Self-hosting back-end: the async/await lowering pass, in Eskiu** (`selfhost/async_lower.esk`,
  a port of `sema/async_transform.cpp`, run between parse and codegen). Each `async fn` is
  rewritten into a frame struct + a `while(1){if st==0…}` state-machine resume function + a
  constructor that returns a `Future<T>*`; each `await` splits a state (evaluate the future,
  `future_poll` it, suspend, resume reading the value), with completion via `atomic_swap` and
  a waker closure, built on the now-available closures, atomics, and generics. Covers
  sequential bodies, **control flow around an await** (if / while / for with break/continue,
  lowered into the state graph), and a desugar pass (`return await E` / `await E;` /
  `x = await E` → let-bound). `async_basic`/`async_if`/`async_for`/`async_multi`/`async_break`/
  `async_return_await` run to parity. (Tests using generic-argument *inference* (`chan_recv(ch)`
  without `<T>`) or async for-in/switch, or closure→fn-pointer extern callbacks, await those
  orthogonal features.) Code generation also now **de-duplicates extern declarations** (the
  same `extern` in two imported stdlib files no longer emits two `declare`s).
- **Generic-argument inference.** A generic function called without explicit type arguments
  (`chan_recv(ch)`, `unwrap(&b)`, `add(7, 5)`) now has its type parameters solved by unifying
  each parameter's pattern (`Chan<T>*`, `Box<T>*`, `T`) against the actual argument types, then
  monomorphized like an explicit `chan_recv<int>`. Pointer spellings are normalized
  (`Box<T>*` ≡ `*Box<int>`) and `cg_etype` now types literals. This unblocks the channel-based
  async tests (`async_channel`, `async_elseif`) and any generic call relying on inference.
- **Unified self-hosted compiler driver + a three-stage bootstrap.** `selfhost/esk_main.esk`
  runs the full pipeline in Eskiu (preprocess → parse → **type-check** → code-gen),
  rejecting ill-typed input without emitting IR. The new gate `tests/selfhost/cg_bootstrap.sh`
  performs the canonical bootstrap: the C++ `eskiuc` builds the driver (cc0), cc0 builds it
  (cc1), and cc1 builds it (cc2); **cc1 and cc2 emit byte-identical IR** for the compiler
  and for a sample program, and cc2 compiles a runnable binary, i.e. stage2 ≡ stage3, a
  true self-hosting fixpoint. **Wired into CI.**
- **Self-hosting reached a bootstrap fixpoint (Phase D).** The self-hosted code
  generator emits valid LLVM IR for the *entire* self-hosted compiler, and `cg_main`
  compiled **by itself** reproduces the C++-built code generator's IR byte-for-byte
  over all 45 inputs (the whole `selfhost/` tree + the corpus). All five drivers
  (lexer/preprocessor/parser/typechecker/codegen), compiled by the self-hosted codegen,
  produce output identical to the C++-built ones. New gate `tests/selfhost/cg_selfhost.sh`
  (emit-validity + fixpoint, **wired into CI**). Eleven codegen bugs were found and fixed
  at the root by dogfooding the compiler's own source.
- **Self-hosting back-end, Phase C: the code generator, in Eskiu.** `selfhost/codegen.esk`
  (driver `cg_main.esk`) generates **textual LLVM IR** (assembled + linked by `clang`,
  no LLVM library). Covers scalars/arithmetic/control-flow, structs + methods + pointers,
  arrays, plain integer enums, **generics via monomorphization** (`List<T>`,
  `Box<int>` → `%Box_int`, `id<int>` → `@id_int`, with `sizeof`/cast so the stdlib
  `alloc<T>` instantiates), global variables, struct-by-value (sret), pointer
  arithmetic, and short-circuit `&&`/`||`. Behavioral oracle: emit `.ll` → `clang` →
  run → compare exit code + stdout to the C++-built binary, over a synthetic corpus
  (`tests/selfhost/cg_parity.sh`, **wired into CI**). Deferred (unused by the compiler's
  own source): floats, ADT enums/`match`, closures, exceptions, async, atomics.
- **Imported files are now preprocessed.** The self-hosted parser ran `import`ed files
  straight into the lexer, skipping the preprocessor, so an import's `#ifdef` kept both
  branches (e.g. `stdlib/mem.esk` emitted a duplicate `free`). `do_import` now runs
  `pp_run` per imported file, mirroring how the C++ folds preprocessing into the lexer.
- **Self-hosting back-end, Phase B: the type checker, in Eskiu.** `selfhost/sema.esk`
  (driver `tc_main.esk`, full pipeline preprocess → parse → check) now catches **all 19
  semantic error classes** in `tests/errors/`: name resolution, argument counts,
  undefined types & fields, async/await rules, switch/match exhaustiveness & duplicates,
  const-correctness (value, field, and pointer-pointee), interface satisfaction for
  bounded generics, the `?` operator's return type, and closure-escape soundness. Gate
  `tests/selfhost/tc_parity.sh` (**wired into CI**): the verdict matches
  `eskiuc --test-typechecker` on all **121** positive corpus files with zero false
  rejections, and every sema negative test is rejected with the right diagnostic. Types
  flow as strings (no structured Type IR), matching the C++ checker's codegen boundary.
- **Self-hosting back-end, Phase A: AST enrichment.** The self-hosted parser's AST
  (`selfhost/{ast,parser}.esk`) now captures what it previously parsed-and-discarded,
  so it can feed a future sema/codegen: generic **type-params + constraints**, struct
  **methods**, enum **ADT payloads**, **bitfield widths**, the **async** modifier, and
  full **interface method signatures**. Validated by a *lockstep* extension of both the
  C++ `ast/ast_printer.cpp` and the self-hosted printer (new `TypeParams:`/`Methods:`
  sections, `Name = tag(t1, t2)`, `type name : N`, `(async)`, `ret name(t1, t2)`),
  keeping `--test-parser` byte-identical (corpus 50/50, synthetic 11/11). Tooling; the
  production compiler change is the additive, debug-only printer extension.
- **Self-hosting milestone 3: the preprocessor, in Eskiu.** `selfhost/preprocessor.esk`
  ports `lexer/preprocessor.cpp`: object- and function-like `#define`/`#undef`,
  `#ifdef`/`#ifndef`/`#else`/`#endif` conditionals, `#pragma` passthrough, `#error`,
  backslash line splicing, recursive identifier-aware macro expansion, and the
  predefined `__FILE__`/`__LINE__`. Validated **through the lexer** (`pp_main`
  preprocesses + lexes; gate `tests/selfhost/pp_parity.sh`): byte-identical to the
  C++ `--test-lexer` over the whole `tests/` + `stdlib/` corpus (156/156, no
  exclusions), clean and directive-using files alike. **Wired into CI.** Tooling;
  the production compiler is untouched.
- **`List_set` / `List_remove`** in `<list>`: set an element by index, and remove
  one (shifting the tail). Surfaced by the preprocessor's macro table (redefine =
  set, `#undef` = remove); generally useful.
- **Self-hosting milestone 2 complete: the parser, in Eskiu.** The self-hosted
  parser (`selfhost/{ast,parser,parse_main}.esk`) now covers the full grammar (
  expressions, statements, declarations, templates/generics, lambdas/async) and is
  byte-identical to the C++ `--test-parser` AST dump over the import-free corpus
  (42/42 real `tests/*.esk` + synthetic). Gate `tests/selfhost/parse_parity.sh`
  (`--full`) is **wired into CI**. Dogfood/tooling; the production compiler is
  untouched. (v0.2.5 shipped this milestone in progress; it is now finished.)
- **Self-hosted parser follows `import`.** It now resolves and recursively parses
  imports (`<name>` → `stdlib/name.esk`, relative `"path"` against the importing
  file's directory), merging the imported decls in declaration order, with dedup and
  shared type-name registration across files (mirroring the C++ parser). Parity now
  covers every `tests/*.esk` whose **transitive import closure** is preprocessor-free
  (corpus 43 → 50); files whose closure touches the preprocessor stay excluded until
  the preprocessor is self-hosted. The harness computes the closure to gate inclusion.

---

## [0.2.5], 2026-06-17

### Added
- **`<ctype>` stdlib module.** Pure-Eskiu ASCII character classification (no libc,
  freestanding-safe): `is_space`, `is_digit`, `is_hex`, `is_alpha`, `is_alnum`,
  `is_ident_start`, `is_ident_cont`. Introduced for the self-hosted lexer; usable
  by any program via `import <ctype>;`.
- **Self-hosting milestone 1: the lexer, in Eskiu** (`selfhost/` +
  `stdlib/ctype.esk`). A full lexer written in Eskiu, byte-identical to the C++
  `--test-lexer` over the entire preprocessor-free corpus. Parity gate
  `tests/selfhost/lex_parity.sh` (`--full` → 114/114; compiles the driver once to a
  native binary, ~seconds) is **wired into CI**. Dogfood/tooling; the production
  compiler is untouched. See `selfhost/README.md`.
- **Self-hosting milestone 2: the parser, in Eskiu** (`selfhost/{ast,parser,parse_main}.esk`),
  in progress. Builds a recursive AST (tagged heap structs) from the self-hosted
  lexer's tokens and prints it byte-identical to `--test-parser`; gate
  `tests/selfhost/parse_parity.sh`. Done so far: the full expression layer
  (precedence chain, unary, postfix call/index/member/`?`, casts, `sizeof`, all
  literal kinds). Statements/declarations/templates next.

### Hardening
- **Resolver consistency check (`ESKIU_RESOLVER_DEBUG`).** Codegen's type
  derivation is split out (`deriveExprEskiuType`) so that, under the env flag, the
  single-resolver table is cross-checked against the structural derivation on every
  hit; a semantic disagreement (the v0.2.4 two-evaluator miscompile class) is
  printed. Across the whole corpus the two agree on every behavior-affecting type
  (the only difference is a benign enum-as-int representation), confirming the
  v0.2.4 reconciliation holds. Behavior-preserving (golden IR 26/26).
- **Fuzzer: backslash-newline in comments & strings.** New generators with a
  self-checking expected-output oracle (the O0/O2 differential is blind to a
  uniformly mis-lexed program), guards the comment-continuation footgun fixed
  below.

### Fixed
- **Preprocessor: a `//` comment ending in `\` no longer swallows the next source
  line.** Backslash-newline line continuation was applied unconditionally, so a
  comment whose last character was a backslash spliced the following line into the
  comment, silently deleting a `return`, an `else` branch, or any statement, with
  no diagnostic. Continuation now fires only when the trailing `\` is genuine code
  (not inside a `//` or `/* */` comment or a string/char literal); legitimate
  `#define` continuation is unaffected. Found by dogfooding the self-hosted lexer.
  Regression test: `tests/comment_backslash_continuation.esk`.

---

## [0.2.4], 2026-06-14

Type unification: making the type checker the single resolver, so codegen stops
re-deriving types independently (closing the two-evaluator risk). Internal
soundness work; the only user-visible effect is three latent miscompiles it
surfaced and fixed.

### Single type resolver (kills codegen's independent re-derivation)
- The type checker is re-run on the post-AsyncTransform AST and its resolved
  per-expression types are handed to codegen, which now consumes them instead of
  re-deriving via `getExprEskiuType`. This eliminated the structural condition
  behind the earlier `checkConstraints` bug, and immediately **surfaced and fixed
  three latent miscompiles** where the two evaluators silently disagreed (benign
  only because the affected test values were small):
  - **Float literals are `double` in sema too** (were typed `float`; codegen
    always emitted a `double` constant, so generic inference like `max(1.5, 2.5)`
    now monomorphizes to `double`, matching the emitted value).
  - **Pointer-arithmetic deref width**: `*(arr + n)` for `arr: *int` loaded `i8`
    in some paths (reading 1 byte of a 4-byte int); now correctly `i32`.
  - **`char` widening signedness**: a `char` widened to `int` used `sext` in some
    paths; `char` is unsigned in Eskiu, so it is now `zext`.
  - Plus a `for-in` consumption fix (the resolved iterable type arrives as
    `struct:List_int`; the loop lowering now strips the `struct:` decoration).
- **One grammar interpreter.** `codegen`'s `getTypeFromString` now dispatches on
  `ty::Type::parse`, the same parser the type checker uses, instead of its own
  hand-rolled string matching. The type-string grammar is interpreted in exactly
  one place across both phases. Behavior-preserving (golden-IR identical; existing
  quirks preserved).

---

## [0.2.3], 2026-06-14

Completing bounded generics, plus a typed internal type representation that
replaces ad-hoc type-string surgery: the foundation for keeping the compiler
sound as it grows.

### Bounded generics: primitives can satisfy a constraint
- A primitive type now satisfies a constraint through a **free function** named
  like the interface method whose first parameter is that primitive, e.g.
  `int cmp(int, int)` makes `int` satisfy `interface Ord { int cmp(Self) }`. Inside
  a generic body a constrained call `t.cmp(x)` on such a `t` lowers to `cmp(t, x)`.
  So `max<T: Ord>(int…)` and a constraint-bounded map over `int` keys now compile.
  Closes the method-only seam; the fn-pointer `HashMap<K,V>` remains for explicit
  `hash`/`eq`. Sema and codegen are gated in lockstep (scalar primitives only).

### Typed `Type` representation (soundness foundation, no behavior change)
- **`sema/type.{h,cpp}`: a structured `ty::Type` IR** (a tagged value with
  `parse`/`str`/`substitute`/`nominalName`), the typed replacement for the
  `std::string` type surgery that was scattered across ~175 call sites. Types
  still travel as canonical strings at the boundaries; the IR is the manipulation
  form, so invariants are enforced by the type instead of by convention (which is
  what produced the earlier `checkConstraints` `struct:`-ordering bug).
- **Migrated onto it:** `substType` (now `Type::substitute`) and the duplicated
  "strip `struct:` + pointers → bare name" surgery at six sema sites, including
  the exact `checkConstraints` strip that caused that bug, which no longer exists.
- **A golden-IR oracle** (`tests/type_zoo/snapshot.sh`) snapshots the emitted IR
  over a 24-program type-zoo and asserts byte-identical output across the
  migration. Every step above is verified behavior-preserving by it, plus the
  suite, sanitizers, and the differential fuzzer. A standalone round-trip unit
  test (`tests/type_roundtrip/`) checks `parse(s).str() == s` over the grammar.
- The registry-coupled `normalizeType`/`unifyTypeParam` and the cross-phase
  consolidation (codegen consuming resolved `Type`s rather than re-deriving from
  strings) are intentionally left for a later step: they are not string surgery.

---

## [0.2.2], 2026-06-13

Exclusively hardening + traits, no new stdlib surface. The goal: make the
compiler trustworthy and close the generics gap before the self-hosting arc.

### Compiler hardening
- **A fuzzer** (`tests/fuzz/eskiu_fuzz.py`): mutates the test corpus and generates
  programs biased toward the known bug classes, using the **LLVM IR verifier** and
  sanitizers as oracles (a crash, a verifier failure, or an asan/ubsan abort is a
  finding). Wired into CI as a bounded, fixed-seed job. Its first run found four
  real bug classes, now fixed:
  - **Unreachable code after a block terminator** (`return`/`break`/…) was emitted
    into the terminated block → "terminator in the middle of a basic block". Codegen
    now stops at the terminator and drops the dead code.
  - **Integer arguments on indirect / closure calls** weren't widened to the
    callee's parameter type → an IR-verifier mismatch. Now coerced (the same family
    as the 0.2.1 sret-argument fix).
  - **`switch` with duplicate case values** reached codegen and produced an invalid
    `switch` → now a clean type error.
  - **A negative array dimension** degraded the variable to a bare pointer that was
    then array-indexed → invalid GEP. Now rejected with a clear message.

### Traits / constraints (bounded generics)
- **`<T: Iface>`** (and `<T: A + B>`) on function and struct type parameters, built
  on the existing structural `interface`. At instantiation the concrete type
  argument must satisfy every listed interface (define its methods); otherwise it's
  a type error at the use site (`type 'X' does not satisfy constraint 'I'`) instead
  of a confusing downstream failure. Inside a generic body, a constrained method
  call resolves to the concrete type's method. Works for both `T f<T: Ord>(…)` and
  `struct Box<T: Ord> { … }`. (Constraints are method-based, so a constrained type
  must be a struct with the methods; primitive keys still use the fn-pointer
  `HashMap<K,V>` from 0.2.1.)

### Hardening + maintainability (0.2.2 re-cut, no behavior change)
- **Fuzzer: O0-vs-O2 differential oracle.** The fuzzer now also builds each
  verifier-clean generated program at `-O0` and `-O2` with clang and compares the
  runtime output; a divergence is a miscompile the IR verifier can't catch
  (valid IR, wrong semantics). Plus four more generators (ADT enums + `match`,
  capturing closures, async/await across control flow, nested structs) and
  mutation of generated programs. Wired into CI.
- **Compiler source modularized.** The three ~2,000-line files were split, with no
  behavior change: `sema/type_checker.cpp` → `typecheck_{decl,stmt,expr,type}.cpp`;
  `codegen/codegen_expr.cpp` → `codegen_{call,closure,adt}.cpp`; `parser/parser.cpp`
  → `parse_{decl,stmt,expr}.cpp`; the lexer's preprocessor pass into
  `lexer/preprocessor.cpp`; and `main.cpp`'s driver utilities into `main_support.cpp`.
  No source file exceeds ~760 lines.

---

## [0.2.1], 2026-06-13

Hardening and ergonomics, shaken out by building a real service (an INE-QR HTTP
API) on 0.2.0.

### Compiler
- **`codegen.cpp` split** into `codegen_{module,type,scope,decl,stmt,expr}.cpp`: the 3,400-line file became six; no behavior change (same 243 tests).
- **Sanitizer hardening gate**: `tests/run.sh` gains `SANITIZE=asan|ubsan` (compiles every positive test with the instrumentation and fails on a sanitizer abort), and CI runs the whole suite under both. A new `loop_locals` stress test locks in the entry-block-alloca fix.
- **Fix: integer arguments to sret-returning functions** were matched against the wrong parameter: the coercion loop didn't skip the hidden sret pointer at index 0, so the first scalar argument was checked against the sret pointer and an `int` literal was left unwidened (an IR-verifier error for a >16-byte-returning function called with int literals). Now offset-correct.
- **Fix: `substType` substitutes type parameters inside function types** (`fn(*K)->uint64` → `fn(*int)->uint64`), so generic APIs with callback parameters type-check and monomorphize.

### Standard library
- **`<bytes>`**: `Bytes`, a growable binary-safe byte buffer (`*uint8` + length; embedded NULs are real data, unlike `String`'s NUL-terminated `*char`): `Bytes_init`/`_free`/`_push`/`_append`/`_append_raw`/`_slice` (non-owning view)/`_eq`/`_from_str`/`_cstr`, plus base64 convenience (`Bytes_from_base64`/`Bytes_to_base64`). `<http>` gains `HttpReq_body`: a non-owning `Bytes` view of the request body.
- **`HashMap<K,V>`** (`<map>`): a hash map over any value-type key: you pass `hash`/`eq` function pointers at init (Eskiu has no trait system to synthesise them, the systems-language answer, like C's `qsort` comparator), with built-in `int_hash`/`int_eq`. The string-keyed, key-owning `Map<V>` is unchanged.

---

## [0.2.0], 2026-06-11

Backend-services phase: async/await, the full HTTP/2 stack (framing, HPACK with
Huffman, streams and flow control, a multiplexed server, and TLS/ALPN), sum types
with `match`, and a broad async/networking standard library, built on the v0.1.0
systems foundation.

### Language
- **`alloc`/`free` are no longer keywords**: heap allocation moved to the `<mem>` stdlib as the generic functions `alloc<T>(n)` and `free(p)`. Under `--freestanding` they target `esk_alloc`/`esk_free` via the predefined `__ESKIU_FREESTANDING__` macro.
- **`const`**: immutable typed bindings, usable as array sizes; field/element mutation of a `const` value is rejected.
- **Raw C function pointers**: casting a top-level function to a pointer type (`(*void)cmp`) yields its bare C function-pointer address rather than the `{fn, env}` closure fat pointer, so an Eskiu function can be handed to a C API as a callback (`qsort`, OpenSSL's ALPN selector, signal handlers, …). Test: `c_callback`.
- **Pointer constness**: `const T*` is a pointer to const T (the pointee is read-only, the pointer is rebindable); `T* const` is a const pointer (the binding is read-only, the pointee is writable); the two compose as `const T* const`. Reading through and rebinding a `const T*` are allowed; writing through it (`*p`, `p[i]`, `p->field`) is rejected, as is any conversion that drops a const qualifier: at initialization, assignment, call arguments and returns. Adding const (`T*` → `const T*`) is always allowed. const has no ABI effect and is stripped before code generation. Works in locals, parameters, struct fields and return types.
- **Integer ranges in `for`-`in`**: `for (i in A..B)` iterates the half-open range `[A, B)` (a new `..` operator). Desugars to a counted `for`, so it composes with `break`/`continue` and the async transform.
- **User-defined variadic functions**: a fixed parameter list followed by `...`, read inside the body with `va_list` / `va_start(ap)` / `va_arg<T>(ap)` / `va_end(ap)` (C default promotions apply: read float arguments with `va_arg<double>`). Lowers to the LLVM `va_arg` machinery; works on arm64 and x86-64.
- **Preprocessor diagnostics**: `__LINE__` (current source line) and `__FILE__` (current file path) expand in place, and `#error message` aborts compilation (honoring `#ifdef` branches). The VS Code grammar now also highlights all preprocessor directives.
- **Algebraic enums + `match`**: an `enum` variant may carry a payload (`enum Shape { Circle(float), Rect(float, float), Unit }`), making the enum a tagged union. Variants are constructed by name (`Circle(2.0)`, bare `Unit`) and destructured with `match s { Circle(r) -> …; _ -> … }`, which binds payload fields per arm. Classic integer enums are unchanged. `match` is checked for **exhaustiveness** (every variant covered, or a `_` default) and rejects duplicate arms. Algebraic enums may be **generic** (`enum Option<T> { None, Some(T) }`, `enum Either<A,B> { Left(A), Right(B) }`) monomorphized per instantiation like template structs; generic variants infer their type arguments from the payload when it determines them (`Some(5)` → `Option<int>`), or take explicit ones otherwise (`None<int>()`, `Left<int,string>(x)`).
- **`#pragma pack(N)` for N > 1**: caps each field's alignment at N (v0.1.0 honored `pack(1)` only). The layout matches the C `#pragma pack(N)` ABI: padding is inserted to N-capped field offsets and the total size rounds up to the struct's alignment (`min(max-field-alignment, N)`). `pack(push, N)` / `pack(pop)` / `pack()` still apply.
- **`alloc_with(&allocator, T, n)`**: explicit-allocator built-in.
- **`volatile let`**: qualifier-first form (like `const int`), in local and top-level declarations.
- Multi-argument `fn` types (`fn(A,B)->R`); function pointers usable as values, parameters, return types, and struct fields, and capturable in closures.
- **`intrinsic`**: function qualifier for compiler-lowered prototypes (distinct from `extern`): a call lowers to inline IR, not a call to a C symbol, and emits no `declare`. Gated on import, so the names stay un-reserved otherwise.
- **`escaping` + `free_closure`**: closures now escape correctly. A non-escaping closure (only called / passed to a non-`escaping` parameter) keeps its environment on the stack (zero cost); an escaping one (returned, stored, or passed to an `escaping` parameter) gets a heap environment that survives. The `escaping` parameter qualifier marks closure-retaining functions; using a non-`escaping` closure parameter beyond a direct call is a compile error. `free_closure(f)` releases an escaping closure's heap environment. (Previously, a closure that outlived its creating function read a dangling stack environment.)
- Fixed: a cast to a type imported from another file (e.g. `(FutureHdr*)p`) was misparsed; imported type names are now visible for cast detection. Follow-up fix: when that type's defining import was *deduplicated* (already imported via another path), its name was still missing from the importing file's parser, so the cast misparsed again. Declared type names are now shared across all sub-parsers (like the import set), so a cast parses regardless of import order.
- **`async` / `await`**: `async T f(...)` whose call yields `*Future<T>`, and the `await E` expression (`E: *Future<T>` → `T`), legal only inside an async function. A pre-codegen pass lowers an async function to a resumable state machine (frame struct + resume + constructor). Single and multiple awaits run end-to-end (fast path and suspend-over-reactor), including `return await` and `async void`. Cancellation works, **every** control-flow construct around await is supported (`if`/`while`/C-style `for`/`switch`/`for-in`, including `break`/`continue`), and the runtime is leak-free (verified with `leaks`). Fixed: a frame-hoisted local used inside a struct literal / `alloc_with` / `free_closure` after an await was not renamed to its frame field (the transform's expression walkers skipped those nodes). All expression nodes now share one child enumeration (`ast/ast_walk.h`).
- **Capturing closures inside generic functions**: a lambda declared in a template function body now captures correctly. Capture analysis runs on template bodies (type-independent), and a capture typed by the type parameter (e.g. `T` or `*Future<T>`) has its environment field substituted per instantiation. (Previously such a lambda got an empty capture list and miscompiled.) `await` also now accepts a `*Future<T>` returned by a generic function (its type arrives already instantiated). Removed the dead `thread`/`spawn`/`mutex` reserved words (the lexer reserved them but nothing used them), freeing those identifiers.
- **`spawn<T>` / `select2<A,B>` / `join2<A,B>`**: generic future combinators with a typed, cast-free call API (`spawn(task)`, `await select2(a, b)`, `await join2(a, b)`, type args inferred). `spawn` detaches a fire-and-forget task; `select2` completes with the index of whichever of two futures finishes first (loser dropped); `join2` completes once both finish. Thin wrappers over one shared type-erased core (`*_hdr`), so no per-instantiation bloat; leak-free (verified with `leaks`).

### Compiler fixes
- **Unsigned widening cast**: `(int)(uint8)255` (and `uint16`/`uint32`/`char`/`bool` → wider) now **zero-extends** (→ 255), matching the source's unsignedness, instead of always sign-extending (→ -1). Fixes byte-level decoding (parsers, the HTTP/2 frame codec, etc.).
- **Allocas are hoisted to the entry block.** A local declared inside a loop body emitted its `alloca` in the loop block, so the slot was re-reserved every iteration and never reclaimed until the function returned. A long-running loop with locals (e.g. `<base64>` encoding a multi-hundred-KB buffer) overflowed the stack and crashed. All stack-slot reservations now live in the function's entry block (stores stay at their original point; only the reservation hoists). Fixes `<base64>` on large inputs and any temp-heavy hot loop.

### Standard library
- **`<http2>`**: HTTP/2 (RFC 7540):
  - *Stage 1:* the frame-header codec (`h2_write_header`/`h2_read_header`, big-endian length/type/flags/31-bit stream id), frame-type + flag constants (`H2_HEADERS`, `H2_SETTINGS`, …), and the connection preface.
  - *Stage 2, connection lifecycle:* a SETTINGS codec (`h2_write_settings`/`h2_apply_settings`) over the six standard parameters plus an `H2Settings` model with RFC defaults; the SETTINGS ACK; PING/PONG (`h2_write_ping`/`h2_send_pong`); GOAWAY (`h2_write_goaway`/`h2_read_goaway`/`h2_send_goaway`) with the §7 error codes; an `H2Conn` connection-state struct; and async I/O over the event loop: `h2_read_full_async` (partial-read loop), `h2_read_frame_async`, and `h2_server_handshake_async` (preface validation → read + apply the client's SETTINGS → reply with ours + an ACK). Tested by `http2_conn` (codecs) and `http2_handshake` (the async handshake over a socketpair).
  - *Stage 5, TLS / ALPN* (`<tls>`): OpenSSL (libssl) by FFI: a server `SSL_CTX` (`tls_server_ctx`) that loads a cert/key and installs an ALPN callback selecting `h2`, blocking `tls_accept`/`tls_read_full`/`tls_write_all`/`tls_close`, and `http2_tls_serve_conn` running the HTTP/2 frame protocol over the encrypted stream. Plus an **async** TLS server (`http2_tls_serve_async`): a non-blocking SSL pump (`tls_accept_async`/`tls_read_async`/`tls_write_all_async` retrying `SSL_*` on `WANT_READ`/`WANT_WRITE` over the reactor) runs many TLS connections on one thread; verified with 3 concurrent `curl --http2` connections. The ALPN selector is passed to OpenSSL as a raw C function pointer (the new `(*void)fn` cast). Verified end-to-end against `curl --http2` (ALPN → h2, `HTTP/2 200`); see `examples/http2_tls_server.esk`. Also fixed: a server must not advertise `SETTINGS_ENABLE_PUSH=1` (§6.5.2). **HTTP/2 is now complete (stages 1–6).**
  - *Stream multiplexing:* the async server routes interleaved request frames to per-stream slots (`H2PendingStream`) and completes each on its END_STREAM, so a client can run many concurrent streams on one connection (responses stay serialized on the single connection writer: no concurrent socket writes). Verified with an interleaved two-stream test (`http2_multiplex`).
  - *Send-side flow control:* the async server (`h2_respond_async`) now spends the per-stream and connection send windows per DATA frame and parks for WINDOW_UPDATE when a window is exhausted, applying connection-level WINDOW_UPDATE in the dispatch loop too, so responses larger than the 65535-byte default window flow correctly (verified delivering 1 MB bodies over `curl`).
  - *Stage 6, server API* (`<http2_server>`): `http2_serve_async(lp, fd, handler, max_conns)` (and `http2_serve_conn_async`), an h2c server mirroring `<http_async>` and reusing `<http>`'s `HttpRequest`/`HttpResponse`. Handshake → frame-dispatch loop → HPACK-decode the request → handler → encode the response (HEADERS `:status`/`content-length`/headers + DATA, END_STREAM); SETTINGS/PING answered, GOAWAY/EOF ends it. Tested end-to-end over a socketpair (`http2_server`). Streams are multiplexed (interleaved request frames routed per-stream), non-blocking, and flow-controlled; cleartext h2c (TLS is in `<tls>`).
  - *Stage 4, streams & flow control:* the per-stream state machine (`H2Stream`: idle → open → half-closed → closed via `h2_stream_on_recv`/`h2_stream_on_send`), credit-based flow control over per-stream and connection windows (`h2_can_send`/`h2_account_sent`/`h2_account_recv`/`h2_grant_window`), the HEADERS/DATA/WINDOW_UPDATE/RST_STREAM codecs, and async HEADERS+CONTINUATION reassembly (`h2_read_header_block_async`). Tested by `http2_stream`.
- **`<hpack>`**: HPACK header compression (RFC 7541), stage 3 (complete): prefix-integer coding (§5.1), string literals (§5.2), the 61-entry static table, a dynamic table with size-based eviction, the §6 decoder/encoder (indexed; literal with/without/never indexing; dynamic table size update), and **Huffman** coding (§5.2 / Appendix B): a decode trie + bit-packed encode whose 257-symbol table is *generated from the RFC text* by `tools/gen_hpack_huffman.py` (→ `stdlib/hpack_huffman.esk`), never hand-transcribed. Verified against the RFC's own vectors, §C.1.1, §C.3.1, §C.4.1 (Huffman), plus encode→decode round-trips (`hpack`). See `docs/dev/http2-design.md` for the full stack.
- **`<alloc>`**: a toolkit of explicit allocators (`Bump`, `Arena`, `Pool`, `FirstFit`) over a buffer you own, via `alloc_with` (the Zig allocator model). Pure pointer arithmetic, no externs. Not a global `malloc` replacement: the default `alloc<T>` stays libc `malloc` in hosted mode; these manage a region and are the building blocks for a `--freestanding` heap.
- **`<sysheap>`**: a general-purpose heap that sources OS pages via `mmap` and allocates with `FirstFit`, so the allocation path never calls libc `malloc` (the Zig `page_allocator` / jemalloc / Go-runtime approach). `heap_init`/`heap_alloc`/`heap_free`/`heap_destroy`; opt-in (the default `alloc<T>` is unchanged).
- **`<time>`**: `time_now_ms`, `time_now_s`, `time_monotonic_ms`, `sleep_ms`.
- **`<env>`**: `env_get`, `env_has`, `env_get_or`, `env_get_int`.
- **`<base64>`**: RFC 4648 `base64_encode` / `base64_decode` over buffers.
- **`<json>`**: JSON builder (`Json`) plus a recursive-descent parser (`json_parse` → `JsonValue`).
- **`<threading>`**: `Mutex`, `Cond`, `Sem` over pthread.
- **`<http>`**: HTTP/1.1 request parser, response builder, and a threaded worker pool (`http_serve`). Also a binary-safe request reader, `HttpReq` + `http_recv` (loops `recv` until the full Content-Length body arrives, into a `*uint8` body; for uploads a single-recv String body would truncate/corrupt), with `HttpReq_header`, `http_reply`, and `http_reply_error`.
- **`<multipart>`**: extract a named part from a `multipart/form-data` body over raw bytes: `multipart_boundary` (parse the boundary from Content-Type) and `multipart_part(body, len, boundary, name, …)` (returns a slice into the body).
- **`<map>`**: `Map<V>`, a string-keyed hash map (open addressing, linear probing, grows at 0.75 load): `Map_init`/`Map_at` (get-or-insert, returns a `*V` slot)/`Map_get`/`Map_free`. Keys are strings (arbitrary-`K` hashing can't be synthesised generically).
- **`<net>`**: `net_accept_addr(fd, *uint32 peer_ip)` accepts a connection and reports the peer's IPv4 address (the plain `net_accept` drops it), enabling per-client rate limiting and logging.
- **`<string>`**: `starts_with`, `ends_with`, `trim`, `split` (List + streaming token iterator).
- **`<path>`**: `path_join`, `path_basename`, `path_dirname`, `path_extension`, `path_is_absolute`.
- **`<eventloop>`**: readiness reactor over kqueue (macOS) / epoll (Linux): `el_new`, `el_add_read`, **`el_add_write`** (write-readiness: `EVFILT_WRITE`/`EPOLLOUT`; a fd waits for read *or* write at a time), `el_del`, `el_run`, `el_stop`, `el_free`. Callbacks are `fn(EventLoop*, int)->void`. Foundation for async I/O and the HTTP stack. Fixed: a one-shot callback's heap env leaked when its fd was re-armed from inside the callback (a sequential read loop on one fd). `el_run` now frees the env it invoked using a per-registration generation counter, not the post-callback `active` flag.
- **`<atomic>`**: `atomic_load`, `atomic_store`, `atomic_swap`, `atomic_cas` over an `int` cell, lowering to LLVM atomics (acquire / release / acq_rel). Foundation for the async runtime's lock-free `Future` state handshake.
- **`<executor>` / `<net_async>`**: async runtime foundation: an `Executor` (event loop + thread-safe ready-queue + self-pipe wakeup, so wakers always resume on the home thread) and leaf futures (`net_read_async`, `net_accept_async`, **`net_write_async`**: non-blocking write that parks on writability and resumes until the whole buffer is sent) plus **`net_set_nonblocking`**, over `<eventloop>`. The async/await de-risk gate validated the runtime model end-to-end (reactor read, cancel, cross-thread resume). The async HTTP/2 server (`<http2_server>`) is now fully non-blocking: responses go out via `net_write_async`, so a slow client can no longer stall the loop (verified: a 40 KB body over `curl --http2-prior-knowledge`).
- **`<http_async>`**: a non-blocking, **concurrent** HTTP/1.1 server over `<eventloop>`: an async accept loop `spawn`s a detached handler per connection (so a slow request doesn't block the others), with a `<channel>` wait-group joining all handlers for clean bounded shutdown. The Phase-2 async-HTTP goal, end to end.
- **`<timer>`**: `timer_after(lp, ms)`, a leaf `*Future<int>` that completes after `ms` of monotonic time. `<eventloop>` gained a timer wheel (`el_add_timer`/`el_del_timer`); `el_run` now blocks only until the nearest deadline. Enables real read-with-timeout: `select2(net_read_async(...), timer_after(lp, ms))`.
- **`<futureval>`**: value-returning async combinators built on sum types: `select2v<A,B>` resolves with the winner's value as `Either<A,B>` (loser dropped), `join2v<A,B>` resolves with both values as a `Pair<A,B>`. Leak-free. (Also fixed: a `Pair<A,B>{...}`-style template struct literal inside a template body now substitutes the enclosing type params instead of mangling to a bogus `Pair_A_B`.)
- **`<either>`**: the standard sum types built on generic algebraic enums: `Option<T>` (`None`/`Some`) and `Either<A,B>` (`Left`/`Right`), with helpers (`opt_is_some`, `opt_unwrap_or`, `either_is_left`, …). `match` works inside these generic helpers (codegen resolves the enum instance from the subject's type under the active type substitution).
- **`<channel>`**: an async message channel over the `Future` runtime: `chan_recv(ch)` is a `*Future<T>` that completes with the next item (immediately if buffered, else it parks until a `chan_send` hands one off and wakes it); `chan_send(ch, v)` enqueues or hands off directly. v1 is a bounded ring with a single outstanding receiver and non-blocking send. Generic and cast-free; leak-free.

### Tooling
- **`eskiuc run file.esk [args...]`**: compile to a temporary executable, run it (forwarding `[args...]`), then delete it; the program's exit code is propagated. Compiler flags precede the script, program arguments follow it.
- **Shebang scripts**: a leading `#!/usr/bin/env eskiuc run` line is ignored by the preprocessor (line numbers preserved), so a `.esk` file can be made executable (`chmod +x`) and run directly.
- **`eskiuc fmt [--check] file.esk …`**: a conservative, comment-preserving source reindenter: normalizes leading indentation (4 spaces per brace level), trailing whitespace, blank-line runs and the final newline, while preserving every line's content (operators, inner spacing, comments, strings) verbatim. Idempotent and meaning-preserving; braces inside strings/comments are ignored. `--check` reports unformatted files (exit non-zero) without writing.
- **`--asan` / `--ubsan`**: real sanitizer instrumentation via the LLVM pass manager. `--asan` runs AddressSanitizer (heap/stack/global memory errors) and links the matching LLVM compiler-rt runtime; `--ubsan` inserts trapping bounds checks (no runtime needed). Both compose with `eskiuc run`.
- **`-Wextra`**: extra warnings layered on top of `-Wall`: comparison between signed and unsigned integers; off by default.

### Documentation
- **`docs/lang/grammar.md`**: a formal EBNF grammar derived from the parser: lexical structure, preprocessor, declarations, the type grammar, statements, and the full expression-precedence chain.
- **`docs/dev/abi.md`**: the C-ABI contract: scalar/pointer lowering (const has no ABI effect), struct/packed/bitfield/union layout, ADT tagged-union shape, the >16-byte sret rule, varargs + `va_list`, fat pointers for closures/interfaces, and template name mangling.
- **`__FILE__`/`__LINE__` reference**: spec §18 now documents all predefined macros (`__LINE__`, `__FILE__`, OS macros, `__ESKIU_FREESTANDING__`) and the shebang interaction.

### Compiler correctness (consistency audit)
- **Mutable parameters.** Every function parameter now gets a stack slot, so a parameter can be reassigned in the body like a local (`int f(int n) { n = n + 1; … }`). Previously this miscompiled (a store into a non-pointer SSA value, caught by the IR verifier). Consequently the receiver resolution for method calls and interface dispatch was made representation-independent: a value-struct receiver passes its address, a pointer receiver passes the pointer it holds, and an interface value (a pointer to its `{data, vtable}` fat struct) is loaded from its slot, fixing a latent bug in calling a struct method through a pointer parameter. Tests: `param_reassign`, `interfaces`.
- **`bool` → wider integer zero-extends.** Returning (or implicitly widening) a `bool` or comparison result into an `int` now yields `1`, not `-1`. The i1 was being sign-extended. `return a < b;` from an `int` function is correct.
- **Async `else-if` with `await`.** In an `async` function, an `if / else if / else` chain whose branches contain `await` and whose `else` terminates (e.g. `return`) no longer miscompiles. The state-machine lowering treated a plain terminating branch as fall-through and appended a state transition after the `return`, yielding invalid IR ("terminator in the middle of a basic block"). The lowering now recognizes terminating statements. Test: `async_elseif`.
- Unsigned integer types use unsigned div/rem/shift/compare; 64-bit integer literals no longer truncate; mixed-width ops sign/zero-extend by signedness; variadic call args get the C default-argument promotions.
- Member access on a struct-valued temporary; `(Type)`/`(Type*)`/alias/enum casts; type alias as a local pointer/array; `List<StructType>` through helper functions; `T*` (trailing-star) pointer deref; nested template close `>>`; closures no longer capture module globals by value.

---

## [0.1.0]

Eskiu is a systems language built to replace the C + Go + C++ + Python stack that compute-intensive backend services typically require. C-style syntax, structural interfaces, monomorphic templates, and explicit memory, compiled to native via LLVM with no garbage collector and no runtime.

This is the first release. It includes everything needed to write real backend services and bare-metal systems code, validated against a cryptographic pipeline running at 74 ms on arm64 (2.5× faster than the reference C implementation) and a bare-metal ARM64 kernel booting in QEMU without libc.

### Closures and threads

- `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`; variables from the enclosing scope are captured by value automatically
- `thread_create(fn()->void)` and `thread_join(*void)` are language keywords; the fat pointer maps directly to pthread's `(start_routine, arg)`: no trampoline
- Link with `-lpthread` on Linux

```eskiu
int base = 10;
let add: fn(int)->int = int(int x) { return x + base; };  // captures base

*void t = thread_create(void() { printf("hello\n"); });
thread_join(t);
```

### Exception handling

- `try { } catch (T name) { } finally { }` with multiple catch clauses
- `throw expr`: any Eskiu value
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

- `sizeof(T)`: compile-time `int64` constant for any type including structs
- `union`: all fields share offset 0; size = sizeof(largest field)
- Typed pointer arithmetic: `p: *int; p + 1` advances 4 bytes, not 1
- `*ptr = value` through pointer parameters works correctly

### Forward declarations and declaration order

- Declaration order no longer matters: a function may call any function defined later, and mutual recursion works
- Body-less forward declarations (`int is_odd(int n);`) parse and are honoured
- Codegen declares every function prototype in a pre-pass before emitting any body, so call-before-define resolves cleanly instead of failing

### Compound assignment

- Bitwise compound assignment operators: `&=`, `|=`, `^=`, `<<=`, `>>=` (alongside the existing `+= -= *= /= %=`)

### Template ergonomics

- Template struct literals: `Pair<int, float> { first: 7, second: 3.14 }` (named and positional)
- Type-argument inference: type parameters are inferred from argument types by structural unification: both when a parameter is the bare type parameter (`max(3, 5)`) and when it appears inside a composite type (`List_get(&nums, i)` infers `T` from the `List<T>*` parameter). Explicit `max<int>(3, 5)` still works
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

- `eskiuc file.esk -o prog` now **compiles and links** in one step, producing a runnable executable: `eskiuc` invokes the system C toolchain (`$CC`, then `cc`/`clang`/`gcc`), the same way `rustc` and `clang` do internally
- `-l<lib>`, `-L<path>`, and `--link-arg=<arg>` are forwarded to the linker (e.g. `eskiuc app.esk -o app -lpthread`)
- Object-only when the `-o` output ends in `.o`, when `-c` is passed, or under `--freestanding` (bare-metal links itself); a C toolchain is the only build dependency besides LLVM

### Enums, type aliases, bitfields, and the preprocessor

- **Enums**: `enum Color { Red, Green = 5, Blue }`: members are `int` constants (implicit or explicit values), the enum type maps to `i32`, and they work in `switch`, comparisons, and as parameter types
- **Type aliases**: `type u8 = uint8;`: a name for any existing type, including pointers and templates; resolved to the underlying type everywhere
- **Bitfields**: `struct F { uint32 a : 1; uint32 b : 3; }`: packed into storage words with masked read-modify-write access; signed fields sign-extend; works with struct literals
- **Preprocessor**: object-like and function-like `#define`/`#undef` (with recursive expansion and backslash-continued **multi-line** macro bodies) plus `#ifdef`/`#ifndef`/`#else`/`#endif` conditional compilation, run as a text pass before lexing; the macro table is shared across `import`/multi-file builds, and source line numbers are preserved
- **Packed structs**: the `packed struct` qualifier removes inter-field padding (back-to-back layout) for matching on-the-wire/on-disk formats and C `__attribute__((packed))` structs. `#pragma pack(push, N)` / `pack(pop)` is honoured for C source compatibility (maintains an alignment stack; `pack(1)` packs subsequent structs); both set the same flag and compose with bitfields and `sizeof`

### Tooling

- **Multi-file compilation**: `eskiuc a.esk b.esk -o prog` compiles several files together, merging their declarations (order-independent thanks to the prototype pre-pass)
- **`-Wall`**: lint-style warnings: unused variables, parameters, and functions, plus assignment used as a condition (`if (x = 0)`); off by default

### Standard library

- `import <name>`: angle-bracket imports resolve stdlib modules from the installation; `import "path"` for local files
- `<fs>`, file I/O: `fs_open`, `fs_close`, `fs_read`, `fs_write`, `fs_seek`, `fs_tell`, `fs_size`, `fs_read_all`, `fs_write_all`, `fs_eof`, `fs_error`
- `<net>`, TCP sockets over the POSIX BSD socket API: `net_tcp_listen`, `net_accept`, `net_tcp_connect`, `net_send`/`net_recv`/`net_send_str`, `net_close`, plus the raw `extern`s and a portable `sockaddr_in`. Pure stdlib; sockets need no compiler support beyond the C FFI and `packed struct`. Includes `examples/http_server.esk` (a working HTTP/1.1 server) and `examples/tcp_echo_server.esk`

### Networking-enabling language features

- **Function-as-value**: a top-level function used as a value (not called) decays to a `fn(T,...)->R`, passable to `thread_create` and callbacks without wrapping it in a lambda. The compiler synthesizes an adapter so the function fits the `{fn_ptr, env_ptr}` closure ABI
- **Predefined OS macros**: the compiler predefines `__APPLE__` (macOS) or `__linux__` (Linux), so stdlib and user code can `#ifdef` per platform; used by `<net>` to pick the correct `sockaddr_in` layout

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
- `asm("cli");`: simple form; passes the string verbatim to the assembler with no inputs, outputs, or clobbers
- `asm("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");`: extended form with GCC-compatible constraint syntax; supports input operands, output operands, and clobber lists
- Lowers to LLVM inline asm nodes; `"memory"` clobber emits a compiler barrier

**Freestanding mode**
- `--freestanding` CLI flag: redirects `alloc` to call `esk_alloc` and `free` to call `esk_free` instead of the libc `malloc`/`free`
- User provides both symbols in their kernel or bare-metal runtime; the compiler emits `declare` stubs and the linker resolves them

**`volatile` qualifier**
- `volatile let reg: *uint8 = (uint8*) ADDR;`: marks all LLVM loads and stores through the pointer as `volatile`, preventing the optimiser from caching, reordering, or eliminating MMIO accesses

**Cross-compilation**
- `--target TRIPLE` CLI flag: sets the LLVM target triple for the output object file
- Both AArch64 and X86 LLVM backends are included in the Eskiu build
- `eskiuc file.esk --target x86_64-pc-linux-gnu` produces an ELF x86-64 object file

**v0.1 prerequisites complete**
- All compiler prerequisites for the kernel-on-QEMU milestone are implemented and tested

## [0.0.13-alpha]

### Fixed
- **Negative literals**: `-1`, `-3.14` etc. now parse as negative literal values directly, not as unary minus applied to a positive literal. Global variable initialisers with negative values (e.g. `int x = -1;`) previously compiled to 0; this is now correct. `evaluateConstantExpr` folds unary minus on numeric constants as a fallback.

---

## [0.0.12-alpha]

### Added

**Lambdas and function pointer types**
- `fn(T,...)->R` function pointer type syntax: first-class function types usable in variable declarations, struct fields, and parameter lists
- Anonymous function expressions: `int(int x) { return x * 2; }`, C-like syntax producing a function pointer value
- Lambda variables: `let double_it: fn(int)->int = int(int x) { return x * 2; };`
- Higher-order functions: lambdas can be passed as arguments to functions expecting `fn(...)` parameters
- `LambdaExpr` AST node; full visitor chain (parser, type checker, codegen, AST printer)
- Codegen: each lambda expression is lowered to a uniquely-named private `llvm::Function`; the expression value is the function pointer

**VS Code extension: real-time diagnostics, hover, and go-to-definition**
- `--hover-at LINE:COL` CLI flag: prints the inferred Eskiu type of the expression at the given source position; consumed by the VS Code extension for hover tooltips
- `--definition-at LINE:COL` CLI flag: prints the `file:line:col` of the definition of the symbol at the given position; consumed for go-to-definition
- VS Code extension (`editor/vscode/`) upgraded from syntax-only to a full language client: spawns `eskiuc --test-typechecker` on save for real-time error squiggles; hover provider calls `--hover-at`; definition provider calls `--definition-at`
- TextMate grammar already present from v0.0.11 provides syntax highlighting

**switch/case type checking**
- The type checker now validates that each `case` value is compatible with the `switch` subject expression type; mismatched case types are reported as errors at the `case` token position

## [0.0.11-alpha]

### Added

**Decoder rewritten in Eskiu (no C pipeline code)**
- `ine_decoder/crypto.esk` (541 lines): AES-256-CBC + RSA-8192 pipeline, hex/base64 decoders, PKCS#1 stripper, 6-bit decoder, WebP reconstruction, `run_no_so_pipeline()`; all in Eskiu calling OpenSSL via `extern`
- `ine_decoder/output.esk` (186 lines): Spanish character table, field splitter, growable JSON buffer, `decode_to_buffers()`; pure Eskiu
- `ine_decoder/crypto.c` and `output_decode.c` removed: replaced entirely by Eskiu
- Only C remaining: `qr_extract.c` shim (12 lines) + `qr_extract_impl.cpp` (CoreGraphics + zxing-cpp)
- Runtime: **80ms** on arm64, identical output to reference

**String literal adjacent concatenation**
- `"abc" "def"` on consecutive lines (or the same line) are now concatenated into a single string at parse time; enables readable multi-line constant definitions

### Fixed (compiler bugs found during Eskiu crypto port)

- **`ptr - ptr` → `int64`**: pointer subtraction for computing byte offsets in buffers
- **Integer widening in arithmetic** (`+ - * /`): `i8 - i32` now ZExts the narrower operand before emitting the instruction
- **`string[i]` → `char`**: indexing a `string` typed value now returns the correct `char` (i8) element instead of computing type `"strin"`
- **Assignment store coercion**: `arr[i] = val` where the array element type is wider than `val` now ZExts/truncates to match (e.g. `int64[0] = 0` was storing i32 into an i64 slot)
- **Return value coercion**: functions that return `int64` but the expression evaluates to `i32` now automatically extend the return value
- **GlobalVariable initializer coercion**: `uint8 X = 0x52` was creating a global with mismatched i32/i8 initializer; now casts to the declared type
- **`validateStructType` multi-level pointers**: `**char` was triggering "undefined struct '*char'"; now strips all pointer levels before checking the base type

---

## [0.0.10-alpha]

### Added

**Global variables**
- `VarDecl` at module scope now emits `llvm::GlobalVariable` instead of `alloca`
- Constant initializers folded at compile time: `int`, `float`, `double`, `bool`, `char`, `string`, `null`
- Non-constant initializers zero-initialize the global (assign in `main()` for complex expressions)
- `globalVarTypes` map tracks Eskiu type strings for globals (complement to function-scoped `varTypeStack`)
- `evaluateConstantExpr()`: folds literal expressions to `llvm::Constant*`
- `visit(IdentExpr*)` now loads from `llvm::GlobalVariable` as well as `AllocaInst`
- `IMAGE_PATH`, `OUT_JSON`, `OUT_WEBP` in `ine_decoder/main.esk` moved to module scope

**sret (large struct return)**
- `needsSret(type)`: returns true for aggregates > 16 bytes (arm64 register limit)
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
- `argv`/`argc` support: programs can accept CLI arguments natively

---

## [0.0.9-alpha]

### Added
- **`ine_decoder/`, INE QR decoder port**: full pipeline running at **74.4 ms** total
  (QR: 71.7 ms + crypto: 2.8 ms + output decode: <1 ms) vs. 188.9 ms reference C and 3–5 s original target; 2.5× faster than hand-written C
  - `types.esk`: `QRPair`, `NoSoKeys`, `IneResult`, `IneFields` structs
  - `extern.esk`: libc + OpenSSL EVP (AES-256-CBC / RSA-8192) + `ine_qr_extract()` declarations
  - `stage1_qr.esk`: QR extraction wrapper
  - `stage2_crypto.esk`: 3-round AES-256-CBC + RSA-8192 via OpenSSL
  - `stage3_output.esk`: pipe-delimited plaintext → JSON + WebP extraction
  - `main.esk`: orchestration, timing, output
  - `qr_extract.c` / `qr_extract_impl.cpp`: C/C++ shim using CoreGraphics + zxing-cpp 3.x
  - `Makefile`, `README.md`

### Fixed
- **Integer width mismatch in comparisons**: `uint8 == int` (e.g. `plaintext[i] == 124`) crashed LLVM with "Both operands to ICmp instruction are not of the same type"; now `ZExt`s the narrower operand to match the wider before emitting any of the six comparison operators
- **Mixed int/float arithmetic**: `i64 * double` (e.g. timing calculation `(t1 - t0) * 1000.0`) generated invalid IR; now detects int/float type mismatch in `+`, `-`, `*`, `/` and promotes the integer operand with `SIToFP` before emitting the floating-point instruction

---

## [0.0.8-alpha]

### Added
- **`import "file.esk"`**: multi-file support with paths resolved relative to the importing file's directory; recursive imports with deduplication (a file imported more than once is parsed only once); `Parser.basedir` + `importedFiles` set propagated to sub-parsers
- **Interface vtable dispatch**: `interface I { void method(); }` generates `%I_vtable = type { ptr, ... }` and `%I_fat = type { ptr data, ptr vtable }` LLVM types; structs are auto-boxed at call sites via `boxAsInterface()`; method dispatch loads the vtable pointer from the fat pointer and calls indirectly; `getExprEskiuType` extended to handle `UnaryExpr("&", ...)` and `UnaryExpr("*", ...)` for boxing detection
- **`String.append()` and `String.concat()`**: added to `stdlib/string.esk` using `memcpy` + pointer arithmetic

### Fixed
- Pointer comparison (`p == null`, `ptr1 == ptr2`) incorrectly used `FCmpOEQ` (float equality); now uses `ICmpEQ`; all six comparison operators updated to check `isFloatingPointTy()` first
- `i1 → i32` widening used `SExt` (sign-extends `true` → `-1`); now uses `ZExt` for `i1` operands so comparisons correctly store `0` or `1`

---

## [0.0.7-alpha]

### Added
- **Bitwise operators**
  - Binary: `&`, `|`, `^`, `<<`, `>>`, new precedence levels in parser (bitwiseOr → bitwiseXor → bitwiseAnd → equality → shift → comparison → additive)
  - Unary: `~` (bitwise NOT), added to unary operator list in parser and `inferUnaryExprType`
  - Codegen: `CreateAnd`, `CreateOr`, `CreateXor`, `CreateShl`, `CreateAShr`
- **Hex literals**: `0xFF`, `0x0F`; lexer reads hex digits after `0x`/`0X` prefix; `stoll(..., 0)` for auto-base detection in codegen
- **Compound assignments**: `+=`, `-=`, `*=`, `/=`, `%=`; new tokens in lexer; desugared in `parseAssignment()` to `x = x op y`
- **`continue` statement**: `ContinueStmt` AST node; full visitor chain; `continueTarget` saved/restored around `WhileStmt` and `ForStmt` bodies (points to loop condition and step block respectively)
- **For-loop with declaration init**: `for (int i = 0; i < n; i += 1)`, parser wraps the declaration in a `BlockStmt`; type checker processes init items directly in `ForStmt` scope, avoiding the inner-scope pop that previously made `i` undefined in the condition, step, and body
- **Pointer arithmetic**: `ptr + n` → `GEP(i8, ptr, n)`, `ptr - n` → `GEP(i8, ptr, -n)` in `visit(BinaryExpr*)`

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
- **Source locations in errors**: `ASTNode` now carries `line`/`col`; parser stamps all expression and statement nodes; errors now report `file.esk:line:col:` instead of `file.esk:0:0:`; filename taken from the CLI input path
- **`&` address-of operator**: correctly returns the lvalue pointer (alloca), enabling `fn(&localVar)` patterns and template method calls via `self` pointer
- **Template member access on pointer types**: trailing `*` stripped before struct field lookup; `self: List<int>*` now correctly resolves to `structFields["List_int"]` in both `visit(MemberExpr*)` and `getExprEskiuType()`
- **`stdlib/list.esk`**: `List<T>` with `List_init`, `List_push`, `List_get`, `List_len`, `List_free` as template functions; tested end-to-end
- **`stdlib/string.esk`**: `String` struct with `String_init`, `String_from`, `String_cstr`, `String_len`, `String_free`
- **Interface structural check**: `isValidAssignment` now verifies that a struct satisfies an interface's method signatures (vtable codegen deferred to 0.0.8)
- **`isPointerType` unified**: now detects both leading `*T` and trailing `T*` conventions

### Fixed
- `&` operator returned a loaded value instead of the address, breaking all pointer-argument patterns
- Template method parameter types with pointer suffix (e.g. `List<T>*`) were incorrectly mangled to `List_int_*` instead of `List_int*`
- `getExprEskiuType` for member chains through a pointer-to-template-struct now resolves correctly

---

## [0.0.5-alpha]

### Added
- **`switch`/`case`**: `SwitchStmt` AST node with `Case { value, stmts }` list; full visitor chain; parser handles `switch (expr) { case val: stmts break; default: stmts }` with fallthrough support; codegen emits LLVM `switch` instruction with `ConstantInt` case values; `break` branches to `switch.end` via existing `breakTarget` mechanism
- **Function templates**: `fn Name<T, E>(T x) -> RetType<T,E> { ... }`; `FunctionDecl.typeParams` field; `TemplateCallExpr` AST node parsed in `parsePostfix()`; lazy instantiation via `typeParamOverride` map that intercepts all type lookups during template body emission; context save/restore preserves insert point and `currentFunction` when instantiating inside another function's body; `substType` handles `Name<T,E>` nested substitution recursively
- **`InterfaceDecl`**: `interface Speakable { void speak(); }` fully parsed and stored in `interfaceDecls`; codegen generates no IR (vtable dispatch deferred)
- **Stdlib base** (`stdlib/`)
  - `result.esk`: `struct Result<T,E>` + `Ok<T,E>` / `Err<T,E>` template constructors
  - `math.esk`: `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` (extern to libm)
  - `io.esk`: `printf`, `fprintf`, `sprintf`, `scanf`, `puts`, `getchar`, `putchar`
  - `mem.esk`: `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `memchr`

### Fixed
- Parser: `int fn<T>(T x)` at top level was silently dropped; `parseDeclaration()` now detects `<` after a name as a function template
- Type checker: template function bodies were visited with unresolved type params; now guarded with `typeParams.empty()` check
- `substType`: did not substitute type params inside `Name<T,E>` template strings; now handles nested template types recursively

---

## [0.0.4-alpha]

### Added
- **Template struct declaration**: `struct Result<T, E> { ... }`; `StructDecl` gains `typeParams` field; template declarations are stored in a separate registry and not emitted to LLVM until first use
- **Template type references**: `Result<int, string>` parsed in `parseType()` via `IDENT < TYPE, ... >` lookahead; stored as `"Result<int,string>"` in the AST
- **Lazy instantiation in type checker**: `normalizeType("Result<int,string>")` detects `<`, looks up the template, substitutes `T→int` / `E→string` in all field types, and registers `"Result_int_string"` as a concrete struct
- **Lazy instantiation in codegen**: `getTypeFromString("Result<int,string>")` calls `ensureTemplateInstantiated()` which creates `%Result_int_string = type { i32, i32, ptr }` on first use
- **Name mangling**: `Result<int,string>` → `Result_int_string` in LLVM IR
- **Type substitution**: `substType` handles `*T` → `*int`, `T*` → `int*`, `T[N]` → `int[N]`, and nested template types recursively
- **`varTypeStack` normalization**: `VarDecl` stores the mangled name so `MemberExpr` resolution finds instantiated struct fields correctly

### Fixed
- Parser: `int fn<T>(...)` at top level was silently dropped; `parseDeclaration()` now detects `<` after a function name as a template parameter list
- Type checker: template bodies were visited with unresolved type parameters; now guarded so only concrete instantiations are type-checked

---

## [0.0.3-alpha]

### Added
- **`alloc(T, N)`**: new `AllocExpr` AST node; emits `call @malloc(i64 N * sizeof(T))`; `sizeof(T)` resolved via `DataLayout` initialized at the start of `generateCode()`
- **`free(ptr)`**: parsed as a regular call; `free` pre-registered in the type checker as variadic; auto-declared in the LLVM module on first use, no explicit `extern` required
- **`getOrDeclareFunc()`**: codegen helper that lazily declares C runtime functions (`malloc`, `free`) into the module without requiring explicit `extern` declarations

### Fixed
- `validateStructType` did not strip the leading `*T` pointer prefix, causing false "undefined struct" errors on `*uint8`, `*Point`, etc.
- `inferBinaryExprType` did not handle the `=` operator for non-numeric types; assignment to pointer fields and struct fields now type-checks correctly
- `isValidAssignment` now allows any-pointer-to-any-pointer assignment for C interop

---

## [0.0.2-alpha]

### Added
- **Struct codegen**: `llvm::StructType::create` per `StructDecl`; `alloca %StructType` for locals; field read/write via `getelementptr`
- **Fixed-size array fields**: `uint8[858]` → `[858 x i8]`; parser now captures the array size (was previously discarded); `IndexExpr` codegen via GEP for both `T[N]` and `*T`
- **Struct literal initialization**: named (`Point { x: 1.5, y: 2.5 }`) and positional (`Point { 1.5, 2.5 }`); fills alloca directly with per-field type coercion
- **Method calls**: methods emitted as `StructName_methodName(ptr self, ...)` mangled functions; `p.method(args)` detects and prepends an implicit `self` pointer
- **`StructInitExpr` AST node**, full visitor chain: parser, type checker, codegen, AST printer
- **`BreakStmt` codegen**: `CreateBr(breakTarget)`; target saved/restored around loop bodies
- **`emitObjectFile()`**: native `.o` via LLVM `TargetMachine` + `legacy::PassManager`; full pipeline `eskiuc file.esk -o file.o` produces a linkable object file
- **Float arithmetic**: `+`, `-`, `*` now emit `fadd`/`fsub`/`fmul` for floating-point operands (previously always emitted integer instructions)

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
- **Phase 0: Build system and CLI**. CMake build with LLVM 17+ integration; `--version` flag; `--test-lexer`, `--test-parser`, `--test-typechecker`, `--test-codegen` modes; `file:line:col` error reporting
- **Phase 1: Lexer**. Complete tokenizer with line/col tracking; all Eskiu keywords (`int`, `float`, `uint8`–`uint64`, `int8`–`int64`, `struct`, `interface`, `enum`, `alloc`, `free`, `extern`, `thread`, `try`/`catch`/`finally`, and more); `COLON` token for type annotations
- **Phase 2: Parser**. Recursive-descent parser producing a visitor-based AST; functions, variables (`let x: int = 5` and C-style `int x = 5` both accepted), structs with fields and methods, `extern` declarations, full control flow (`if`/`else`, `for`, `while`, `break`, `return`), expressions with correct precedence, cast expressions `(TYPE)expr`
- **Phase 3: Codegen**. LLVM IRBuilder backend; arithmetic, comparison, and logical operators; `if`/`else`, `while`, `for`; function calls; integer and float literals; type coercion on initializers; correct lvalue/rvalue split (`evaluateLValue`) so assignments emit `store` to an `alloca` rather than to a value
- **Phase 4: Type checker**. Scope-aware analysis; type inference for all binary and unary operators; struct field validation; function signature checking; `MemberExpr` member-type resolution; parameters registered before the validation pass
- **Types**: `uint8`/`uint16`/`uint32`/`uint64` and `int8`/`int16`/`int32`/`int64` as first-class types mapped to LLVM `i8`–`i64`; `bool` → `i1`; `char` → `i8`; `string` → `i8*`
- **Pointer types**: both leading `*T` and trailing `T*` syntax accepted throughout lexer, parser, and type checker
- **Examples**: `examples/hello.esk`, `examples/test_struct.esk`, `examples/test_struct_error.esk`

### Fixed
- Lexer: `COLON` token was not recognized, breaking `let`-style type annotations
- Type checker: function parameters were not registered before the body validation pass, causing false "undeclared identifier" errors

### Known limitations (resolved in later releases)
- Struct codegen not wired (Phase 5); `MemberExpr` type-checks but does not emit IR
- No heap allocation (`alloc`/`free`), stack only (Phase 6)
- No interfaces or templates (Phase 5)
- No standard library or `Result<T,E>` (Phase 7)
- No lambdas, threads, or async (Phase 8+)
