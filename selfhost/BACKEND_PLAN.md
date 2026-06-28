# Self-hosting the back-end — roadmap

AST enrichment → sema → codegen → self-compilation. The plan for finishing the
self-hosted compiler after the front-end (lexer, parser+imports, preprocessor) landed.

## Context

The Eskiu front-end is self-hosted and CI-gated: the **lexer** (114/114 vs
`--test-lexer`), the **parser** with import resolution (50/50 vs `--test-parser`), and
the **preprocessor** (156/156 through the lexer). Each was a faithful port of a
~700-line C++ pass with a byte-exact parity oracle, and the dogfood caught real bugs.

Directive: **eventually self-host everything** (the v1.0 goal — `eskiuc` compiles
itself). This is the roadmap for the remaining back-end — semantic analysis and code
generation — plus the capstone of compiling the compiler with itself. It is a
multi-phase, multi-session roadmap: Phase A is detailed (next, and unblocks the rest);
Phases B–D are sketched at decreasing detail and re-sliced when reached.

The back-end is qualitatively harder than the front-end:
- **Bigger**: `sema/` is 3335 lines, `codegen/` is 3990 — each larger than the whole
  front-end already ported (2377).
- **AST is lossy**: the self-hosted AST (`selfhost/ast.esk`) was built only to
  reproduce the `--test-parser` dump, so the parser *consumes but discards* struct
  methods, generic type-params + constraints, bitfield widths, ADT payloads, async/
  escaping/const/packed modifiers, full interface signatures, asm clobbers, and source
  positions. All are needed downstream → **AST enrichment is the shared prerequisite**
  for both sema and codegen.
- **Weaker oracles**: most programs type-check cleanly (sema oracle ≈ "succeeded"
  verdict), and codegen IR can't be matched byte-for-byte (SSA numbering, constant
  folding). So the back-end leans on verdict+diagnostic parity (sema) and *behavioral*
  parity (codegen), not the byte-exact diffs the front-end enjoyed.

## Goal

A self-hosted semantic analyzer and code generator, each parity-gated against the
existing C++ oracles (`--test-typechecker`, `--test-codegen`), built incrementally;
culminating in compiling a real `.esk` program end-to-end with the Eskiu-written
pipeline (and ultimately the compiler's own source). Tooling/dogfood — the production
C++ compiler stays untouched except for *additive, lockstep* debug-printer extensions.

---

## Phase A — AST enrichment (the prerequisite)

**Status (2026-06-22): structural items DONE + lockstep-gated.** Captured: generic
type-params + constraints, struct methods, enum ADT payloads, bitfield widths, the
`async` modifier, full interface method signatures. (Match-arm bindings were already
captured/printed.) Each validated by extending both printers in lockstep; corpus
parity 50/50, synthetic 11/11 (`enriched.esk` exercises all of it). **Deferred to
Phase B** (parity-neutral — the C++ `--test-parser` printer doesn't show them, so no
lockstep gate; add when sema needs them): var `const`/`volatile` (recoverable from the
type string, which IS gated), struct `packed`/`packAlign`, per-param `escaping`, asm
clobbers, and source line/col positions.

Make `selfhost/ast.esk` + `selfhost/parser.esk` capture everything sema/codegen need.
Today the parser already *parses* these constructs (it must, to stay byte-identical),
it just throws the details away — so this is mostly "store what's already being read,"
not new grammar.

**Field checklist** (each maps to a known parser discard site → an `ast/ast.h` field):

| Add to AST | Currently discarded at | C++ field |
|---|---|---|
| Generic type-params + constraints (fn/struct/enum) | `parser.esk` `skip_type_params` (~1079, 1120, 1190, 1258) | `typeParams`, `constraints` |
| Struct methods (FunctionDecl list) | `parser.esk` ~1196 (consumed, "printer ignores") | `StructDecl::methods` |
| Bitfield widths on fields | `parser.esk` ~1204 | field `bitWidth` |
| Enum ADT payloads (variant → field types) | `parser.esk` ~1265 | `EnumDecl::payloads` |
| Per-param `escaping` flags | `parser.esk` ~1106 | `paramEscaping` |
| `async` fn modifier | `parser.esk` ~1347 | `isAsync` |
| Var `const`/`volatile` quals | `strip_binding_const` ~730 | `isConst`, `isVolatile` |
| Packed struct + `#pragma pack` align | `parser.esk` ~1349 | `isPacked`, `packAlign` |
| Full interface method signatures (params) | `parser.esk` ~1244 | `InterfaceDecl::methods` |
| Match-arm binding names (structured, not in label string) | `parser.esk` ~938 | match arm `bindings` |
| Asm clobbers | `parser.esk` ~1007 | `AsmStmt::clobbers` |
| **Source line/col on every node** | not tracked (Token carries line/col; parser drops it) | `ASTNode::line/col` |

Positions are free-ish: the self-hosted `Token` already has `line`/`col` (lex_main
prints them); the parser just needs to copy them onto each node at creation.

**Verification (strong gate, recommended):** extend BOTH printers in lockstep —
`ast/ast_printer.cpp` (C++) and `selfhost/ast.esk`'s `print_*` — to emit the
previously-omitted details (type-params, methods, payloads, modifiers, positions).
`--test-parser` is a debug dump (no golden snapshot; suite + golden-IR don't depend on
it), so adding sections is safe as long as the two printers stay byte-identical. This
turns enrichment into a real parity gate (`parse_parity.sh --full` stays green, now
covering the richer dump) instead of an invisible change validated only later.
Fallback if we'd rather not touch the C++ printer: keep enrichment parity-neutral (new
fields un-printed) and let Phase B/C validate them downstream.

**Critical files:** `selfhost/ast.esk` (new fields on `ExprNode`/`StmtNode`/`DeclNode`
+ a `Param.bit_width`, methods/payloads lists), `selfhost/parser.esk` (stop discarding;
copy token line/col), `ast/ast_printer.cpp` + `selfhost/ast.esk` printer (lockstep, if
taking the strong-gate route). Reference: `ast/ast.h` for exact field semantics.

---

## Phase B — Sema (the type checker)

Port the C++ type checker to `selfhost/sema.esk`, consuming the enriched AST. Mirror
its structure:
- **Type IR** — a tagged struct mirroring `ty::Type` (Kind enum: Void/Int/Float/Bool/
  Char/String/Pointer/Array/Fn/Struct/Interface/Template/Named/Param/…), with the
  round-trip `parse(s).str()==s` invariant and `substitute()` for generics. (`type.cpp`,
  237 lines — port first, it underpins everything.)
- **Symbol table** — a stack of name→Symbol maps (`{type, isConst, isParam, used,
  line,col}`); push/pop scope; innermost-out lookup. (`type_checker.cpp:228–365`.)
- **Registries** — structs, templates, interfaces, enum constants, ADT variants, type
  aliases, function signatures.
- **Two-pass walk** — pass 1 registers all decls (forward refs); pass 2 visits bodies,
  annotating `expr→type`. (`type_checker.cpp:21–138`.)
- **Diagnostics** — `file:line:col: message`; queue errors (verdict fails if any),
  warnings to stderr; exit 0/1. Driver `tc_main.esk` mirrors `testTypeChecker`.

**Status (2026-06-24): the type-free checks DONE** (7 error classes). `tc_parity`
HANDLED = undefined_var, arg_count, undefined_type, await_outside_async, async_no_await,
switch_dup_case, unknown_intrinsic. 121/121 verdict parity, 0 false rejections. Built on
registries: FnSig (arg-count), type-names (undefined_type via struct-init base name),
in_async/await_seen (async), per-switch case-value dedup, intrinsic name set.
Writing arg-count surfaced a **real codegen bug — `&&`/`||` didn't short-circuit**
(fixed in `codegen/codegen_expr.cpp`, regression `tests/short_circuit.esk`; see NOTES).
Then a string-based type layer landed (no full ty::Type port — the C++ flows types as
strings at the codegen boundary anyway): typed symbols (`Sym.type`), a struct
field+method registry (`Struct.member` keys), and `sema_infer_type` (idents for now).
That unlocked **undefined_field** (`ident.field`, auto-deref via `sema_struct_of_type`),
**match_duplicate** (dup arm variant), and **match_nonexhaustive** (enum-variant
registry `s.evars` + subject-type inference + `_` wildcard). HANDLED is now **10**.
Still 121/121, 0 false rejections.

**const_* DONE (all 5).** Captured the value-const flag in ast/parser (`mk_vardecl`/
`mk_decl_var` carry `is_const`; `is_value_const`/`strip_binding_const` split — parity-
neutral, parser still 51/51). Sema: `Sym.is_const`, `sema_is_readonly_lvalue`
(ident / member-of-const / index / `*ptr`-to-const), const-no-init, and pointer-const
via spelling (`sema_pointee_const` = starts `const ` + has `*`): const_reassign,
const_no_init, const_field, const_ptr_write, const_ptr_drop. **HANDLED is now 15.**

**Phase B error detection COMPLETE (2026-06-26) — all 19 sema classes.** Added the last
four: trait_unsatisfied/trait_primitive_unsat (structural interface satisfaction — a
struct via a real method `C.m`, a scalar primitive via a free fn `m(C,...)`; checked at
`Name<args>(...)` template calls against the Phase-A type-param constraints),
question_bad_return (`?` requires the enclosing fn to return a Result), escaping_param
(escape analysis — watch non-escaping `fn(...)`-type params, captured `param_escaping`
in the parser; a use outside callee position escapes, mirroring the C++ calleeContext).
`tc_parity` verdict-matches `--test-typechecker` on all 121 positive files AND rejects
all 19 sema `tests/errors/` cases with the right message; the 5 remaining error files are
NOT sema (lexer/parser/pp — caught upstream). Suite 267/0, golden 26/26, all parities green.

**NEXT: Phase C — codegen.** The checker is done; codegen needs fuller expr→type
annotation (grow `sema_infer_type`: call returns, binary promotion, member types) as the
textual LLVM IR emitter needs it. `async_transform` (a lowering pass) is a later sub-phase.

**Status (2026-06-23): S0 DONE.** `selfhost/{sema,tc_main}.esk` + `tc_parity.sh`
(CI-wired). Two-pass name resolution (flat `{name,depth}` scope stack — avoids nested-
List mutation) catches undefined variables; verdict matches `--test-typechecker` on all
120 positive files (0 false rejections), `undefined_var` rejected. Symbol table chose
the flat-list design over nested scopes. The for-init is wrapped in a BlockStmt — must
walk its items in the FOR scope, not via a sub-scope (else the loop var is dropped).
Lambda/match-arm/catch bodies not yet walked (bindings unextracted; safe under-check).
The Type IR (ty::Type port) is deferred to S1 (S0 needs only name presence).

**Slices (each parity-gated):** S0 (DONE) name resolution → undefined-var. S1 Type IR
+ literals/idents/primitive inference + binary/unary promotion. S2
var-decls + const-correctness (`const_*` error tests). S3 struct/enum/fn registration +
calls (arg-count, undefined var/type/field). S4 method dispatch + interfaces +
constraints (trait tests). S5 generics instantiation + unification. S6 match
exhaustiveness + switch dup. S7 async/await validation + escape soundness. Defer
`async_transform.cpp` (651 lines — a *lowering* pass, not checking; needed only for
codegen of async, so it becomes a late codegen sub-phase).

**Oracle:** no byte-exact dump like the front-end. Gate = (a) the whole clean corpus
must type-check ("succeeded", exit 0), and (b) each `tests/errors/` sema case (18 of 24
— the rest are lexer/parser/pp, already ours) must fail (exit 1) with the
`EXPECT-ERROR:` substring present — mirroring `tests/run.sh`'s negative tests.
`tc_parity.sh` runs both `--test-typechecker` and `tc_main` and compares verdict +
diagnostics. Byte-exact diagnostic text (incl. positions) is a stretch goal once
Phase A gives real line/col.

**Critical files:** new `selfhost/sema.esk`, `selfhost/tc_main.esk`,
`tests/selfhost/tc_parity.sh` + a synthetic `tests/selfhost/tc_inputs/`. Reference:
`sema/type.{h,cpp}`, `sema/type_checker.{h,cpp}`, `sema/typecheck_{decl,expr,stmt,type}.cpp`.

---

## Phase C — Codegen (LLVM IR)

Port `codegen/` to `selfhost/codegen.esk`. The C++ uses the LLVM **C++ API** (not
C-ABI → not callable from Eskiu), so the approach is **textual LLVM IR emission**: the
self-hosted codegen walks the typed AST and writes `.ll` text directly (no LLVM
library). This keeps the self-host dependency-free and is the standard bootstrap path.

**Oracle is behavioral, not byte-exact.** LLVM's printer auto-numbers SSA values and
the C++ IRBuilder constant-folds, so matching `--test-codegen` text verbatim is
infeasible. Instead: emit `.ll` → `clang`/`llc` → run → compare program output (and
exit code) against the C++-compiled binary, over a representative corpus. (Optionally
also a *normalized* IR diff — renumber SSA values in definition order — as a tighter
secondary check.)

**Status (2026-06-26): S0 DONE.** `selfhost/{codegen,cg_main}.esk` + `cg_parity.sh`
(behavioral oracle: emit .ll → clang → run, compare exit/stdout to the C++ build).
`int main(){return N;}` → valid `.ll` → native binary with the matching exit code (3/3).
Design: named SSA temps `%tN` (avoid LLVM's strict unnamed 0,1,2… numbering); `cg_lltype`
maps Eskiu types → LLVM (`*`→`ptr`, int→i32, …); `cg_expr` returns an operand string.
Not yet CI-wired (needs clang at runtime + more coverage first).

**Slices:** S0 (DONE) constant return. **S1 (DONE)** scalar locals + params via
alloca/load/store, integer arithmetic / comparison / bitwise / unary-negate, assignment
(`cg_expr` returns `{op, ty}`; named `%tN` slots/temps); cg 7/7. **S2 (DONE)** control
flow: if/while/C-for/break/continue via labelled blocks + `br`/`condbr`, a `terminated`
flag (no double terminators; unreachable trailing block → `unreachable`), loop_cont/
loop_end stacks for break/continue; cg 12/12. (Caveat: allocas aren't hoisted to entry
yet — fine for short loops, revisit for long ones.) **S3a (DONE)** direct function calls
+ recursion: `cg_program` pre-pass records fn→retty (`CgFn`); `call <retty> @f(args)`,
void calls drop the result; cg 15/15. **S3b (DONE)** printf: extern `declare`s, string
literals (module-top `@.strN` constants, LLVM-escaped + opaque-ptr pass), variadic call
type `call i32 (ptr, ...) @printf` — **stdout parity** (cg 19/19). Real corpus programs
that stay within int/control-flow/calls/printf now compile + run identically to the C++
build (hello, recursion, control_flow). **S4a (DONE)** structs + pointers: named struct
types `%P = type {…}`, struct-init via GEP/store, member read/write (lvalue machinery
`cg_lval`/`cg_etype` tracking Eskiu types), address-of `&`, deref `*`; `cg_strip_ptr`
handles both `*P` and `P*`; cg 21/21, real programs ptr_member/test_struct MATCH. **S4b (DONE)** methods: `cg_emit_fn`
factored out (self = leading `ptr %p0`, bound to `self`/`*Struct`); methods emitted +
registered mangled `Struct_method`; `recv.m(...)` call passes self first; structs_methods
MATCH, cg 22/22. **S5 (DONE)** arrays: `T[N]`→`[N x T]`, indexing `a[i]` via GEP (array
base `…, i32 0, idx`; pointer base `T, ptr, idx`); cg 24/24. **S6a (DONE)** plain integer enums (constant → int, type → i32); cg 25/25.
**S7 (DONE) generics/monomorphization** — the big bootstrap slice. `cg_mangle`
(`Box<int>`→`Box_int`, `List_init<int>`→`List_init_int`), `cg_apply_sub` (whole-token
T→concrete in spellings), an `inst_seen` dedup set + `worklist` of `CgInst`. Generic
structs (`gstructs`) instantiate immediately in `cg_lltype`/`cg_find_struct` (substituted
fields → `%Box_int = type{…}`); generic functions (`gfns`) register a concrete signature
at the `EK_TEMPLATECALL` site and emit bodies later via `cg_drain_worklist` with
`cg_push_sub` setting `g.cursub` (param/local/ret etys substituted). Added `EK_SIZEOF`
(GEP-null + ptrtoint — no hardcoded sizes) and `EK_CAST` (ptr/int coercions) so the
stdlib `alloc<T>` (`(*T)malloc(n*sizeof(T))`) monomorphizes. Verified end-to-end on a
self-contained generic dynamic array (`generic_vec.esk`): generic struct w/ pointer
field + alloc + indexing + N generic fns; cg 28/28. **Import-preprocessing (DONE)**:
`do_import` now runs `pp_run` per imported file (the C++ folds preprocessing into the
lexer) so `mem.esk`'s `#ifdef` picks one branch — the stdlib `List<int>` monomorphizes
+ runs to parity (`stdlib_list.esk`); no regression (parse 51/51, cg 28/28). **S8
(DONE) sret + globals, cg 31/31.** sret needed NO code — LLVM first-class aggregates
emit `ret %T`/`call %T`/`store %T` directly and clang lowers them (struct-by-value
params + returns, incl. generic structs, run to parity; `struct_value.esk`). The
compiler returns `CgVal`/`CgStruct` by value pervasively → confirmed. **Global vars**:
pre-pass 1d registers top-level `DK_VARDECL` into `g.gvars` (`@name` = storage address)
+ emits `@name = global <llty> <init>` (literal scalar folds; else `zeroinitializer`,
set up at runtime). EK_IDENT/`cg_lval`/`cg_etype` resolve globals after locals/enums.
Verified the compiler's own `g_imported` pattern: a global `List<int>` (zeroinitializer)
+ runtime `List_init`/push/get (`global_list.esk`).

**PHASE D REACHED — self-compilation works (2026-06-27).** Dogfooding the codegen against
the real compiler source surfaced + fixed (all ROOT fixes, no patches): nested-generic
struct-type interleave (assemble body before emitting the line); pointer comparison/
truthiness (`icmp ptr … null`, not `0`); member access on an rvalue (call result)
materialized into an alloca; `cg_deref_ty` strips ONE pointer level (`**X`→`*X`, vs
cg_strip_ptr's all); `string` indexed as `*char` (i8); pointer arithmetic → getelementptr;
i64 GEP indices; `&&`/`||` short-circuit (alloca result slot); **argument coercion** to
the callee's declared param types (an i32 count widening to an `int64` param — the bug
that segfaulted `alloc<*ExprNode>`); type-arg resolution through the active substitution
in template calls (`alloc<T>`→`alloc<Param>`, not `alloc_T`); and the keystone:
**top-level-only `*` detection** so `List<*ExprNode>` is a generic struct (value), not a
pointer — it was mis-lowered to `ptr`, corrupting `ExprNode`'s layout. RESULT: all 5
drivers (lex/pp/parse/tc/cg), compiled by the self-hosted codegen, produce byte-identical
output to the C++-built ones; and `cg_main.self` (the codegen compiled by ITSELF) emits
byte-identical IR to the C++-built codegen for all 45 inputs incl. the whole compiler —
a verified **bootstrap fixpoint**. Gate: `tests/selfhost/cg_selfhost.sh` (emit-validity +
fixpoint, 45/45). cg 33/33. **Unified driver + 3-stage bootstrap (DONE).** `esk_main.esk`
runs the full pipeline pp→parse→**sema**→codegen (rejects ill-typed input without emitting
IR; all selfhost sources type-check clean). `cg_bootstrap.sh` does the canonical 3-stage
build: C++ eskiuc → cc0, cc0 → cc1, cc1 → cc2; **stage2 ≡ stage3** (cc1 and cc2 emit
identical IR for the compiler AND a sample, cc2 runs correctly). Binary byte-equality is
NOT asserted — Mach-O LC_UUID + ad-hoc signature differ for identical input (linker
artifact). All three codegen gates (cg_parity, cg_selfhost, cg_bootstrap) wired into CI
with `CLANG=clang-22`.

**FEATURE COVERAGE beyond the bootstrap subset (for arbitrary user programs; the compiler
itself uses none of these).** DONE: **floats** (fadd/fsub/fmul/fdiv, fcmp, fneg, int↔float
casts via sitofp/fptosi/fpext/fptrunc, mixed promotion; `cg_is_float`/`cg_farith_op`/
`cg_fcmp_pred`, cg_coerce extended); **switch** (LLVM `switch`, C fall-through + break);
**ADT enums + match** (tagged union `{i32 tag,[N x i64]}`, `CgVariant` registry,
`cg_build_variant`, match = tag switch + payload-binding extraction with arm-scoped
locals). Also a root fix: SK_RETURN now coerces to the fn's return type (bool→int = zext).
**closures (DONE)** — free-variable analysis (`cg_collect_idents_*`) finds captured
enclosing locals; env is a stack struct, the closure value a fat pointer `%closure =
{ptr fn, ptr env}`; lambdas emitted via a worklist (`cg_emit_lambda`), called indirectly
by extracting {fn,env}; fn-type return resolved from the assignment context; C vararg
float→double promotion. `closures.esk` byte-identical. **exceptions (DONE) — full Itanium
ABI** (per user pref, not setjmp): every call in a try body becomes an `invoke` (centralized
in `cg_emit_callish` gated on `cur_unwind`); `throw` allocates a 16-byte `{i64 val, ptr
typename}` and `__cxa_throw`s it (invoke inside a try); the landingpad does `__cxa_begin_catch`,
strcmp-matches catch clauses by type name, binds, `__cxa_end_catch`; `finally` runs at the
join; functions with a try get `personality @__gxx_personality_v0`; EH decls emitted once
when used; programs link `-lc++abi`. `exceptions.esk` byte-identical. cg_parity 37/37,
cg_selfhost 50/50, bootstrap fixpoint green. **atomics (DONE)** — `atomic_load`/`store`/
`swap`/`cas` intercepted in EK_CALL (`cg_is_atomic`/`cg_emit_atomic`) → `load atomic`/
`store atomic`/`atomicrmw xchg`/`cmpxchg` with acquire/release/acq_rel orderings; cas
returns the `{i32,i1}` success bit. `atomics.esk` matches; cg_parity 38/38, cg_selfhost
51/51.

**async/await — LINEAR CASE DONE (2026-06-29).** `selfhost/async_lower.esk` (port of
async_transform.cpp), run in cg_main/esk_main between parse and codegen: each `async fn`
→ frame struct (`ret` first so `&fr.ret == fr`) + `__name_resume` (`while(1){if st==N…}`
state machine) + ctor returning `Future<T>*`; each await splits a state (eval future →
`future_poll<Ti>` → suspend/return → resume reads `fr.__awI.value` + `free_future`);
completion = `fr.ret.value=v; if(atomic_swap(&fr.ret.state,2)==1) fr.ret.waker();`. The
awaited type Ti is resolved from the callee's return type (`produce()`→`Future<int>*`→int,
`al_inner_of_future`). Locals→`fr.<name>` via `al_rewrite`. Needed a codegen fix:
`recv.field()` where field is a closure-typed member is an INDIRECT call (`cg_indirect_call`
factored out), not a method. Linearity guard (`al_is_linear`) leaves control-flow-around-
await async fns untransformed (a clean boundary, not a miscompile). `async_basic` +
multi-await/param run to parity; cg_parity 39/39, cg_selfhost 52/52, fixpoint green.
**NEXT (async, the harder half): control flow around await** — lower if/while/for/switch
containing an await into the state graph with break/continue retargeting (`brkTargets`/
`contTargets`), + the desugar pass (await not already in a `let` → `let __awN = await E`).
Then the remaining async tests (async_break/elseif/cancel/…). After that:

**REMAINING (other): the capstone is largely covered.** It is NOT a
codegen slice but an AST→AST lowering pass (`sema/async_transform.cpp`, 634 lines), run
between parse and codegen. Design (to port into a new `selfhost/async_lower.esk`, invoked
in cg_main/esk_main after `parse_program`): for each `async fn name() -> T`, synthesize
(a) a frame struct `__name_frame { Future<T> ret; int st; FutureHdr* awaiting; <params>;
<hoisted locals>; *Future<Ti> __awI; }`; (b) a `void __name_resume(__name_frame* fr)` whose
body is a `while(1) switch(fr.st)` state machine — each `await` splits a state: evaluate
the future, register a waker closure, `if (!ready) return;`, then resume at the next state
reading `fr.__awI.value`; (c) the entry `Future<T>* name(params){ frame=alloc; init; st=0;
__name_resume(frame); return &frame.ret; }`. Completion: `fr.ret.value = v; if
(atomic_swap(&fr.ret.state, 2) == 1) fr.ret.waker();` (atomics — now available). Pre-passes:
desugar every `await` to a `let __awN = await E;` (recursing control flow); hoist all locals
to frame fields; rewrite local refs to `fr.<name>`. **PREREQUISITES/blockers:** (1) the
awaited type Ti (C++ uses sema's `aw->resolvedType`) — the self-hosted AST lacks it; resolve
it in the pass by looking up the awaited call's return type (`produce()` → `Future<int>*` →
Ti=int). (2) closures (DONE — for the waker), atomics (DONE), generics/Future runtime (DONE).
**De-risk:** `async_basic.esk` (one await of a ready future, linear body) — get that to
byte-match, THEN add control-flow-around-await (if/while/for/switch + break/continue → the
state-graph retargeting, the gnarliest ~half of the pass). Other remaining: unions,
bitfield layout, dynamic trait dispatch. Polish: esk_main errors→stderr.

**Critical files:** new `selfhost/codegen.esk`, `selfhost/cg_main.esk`,
`tests/selfhost/cg_parity.sh`. Reference: `codegen/codegen_{module,decl,stmt,expr,
type,call,closure,adt,scope}.cpp`; `--test-codegen` for the IR shape to emit.

---

## Phase D — Self-compilation (capstone, v1.0)

Wire lexer → preprocessor → parser → sema → codegen (all Eskiu) into one driver that
compiles an arbitrary `.esk` to an object/executable, validated behaviorally against
`eskiuc`. The endgame: feed the compiler's *own* source through it (bootstrap). Far
horizon; planned in detail only once Phases A–C land. Likely surfaces the most dogfood
bugs of all.

---

## Design decisions (baked in)

1. **Codegen = textual IR emission**, behavioral oracle; LLVM-C bindings only as a
   narrow fallback for exceptions/atomics. (Avoids a large FFI surface + LLVM-version
   coupling.)
2. **Defer `async_transform` and exceptions/atomics** to late sub-phases — biggest,
   least-common pieces; core self-hosting shouldn't block on them.
3. **Sema oracle = verdict + `EXPECT-ERROR` substring** (mirrors `tests/run.sh`), not
   byte-exact diagnostics. Byte-exact (with positions) is a stretch goal post-Phase-A.
4. **Phase A uses lockstep printer extension** for a strong parity gate (touches
   `ast_printer.cpp` additively — safe, no golden depends on `--test-parser`).
5. **Feature freeze holds** — fix language/stdlib pain the dogfood surfaces (as with
   `List_set`/`List_remove`); don't add language features. Log finds in `NOTES.md`.

## Verification (per phase)

- **A:** `parse_parity.sh --full` stays green (50/50, now over the richer dump if
  lockstep route); enriched fields populated (spot-check via the extended dump).
- **B:** `tc_parity.sh` — clean corpus all "succeeded"; 18 `tests/errors/` sema cases
  fail with the right substring. Wire into CI.
- **C:** `cg_parity.sh` — each corpus program, self-host-compiled, runs to the same
  output/exit as the C++ build. Wire into CI.
- **All phases:** existing gates stay green — `tests/run.sh` (265), golden IR (26/26),
  asan/ubsan, lexer/parser/pp parity. Purely additive `selfhost/` + tests; the one
  allowed C++ touch is the additive `ast_printer.cpp` extension in Phase A.

## Sequencing

A → B → C → D, strictly (each unblocks the next). Multi-session; the bulk of the road
to v1.0 self-hosting. Update `docs/dev/phases.md`, `CHANGELOG [Unreleased]`,
`STATUS.md`, and `selfhost/{README,NOTES}.md` as each phase/slice lands (the
"docs-before-next-step" rule). Keep `selfhost/` parity steps in CI green throughout.
