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

The runner needs no list, but this index keeps the suite legible. Keep it in sync
when you add a test.

### `run` tests (exact-match)

| Test | Exercises |
|------|-----------|
| `arithmetic` | integer arithmetic, precedence, `/` `%`, comparison, `&&`/`\|\|` |
| `int64_arith` | an `int` literal mixed with an `int64` operand widens to 64-bit |
| `int_width` | integer width, signedness, and variadic-promotion correctness |
| `bitwise_assign` | compound bitwise assignment `&= \|= ^= <<= >>=` |
| `control_flow` | `if`/`else if`/`else`, `while`, `for`, `break`, `continue` |
| `for_in` | `for x in …` over a fixed-size array |
| `recursion` | self-recursion: factorial, fibonacci, Ackermann |
| `forward_decl` | forward declarations, call-before-define, mutual recursion |
| `pointers` | `&`/`*`, deref, write-through, indexing, typed pointer arithmetic |
| `ptr_member` | member read/write through a local `*T` variable (`p.x`) |
| `ptr_trailing_star` | the trailing-star spelling `T*` dereferences correctly |
| `strings` | `%s`, `string[i] -> char`, char codes |
| `question_op` | `?` postfix error-propagation operator |
| `volatile` | `volatile let` pointer load/store marked volatile in IR |
| `inline_asm` | `asm(...)` simple + extended compiles, links, runs |
| `const` | immutable bindings, usable as array sizes |
| `sizeof_union_ptr` | `sizeof`, `union` (incl. float member), typed pointer arithmetic |
| `os_macros` | exactly one host-OS macro (`__APPLE__`/`__linux__`) is defined |
| `preprocessor` | object-like and function-like `#define`, `#ifdef` |
| `pp_pack` | backslash-continued function-like macro; `#pragma pack` |
| `structs_methods` | named initializers, field access, method calls, `self` mutation |
| `interfaces` | structural interfaces, vtable fat-pointer dispatch |
| `enums` | enum members as int constants, usable in `switch`/comparisons |
| `type_alias` | `type u8 = uint8;` resolves to the underlying type |
| `bitfields` | bitfield assignment + masked read; signed fields sign-extend |
| `templates_result` | `Result<int,string>` monomorphization, `Ok`/`Err` |
| `template_inference` | `T` inferred when it appears directly as a parameter type |
| `template_inference_composite` | `T` inferred from a composite param type (`List<T>*`) |
| `template_struct_literal` | named template struct literal `Pair<int,float>{…}` |
| `template_nested_close` | a lexed `>>` closes nested brackets (`List_init<List<int>>`) |
| `nested_template` | a template fn calling another with the type param forwarded |
| `list_struct` | `List<StructType>` used through helper functions |
| `member_temp` | member access on a struct-valued temporary (call result) |
| `cast_alias` | casts to struct-pointer / alias / enum; alias used as a type |
| `lambdas` | anonymous functions, `fn(T)->R`, higher-order functions |
| `closures` | capturing & non-capturing lambdas through higher-order functions |
| `closure_global` | a module global read inside a closure reads the global (not a stale copy) |
| `fn_pointer` | function pointers as values and parameters |
| `fn_more` | fn-pointer as a return type; calling a fn-pointer struct field |
| `exceptions` | `try`/`catch`/`finally`, `throw`, exception from a nested call |
| `sret` | a struct larger than 16 bytes is returned via sret (arm64) |
| `alloc` | `<mem>` `alloc<T>(n)`/`free`; `<alloc>` Bump/Arena/Pool/FirstFit |
| `alloc_with` | `alloc_with(&a, T, n)` over a caller-provided buffer |
| `atomic` | `<atomic>` `atomic_load`/`store`/`swap`/`cas` → LLVM atomics |
| `base64` | `<base64>` encode/decode over buffers |
| `env` | `<env>` `env_get`/`has`/`get_or`/`get_int` |
| `fs` | `<fs>` file I/O |
| `http` | `<http>` request parser + response builder |
| `json` | `<json>` builder + recursive-descent parser |
| `net_echo` | `<net>` loopback TCP echo (server thread passed as a bare fn) |
| `path` | `<path>` `join`/`basename`/`dirname`/`extension`/`is_absolute` |
| `string_methods` | `<string>` builder ops (`push`, `char_at`, `len`) |
| `string_ops` | `<string>` `split` (e.g. `"a,b,,c"` on `','`) |
| `threading` | `<threading>` `Mutex`/`Cond`/`Sem`; shared state captured by pointer |
| `time` | `<time>` wall clock / monotonic / `sleep_ms` |

### `smoke` tests (compile + link + exit 0)

| Test | Exercises |
|------|-----------|
| `eventloop` | single-threaded echo server over the `<eventloop>` reactor |
| `http_roundtrip` | worker-pool server + client round trip |
| `threads` | `thread_create`/`thread_join` — output order is non-deterministic |
| `test_struct` | minimal struct field access |

### `error` tests (must be rejected)

| Test | Diagnostic asserted |
|------|---------------------|
| `errors/undefined_var` | reference to an undeclared variable |
| `errors/undefined_type` | use of an undefined type |
| `errors/undefined_field` | access to an undefined struct member |
| `errors/const_no_init` | `const` declared without an initializer |
| `errors/const_reassign` | reassigning a `const` |
| `errors/const_field` | assigning to a field of a `const` value |
| `errors/question_bad_return` | `?` used in a function not returning `Result` |
| `errors/unknown_intrinsic` | an `intrinsic` declared with a name the compiler can't lower |
| `errors/parse_error` | malformed syntax |
| `errors/unterminated_string` | unterminated string literal |
| `errors/unterminated_char` | malformed/unterminated char literal |
| `errors/unterminated_comment` | unterminated block comment |

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
