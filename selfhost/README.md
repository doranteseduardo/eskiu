# selfhost/ — Eskiu in Eskiu

Self-hosting, written in Eskiu and validated for byte-identical parity against the
production C++ tooling. Dogfood/tooling — not part of the compiler build; the C++
pipeline is untouched.

- **Milestone 1 — the lexer** (`tokens.esk` + `lexer.esk` + `lex_main.esk`):
  byte-identical to `--test-lexer` over the whole preprocessor-free corpus.
- **Milestone 2 — the parser** (`ast.esk` + `parser.esk` + `parse_main.esk`):
  builds a recursive AST from the lexer's tokens and prints it byte-identical to
  `--test-parser`. Covers the full grammar — expressions, statements, declarations,
  templates/generics, lambdas/async. Parity over the import-free corpus
  (`parse_parity.sh --full`); excludes files that `import` (the C++ parser follows
  imports) or that the C++ `--test-parser` can't print (top-level prototypes crash
  its printer — see NOTES.md).

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

## Notes

Dogfood findings (and the one real bug this milestone already caught + fixed) are
in `NOTES.md`.
