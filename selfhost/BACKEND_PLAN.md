# Self-hosting the Eskiu back-end — design, method, and record

How the compiler's back-end (and ultimately the whole pipeline) was reimplemented in
Eskiu. The front-end (lexer/parser/preprocessor) self-hosted first; this records the
back-end (sema → codegen → self-compilation) and the method that kept it honest.

**Outcome (shipped in v0.3.0):** the entire compiler runs in Eskiu (`selfhost/`), reaches
a **3-stage bootstrap fixpoint** (cc1 ≡ cc2), and the code generator is **feature-complete
against the C++ corpus** — a full feature sweep passes 50/50. The slice-by-slice build
history lives in git, `CHANGELOG.md`, and the dogfood lessons in `NOTES.md`; this file is
the durable design + method, not a changelog.

## Approach & design decisions

1. **Codegen emits textual LLVM IR**, not LLVM-C bindings — `clang` assembles + links the
   `.ll`. Keeps the self-host dependency-free and LLVM-version-decoupled; the standard
   bootstrap path. (No FFI fallback was needed — even exceptions/atomics lower to text.)
2. **Behavioral oracle for codegen.** LLVM renumbers SSA values and constant-folds, so the
   emitted IR can't be matched byte-for-byte. Instead: emit `.ll` → clang → run → compare
   exit code + stdout to the C++-built binary, over a representative corpus.
3. **AST enrichment is the shared prerequisite.** The parser was built to reproduce
   `--test-parser`, so it *discarded* details sema/codegen need (type-params + constraints,
   struct methods, ADT payloads, bitfield widths, `async`, interface signatures,
   `packed`/`pack(N)`). Phase A captured them, gated by a **lockstep printer extension**
   (extend both `ast/ast_printer.cpp` and the Eskiu printer; `--test-parser` parity stays
   byte-exact over the richer dump).
4. **Feature freeze holds.** Fix language/stdlib pain the dogfood surfaces (e.g.
   `List_set`/`List_remove`); don't add language features. Log finds in `NOTES.md`.
5. **The production C++ `eskiuc` stays the oracle** and is otherwise untouched — the one
   allowed change was the additive `ast_printer.cpp` lockstep extension.

## The phases

Built strictly in order (each unblocks the next); each parity-gated in CI.

- **A — AST enrichment.** Stop discarding parser detail; store it on the AST. Gated by the
  lockstep printer (`parse_parity.sh --full`, 51/51).
- **B — Sema** (`sema.esk`). Two-pass name resolution + a string-based type layer (the C++
  flows types as strings at the codegen boundary, so sema does too — no structured
  `ty::Type` port). Catches all **19** semantic error classes.
- **C — Codegen** (`codegen.esk`). Walks the typed AST and writes textual LLVM IR. Started
  with the bootstrap subset, then extended to full feature coverage (below).
- **D — Self-compilation.** The self-hosted codegen emits valid IR for the *entire*
  compiler; `cg_main` compiled by itself reproduces the C++-built codegen's IR; the unified
  `esk_main` driver reaches a 3-stage bootstrap fixpoint.

## Method — the parity oracle, per phase

The available ground truth differs by phase, so the gate does too:

| Phase | Gate | Oracle |
|---|---|---|
| Front-end (lex/parse/pp) | `lex/parse/pp_parity.sh` | **Byte-exact** diff vs `--test-{lexer,parser}` (pp runs *through* the lexer) |
| Sema | `tc_parity.sh` | **Verdict + diagnostic**: same accept/reject as `--test-typechecker`; every error class caught with the right `EXPECT-ERROR` substring |
| Codegen | `cg_parity.sh` | **Behavioral**: emit `.ll` → clang → run, compare exit + stdout to the C++ build |
| Self-compilation | `cg_selfhost.sh` | The whole compiler emits valid IR; `cg_main`-built-by-itself reproduces the C++ codegen's IR |
| Bootstrap | `cg_bootstrap.sh` | 3-stage cc0→cc1→cc2; assert **cc1 ≡ cc2** (binary byte-equality is *not* asserted — Mach-O `LC_UUID`/signature differ) |

All run with `CLANG=clang-22` in CI, alongside the existing gates (`tests/run.sh`, golden
IR, asan/ubsan, fuzz) — which stayed green throughout. `selfhost/` + its tests are purely
additive.

## Feature coverage (codegen)

Beyond the bootstrap subset (scalars, control flow, structs/methods/pointers, arrays,
plain enums, generics via monomorphization, `sizeof`/cast, globals, struct-by-value), the
code generator covers: floating point, `switch`, ADT enums + `match` (generic, and payloads
wider than one 8-byte slot), closures, exceptions (full Itanium ABI), atomics, generics with
argument inference, async/await, unions, bitfields, interfaces (vtable dynamic dispatch),
type aliases, function-as-value decay, packed structs (`packed` / `#pragma pack(N)`),
user-defined variadics + `va_list`/`va_arg`, the `alloc_with`/`thread_create`/`thread_join`/
`free_closure` builtins, and the `?` error-propagation operator.

**"Feature-complete" is an earned claim, not assumed.** The bootstrap fixpoint only
exercises the subset the compiler's own source uses, so it does *not* prove general
coverage. Completeness was verified by pushing the whole C++ feature corpus through the
behavioral oracle and requiring a clean sweep (50/50). This bit us repeatedly — see the
lesson in `NOTES.md`: never claim "complete" from the bootstrap alone, and enumerate the
*full* failure set every round (an early triage truncated 21 failures to 17 and undercounted
the remaining work).

## Residual & the road to v1.0

Not gaps, but worth noting: async `for-in` resolves its element type with a local heuristic
in `async_lower.esk` rather than a sema-stamped type; the parse-parity corpus could expand
to the ~70 preprocessor-touching files. For v1.0: a package manager, and promoting the
Eskiu-written compiler to the primary build (it currently rides alongside the C++ `eskiuc`).
