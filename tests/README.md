# Tests

Compiler regression tests. Run the whole suite with:

```bash
cmake --build build          # make sure the compiler is current
tests/run.sh
```

`run.sh` exits non-zero if anything fails, so it is safe to use as a CI gate.
Override the compiler or C linker with `ESKIUC=...` / `CC=...`.

## How a test is judged

The runner classifies every file automatically — there is no list to maintain.

| Kind  | Files | Pass condition |
|-------|-------|----------------|
| **run**   | `NAME.esk` **+** `NAME.expected` | compiles, links, runs, and stdout matches `NAME.expected` **exactly** |
| **smoke** | `NAME.esk` with no `.expected`   | compiles, links, and exits `0` (output not checked) |
| **error** | `errors/NAME.esk`                | `--test-typechecker` exits non-zero **and** the diagnostics contain the `EXPECT-ERROR:` substring from the file's first line |

These are *honest* tests: a `run` test fails the moment the generated program
prints anything different, and an `error` test fails if the compiler ever starts
**accepting** code it should reject. (Verified by deliberately corrupting an
expected file and by adding a compilable file under `errors/` — both make
`run.sh` exit 1.)

## Coverage

| Test | Exercises |
|------|-----------|
| `arithmetic` | int/float ops, precedence, `/` `%`, comparison, `&&`/`\|\|`, `int64` |
| `control_flow` | `if`/`else if`/`else`, `while`, `for`, `break`, `continue` |
| `recursion` | self-recursion: factorial, fibonacci, Ackermann |
| `forward_decl` | forward declarations, call-before-define, mutual recursion |
| `pointers` | `&`/`*`, `alloc`/`free`, indexing, typed pointer arithmetic |
| `strings` | `%s`, `string[i] -> char`, char codes, NUL scan |
| `structs_methods` | named initializers, field access, method calls, `self` mutation |
| `interfaces` | structural interfaces, vtable fat-pointer dispatch |
| `templates_result` | `Result<int,string>` monomorphization, `Ok`/`Err` |
| `closures` | captures by value, closures through higher-order functions |
| `lambdas` | anonymous functions, `fn(T)->R`, higher-order functions |
| `exceptions` | `try`/`catch`/`finally`, `throw`, exception from nested call |
| `test_struct` *(smoke)* | minimal struct field access |
| `sizeof_union_ptr` | `sizeof`, `union` (incl. float member), typed pointer arithmetic |
| `threads` *(smoke)* | `thread_create`/`thread_join` — output order is non-deterministic |
| `errors/undefined_field` | access to an undefined struct member |
| `errors/undefined_var` | reference to an undeclared variable |
| `errors/undefined_type` | use of an undefined type |

## Previously known issues (now fixed, and guarded by tests)

Two real compiler bugs were found while building this suite. Both are now fixed and
covered by exact-match `run` tests, so a regression would fail `run.sh`.

1. **`union` float member store** — *fixed.* `v.f = 3.14` stored the full 8-byte
   double into the union's offset-0 storage without truncating to `float`, so a
   later read got garbage (`126443839488.0`). The assignment path now coerces the
   RHS to the LHS member's declared scalar type. Guarded by `sizeof_union_ptr.esk`.

2. **No forward function resolution** — *fixed.* Codegen emitted functions in source
   order, so a call to a function defined later (or a `int b(int n);` forward
   declaration) failed with `Undefined variable or function`. Codegen now declares
   all prototypes in a pre-pass before emitting bodies, enabling call-before-define
   and mutual recursion. Guarded by `forward_decl.esk`.

## Adding a test

- **Positive, deterministic output:** add `NAME.esk` and `NAME.expected` (the exact
  stdout). Generate the expected file from a *known-correct* run, then read it to
  confirm it is right before committing.
- **Positive, non-deterministic / smoke only:** add `NAME.esk` with no `.expected`.
- **Negative:** add `errors/NAME.esk` whose first line is
  `// EXPECT-ERROR: <substring of the diagnostic>`.
