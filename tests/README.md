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
| `int_widen` | integer widening honors source signedness at call/assign/init/struct/enum sites |
| `int64_arith` | an `int` literal mixed with an `int64` operand widens to 64-bit |
| `int_width` | integer width, signedness, and variadic-promotion correctness |
| `bitwise_assign` | compound bitwise assignment `&= \|= ^= <<= >>=` |
| `control_flow` | `if`/`else if`/`else`, `while`, `for`, `break`, `continue` |
| `for_in` | `for x in …` over a fixed-size array |
| `range_for` | `for (i in A..B)` half-open integer ranges (nested, variable bounds, empty) |
| `recursion` | self-recursion: factorial, fibonacci, Ackermann |
| `forward_decl` | forward declarations, call-before-define, mutual recursion |
| `pointers` | `&`/`*`, deref, write-through, indexing, typed pointer arithmetic |
| `ptr_member` | member read/write through a local `*T` variable (`p.x`) |
| `ptr_trailing_star` | the trailing-star spelling `T*` dereferences correctly |
| `strings` | `%s`, `string[i] -> char`, char codes |
| `question_op` | `?` postfix error-propagation operator |
| `volatile` | `volatile let` pointer load/store marked volatile in IR |
| `inline_asm` | `asm(...)` simple + extended compiles, links, runs |
| `variadic` | user-defined variadic fn — `...` + `va_list`/`va_start`/`va_arg<T>`/`va_end` (int + double) |
| `http2_frame` | `<http2>` 9-byte frame-header encode/decode round-trip (incl. 31-bit stream id) |
| `http2_conn` | `<http2>` stage 2 codecs: SETTINGS write/apply, ACK, PING/PONG, GOAWAY round-trips |
| `http2_handshake` | `<http2>` async server opening handshake over a socketpair (preface + SETTINGS exchange + ACK) |
| `hpack` | `<hpack>` HPACK (RFC 7541): integer/string codecs, static + dynamic tables, §6 decode/encode, Huffman — RFC vectors |
| `http2_stream` | `<http2>` stage 4: stream state machine, flow-control accounting, HEADERS/DATA/WINDOW_UPDATE/RST_STREAM codecs |
| `http2_server` | `<http2_server>` stage 6: the h2c server end-to-end over a socketpair (request → handler → response) |
| `http2_chunking` | response bodies > 16384 split into MAX_FRAME_SIZE DATA frames (last has END_STREAM) |
| `async_elseif` | async `if/else-if/else` with `await` in branches + terminating `else` (transform regression) |
| `http2_multiplex` | interleaved two-stream multiplexing — per-stream request assembly, each routed + answered |
| `const` | immutable bindings, usable as array sizes |
| `param_reassign` | reassigning scalar/pointer parameters; method call through a pointer parameter |
| `c_callback` | passing a top-level Eskiu function to a C API as a raw callback (drives libc `qsort`) |
| `pointer_const` | `const T*` (pointee read-only) vs `T* const` (binding read-only); read/rebind allowed, write-through and const-drop rejected |
| `pack_n` | `#pragma pack(N)` for N > 1 — field-alignment cap, padding, size matches the C ABI |
| `sizeof_union_ptr` | `sizeof`, `union` (incl. float member), typed pointer arithmetic |
| `os_macros` | exactly one host-OS macro (`__APPLE__`/`__linux__`) is defined |
| `preprocessor` | object-like and function-like `#define`, `#ifdef` |
| `pp_pack` | backslash-continued function-like macro; `#pragma pack` |
| `pp_loc` | `__LINE__` / `__FILE__` preprocessor expansion |
| `shebang` | a leading `#!` line is ignored by the preprocessor (line numbers preserved) |
| `structs_methods` | named initializers, field access, method calls, `self` mutation |
| `interfaces` | structural interfaces, vtable fat-pointer dispatch |
| `enums` | enum members as int constants, usable in `switch`/comparisons |
| `enum_adt` | algebraic enums + `match` — payload variants, multiple bindings, `_` default, classic enum coexisting |
| `enum_generic` | generic algebraic enums `Option<T>`/`Either<A,B>` — turbofish construction, per-instance monomorphization, `match` |
| `either_stdlib` | `<either>` `Option`/`Either` helpers (`opt_is_some`, `opt_unwrap_or`) |
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
| `closure_escape` | escape analysis: non-escaping closure on the stack, escaping one heap + `free_closure` |
| `generic_closure` | a capturing closure inside a **generic** function body; the `T`-typed capture's env field is substituted per instantiation (`box<int>` / `box<int64>`) |
| `import_cast` | a cast to a type imported from another file (`(FutureHdr*)p`) parses as a cast |
| `closure_global` | a module global read inside a closure reads the global (not a stale copy) |
| `fn_pointer` | function pointers as values and parameters |
| `fn_more` | fn-pointer as a return type; calling a fn-pointer struct field |
| `exceptions` | `try`/`catch`/`finally`, `throw`, exception from a nested call |
| `sret` | a struct larger than 16 bytes is returned via sret (arm64) |
| `alloc` | `<mem>` `alloc<T>(n)`/`free`; `<alloc>` Bump/Arena/Pool/FirstFit |
| `alloc_with` | `alloc_with(&a, T, n)` over a caller-provided buffer |
| `sysheap` | `<sysheap>` mmap-backed `FirstFit` heap — allocate/free/reuse with no libc `malloc` |
| `atomic` | `<atomic>` `atomic_load`/`store`/`swap`/`cas` → LLVM atomics |
| `base64` | `<base64>` encode/decode over buffers |
| `env` | `<env>` `env_get`/`has`/`get_or`/`get_int` |
| `fs` | `<fs>` file I/O |
| `http` | `<http>` request parser + response builder |
| `math` | `<math>` libm wrappers: sqrt/fabs/pow/floor/ceil/fmod/abs |
| `json` | `<json>` builder + recursive-descent parser |
| `net_echo` | `<net>` loopback TCP echo (server thread passed as a bare fn) |
| `path` | `<path>` `join`/`basename`/`dirname`/`extension`/`is_absolute` |
| `string_methods` | `<string>` builder ops (`push`, `char_at`, `len`) |
| `string_ops` | `<string>` `split` (e.g. `"a,b,,c"` on `','`) |
| `threading` | `<threading>` `Mutex`/`Cond`/`Sem`; shared state captured by pointer |
| `time` | `<time>` wall clock / monotonic / `sleep_ms` |
| `executor` | async `Executor` — scheduled wakers run on the loop thread in FIFO order |
| `net_async` | leaf future: a coroutine awaits `net_read_async` over the reactor |
| `async_basic` | `async`/`await` transform: one await of an already-ready future (fast path) |
| `async_io` | suspending await over a real reactor read; params live across the await |
| `async_multi` | multiple awaits (fast path); values thread through frame states |
| `async_multi_io` | two suspending awaits over the reactor (multi-state chain) |
| `async_return_await` | `return await E;` desugaring and `async void` (uint8 unit) |
| `async_cancel` | `future_drop` on a suspended async function: cascade-drop + free, no UAF |
| `async_loop` | `while` loop containing an await (read-until-EOF; loop back-edge) |
| `async_if` | `if`/`else` with an await in each branch (branch-join states) |
| `async_for` | C-style `for` loop containing an await |
| `async_break` | `break`/`continue` inside an awaiting `while`/`for` (state transitions; `for` continue runs the step) |
| `async_switch` | `switch` containing an await: fall-through + suspending case + `default` + `break` |
| `async_for_in` | `for-in` containing an await, over a fixed-size array and a `List`-like struct (suspending) |
| `async_timer` | `<timer>` `timer_after` leaf future: a delayed await + read-with-timeout via `select2(read, timer)` |
| `async_frame_expr` | frame-hoisted locals used in a struct literal / index / call after an await are renamed to `fr.x` (shared child-enumeration) |
| `select_value` | `<futureval>` `select2v` — winner's value as `Either<A,B>` over the reactor (timer-wins / data-wins) |
| `join_value` | `<futureval>` `join2v` — both values as a `Pair<A,B>` |
| `async_channel` | `<channel>` `chan_send`/`chan_recv` — buffered fast path + parked handoff; also guards the cast-after-deduplicated-import parser fix |
| `async_spawn` | detached generic `spawn<T>` of async tasks (ready + suspending), leak-free |
| `async_select` | generic `select2<A,B>` — await the first of two futures; loser dropped (A-wins + B-wins) |
| `async_join` | generic `join2<A,B>` — await both futures, then read both values |

### `smoke` tests (compile + link + exit 0)

| Test | Exercises |
|------|-----------|
| `eventloop` | single-threaded echo server over the `<eventloop>` reactor |
| `http_roundtrip` | worker-pool server + client round trip |
| `http_async` | non-blocking async HTTP server (`<http_async>`) + client round trip |
| `http_async_concurrent` | concurrent async HTTP server: 3 simultaneous clients, channel wait-group shutdown |
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
| `errors/arg_count` | calling a function with the wrong argument count |
| `errors/async_no_await` | an `async` function with no `await` is rejected |
| `errors/match_duplicate` | two `match` arms for the same variant |
| `errors/const_field` | assigning to a field of a `const` value |
| `errors/const_ptr_write` | writing through a pointer-to-const (`*r = …`) |
| `errors/const_ptr_drop` | a conversion that discards a const qualifier (`int* = const int*`) |
| `errors/question_bad_return` | `?` used in a function not returning `Result` |
| `errors/unknown_intrinsic` | an `intrinsic` declared with a name the compiler can't lower |
| `errors/pp_error` | a `#error` directive aborts compilation |
| `errors/match_nonexhaustive` | a `match` missing a variant (no `_`) is rejected |
| `errors/escaping_param` | a non-`escaping` closure parameter used beyond a direct call |
| `errors/await_outside_async` | `await` used outside an `async` function |
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
