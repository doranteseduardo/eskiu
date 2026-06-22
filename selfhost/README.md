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
- **Back-end (in progress)** — roadmap in `BACKEND_PLAN.md`. AST **enrichment**
  (Phase A) is done: `ast.esk`/`parser.esk` now capture type-params + constraints,
  struct methods, ADT payloads, bitfields, `async`, and interface signatures (lockstep
  printer gate, corpus 50/50). **Sema** (Phase B: `sema.esk` + `tc_main.esk`) is
  underway — S0 does name resolution (undefined variables), the verdict matching the
  C++ `--test-typechecker` on all 120 positive corpus files. Codegen + self-compilation
  follow.

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
- `sema.esk` — the type checker (Phase B; mirrors `sema/type_checker.cpp`). S0:
  two-pass name resolution with a flat `{name, depth}` scope stack.
- `tc_main.esk` — driver: preprocess → parse → check `argv[1]`, in the
  `--test-typechecker` format (banner + verdict + exit code).
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
tests/selfhost/parse_parity.sh --full  # parser AST dump → 50/50
```

The preprocessor has no standalone oracle, so `pp_parity.sh` validates it *through
the lexer*: `pp_main` preprocesses + lexes, and the token stream is diffed against
the C++ `--test-lexer` (which also preprocesses). Because every directive is now
handled, `--full` has **no exclusions** — the whole `tests/` + `stdlib/` corpus must
match. **Wired into CI** as the "Preprocessor self-host parity" step.

## Notes

Dogfood findings (and the one real bug this milestone already caught + fixed) are
in `NOTES.md`.
