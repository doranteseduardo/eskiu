# Tests

Compiler regression tests. Run the whole suite with:

```bash
cmake --build build          # make sure the compiler is current
tests/run.sh
```

`run.sh` exits non-zero if anything fails, so it is safe to use as a CI gate.
Override the compiler or C linker with `ESKIUC=...` / `CC=...`.

## How a test is judged

The runner classifies every file automatically. There is no list to maintain.

| Kind  | Files | Pass condition |
|-------|-------|----------------|
| **run**   | `NAME.esk` **+** `NAME.expected` | compiles, links, runs, and stdout matches `NAME.expected` **exactly** |
| **smoke** | `NAME.esk` with no `.expected`   | compiles, links, and exits `0` (output not checked) |
| **error** | `errors/NAME.esk`                | `--test-typechecker` exits non-zero **and** the diagnostics contain the `EXPECT-ERROR:` substring from the file's first line |
| **lint**  | `warnings/NAME.esk`              | type-checks under `-Wall` and emits exactly the warnings named by its `// EXPECT-WARNING:` lines (none listed means none allowed) |

These are *honest* tests: a `run` test fails the moment the generated program
prints anything different, and an `error` test fails if the compiler ever starts
**accepting** code it should reject. (Verified by deliberately corrupting an
expected file and by adding a compilable file under `errors/`: both make
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
| `variadic` | user-defined variadic fn: `...` + `va_list`/`va_start`/`va_arg<T>`/`va_end` (int + double) |
| `http2_frame` | `<http2>` 9-byte frame-header encode/decode round-trip (incl. 31-bit stream id) |
| `http2_conn` | `<http2>` stage 2 codecs: SETTINGS write/apply, ACK, PING/PONG, GOAWAY round-trips |
| `http2_handshake` | `<http2>` async server opening handshake over a socketpair (preface + SETTINGS exchange + ACK) |
| `hpack` | `<hpack>` HPACK (RFC 7541): integer/string codecs, static + dynamic tables, §6 decode/encode, Huffman: RFC vectors |
| `http2_stream` | `<http2>` stage 4: stream state machine, flow-control accounting, HEADERS/DATA/WINDOW_UPDATE/RST_STREAM codecs |
| `http2_server` | `<http2_server>` stage 6: the h2c server end-to-end over a socketpair (request → handler → response) |
| `http2_chunking` | response bodies > 16384 split into MAX_FRAME_SIZE DATA frames (last has END_STREAM) |
| `async_elseif` | async `if/else-if/else` with `await` in branches + terminating `else` (transform regression) |
| `http2_multiplex` | interleaved two-stream multiplexing: per-stream request assembly, each routed + answered |
| `const` | immutable bindings, usable as array sizes |
| `param_reassign` | reassigning scalar/pointer parameters; method call through a pointer parameter |
| `c_callback` | passing a top-level Eskiu function to a C API as a raw callback (drives libc `qsort`) |
| `pointer_const` | `const T*` (pointee read-only) vs `T* const` (binding read-only); read/rebind allowed, write-through and const-drop rejected |
| `pack_n` | `#pragma pack(N)` for N > 1: field-alignment cap, padding, size matches the C ABI |
| `sizeof_union_ptr` | `sizeof`, `union` (incl. float member), typed pointer arithmetic |
| `os_macros` | exactly one host-OS macro (`__APPLE__`/`__linux__`) is defined |
| `preprocessor` | object-like and function-like `#define`, `#ifdef` |
| `pp_pack` | backslash-continued function-like macro; `#pragma pack` |
| `pp_loc` | `__LINE__` / `__FILE__` preprocessor expansion |
| `shebang` | a leading `#!` line is ignored by the preprocessor (line numbers preserved) |
| `structs_methods` | named initializers, field access, method calls, `self` mutation |
| `interfaces` | structural interfaces, vtable fat-pointer dispatch |
| `enums` | enum members as int constants, usable in `switch`/comparisons |
| `enum_adt` | algebraic enums + `match`: payload variants, multiple bindings, `_` default, classic enum coexisting |
| `enum_generic` | generic algebraic enums `Option<T>`/`Either<A,B>`: turbofish construction, per-instance monomorphization, `match` |
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
| `sysheap` | `<sysheap>` mmap-backed `FirstFit` heap: allocate/free/reuse with no libc `malloc` |
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
| `executor` | async `Executor`: scheduled wakers run on the loop thread in FIFO order |
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
| `select_value` | `<futureval>` `select2v`: winner's value as `Either<A,B>` over the reactor (timer-wins / data-wins) |
| `join_value` | `<futureval>` `join2v`: both values as a `Pair<A,B>` |
| `async_channel` | `<channel>` `Chan_send`/`Chan_recv`: buffered fast path + parked handoff; also guards the cast-after-deduplicated-import parser fix |
| `async_spawn` | detached generic `spawn<T>` of async tasks (ready + suspending), leak-free |
| `async_select` | generic `select2<A,B>`: await the first of two futures; loser dropped (A-wins + B-wins) |
| `async_join` | generic `join2<A,B>`: await both futures, then read both values |
| `const_method` | A method declaring `const T* self` may be called on a const value. |
| `decl_repeats_ok` | Repeated top-level declarations that stay legal: a prototype before its definition, an `extern` declaration beside the variable's definition, and... |
| `enum_dup_value` | Classic enum members may share a value (as in C). A match is on the value, so one arm covers every member with that value. |
| `generic_ptr_leading` | A leading-star pointer to a generic struct instance (`*List<int>`, `*B<int>`) is a valid declared type, including one used only as an explicit... |
| `method_free_fn` | Method-call syntax on free-function methods (`Type_method(self, ...)`), including stdlib ones: `c.get()`, `s.trim()`, `n.push('!')`. |
| `nullable_narrow` | Null-narrowing forms for `?*T`: each dereference below is proven safe. |
| `return_flow` | Definite-return analysis: forms that always return are accepted. |
| `sema_accepts` | Valid forms next to the stricter checks: a lambda whose return type is reconciled to the declared one, an enum-member case label, a struct named... |
| `sizeof_var` | sizeof(variable) measures the variable's type (C semantics). |
| `uninit_chain_assign` | A chained or nested assignment initializes its targets; they are not read first. |

### `smoke` tests (compile + link + exit 0)

| Test | Exercises |
|------|-----------|
| `eventloop` | single-threaded echo server over the `<eventloop>` reactor |
| `http_roundtrip` | worker-pool server + client round trip |
| `http_async` | non-blocking async HTTP server (`<http_async>`) + client round trip |
| `http_async_concurrent` | concurrent async HTTP server: 3 simultaneous clients, channel wait-group shutdown |
| `threads` | `thread_create`/`thread_join`: output order is non-deterministic |
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
| `errors/addr_of_bitfield` | rejected with "cannot take the address of bitfield 'a'" |
| `errors/addr_of_call` | rejected with "cannot take the address of this expression" |
| `errors/addr_of_literal` | rejected with "cannot take the address of this expression" |
| `errors/alias_cycle` | rejected with "type alias 'A1' refers to itself" |
| `errors/arg_type_mismatch` | rejected with "argument 1 type mismatch" |
| `errors/assign_float_to_int` | rejected with "cannot assign a floating-point value" |
| `errors/assign_rvalue` | rejected with "cannot assign to this expression" |
| `errors/assign_ternary` | rejected with "cannot assign to this expression" |
| `errors/await_in_lambda` | rejected with "await is only allowed inside an async function" |
| `errors/bitfield_float` | rejected with "must have an integer type" |
| `errors/bitfield_too_wide` | rejected with "is 40 bits wide, more than its type 'uint32' holds" |
| `errors/break_outside` | rejected with "'break' outside of a loop or switch" |
| `errors/call_non_fn` | rejected with "undefined function 'x'" |
| `errors/case_nonconst` | rejected with "switch case value must be a constant integer" |
| `errors/cast_ptr_float` | rejected with "cannot cast 'string' to 'float'" |
| `errors/cast_struct` | rejected with "cannot cast" |
| `errors/compound_lit_range` | rejected with "integer literal 300 is out of range for 'uint8'" |
| `errors/const_addr_arg` | rejected with "discards a const qualifier" |
| `errors/const_method_call` | rejected with "cannot call method 'set' on a read-only value" |
| `errors/const_struct_addr` | rejected with "discards a const qualifier" |
| `errors/continue_in_switch` | rejected with "'continue' outside of a loop" |
| `errors/dup_default` | rejected with "multiple 'default' labels" |
| `errors/dup_enum` | rejected with "redefinition of enum 'C'" |
| `errors/dup_enum_member` | rejected with "redefinition of enum member 'R'" |
| `errors/dup_field` | rejected with "duplicate field 'x' in struct 'A'" |
| `errors/dup_field_method` | rejected with "has the same name as a field" |
| `errors/dup_fn_global` | rejected with "is declared as both a function and a variable" |
| `errors/dup_global` | rejected with "redefinition of global variable 'G'" |
| `errors/dup_local` | rejected with "redefinition of 'x' in the same scope" |
| `errors/dup_method_fn` | rejected with "'S_get' is declared as both a method and a function" |
| `errors/dup_param` | rejected with "duplicate parameter 'a'" |
| `errors/dup_struct` | rejected with "redefinition of struct 'A' with different fields" |
| `errors/dup_struct_fn` | rejected with "is declared as both a struct and a function" |
| `errors/field_unknown_type` | rejected with "unknown type 'Nope'" |
| `errors/float_expr_to_int` | rejected with "cannot assign a floating-point value" |
| `errors/fn_chained_arg_count` | rejected with "expects 1 argument(s), got 2" |
| `errors/fn_field_arg_count` | rejected with "'cb' expects 1 argument(s), got 0" |
| `errors/fn_value_arg_count` | rejected with "'f' expects 1 argument(s), got 2" |
| `errors/fn_value_arg_type` | rejected with "argument 1 type mismatch" |
| `errors/generic_arg_conflict` | rejected with "argument 2 type mismatch" |
| `errors/generic_arg_count` | rejected with "function 'id' expects 1 argument(s), got 2" |
| `errors/generic_explicit_arg_count` | rejected with "function 'id' expects 1 argument(s), got 2" |
| `errors/generic_type_arg_count` | rejected with "generic function 'id' expects 1 type argument(s), got 2" |
| `errors/generic_uninferable` | rejected with "cannot infer type argument(s) T of generic function 'mk'" |
| `errors/iface_arity_mismatch` | rejected with "does not satisfy interface 'Shape'" |
| `errors/iface_call_arg_count` | rejected with "method 'area' expects 0 argument(s), got 2" |
| `errors/iface_return_mismatch` | rejected with "does not satisfy interface 'Shape'" |
| `errors/index_scalar` | rejected with "cannot index into a value of type 'int'" |
| `errors/index_struct` | rejected with "cannot index into a value of type 'S'" |
| `errors/init_ptr_from_int` | rejected with "cannot convert 'int' to '*int'" |
| `errors/init_string_from_int` | rejected with "cannot convert 'int' to 'string'" |
| `errors/lambda_sig_mismatch` | rejected with "incompatible function type" |
| `errors/match_dup_value` | rejected with "duplicate match value" |
| `errors/match_non_enum` | rejected with "match subject must be an enum" |
| `errors/member_of_int` | rejected with "cannot access member 'y' on non-struct type 'int'" |
| `errors/missing_return_dowhile_break` | rejected with "missing return in non-void function 'f'" |
| `errors/missing_return_labeled_break` | rejected with "missing return in non-void function 'f'" |
| `errors/missing_return_switch_break` | rejected with "missing return in non-void function 'f'" |
| `errors/must_use_method` | rejected with "result of 'C_get' must be used" |
| `errors/nullable_loop_reassign` | rejected with "cannot dereference a possibly-null pointer" |
| `errors/nullable_reassign` | rejected with "cannot dereference a possibly-null pointer" |
| `errors/nullable_shadow` | rejected with "cannot dereference a possibly-null pointer" |
| `errors/operator_redefinition` | rejected with "redefinition of function 'operator +(V, V)'" |
| `errors/param_unknown_type` | rejected with "unknown type 'Nope'" |
| `errors/proto_conflict` | rejected with "conflicting declaration of function 'f'" |
| `errors/question_non_result` | rejected with "requires a Result-like value" |
| `errors/return_in_void` | rejected with "return type mismatch: expected void" |
| `errors/shift_by_float` | rejected with "invalid operands for operator" |
| `errors/sizeof_unknown` | rejected with "unknown type 'Nope'" |
| `errors/slice_bounds_oob` | rejected with "slice bound 9 is out of bounds for array of size 4" |
| `errors/slice_bounds_order` | rejected with "slice bounds out of order" |
| `errors/string_plus_string` | rejected with "invalid operands for operator" |
| `errors/struct_init_dup_field` | rejected with "field 'v' is initialized more than once" |
| `errors/struct_init_field_type` | rejected with "field 'v': cannot convert 'string' to 'int'" |
| `errors/struct_init_generic_field` | rejected with "field 'v': cannot convert 'string' to 'int'" |
| `errors/struct_init_lit_range` | rejected with "integer literal 999 is out of range for 'uint8'" |
| `errors/struct_init_too_many` | rejected with "too many initializers for struct 'S'" |
| `errors/struct_value_cycle` | rejected with "contains itself by value" |
| `errors/template_arg_count` | rejected with "'Pair' expects 2 type argument(s), got 1" |
| `errors/template_unknown_arg` | rejected with "unknown type 'Nope'" |
| `errors/ternary_lit_range` | rejected with "integer literal 300 is out of range for 'uint8'" |
| `errors/undefined_template_fn` | rejected with "undefined template function 'nope'" |
| `errors/unknown_sig_type` | rejected with "unknown type 'Nope'" |
| `errors/unknown_var_type` | rejected with "unknown type 'Nope'" |
| `errors/void_field` | rejected with "field 'v' of 'S' cannot have type 'void'" |
| `errors/void_param` | rejected with "parameter 'x' cannot have type 'void'" |
| `errors/void_var` | rejected with "variable 'x' cannot have type 'void'" |

### `lint` tests (-Wall)

| Test | Warnings asserted |
|------|-------------------|
| `warnings/wall_uses` | -Wall uses: dot-call, interface and operator functions, prototype and lambda parameters |

## Previously known issues (now fixed, and guarded by tests)

Two real compiler bugs were found while building this suite. Both are now fixed and
covered by exact-match `run` tests, so a regression would fail `run.sh`.

1. **`union` float member store**: *fixed.* `v.f = 3.14` stored the full 8-byte
   double into the union's offset-0 storage without truncating to `float`, so a
   later read got garbage (`126443839488.0`). The assignment path now coerces the
   RHS to the LHS member's declared scalar type. Guarded by `sizeof_union_ptr.esk`.

2. **No forward function resolution**: *fixed.* Codegen emitted functions in source
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
- **Lint:** add `warnings/NAME.esk` with one `// EXPECT-WARNING: <substring>` line per
  expected `-Wall` warning (no such line means the file must produce no warning).
