# selfhost/ — Eskiu in Eskiu

Self-hosting milestone 1: **the lexer, written in Eskiu**, validated for
byte-identical parity against the production C++ lexer. This is dogfood/tooling,
not part of the compiler build — the C++ pipeline is untouched.

## Files

- `tokens.esk` — `TokenType` enum (mirrors `lexer/lexer.h` in exact order) + the
  `Token` struct (lexeme by `[start, len)` offsets) + `token_name`.
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
Byte-identical = pass.

**Excluded from `--full` (6 files): preprocessor-dependent.** The C++ `Lexer` runs
`preprocess()` first, so files that `#define`/`#ifdef`/`#include`, use
`__LINE__`/`__FILE__`, or carry a shebang would diverge from a raw lex. `#pragma`
is NOT excluded — it passes through and lexes identically. The preprocessor is a
separate future self-host target.

Not wired into `tests/run.sh`: the gate recompiles `lex_main.esk` per file, which
is too slow for the main suite. Run it on lexer changes.

## Notes

Dogfood findings (and the one real bug this milestone already caught + fixed) are
in `NOTES.md`.
