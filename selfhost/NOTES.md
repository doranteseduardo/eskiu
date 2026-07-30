# Self-hosting dogfood notes

Dogfooding the compiler in Eskiu found bugs the fuzzer couldn't, which is partly the point
of the milestone. The individual fixes live in git history and `CHANGELOG.md`; this file
keeps only the **durable lessons** and the **follow-ups still worth acting on**.

## Lessons that still apply

- **`alloc` doesn't zero; never read a field a node kind doesn't set.** Several AST walkers
  recursed into `s1`/`s2`/`a`/`b`/`kids` for *every* node kind and dereferenced garbage from
  fields the parser never set for that kind, a Linux-only crash (the macOS heap happened to
  be zero). Make every traversal kind-aware. Surface it before CI with macOS **guard malloc**
  (`MallocPreScribble=1 MALLOC_PROTECT_BEFORE=1` fills fresh allocations with `0xAA`, giving a
  deterministic fault at `0xaaaa…`).

- **Never claim "feature-complete" from the bootstrap fixpoint.** It only exercises the slice
  of the language the compiler's own source uses. It says nothing about floats, unions, the
  `?` operator, etc. The only proof is pushing the *whole* feature corpus through the
  behavioral oracle for a clean sweep. And enumerate the **full** failure set every round: a
  truncated triage (21 failures shown as 17) made the punch-list undercount twice. A system
  that checks itself only confirms the paths it walks.

- **Dogfooding finds what the corpus can't.** Writing real Eskiu programs surfaced genuine
  compiler bugs the fuzzer never generated: `&&`/`||` not short-circuiting, a parenthesized
  struct literal mis-parsed as a cast, and more. Conversely, the preprocessor (a faithful
  port) hit byte-identical parity on its first run over the whole corpus, a sign the language
  and stdlib are mature.

- **IR attributes are UNSOUND to emit without dedicated analysis, even `sret`.** A reviewer
  asked for richer LLVM metadata (`nonnull`/`noundef`/`nocapture`/TBAA/...). Most is unsound
  here: pointers are nullable (so a parameter, including a method receiver, since `p.m()`
  passes a possibly-null `p`) can't be `nonnull`; locals can be uninitialized, so params/
  returns can't be `noundef`; `nocapture`/TBAA need escape/alias analysis the front-end
  doesn't have. The `sret` pointer *looked* safe (a fresh dedicated alloca per call site) and
  passed the full macOS/arm64 suite + 800 O0-vs-O2 fuzz iterations. **But `noalias`/`nonnull`
  on it crashed three HTTP/2 tests with SIGILL on Linux/x86-64 CI** (the `currentSretParam`
  forwarding path, the `x = x.method()` style, can make the slot alias, and the x86-64 backend
  exploited it into an `unreachable`/`ud2`). Reverted. Lessons: (1) an attribute needs a
  *soundness proof over every path that produces the value*, not just the common one; (2) the
  macOS arm64 suite + fuzzer is **not** sufficient: a wrong attribute can pass there and
  still miscompile on Linux/x86-64, so attribute work must be gated on the Linux CI before
  shipping. Attributes are a real analysis task (tracked in `PROMOTION_PLAN.md`), not a quick
  add.

- **The two back-ends can silently disagree on a synthesized default, and the corpus sweep
  will not catch it.** A non-void function that fell off the end compiled under both, but the
  C++ back-end synthesized `ret 0`/`ret null` (so it ran, returning zero) while the self-host
  emitted `unreachable` (so it crashed with SIGILL). The feature-complete `cg_parity` sweep
  stayed green because every corpus program had explicit returns, so nothing exercised the
  fall-through terminator. The fix was to make the *language* define the case (missing return
  in a non-void function is now a sema error in both compilers, via a definite-return
  analysis) rather than leave each back-end to invent a default. Lessons: (1) any
  compiler-synthesized construct (a default terminator, an implicit conversion, a zero-init)
  is a place the two implementations can drift, so pin the semantic in a *shared front-end
  pass*, not in each back-end; (2) a clean whole-corpus sweep proves parity only for inputs
  the corpus contains: it says nothing about the edges (fall-through, empty match, zero-trip
  loop) no program happens to hit, which is exactly where P3 needs adversarial inputs, not
  just the existing files.

## Promotion P3: whole-corpus behavioral equivalence (in progress)

Running the WHOLE `tests/` corpus through the Eskiu-built compiler (emit `.ll` → clang →
run, compare to `.expected`) is the P3 acceptance gate. First sweep: 118/138 positive tests
passed; the deltas are the real self-host-vs-C++ divergences P3 must close. Codegen bucket
progress (each fix in lockstep, bootstrap fixpoint held throughout):

- **DONE** http2 (7): a stdlib name collision (`H2_STREAM_CLOSED`); renamed the stream-state
  enum to `H2_STATE_*`. See the codegen note above.
- **DONE** async-generic futures (3: join_value, net_async, select_value): a lambda queued
  inside a generic function lost the type-param substitution when emitted later, so
  `future_complete<Pair<A,B>>` never monomorphized. `CgLambda` now snapshots `cursub`, and
  the drain loop runs to a fixpoint (lambdas/instantiations can queue each other).
- **DONE** bare-fn-as-closure (3: net_echo, http_async[_concurrent]): `thread_create`/
  `free_closure` did `extractvalue %closure @fn` on a raw fn pointer; now coerced to a
  `{@__fnptr, null}` closure first.
- **DONE** const array dims (const): a named-const struct-field dimension (`int[CAP]`) wasn't
  folded before the struct type was emitted; added a Pass 0b that registers const-ints first.
- **DONE** synchronous for-in (for_in, const, slice, slice_ptr): `cg_stmt` had NO `SK_FORIN`
  path (only async fns got for-in desugared), so sync for-in emitted nothing → 0. Added an
  inline desugar to a counted for (array / slice / list-like), testing slice before array
  since `T[]` also matches `cg_is_array`.

Remaining codegen deltas (not yet done):
- **escapes**: a string literal with an embedded `\0` (`"a\0b"`) is truncated. String values
  flow through the lexer/parser/codegen as C strings, so bytes after the NUL are lost. Needs
  an explicit byte length threaded from the lexer through the AST (`ExprNode`) to
  `cg_string_global` (which currently does `cg_strlen`). Niche; deferred.
- **loop_locals**: local `alloca`s are emitted inline at the vardecl, not hoisted to the
  function entry block, so a local declared in a loop body allocates every iteration and a
  long loop overflows the stack (segfault). The C++ back-end hoists via `entryAlloca`. Fix:
  buffer local allocas and emit them in the entry block.
- **multipart**: emits `sub ptr 0, %p` (pointer negation) → invalid IR. A pointer-difference
  / negative-offset codegen path needs to compute the offset in an integer type.

Sema bucket (separate, larger): 26/51 negative tests are not rejected by the self-host sema
(missing checks: bounds, compare-typing, div-by-zero, float→int, redefinition, uninitialized,
main-void, …), and 5 valid programs are wrongly REJECTED (errdefer + the generics map/
map_generic/sort/variadic). This is the deferred "self-host sema parity" residual.

## Open follow-ups (worth doing, not yet done)

- **Self-host codegen does not unique duplicate top-level global names, and folds const
  ints first-wins where C++ loads last-wins.** Two module-level `const int`s with the same
  name emit two `@name` globals (clang: `redefinition of global`), while the C++ back-end
  relies on LLVM auto-uniquing (`@name.2`) + `defineSymbol` last-wins, so it stays valid and
  resolves references to the *later* definition. The self-host also folds a const-int
  reference through `econsts` (first-wins) instead of loading the global, so even absent the
  redefinition the value would differ. Surfaced by the only real collision in the corpus
  (`H2_STREAM_CLOSED` in `stdlib/http2.esk`, an RFC error code that shadowed a stream state),
  fixed at the root by renaming the stream-state enum to `H2_STATE_*` (promotion P3, codegen
  slice 1). No other post-preprocess collision exists in-tree, so this is a latent robustness
  gap, not a corpus blocker: if one ever appears, teach `cg_add_global` to unique the name
  and make the `econsts`/global lookup last-wins to match C++.

- **Self-host codegen: `List<T>` (and generic structs) instantiated over a
  function-pointer element type (`List<fn()->int>`) emits invalid IR.** The method
  self-parameter is declared as the by-value struct type instead of `ptr` (a mangling /
  by-value-vs-pointer confusion specific to a fn-ptr type argument), so clang rejects the
  call. The C++ back-end handles it. Niche (closures are the idiom for stored callables),
  so deferred; fix in the self-host generic-instantiation mangling. Found in the 0.4.0
  correctness sweep.

- **Keyword-as-identifier diagnostic: DONE in both parsers (C++ v0.3.0, self-host R3).**
  `fn`/`in`/`match` (and type names) used as a variable/param/field name now report
  `expected a name, found keyword 'fn'` at the cause instead of a downstream
  `Expected ';'`/`Expected expression`. This was the single most recurring papercut of the
  self-host (~13 strikes). The C++ fix lives in `Parser::consume` (the IDENT case), the
  typed-local-decl path in `parse_decl.cpp`, and the speculative decl-vs-expression
  fallback in `parse_stmt.cpp` (only an IDENT/`*` start falls back; a leading type keyword
  surfaces its real error). The self-hosted `parser.esk` mirror (`check_name_not_keyword`,
  called from `try_var_decl` and `parse_decl_fallback`) shipped as R3; the promotion work
  it unblocked (P0 native link, P1 full CLI parity) has since caught its own slips (an
  `fn`-named local in `esk_main`).
