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

- **IR attributes are UNSOUND to emit without dedicated analysis — even `sret`.** A reviewer
  asked for richer LLVM metadata (`nonnull`/`noundef`/`nocapture`/TBAA/...). Most is unsound
  here: pointers are nullable (so a parameter — including a method receiver, since `p.m()`
  passes a possibly-null `p`) can't be `nonnull`; locals can be uninitialized, so params/
  returns can't be `noundef`; `nocapture`/TBAA need escape/alias analysis the front-end
  doesn't have. The `sret` pointer *looked* safe (a fresh dedicated alloca per call site) and
  passed the full macOS/arm64 suite + 800 O0-vs-O2 fuzz iterations — **but `noalias`/`nonnull`
  on it crashed three HTTP/2 tests with SIGILL on Linux/x86-64 CI** (the `currentSretParam`
  forwarding path — `x = x.method()` style — can make the slot alias, and the x86-64 backend
  exploited it into an `unreachable`/`ud2`). Reverted. Lessons: (1) an attribute needs a
  *soundness proof over every path that produces the value*, not just the common one; (2) the
  macOS arm64 suite + fuzzer is **not** sufficient — a wrong attribute can pass there and
  still miscompile on Linux/x86-64, so attribute work must be gated on the Linux CI before
  shipping. Attributes are a real analysis task (tracked in `PROMOTION_PLAN.md`), not a quick
  add.

## Open follow-ups (worth doing, not yet done)

- **Keyword-as-identifier diagnostic — DONE in the C++ parser (v0.3.0).** `fn`/`in`/`match`
  (and type names) used as a variable/param/field name now report `expected a name, found
  keyword 'fn'` at the cause instead of a downstream `Expected ';'`/`Expected expression`.
  This was the single most recurring papercut of the self-host (~13 strikes). The fix lives
  in `Parser::consume` (the IDENT case), the typed-local-decl path in `parse_decl.cpp`, and
  the speculative decl-vs-expression fallback in `parse_stmt.cpp` (only an IDENT/`*` start
  falls back; a leading type keyword surfaces its real error). The self-hosted `parser.esk`
  mirror is still pending, tracked as R3 in `PROMOTION_PLAN.md`.
