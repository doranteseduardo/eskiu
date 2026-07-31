# Promoting the Eskiu-written compiler to the primary build (road to v1.0)

The self-hosting milestone (v0.3.0) proved the Eskiu-written compiler is *correct*
(a 3-stage bootstrap fixpoint, cc1 ≡ cc2) and *feature-complete* (a clean 50/50
sweep of the C++ corpus through the behavioral oracle). It still rides **alongside**
the C++ `eskiuc`: the production binary is C++, and the Eskiu compiler is dogfood
validated in CI. v1.0 promotes the Eskiu-written compiler to *the* compiler.

This is a consolidation goal, not a feature: the feature freeze holds. The plan is
staged and every stage is parity-gated, mirroring how the back-end was self-hosted
(see `BACKEND_PLAN.md`). No big-bang flip.

## The gap today

`selfhost/esk_main.esk` (~80 lines) is a one-shot: it reads one `.esk`, runs
preprocess → parse → sema → codegen, and writes **textual LLVM IR to stdout**.
A test harness pipes that IR through `clang` to get a binary. The C++
`main.cpp` (~470) + `main_support.cpp` (~290) own the entire *user-facing*
compiler: argument/option parsing, the `--test-{lexer,parser,typechecker,codegen}`
debug modes, `eskiuc run`, `eskiuc fmt [--check]`, `-o`, `--asan`/`--ubsan`,
warning flags, multi-file input, and the **assemble + link handoff to clang**
(building the clang command line, sanitizer wiring, temp-file management for `run`).

So "promote to primary build" decomposes into a **driver-parity** problem (the
Eskiu compiler must own the whole CLI and the link step, not just IR emission),
then a **flip** (build/install/dist produce the Eskiu-built binary as `eskiuc`),
each gated against the C++ oracle that v0.3.0 established.

## Prerequisites: the three open residuals (close these first)

These are the known soft spots from self-hosting. They become *user-facing* the
moment the Eskiu compiler is primary, so they are prerequisites, not afterthoughts.

- **R1: `for-in` element typing. DONE (stamp + fallback, matching C++).** Sema now stamps
  the loop-variable (element) type onto the `SK_FORIN` node (`resolved_elem`, mirroring the
  C++ `resolvedElemType`): arrays `T[N]` and slices `T[]` resolve from the iterable's
  inferred type, and the loop variable is typed accordingly instead of the old untyped `""`.
  Both consumers (the async desugar in `async_lower.esk` and the synchronous `for-in`
  lowering in `codegen.esk`, added in P3) prefer the stamped type and fall back to the
  structural resolution only when it is absent (a list-like container, or the no-sema
  `--test-codegen` path). This is exactly the C++ architecture: `typecheck_stmt` stamps,
  the lowering consumes with an empty-string fallback (`async_transform`). Fully removing
  the structural resolver is deliberately NOT pursued: the self-host sema has no struct
  field-type table or generic substitution (list-like element typing lives at the lowering
  layer, where `al_generic_data_elem` already resolves it correctly), so a full removal
  would mean expanding the self-host type system for no correctness gain. The original
  generic-container miscompile was fixed back in 0.4.0; this closes the maintainability
  item by matching C++'s stamp-and-consume shape. All gates green (bootstrap, tc/cg/parse/
  corpus parity).
- **R2: parse-parity corpus coverage. DONE (v0.3.1).** `parse_main` now preprocesses
  the top-level file (matching the C++ `--test-parser`, which folds preprocessing into
  the lexer), with `g_pp_os=""` and an empty `__FILE__` to mirror `loadProgram` exactly.
  The preprocessor-closure exclusion is gone, so `parse_parity.sh --full` covers the
  whole corpus: **51 → 121**, no "excluded files" caveat remains.
- **R3: keyword-as-identifier diagnostic.** Using `fn`/`in`/`match` as a name fails
  far from the cause (`Expected ';'`, etc.), the single most recurring self-host
  papercut. **The C++ parser is done (v0.3.0):** `expected a name, found keyword 'fn'`
  at the cause (`Parser::consume`, the typed-local-decl path, and the speculative
  decl-vs-expression fallback). **DONE:** mirrored in `selfhost/parser.esk` via
  `check_name_not_keyword` (called at the var-decl name sites in `try_var_decl` and
  `parse_decl_fallback`); the self-host previously accepted `int fn;` silently (it had no
  parser error path at all), now it prints `expected a name, found keyword 'fn'` at the
  cause and exits 1. This also gave the self-host its first parser-error mechanism (a
  building block for P0+). The core message matches C++; the C++'s "Error parsing
  declaration:" prefix (a driver-level recovery wrapper) and multi-error recovery are not
  mirrored (single error + exit for now). Negative test: `tests/errors/keyword_as_name.esk`.
  (Parser error text is not in any byte-exact gate, so this is a consistency task.)

## Stages (each parity-gated, built in order)

- **P0: Driver parity scaffold. DONE.** `esk_main.esk` is now a real CLI: it parses
  `[-o <out>] <file.esk>`, and with `-o` it assembles the IR, writes a temp `.ll`, and
  invokes clang (via a new `system()` extern) to link a *native binary* end to end. No
  process/exec extern existed yet, so `system()` was added. Without `-o` it still emits
  `.ll` to stdout (the bootstrap harness relies on that, unchanged). Gate:
  `tests/selfhost/driver_parity.sh` builds a sample with both compilers and asserts
  identical exit code + stdout from the resulting binaries (corpus in
  `tests/selfhost/driver_inputs/`; 4/4 green). Bootstrap fixpoint still holds. The corpus
  is plain-clang-linkable programs; sanitizer/thread/exception link flags are P1.
- **P1: Flag + mode parity. DONE.** Port the rest of the C++ CLI surface to the
  Eskiu driver. **DONE (slice 1):** `esk_main` now dispatches `--version`,
  `--test-parser`, `--test-typechecker`, and `--test-codegen`, and the `parse_parity`,
  `tc_parity`, and `cg_parity` gates were switched to drive through it (`esk_main
  --test-X`), all green (parse 142/142, tc OK, cg 97/97). Two mode-specific subtleties
  had to match the C++ test path: it leaves the platform macro unset for
  `--test-parser`/`--test-typechecker` (so `esk_main` sets `g_pp_os=""` there, host macro
  otherwise), and `--test-parser` leaves `__FILE__` empty (pass "" as the pp filename).
  **DONE (slice 2):** `--test-lexer` too. The token dump was extracted from `lex_main`
  into `lexer.esk` (`lex_dump` + `lex_emit_value`, renamed to avoid a clash with
  `pp_main`'s own `emit_value`), and `lex_parity` was switched to `esk_main --test-lexer`
  (137/137). So all four `--test-*` modes plus `--version` now dispatch through `esk_main`,
  and all four parity gates drive through it; bootstrap fixpoint holds.
  **DONE (slice 3):** the remaining `-o`-path surface. `esk_main` now accepts multiple
  input files (parses each into one shared `*Program` via `parse_into`, so the shared
  import/type-name session dedups across files like the C++ driver), threads `--asan`/
  `--ubsan` into the clang link as `-fsanitize=address`/`-fsanitize=undefined`, and
  accepts (and ignores) unknown `-flags` like `-Wall`/`-Wextra` so the CLI shape matches.
  `--version` prints `Eskiu X.Y.Z` (the C++ appends `(LLVM …)`, which the self-host does
  not link). All four `--test-*` modes, `--version`, `-o` native linking, sanitizers, and
  multi-file input now dispatch through `esk_main`; every parity gate + bootstrap fixpoint
  holds.
- **P2: `run` + `fmt` parity. DONE.** `esk_main run script.esk [args...]` compiles to a
  unique `/tmp` exe (reusing the P0 clang link path), execs it forwarding the program's
  argv, propagates the child's exit code (`WEXITSTATUS`), and deletes the temp; flags
  before the script go to the compiler, everything after it is the program's argv, exactly
  like the C++. `esk_main fmt [--check] file …` reindents in place: the reformatter is a
  new module `selfhost/fmt.esk` (`format_source`), a byte-for-byte mirror of the C++
  `formatSource` (4-space nesting, one blank line between blocks, trailing whitespace
  stripped, verbatim inside strings/chars/comments), driven by a self-host `run_fmt` that
  mirrors the C++ (`--check` prints unformatted files and exits 1). Gates:
  `tests/selfhost/run_parity.sh` (self-host `run` vs C++ `run`, exit + stdout, 4/4) and
  `tests/selfhost/fmt_parity.sh` (self-host `fmt` byte-identical to C++ `fmt` over the
  whole in-repo corpus, 392/392, which implies idempotency since the C++ formatter is).
  `fmt.esk` joined the `cg_selfhost` module set (110/110); bootstrap fixpoint holds.
- **P3: Whole-corpus behavioral equivalence. DONE.** The parity oracle now covers the
  **entire** `tests/` corpus on every CI run, not just the self-host input set:
  - **Positive side** (`tests/selfhost/corpus_parity.sh`, new): every positive test is
    compiled by the Eskiu-built compiler end to end (emit `.ll` with the full pipeline incl.
    sema, then clang, then run) and must produce the exact exit + stdout the C++ compiler
    does (`.expected` for run tests, exit 0 for smoke tests). 143/143.
  - **Negative side** (`tests/selfhost/tc_parity.sh`, extended): the self-host
    `--test-typechecker` verdict must match C++ on every positive file (never falsely
    reject), and every error class must reject with the C++ `EXPECT-ERROR` diagnostic.
  Closing this required two campaigns, both landed in lockstep with all gates green
  (bootstrap `cc1 ≡ cc2`, cg/driver/run/fmt parity):
  - **Codegen**: 7 fixes so all 138 positive tests emit valid IR with correct output
    (http2 stdlib name collision; generic substitution into deferred closure bodies; bare
    fn -> closure decay; const array-dim folding; synchronous `for-in`; entry-block alloca
    hoisting; pointer difference; embedded-NUL string length; see the codegen notes above).
  - **Sema**: the self-host checker reached accept/reject parity with C++. First the 5
    false-rejects (mostly: it type-checked generic bodies, which C++ defers to instantiation),
    then all 23 missing checks (main-int, redefinition, defer escape, div-by-zero, non-lvalue
    incdec, a real expression-type-inference layer feeding init/compare/ternary/fn-type
    checks, constant array bounds, dangling-local and uninitialized-read flow scans, multi-
    char char literal, `#error`). 51/51 negatives now reject; three still differ only in
    lexer/parser diagnostic *text* (a consistency item, tracked in `NOTES.md`, not a
    behavioral divergence). Detailed method + rules in `NOTES.md`.
- **P4: The flip. DONE as a dual-build; the C++ binary stays the shipped artifact.**
  CMake now builds both: the C++ `eskiuc` (bootstrap seed + differential oracle) and,
  with it, the Eskiu-written compiler from `selfhost/esk_main.esk` as `eskiuc-esk`
  (`add_custom_target(eskiuc-selfhost ALL ...)`, guarded on clang being present). CI builds
  the `eskiuc-selfhost` target on every run, and its behavior is gated by the bootstrap +
  `corpus_parity`/`tc_parity` suites. So the self-hosted compiler is a first-class, verified
  build artifact.

  **What was deliberately NOT flipped: the distributed binary.** The plan's original intent
  was to *ship* the Eskiu-built binary. The blocker is fundamental, not incidental: the
  self-host has no LLVM bindings by design (it emits LLVM IR *text* to stay dependency-free),
  so `eskiuc-esk` shells out to `clang` to assemble + link, i.e. it needs clang installed at
  runtime. The C++ `eskiuc` bundles LLVM and emits objects itself, so it only needs any C
  linker. Shipping the Eskiu-built binary would therefore *add* a clang runtime requirement
  for users, a real regression. The decision (see the P4 discussion) is to keep shipping the
  self-contained C++ binary and treat `eskiuc-esk` as the dogfood/verification artifact. A
  true shipped flip is deferred until either the clang dependency is accepted or the
  self-host gains direct object emission (which would mean LLVM bindings, a large project
  that trades away the self-host's dependency-free property).

## What stays true throughout

- **The C++ compiler is never deleted.** It is the bootstrap seed and the differential
  oracle. "Primary" means *shipped + default*, not *only*.
- **Every stage is reversible and gated.** Nothing flips until P3 (whole-corpus
  behavioral equivalence) is green in CI alongside the existing gates (suite,
  asan/ubsan, fuzz, golden IR, the `selfhost/` parity/bootstrap suite).
- **Feature freeze holds.** Any language/stdlib pain the work surfaces is fixed at the
  root (as with `List_set`/`List_remove`); no new language surface. Log finds in
  `NOTES.md`.
- **Lockstep where two implementations exist.** Diagnostics and printer changes (R3,
  any sema message tweak) land in both the C++ and Eskiu sources together, so parity
  stays byte-exact regardless of which binary is primary.

## Sequencing

R1–R3 (prerequisites) → P0 → P1 → P2 → P3 → P4: **all landed.** The Eskiu-written compiler
is behaviorally equivalent to the C++ one over the whole corpus (CI-gated) and is a
first-class build artifact (`eskiuc-esk`, dual-built by CMake). What remains outside this
plan for v1.0 is product surface (a package manager) and, if ever desired, a *shipped*
flip (deferred: the self-host links via clang, so shipping it would add a runtime clang
requirement; see P4). Update `docs/dev/phases.md`, `CHANGELOG`, and `NOTES.md` as any
follow-up lands (the docs-before-next-step rule).
