# Eskiu Self-Hosting

How the Eskiu compiler is written in Eskiu, and how that's kept honest. Shipped in
**v0.3.0**.

---

## What "self-hosting" means here

The production compiler is `eskiuc`, written in C++17. Alongside it, the entire compiler
pipeline is **reimplemented in Eskiu** under `selfhost/`:

```
lexer.esk → preprocessor.esk → parser.esk → sema.esk → async_lower.esk → codegen.esk
```

with drivers `lex_main`, `pp_main`, `parse_main`, `tc_main`, `cg_main`, and the unified
`esk_main` (preprocess → parse → type-check → generate). On the promotion track (see
`selfhost/PROMOTION_PLAN.md`) `esk_main` has grown into the full user-facing CLI: it
dispatches every `--test-*` debug mode plus `--version`, takes multiple input files, and
with `-o` assembles the IR and invokes `clang` to link a native binary (threading
`--asan`/`--ubsan` into the link). It also owns the two subcommands: `run script.esk
[args...]` compiles to a temp exe, execs it forwarding argv, and propagates the exit code;
`fmt [--check] file …` reindents in place via `fmt.esk`. The four per-pass parity gates
drive *through* `esk_main --test-*`, and `run`/`fmt` have their own parity gates
(`run_parity.sh`, `fmt_parity.sh`). The code generator emits **LLVM IR as text** (no LLVM library is
linked), which `clang` then assembles and links. This keeps the self-hosted compiler
dependency-free and is the standard bootstrap path.

The endgame (v1.0): `eskiuc` compiles its own source. v0.3.0 reached the key milestones (a
3-stage bootstrap fixpoint and feature-completeness against the C++ corpus), and the
promotion track (`selfhost/PROMOTION_PLAN.md`) has since finished it: `esk_main` is the full
CLI, and the Eskiu-written compiler is now *behaviorally equivalent* to the C++ one over the
whole corpus (positive output + negative verdict/diagnostic, CI-gated) and is dual-built by
CMake as `eskiuc-esk`. The C++ binary stays the shipped artifact (the self-host links via
clang, so shipping it would add a runtime clang requirement); it is the bootstrap seed and
the differential oracle.

## Validation: parity oracles, not faith

Each pass is validated against the C++ `eskiuc`, which is the oracle. The method differs by
phase, because the available ground truth differs:

| Pass | Oracle |
|---|---|
| Lexer / Parser / Preprocessor | **Byte-exact** diff against `eskiuc --test-{lexer,parser}` (preprocessor parity runs *through* the lexer) |
| Semantic analysis | **Verdict + diagnostic**: same accept/reject as `--test-typechecker`, every error class caught with the right message |
| Code generation | **Behavioral**: emit `.ll` → `clang` → run, compare exit code + stdout to the C++-built binary (LLVM renumbers SSA values and constant-folds, so IR can't be matched byte-for-byte) |

These run as CI gates: `tests/selfhost/{lex,parse,pp,tc,cg}_parity.sh`, plus the
promotion-track gates that exercise the whole driver end to end: `cg_bootstrap.sh` (the
3-stage self-host fixpoint), `driver_parity.sh` / `run_parity.sh` / `fmt_parity.sh` (the
`-o` / `run` / `fmt` CLI paths), and `corpus_parity.sh` (P3: every positive test compiled by
the Eskiu-built compiler produces the same exit + stdout as C++). The negative-corpus
verdict + diagnostic parity is part of `tc_parity.sh`.

## The bootstrap fixpoint

Beyond per-pass parity, the self-hosted compiler is validated **against itself**:

- **`cg_selfhost.sh`**: the self-hosted codegen emits valid IR for the *entire* self-hosted
  compiler, and `cg_main` compiled by itself reproduces the C++-built codegen's IR
  byte-for-byte.
- **`cg_bootstrap.sh`** runs a 3-stage build: the C++ `eskiuc` builds `cc0`, `cc0` builds
  `cc1`, `cc1` builds `cc2`; the gate asserts **`cc1` ≡ `cc2`** (identical IR for the
  compiler's own source). A self-built compiler reproducing its own output is a true
  bootstrap fixpoint.

(Binary byte-equality is *not* asserted; a Mach-O `LC_UUID` and ad-hoc signature differ even
for identical input, so the IR fixpoint is the real proof.)

## Feature-completeness

The compiler's own source only exercises a subset of the language, so the bootstrap fixpoint
alone does **not** prove general feature coverage. To verify it, the full C++ feature corpus
is pushed through the behavioral codegen oracle. A clean sweep (every program self-host
compiles to the same behavior as the C++ build) is what earns the "feature-complete" claim.

As of v0.3.0 the self-hosted code generator covers, beyond the bootstrap subset: floating
point, `switch`, ADT enums + `match` (generic, and payloads wider than one word), closures,
exceptions (the Itanium ABI), atomics, generics with argument inference, async/await, unions,
bitfields, interfaces (dynamic dispatch), type aliases, function-as-value decay, packed
structs (`packed` / `#pragma pack(N)`), user-defined variadics + `va_list`/`va_arg`, the
`alloc_with`/`thread_create`/`thread_join`/`free_closure` builtins, and the `?` error-
propagation operator.

## Running the gates

```bash
tests/selfhost/lex_parity.sh   --full   # lexer
tests/selfhost/parse_parity.sh --full   # parser
tests/selfhost/pp_parity.sh    --full   # preprocessor
tests/selfhost/tc_parity.sh              # semantic analysis
tests/selfhost/cg_parity.sh              # codegen (behavioral)
tests/selfhost/cg_selfhost.sh            # self-compilation + emit validity
tests/selfhost/cg_bootstrap.sh           # 3-stage bootstrap fixpoint
```

All are wired into CI. The sources live in `selfhost/`; the slice-by-slice development record
is in `selfhost/BACKEND_PLAN.md`.
