# Self-hosting dogfood notes

Findings surfaced while writing the lexer in Eskiu. The milestone's point is partly
this: dogfooding finds bugs the fuzzer can't. First payload below.

## Resolved: `//` comment ending in `\` silently swallowed the next source line

**One preprocessor bug, found via two confusing symptoms.** Writing the lexer I hit
what looked like two separate problems:

1. A helper `decode_char_escape(int e)` returned **0** from its `return e`
   fall-through (its literal-return branches were fine) — looked like a codegen
   miscompile, and seemed "layout-sensitive" (vanished when surrounding code
   changed).
2. A deeply nested `if/else` chain failed to parse with `Expected expression, got
   ELSE` — looked like a parser quirk with `else { if ... }`.

Both were the **same** root cause: the preprocessor applied backslash-newline line
continuation **unconditionally**, including when the trailing `\` was inside a `//`
comment. My helper had

```
if (e == 92) { return 92; }   // \\   <- comment ends in '\'
return e;                              <- spliced INTO the comment above, gone
```

so `return e` was eaten → the function fell off the end → garbage/0. The "parser
quirk" was the same: a `// \\` comment ate the next `else` branch, unbalancing the
braces. The "layout sensitivity" was just me editing those `\\`-ending comments
away.

**Fix:** `lexer/preprocessor.cpp` `backslashContinuesLine()` — line continuation
now fires only when the trailing `\` is genuine code, not inside a `//` or `/* */`
comment or a string/char literal. Legit `#define` continuation still works.
Regression test: `tests/comment_backslash_continuation.esk`. Suite 265/0, golden IR
26/26.

**Lesson:** a footgun-class bug (silent code deletion, no diagnostic) that the
fuzzer never hit because it doesn't generate trailing-`\` comments. Worth a fuzzer
generator for backslash-newline in comments/strings.

## Resolved: `--test-parser` crashed on a body-less FunctionDecl  ✅ tooling

`ASTPrinter::visit(FunctionDecl*)` (ast/ast_printer.cpp) dereferenced a null
`node->body`, so a **top-level function prototype** (`int helper(int n);`, forward
declaration) segfaulted `eskiuc --test-parser` (SIGSEGV). Found dogfooding the
self-hosted parser over the real corpus (`tests/forward_decl.esk`). **Fixed:** the
printer now omits the `Body:` section when `body` is null (as `ReturnStmt` already
does for a null value); the self-hosted parser + printer handle prototypes the same
way, so `forward_decl.esk` is back in the parity corpus (43/43). The harness still
skips any file the C++ oracle can't print, as a guard. Codegen was never affected —
only the debug printer.

## Resolved: `<list>` lacked `set` / `remove`  ✅ stdlib gap

Writing the preprocessor's macro table (milestone 3) surfaced that `List<T>` had only
`init`/`push`/`get`/`len`/`free` — no way to overwrite an element (macro redefine) or
remove one (`#undef`). Worked around-free: **added `List_set<T>` and `List_remove<T>`
to `stdlib/list.esk`** (generally useful, not preprocessor-specific). The macro table
then maps cleanly — redefine = `List_set`, `#undef` = `List_remove`, `#else`/`#endif`
edit/pop the conditional stack the same way. Not a bug; a genuine stdlib gap the
dogfood found.

Worth noting: the preprocessor (a faithful port of `lexer/preprocessor.cpp` —
conditionals, recursive object/function macros, splicing, `__FILE__`/`__LINE__`)
passed byte-identical parity on its **first** real run over the whole `tests/` +
`stdlib/` corpus (156/156). A positive signal: the language + stdlib are mature
enough that a non-trivial text-processing pass ports without compiler surprises.

## Resolved: `&&` / `||` did not short-circuit  ✅ codegen (real bug)

**The biggest dogfood find yet.** Writing the self-hosted type checker (Phase B S0/S1),
a guarded `np > 0 && List_get(&params, np-1).ty == "..."` segfaulted when `np == 0`.
Root cause: `CodeGen::visit(BinaryExpr)` evaluated BOTH operands eagerly
(`left = evaluateExpr(...); right = evaluateExpr(...)`) and then emitted
`CreateLogicalAnd/Or` — so `&&` and `||` **never short-circuited**. It was masked
across the whole corpus because almost every RHS is safe to evaluate when the LHS
decides the result (e.g. `a[i] != 0 && b[i] != 0` — `b[i]` reads a valid NUL-terminated
buffer). It only bites when the RHS is *unsafe* under the short-circuit (a null/OOB
dereference): `p != null && p.field` would dereference null. A scalar OOB read happened
not to fault, which is why it surfaced only with a struct-returning `List_get<P>().ty`.

**Fix:** `codegen/codegen_expr.cpp` now lowers `&&`/`||` with a real conditional
branch — evaluate the LHS, `CreateCondBr` to an `sc.rhs` block only when needed, and a
PHI at `sc.cont` for the short-circuit value. Regression test `tests/short_circuit.esk`
(side-effect skip + null-guard). Suite 267/0, golden IR 26/26 (only `map_generic`
re-captured — a `while (i<n && a[i]==b[i])` loop now correctly guards the array read).

## Resolved: imports bypassed the preprocessor  ✅ parser (real gap)

Surfaced wiring up generic codegen (Phase C). The self-hosted driver ran `pp_run` on
the *entry* file only; `parse_program` then resolved `import`ed files (mem/list/…)
directly — they never passed through the preprocessor. So an imported file's `#ifdef`
went unevaluated: `stdlib/mem.esk`'s `#ifdef __ESKIU_FREESTANDING__ … #else … #endif`
kept **both** branches → duplicate `free` (declare + define) + two `alloc<T>` → clang
rejected the `.ll`. Root cause: the C++ folds preprocessing into the lexer and threads
a shared `macros` table per imported file (`Lexer(src, macros, fullPath)`); the
self-host had split `pp_run` out, so imports skipped it. FIX: `do_import` now runs
`pp_run` on each imported file's source before lexing (parser.esk imports
preprocessor.esk — no cycle: pp imports string/list/mem). The critical macro
(`__ESKIU_FREESTANDING__`) is predefined-only, so per-file `pp_run` with a fresh table
suffices (cross-file `#define` sharing not yet threaded — rare, defer). Now the stdlib
`List<int>` monomorphizes + runs to parity (`cg_inputs/stdlib_list.esk`). No regression:
parse_parity 51/51 (preprocessor-free closures → `pp_run` is identity), cg 28/28.

## Phase D: self-compilation reached — dogfooding the codegen found 11 root bugs ✅

Emitting IR for the compiler's own source (tokens→codegen + drivers) through the
self-hosted code generator surfaced eleven codegen bugs, each fixed at the root (no
patches): generic-field struct-type interleave; pointer comparison vs `null`; member
access on a call-result rvalue (materialize into an alloca); one-level vs all-level
pointer stripping (`cg_deref_ty`); `string` indexed as `*char`; pointer arithmetic →
getelementptr; i64 GEP indices; `&&`/`||` short-circuit; call-argument coercion to the
callee's param types; template type-arg resolution through the active substitution; and
the keystone — counting only a TOP-LEVEL `*` so `List<*ExprNode>` is a generic struct
value, not a `ptr` (the mis-lowering corrupted `ExprNode`'s layout). The last two were
the segfault root causes (a bad `alloc` size, a wrong field offset). All five drivers,
compiled by the self-hosted codegen, now match the C++-built ones byte-for-byte, and
`cg_main` compiled by itself reproduces the C++-built codegen's IR exactly over the whole
compiler (a bootstrap fixpoint). Gate: `tests/selfhost/cg_selfhost.sh` (45/45). The
milestone's thesis held once more: dogfooding finds what the corpus can't.

## Resolved: uninitialized-field read in new AST traversals (Linux-only crash) ✅

Adding exceptions/closures introduced two *unconditional* AST walkers — `cg_has_try_s`
(does a function body contain a `try`? → needs an EH personality) and
`cg_collect_idents_*` (a lambda's free variables). Both recursed into `s.s1`/`s.s2`/
`e.a`/`e.b`/`kids`/`stmt` for **every** node kind. But `alloc` doesn't zero and the parser
only sets the fields each kind uses (`mk_lit` leaves `a`/`b`/`kids` garbage; a vardecl's
`s1`/`s2` are never assigned) — so these walkers dereferenced garbage pointers. It passed
on macOS (heap happened to be zero) and on the first Linux CI run, then a later push
tripped it: `cg parity` FAILed `global_list`/`stdlib_list` with "self-host codegen errored".
Caught locally with macOS guard malloc + `MallocPreScribble=1 MALLOC_PROTECT_BEFORE=1`
(fills fresh allocations with `0xAA`): a deterministic `EXC_BAD_ACCESS` at `0xaaaa…` in
`cg_has_try_s`. FIX: made both walkers **kind-aware** (recurse only into the fields a kind
actually populates), matching the convention the printer / `cg_stmt` / `cg_expr` already
follow. The codebase's standing rule bites again — *alloc doesn't zero; never read a field
a node kind doesn't set.* Guard malloc is the tool to surface it before CI does.

## Resolved: feature-coverage sweep — `(P{…})` parsed as a cast, + the "complete" trap ✅

Taking the codegen from bootstrap-subset to feature-complete (v0.3.0) surfaced a second
wave of dogfood finds. The notable ones:

- **A parser bug, not codegen.** `member_temp` crashed in `parse_postfix`. Root cause:
  `(P { x: 9 }).y` — a parenthesized struct literal — was mis-parsed as a **cast** `(P)…`,
  because `P` is a known type and the cast heuristic only checked "`(` then a type name",
  never that a `)` actually follows. Fixed by making cast detection *speculative*: parse the
  type, treat it as a cast only if `)` follows, else back out and let the parenthesized
  expression parse. (`(int)x` / `(*void)p` casts still work.) Self-hosting dogfoods the
  *parser* too, not just codegen.
- **Method-on-primitive dispatch.** `a.cmp(b)` where `a: int` (a primitive satisfying a
  constraint via a free function) ran the struct-method path and indexed a non-existent
  struct's fields → crash. Fixed by lowering `a.m(b)` → `m(a, b)` (receiver by value) when
  the receiver isn't a struct/ADT and a free `m` exists — mirroring the C++ scalar-primitive
  constraint dispatch.

**The meta-lesson (paid for ~4×): never claim "feature-complete" from the bootstrap
fixpoint.** It only exercises the slice of the language the compiler's *own* source uses — it
says nothing about floats, unions, the `?` operator, etc. The only proof is to push the
*whole* feature corpus through the behavioral oracle and get a clean sweep. And enumerate the
**full** failure set every round: an early triage truncated 21 failures to 17 (the terminal
cut off the tail), so the punch-list undercounted and "only N left" was wrong twice. A system
that checks itself only confirms the paths it walks.

## Minor ergonomics (not bugs)

- Nested conditionals: prefer `else if`, flat `if`+`return` (see `keyword_type`), or
  an accumulator over deeply nested `else { if ... }` — all parse fine; the earlier
  "parse failure" was the comment bug above, not the syntax.
- **The keyword-as-identifier footgun (struck ~13× across the whole self-host).**
  `fn`, `in`, and `match` are reserved keywords; using one as a variable / parameter /
  field name makes `eskiuc` fail far from the cause — a local `fn` gives `Error parsing
  declaration: Expected ';'`, a param `fn` gives `Expected parameter name` then `Expected
  expression, got FN`, a counter `int fn` gives `Expected expression, got INT`. The first
  cost an hour to bisect; later ones cost minutes once the pattern was known. Renames used:
  `f`/`nf`/`fnp`/`nm`/`curfn` (for `fn`), `inp` (for `in`), `eq` (for `match`). **This is
  the single most recurring papercut of the project — improving the C++ parser's diagnostic
  to "expected a name, found keyword 'fn'" would pay for itself many times over.** Not a
  correctness bug.
