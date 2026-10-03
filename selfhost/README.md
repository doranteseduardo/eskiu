# The Eskiu compiler in Eskiu

This directory is a second implementation of the compiler, written in Eskiu. It covers the
whole pipeline (preprocessor, lexer, parser, type checker, async lowering, code generator)
and the full command line, and it behaves like the C++ `eskiuc` over the entire test
corpus. [`docs/dev/self-hosting.md`](../docs/dev/self-hosting.md) explains how that is
checked.

The C++ `eskiuc` is still the released binary: it bundles LLVM, while this compiler writes
LLVM IR as text and calls `clang` to assemble and link it.

## Building

CMake builds it next to the C++ compiler when `clang` is available:

```bash
cmake -S . -B build && cmake --build build
./build/eskiuc-esk hello.esk -o hello
```

`eskiuc-esk` takes the same flags as `eskiuc` (`-o`, `-c`, `--target`, `-O0` to `-O3`,
`--safe`, the `--test-*` modes, `run`, `fmt`). It finds `stdlib/` through `$ESKIU_ROOT` or
the install layout, and uses `$CLANG` (default `clang`) to link.

## Files

| File | Role |
|---|---|
| `tokens.esk`, `lexer.esk` | Token kinds and the lexer |
| `preprocessor.esk` | `#define`, `#if`/`#ifdef`, `#pragma` and macro expansion, run before lexing |
| `ast.esk` | AST nodes and the `--test-parser` printer |
| `parser.esk` | Recursive-descent parser; follows `import` and merges the imported declarations |
| `sema.esk` | Type checker |
| `async_lower.esk` | Rewrites each `async` function into a frame struct and a state machine |
| `codegen.esk` | Writes LLVM IR as text |
| `fmt.esk` | The `fmt` subcommand |
| `esk_main.esk` | The compiler driver (`eskiuc-esk`) |
| `lex_main.esk`, `pp_main.esk`, `parse_main.esk`, `tc_main.esk`, `cg_main.esk` | Single-pass drivers used by the parity checks |
| `bigstack.esk` | Runs a driver on a large stack so deeply nested input does not overflow |

## Things to know when editing

- `fn`, `in` and `match` are keywords, so they cannot name a variable, parameter or field.
- `alloc<T>` returns zeroed memory, but initialize every `List` field of a struct with
  `List_init` before pushing to it.
- AST walks should only read the fields a node kind sets. A field a kind does not use is
  zero, which can look like a valid empty child.
- A change here usually needs the same change in the C++ compiler. Run the checks in
  `tests/selfhost/` before committing, including `cg_bootstrap.sh`, which rebuilds the
  compiler with itself.
