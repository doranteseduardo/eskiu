# selfhost/ — Eskiu in Eskiu

The Eskiu compiler reimplemented in Eskiu, validated for parity against the production
C++ tooling. Tooling/dogfood — **not** part of the compiler build; the C++ pipeline is
untouched. Shipped as the **v0.3.0** milestone.

The whole pipeline is here — lexer → preprocessor → parser → type checker → code
generator — plus a unified driver. It reaches a **3-stage bootstrap fixpoint** (a compiler
built by itself reproduces its own IR), and the code generator is **feature-complete
against the C++ corpus** (a full feature sweep passes 50/50).

See [`docs/dev/self-hosting.md`](../docs/dev/self-hosting.md) for the narrative;
[`BACKEND_PLAN.md`](BACKEND_PLAN.md) for the design + method + build record; and
[`NOTES.md`](NOTES.md) for dogfood findings.

## The passes

- **Lexer** (`tokens.esk` + `lexer.esk` + `lex_main.esk`) — byte-identical to
  `--test-lexer` over the whole preprocessor-free corpus.
- **Preprocessor** (`preprocessor.esk` + `pp_main.esk`) — the text pass before lexing:
  object/function `#define`/`#undef`, `#ifdef`/`#ifndef`/`#else`/`#endif`, `#pragma`,
  `#error`, backslash splicing, recursive macro expansion, `__FILE__`/`__LINE__`.
  Validated *through the lexer*, byte-identical to `--test-lexer` over the whole `tests/`
  + `stdlib/` corpus (157/157, no exclusions).
- **Parser** (`ast.esk` + `parser.esk` + `parse_main.esk`) — full grammar
  (expressions, statements, declarations, templates/generics, lambdas/async), byte-
  identical to `--test-parser`. **Follows `import`** (resolves `<name>`/relative paths
  recursively, preprocessing each, merging decls). The AST is **enriched** beyond the
  printer's needs (type-params + constraints, struct methods, ADT payloads, bitfield
  widths, `async`, interface signatures, `packed`/`pack(N)`) so sema + codegen can consume it.
- **Type checker** (`sema.esk` + `tc_main.esk`) — two-pass name resolution + a
  string-based type layer catching all **19** semantic error classes; verdict matches
  `--test-typechecker` on every positive corpus file, every negative test rejected.
- **Async lowering** (`async_lower.esk`) — an AST→AST pass (run between parse and
  codegen) that rewrites each `async fn` into a frame struct + state-machine resume
  function + a `Future<T>*` constructor.
- **Code generator** (`codegen.esk` + `cg_main.esk`) — emits **textual LLVM IR**
  (no LLVM library; `clang` assembles + links). Behavioral oracle: emit `.ll` → clang →
  run, compared to the C++-built binary. **Feature-complete**: beyond the bootstrap
  subset (scalars, control flow, structs/methods/pointers, arrays, plain enums, generics,
  globals, `sizeof`/cast) it covers floats, `switch`, ADT enums + `match` (generic +
  >8-byte payloads), closures, exceptions (Itanium ABI), atomics, generic argument
  inference, async/await, unions, bitfields, interfaces (dynamic dispatch), type aliases,
  function-as-value, packed structs, user variadics + `va_*`, the
  `alloc_with`/`thread_*`/`free_closure` builtins, and the `?` operator.
- **Unified driver** (`esk_main.esk`) — preprocess → parse → **sema** → codegen; rejects
  ill-typed input (exit 1, no IR). This is the binary the 3-stage bootstrap builds from
  itself.

## Files

| File | Role |
|---|---|
| `tokens.esk` | `TokenType` enum (mirrors `lexer/lexer.h` order) + `Token` (lexeme by `[start,len)`) + escape decode |
| `lexer.esk` / `lex_main.esk` | the lexer cursor (`lx_next`) / driver printing the `--test-lexer` format |
| `preprocessor.esk` / `pp_main.esk` | the text preprocessor (`PPMacro` table, conditional stack, `pp_expand`) / driver (preprocess + lex) |
| `ast.esk` | AST node structs (tagged heap structs) + the `--test-parser` printer (mirrors `ast/ast_printer.cpp`) |
| `parser.esk` / `parse_main.esk` | recursive-descent parser (token buffer + backtracking) / AST-dump driver |
| `sema.esk` / `tc_main.esk` | the type checker (mirrors `sema/type_checker.cpp`) / `--test-typechecker`-format driver |
| `async_lower.esk` | the async→state-machine lowering pass (port of `sema/async_transform.cpp`) |
| `codegen.esk` / `cg_main.esk` | the textual-LLVM-IR code generator (mirrors `codegen/`) / `.ll`-emitting driver |
| `esk_main.esk` | the unified pp→parse→sema→codegen driver (the bootstrap binary) |
| `../stdlib/ctype.esk` | pure-Eskiu ASCII classification (freestanding-clean), used by the lexer |

## Parity gates (all CI-wired)

```
tests/selfhost/lex_parity.sh   --full    # lexer       → 114/114 (preprocessor-free corpus)
tests/selfhost/pp_parity.sh    --full    # preprocessor → 157/157 (through the lexer, no exclusions)
tests/selfhost/parse_parity.sh --full    # parser AST dump → 51/51
tests/selfhost/tc_parity.sh              # type-checker verdict (121 positives + 19 error classes)
tests/selfhost/cg_parity.sh              # codegen: emit .ll → clang → run, vs the C++ binary
tests/selfhost/cg_selfhost.sh            # whole compiler emits valid IR + self-build fixpoint
tests/selfhost/cg_bootstrap.sh           # 3-stage bootstrap: cc0→cc1→cc2, cc1 ≡ cc2
```

Method by phase: front-end = **byte-exact** diff (the C++ banner is stripped); sema =
**verdict + diagnostic** parity; codegen = **behavioral** (run + compare exit/stdout —
LLVM renumbers SSA values, so IR can't be matched verbatim). The codegen gates need
`clang` on `PATH` (override with `CLANG=…`; CI passes `CLANG=clang-22`).

`cg_selfhost.sh` checks `cg_main` and `cg_main`-built-by-itself emit identical IR for the
whole `selfhost/` tree. `cg_bootstrap.sh` goes a generation further over `esk_main`: the
C++ eskiuc builds cc0, cc0 builds cc1, cc1 builds cc2, and cc1 ≡ cc2. Binaries aren't
byte-compared — macOS Mach-O embeds an `LC_UUID` + ad-hoc signature that differ for
identical input; the IR fixpoint is the proof.
