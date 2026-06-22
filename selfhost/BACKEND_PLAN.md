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

**Slices:** S0 de-risk — `int main(){return 42;}` → emit `.ll` → clang → run → exit 42.
Then: arithmetic/locals (alloca/load/store), control flow (if/while → labelled blocks +
br/condbr), structs + GEP + bitfield layout, function calls + sret, closures (fat
`{ptr,ptr}` + env structs), ADTs (`{i32 tag,[N x i64]}`). **Defer** exceptions
(landingpad/invoke) and atomics — the gnarliest ~40%; if textual emission proves too
painful there, bind just those few ops via the LLVM-**C** API (`extern`) as a targeted
hybrid, rather than binding the whole API.

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
