# selfhost/ — Eskiu in Eskiu

Self-hosting, written in Eskiu and validated for byte-identical parity against the
production C++ tooling. Dogfood/tooling — not part of the compiler build; the C++
pipeline is untouched.

- **Milestone 1 — the lexer** (`tokens.esk` + `lexer.esk` + `lex_main.esk`):
  byte-identical to `--test-lexer` over the whole preprocessor-free corpus.
- **Milestone 2 — the parser** (`ast.esk` + `parser.esk` + `parse_main.esk`):
  builds a recursive AST from the lexer's tokens and prints it byte-identical to
  `--test-parser`. Covers the full grammar — expressions, statements, declarations,
  templates/generics, lambdas/async. **Follows `import`** — resolves `<name>` →
  `stdlib/name.esk` and relative `"path"` against the importing file's dir, parses
  them recursively, and merges their decls in order (dedup + shared type names, like
  the C++ parser). Parity (`parse_parity.sh --full`) covers every `tests/*.esk` whose
  transitive import closure is preprocessor-free; files whose closure touches the
  preprocessor are excluded until it's self-hosted (the harness computes the closure).
- **Milestone 3 — the preprocessor** (`preprocessor.esk` + `pp_main.esk`): the text
  pass that runs before lexing — object/function-like `#define`/`#undef`,
  `#ifdef`/`#ifndef`/`#else`/`#endif`, `#pragma` passthrough, `#error`, backslash
  splicing, recursive macro expansion, predefined `__FILE__`/`__LINE__`. Validated
  *through the lexer* (`pp_main` preprocesses + lexes), byte-identical to the C++
  `--test-lexer` over the whole `tests/` + `stdlib/` corpus (156/156, no exclusions).
- **Back-end** — roadmap in `BACKEND_PLAN.md`. AST **enrichment** (Phase A): `ast.esk`/
  `parser.esk` capture type-params + constraints, struct methods, ADT payloads,
  bitfields, `async`, interface signatures (lockstep printer gate). **Sema** (Phase B:
  `sema.esk` + `tc_main.esk`): two-pass name resolution + a string-based type layer
  catching **all 19** semantic error classes; verdict matches `--test-typechecker` on
  121/121 positive corpus files, every sema negative test rejected (`tc_parity.sh`).
  **Codegen** (Phase C: `codegen.esk` + `cg_main.esk`): **textual LLVM IR** (assembled
  by `clang`), covering scalars/control-flow, structs+methods+pointers, arrays, plain
  enums, **generics (monomorphization)**, `sizeof`/cast, globals, struct-by-value,
  pointer arithmetic, `&&`/`||` short-circuit. Behavioral oracle (`cg_parity.sh`).
- **Self-compilation reached (Phase D).** The self-hosted codegen emits valid IR for the
  *entire* self-hosted compiler, and `cg_main` compiled **by itself** reproduces the
  C++-built codegen's IR byte-for-byte over all 45 inputs — a **bootstrap fixpoint**
  (`cg_selfhost.sh`). All five drivers, self-compiled, match the C++-built ones. Deferred
  (unused by the compiler's own source): floats, ADT enums/`match`, closures, exceptions,
  async, atomics — needed only to compile arbitrary user programs, not to self-host.

## Files

- `tokens.esk` — `TokenType` enum (mirrors `lexer/lexer.h` in exact order) + the
  `Token` struct (lexeme by `[start, len)` offsets) + `token_name` + escape decode.
- `ast.esk` — AST node structs (tagged heap structs) + the `--test-parser`-format
  printer (mirrors `ast/ast_printer.cpp`).
- `parser.esk` — recursive-descent parser over the lexer's tokens (token buffer +
  index, like the C++; backtracking for decl-vs-stmt and template `<`).
- `parse_main.esk` — parser driver (reads `argv[1]`, prints the AST dump).
- `lexer.esk` — the lexer: a `Lexer` cursor over the source bytes and `lx_next`,
  mirroring `lexer/lexer.cpp` (peek/advance, skip ws + comments, identifiers +
  keyword table, numbers, strings/chars, the operator switch).
- `lex_main.esk` — driver: reads `argv[1]`, prints each token in the EXACT format
  of `eskiuc --test-lexer` (decoding string/char escapes like the C++ does).
- `preprocessor.esk` — the text preprocessor (mirrors `lexer/preprocessor.cpp`):
  `PPMacro` table (redefine via `List_set`, `#undef` via `List_remove`), conditional
  stack, recursive `pp_expand`, line splicing, `__FILE__`/`__LINE__`.
- `pp_main.esk` — driver: preprocesses `argv[1]` (filename `""`, matching
  `--test-lexer`) then lexes the result and prints tokens in the `--test-lexer` format.
- `sema.esk` — the type checker (Phase B; mirrors `sema/type_checker.cpp`): two-pass
  name resolution + a string-based type layer; all 19 semantic error classes.
- `tc_main.esk` — driver: preprocess → parse → check `argv[1]`, in the
  `--test-typechecker` format (banner + verdict + exit code).
- `codegen.esk` — the code generator (Phase C; mirrors `codegen/`): walks the parsed
  AST and writes **textual LLVM IR** (named SSA temps, monomorphized generics, struct
  GEPs, ...). No LLVM library — `clang` assembles + links the emitted `.ll`.
- `cg_main.esk` — driver: preprocess → parse → codegen `argv[1]`, printing the `.ll`.
- `esk_main.esk` — the **unified compiler driver**: preprocess → parse → **sema** →
  codegen. Rejects ill-typed input (exit 1, no IR); otherwise prints the `.ll`. This is
  the binary the 3-stage bootstrap (`cg_bootstrap.sh`) builds from itself.
- `../stdlib/ctype.esk` — pure-Eskiu ASCII classification (freestanding-clean; no
  libc ctype), used by the lexer.

## Parity gate

```
tests/selfhost/lex_parity.sh           # synthetic corpus (tests/selfhost/inputs/)
tests/selfhost/lex_parity.sh --full    # the real corpus: tests/*.esk → 114/114
tests/selfhost/lex_parity.sh FILE ...  # specific files
```

Both lexers run; the C++ banner is stripped and the token streams are raw-`diff`ed.
Byte-identical = pass. The Eskiu driver is compiled once to a native binary (not
re-`run` per file), so the whole corpus takes ~seconds. **Wired into CI** as the
"Lexer self-host parity" step (`--full`).

**Excluded from `--full` (6 files): preprocessor-dependent.** The C++ `Lexer` runs
`preprocess()` first, so files that `#define`/`#ifdef`/`#include`, use
`__LINE__`/`__FILE__`, or carry a shebang would diverge from a raw lex. `#pragma`
is NOT excluded — it passes through and lexes identically. The preprocessor is a
separate future self-host target.

Runs as its own CI step (not folded into `tests/run.sh`) right after the fuzzer.

```
tests/selfhost/pp_parity.sh            # synthetic corpus (tests/selfhost/pp_inputs/)
tests/selfhost/pp_parity.sh --full     # tests/*.esk + stdlib/*.esk → 156/156
tests/selfhost/parse_parity.sh --full  # parser AST dump → 51/51
tests/selfhost/tc_parity.sh            # type-checker verdict parity (121 + 19 errors)
tests/selfhost/cg_parity.sh            # codegen: emit .ll → clang → run vs C++ binary
tests/selfhost/cg_selfhost.sh          # Phase D: whole compiler emits valid IR + fixpoint
tests/selfhost/cg_bootstrap.sh         # 3-stage bootstrap: cc0→cc1→cc2, stage2 ≡ stage3
```

The codegen gates need `clang` on `PATH` (override with `CLANG=…`; CI passes
`CLANG=clang-22`). `cg_selfhost.sh` checks `cg_main` and `cg_main`-built-by-itself emit
identical IR for the whole `selfhost/` tree. `cg_bootstrap.sh` goes a generation further
over the unified `esk_main` driver: the C++ eskiuc builds cc0, cc0 builds cc1, cc1 builds
cc2, and cc1 ≡ cc2 (identical IR). Binaries aren't byte-compared — macOS Mach-O embeds an
LC_UUID + ad-hoc signature that differ for identical input.

The preprocessor has no standalone oracle, so `pp_parity.sh` validates it *through
the lexer*: `pp_main` preprocesses + lexes, and the token stream is diffed against
the C++ `--test-lexer` (which also preprocesses). Because every directive is now
handled, `--full` has **no exclusions** — the whole `tests/` + `stdlib/` corpus must
match. **Wired into CI** as the "Preprocessor self-host parity" step.

## Notes

Dogfood findings (and the one real bug this milestone already caught + fixed) are
in `NOTES.md`.
