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

- **R1: async `for-in` element typing. Miscompile FIXED (0.4.0); refactor still open.**
  `async_lower.esk` resolves the loop element type structurally (array `T[N]` → `xs[i]`;
  list-like → `xs.data[i]`). The bug was that a generic container (`List<T>`, T != int)
  fell through to a default of `int`, truncating elements or emitting invalid IR; the
  desugar now substitutes the container's type argument (`al_generic_data_elem`), matching
  the C++ back-end (verified via `cg_inputs/async_forin_generic.esk`). The cleaner
  end-state, still open, is for sema to STAMP the element type (as the C++
  `resolvedElemType` does) and the lowering to consume it, removing the structural
  heuristic entirely. Not a correctness blocker anymore, but a maintainability item.
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
- **P1: Flag + mode parity. IN PROGRESS.** Port the rest of the C++ CLI surface to the
  Eskiu driver. **DONE (slice 1):** `esk_main` now dispatches `--version`,
  `--test-parser`, `--test-typechecker`, and `--test-codegen`, and the `parse_parity`,
  `tc_parity`, and `cg_parity` gates were switched to drive through it (`esk_main
  --test-X`), all green (parse 142/142, tc OK, cg 97/97). Two mode-specific subtleties
  had to match the C++ test path: it leaves the platform macro unset for
  `--test-parser`/`--test-typechecker` (so `esk_main` sets `g_pp_os=""` there, host macro
  otherwise), and `--test-parser` leaves `__FILE__` empty (pass "" as the pp filename).
  **PENDING:** `--test-lexer` (needs the token-dump helper extracted from `lex_main`),
  `-Wall`/`-Wextra`, `--asan`/`--ubsan` (clang `-fsanitize` flags in the `-o` path), and
  multi-file input. `--version` prints `Eskiu X.Y.Z` (the C++ appends `(LLVM …)`, which
  the self-host does not link).
- **P2: `run` + `fmt` parity.** Port `eskiuc run script.esk [args...]` (compile to a
  temp exe, exec, propagate exit code, clean up) and `eskiuc fmt [--check]` (the
  conservative reindenter). Gate: `run` over the runnable corpus matches; `fmt` stays
  idempotent and byte-identical to the C++ formatter over every test (the existing
  formatter-idempotency pass, now run through the Eskiu binary).
- **P3: Whole-corpus behavioral equivalence.** Promote the parity oracle from the
  self-host input set to the **entire** `tests/` + `stdlib/` corpus on every CI run:
  for each program, the Eskiu-built compiler and the C++ compiler must produce
  binaries with identical exit + stdout. This is the acceptance gate for the flip:
  no divergence anywhere the C++ compiler is exercised.
- **P4: The flip (dual-build, Eskiu primary).** Make the build produce the
  Eskiu-built binary as `eskiuc` and install/dist it; keep the C++ compiler buildable
  as the bootstrap seed (`eskiuc-cxx`) and as the CI equivalence oracle. Concretely:
  CMake (or a thin bootstrap script) builds the C++ seed, the seed builds the Eskiu
  compiler, and the Eskiu binary is the shipped artifact. The Release workflow
  packages the Eskiu-built `eskiuc`. The C++ compiler is retained, not deleted; it
  is the reproducible bootstrap root and the differential oracle.

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

R1–R3 (prerequisites) → P0 → P1 → P2 → P3 → P4, strictly (each unblocks the next).
Multi-session. Update `docs/dev/phases.md` (the v1.0 item), `CHANGELOG`, `STATUS.md`,
and `NOTES.md` as each stage lands (the docs-before-next-step rule).
