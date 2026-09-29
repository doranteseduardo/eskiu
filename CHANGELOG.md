
# Changelog

All notable changes to Eskiu are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow `MAJOR.MINOR.PATCH-stage` (e.g. `0.0.9-alpha`).

---

## [0.9.2] - 2026-09-26
A full-project audit (codegen, type checker, self-host parity, stdlib, front end, driver
and docs) found about a hundred latent bugs that the existing corpus did not reach. All
are fixed lockstep in the C++ and self-hosted compilers unless noted, each with a
regression test. A second pass added three fuzzers (a C oracle, a negative corpus and an
ASan fuzzer for the stdlib parsers), which found about twenty more, and resolved the two
known limitations left from 0.9.1 (R and S). Seven more blind audit rounds followed, each
on a frozen tree and each fixed the same way, for about 500 fixes in total. The last
rounds concentrated on the C ABI, generics, `volatile` and the HTTP servers under hostile
input. What is still open is listed under Known issues.

### Added
- **`#pragma link("name")`** links the executable with `-lname`. The driver also adds
  the libraries a program implies: the stdlib's own pragmas (`libm` on Linux, `pthread`,
  `ws2_32`), the C++ exception runtime when the program throws or catches, and
  `pthread` when it calls `thread_create`. `-lm`, `-lc++`/`-lstdc++` and `-lpthread`
  are no longer needed. `--no-default-libs` turns all of this off.
- **Inline methods on generic structs** (`struct Box<T> { T get() {...} }`), instantiated
  per struct instance on first call.
- **Dot-calls on generic instances**: `l.push(8)` on a `List<int>` is
  `List_push<int>(&l, 8)`, and `ch.send(v)`, `m.get(k, &out)` work the same way. The
  type arguments come from the receiver, then the call's arguments. The call is lowered
  as an ordinary call, so arguments convert, struct results use `sret` and a call
  inside `try` unwinds.
- **C callbacks with structs by value**: a top-level function passed to C as `(*void)f`
  that takes or returns a struct by value is reached through a thunk with the C
  convention.
- **Fuzzers**: `tests/fuzz/c_oracle.py` (programs compiled by both Eskiu compilers and
  by clang as C, outputs compared), `tests/fuzz/neg_fuzz.py` (one injected error per
  program, both compilers must give a located diagnostic) and `tests/fuzz/stdlib_fuzz.py`
  (the stdlib parsers under ASan). Each has a CI gate, and
  `tests/selfhost/cabi_parity.sh` compares the self-host's lowered C signatures with the
  C++ ones per target.
- **`#if` / `#elif`** with C integer constant expressions (`defined(X)`, `defined X`,
  arithmetic, shifts, comparisons, `&&`, `||`, `?:`). An identifier left after macro
  expansion counts as `0`, as in C.
- **Interface values.** An interface is now a real `{data, vtable}` value, so an
  interface-typed local, struct field, return value or assignment works (these compiled
  and then crashed before). The value refers to a struct through `&x`.
- **`const` receivers.** A method declared with `const T* self` can be called on a
  `const` value.
- **C ABI for structs passed or returned by value across `extern`**: AArch64, x86-64
  SysV, Windows x64 and 32-bit ARM, in both compilers (the self-host picks the
  convention from `--target`). New predefined macros `__aarch64__`, `__x86_64__` and `__arm__` follow the
  target.
- **Test runner:** a `run` or smoke test may have a C companion `tests/NAME.c` that is
  compiled and linked in; `tests/warnings/NAME.esk` is a new lint kind that asserts the
  exact `-Wall` warnings (`// EXPECT-WARNING:` lines).
- The compiler builds against LLVM 23 as well as LLVM 22. The minimum is now LLVM 21.
- **Generic async functions** (`async T f<T>(...)`), in both compilers.
- **`(void)expr`** evaluates an expression and discards its value, which also silences
  `must_use`.
- `<http2_server>`: the `H2Server` engine (flow control, SETTINGS validation, stream
  states) serves both h2c and TLS connections. `<future>` gains `free_future_polled` for
  a future whose waker was installed by hand, and `<http>` gains `http_content_length`.
- `<json>` and `<regex>` limit nesting to 512 levels (`JSON_MAX_DEPTH`, `RE_MAX_DEPTH`),
  so deep input fails cleanly instead of exhausting the stack.

### Changed (may reject or change the behavior of existing programs)
- **A non-constant global initializer is a compile error.** `int g = f();`, or a read of
  a non-`const` global, used to compile to a silent `0`. Constant expressions
  (arithmetic, casts, ternaries, `sizeof`, enum members, `const` values, `&global`) fold
  correctly.
- **Narrow integer operands promote to `int` before arithmetic and comparison** (the C
  integer promotions). `(uint8)200 + (uint8)100` is `300`, and `uint8 200 > int8 -1` is
  true.
- **Conversion to `bool` is `!= 0`** for integer, float and pointer sources. It used to
  keep only the low bit, so `(bool)2` was false.
- **`ptr - ptr` counts elements**, not bytes (C++; the self-host already did).
- **Passing a struct by value where an interface is expected is an error**; pass `&x`.
- **Interface conformance checks method signatures** (return type, arity, parameter
  types), not only method names.
- **Duplicate and conflicting declarations are errors**: same-scope locals, globals,
  parameters, fields, enum members, structs with different fields, a second `default:`,
  one name used for a function and a global (or a struct, or a method), and a prototype
  whose signature differs from its definition.
- **Struct literals are type-checked**: field types, literal ranges, too many
  initializers, a field given twice. Omitted fields are zero-filled.
- **`sizeof(variable)`** gives the size of the variable's type; an unknown name is an
  error.
- **Methods called on a `const` value** must declare `const T* self`, and `&` of a
  `const` value no longer converts to a non-`const` pointer.
- **`match` on a classic enum** whose members share a value rejects a second arm for the
  same value.
- **Preprocessor:** unknown directives, `#include` (use `import`), a stray or duplicate
  `#else`/`#elif`/`#endif`, a conditional left open, and `#` or `##` in a macro body are
  located errors instead of being silently dropped.
- **Numeric literals:** an invalid digit in an octal literal (`08`, `019`) is an error
  (it was read as decimal), as are `0x` with no digits and any suffix (`0b101`, `1_000`,
  `3.5f`).
- **Strict stdlib parsers:** `json_parse` follows RFC 8259 (exact literals, no trailing
  garbage, `\uXXXX` decoding); `base64_decode` rejects bad padding and truncated input; a
  malformed or overflowing `Content-Length` gets a 400; `String_to_int` saturates and
  accepts a leading `+`; `http_reason` returns the class name (or an empty string) for
  an unknown code instead of "OK".
- **C's usual arithmetic conversions** apply to mixed-sign operands: a signed and an
  unsigned operand of the same width give an unsigned result, a shift has the type of
  its promoted left operand, unary `-` and `~` promote a narrow operand to `int`, and
  two different narrow ternary arms meet as `int`. An integer literal too wide for
  `int` is an `int64`. Constant initializers fold with exactly these rules.
- **An `extern` parameter of fn type is a C function pointer.** The call passes a
  top-level function by name or `null`; a lambda or a fn-typed variable there is an
  error (its environment cannot cross into C). It used to pass the `{fn, env}` closure.
- **Bitfields follow the target's C layout** (ABI change for structs mixing bitfield
  types). On SysV/AAPCS targets a bitfield shares bytes with its neighbours while it
  fits in an aligned unit of its declared type, so `uint8 a:4; uint32 w:12;` is now 4
  bytes. Windows targets use the MS layout, including under `#pragma pack(N)`.
- **Each generic instantiation is type-checked** with its concrete type arguments
  (known limitation S), so an error that exists only for some arguments is reported as
  `... (in instantiation of f<Box>)`. The self-host now checks method-call argument
  types (known limitation R).
- **A local in a function's outermost block may not reuse a parameter name** (the
  parameters and that block share one scope, as in C).
- **Range loops**: `for (i in A..B)` takes the bounds' common integer type (it was always
  `int`, so an `int64` bound wrapped), a non-integer bound is an error, and a bound that
  names the loop variable reads the outer variable of that name.
- **Duplicate `case` labels are found by value**, so `case 7:` and `case (4 * 2) - 1:`, or
  a `const` name and its value, collide.
- The self-host rejects bad operands, calls, returns and global types the way the C++
  compiler does.
- **A lambda may not assign a captured variable** (`n = 1`, `n += 1`, `n++`): the
  closure holds a copy, so the write was silently lost. Write through a pointer, or use
  a global or a `static` local, which are not captured.
- **Conditions** take a `bool`, a number or a pointer (`string` and `?*T` included). A
  float condition is `!= 0.0`, so NaN is true, and `!=` on floats is unordered, as in C.
- **Aggregates** (structs, arrays, slices, closures, interface values) have no built-in
  `==`, `<` or truth value; define an operator overload. An overload needs at least one
  user-type operand.
- **Interface boxing** takes a pointer to a struct with exactly one `*`; a `?*T` must be
  null-checked first, and a pointer to `const` only boxes into an interface whose
  methods all take `const T* self`.
- **More constant checks**: array sizes must be positive, a constant shift count must be
  inside the operand width, a `case` value must fit the subject type, enum member values
  must fit `int`, and constant array indices and slice bounds are folded and checked
  (`lo <= hi` on any base).
- **Read-only rules**: `const` parameters can't be assigned, a `const` array can't be
  sliced, and `s.len` of a slice is read-only.
- A struct literal can't mix positional and named fields; a second `_` arm in a `match`
  is an error; returning the address of a local's field, element or slice is an error
  (a `static` is fine); `#pragma pack` takes 1, 2, 4, 8 or 16.
- A top-level function called with dot syntax needs a pointer `self`; `async main` and
  a lambda that can fall off its end without returning a value are errors.
- A lambda written directly in `thread_create` is owned by the thread, which frees its
  environment when it finishes.
- Globals get C linkage, so an `extern` next to its definition refers to the same
  symbol. An assignment evaluates its target before its value, as the other compound
  forms already did.
- Self-host diagnostics go to stderr, as the C++ compiler's do.
- **Pointer conversions between unrelated pointee types need a cast** (`*int` to `*Big`).
  `*void`, `null` and the byte pointers (`string`, `*char`, `*int8`, `*uint8`) stay
  implicit, and `int`/`int32` and `uint`/`uint32` spellings of the same type match.
- **A global initializer or a function body may not name a global defined later in the
  file**, as in C (it compiled to a zero address before).
- A by-value recursive enum (`enum L { Cons(int, L), Nil }`) is an error, like a struct
  that contains itself. `Box<void>`, `void[]` and a non-constant array size are errors.
- A lambda may not write a field or element of a captured struct or array (`p.a = 5`,
  `arr[0] = 9`); writes through a pointer, a slice or a string are allowed.
- A bare nullary variant of a generic enum needs its type arguments (`None<int>()`), and
  a variant constructor's payload is checked against the declared instance.
- `?*T` narrowing ends at any call or `await` for a global, and an address-taken
  variable is never narrowed. A call in a condition (also in a `&&` right operand, a
  ternary condition or an early-exit guard) or in the branch that falls through ends a
  global's narrowing from that point on.
- A lambda that captures a non-`escaping` closure parameter and outlives the call
  (returned, stored, passed to an `escaping` parameter or to `thread_create`) needs the
  parameter marked `escaping`.
- A pointer to a sum type converts only to and from a pointer to the same sum type (or
  `*void`), and a pointer to an array or slice (`&arr`) is not a pointer to a struct.
- A `\`-newline inside a string or char literal on a directive line splices the next
  line, as in C, so a `#define` body's literal may span lines.
- `?` needs an integer or `bool` `ok` field; a union literal names one member; an inline
  method can't declare a parameter named `self`, and an operator overload must have the
  operator's arity.
- `va_start` needs a variadic function (`...`), and `va_start`, `va_arg` and `va_end`
  take one `va_list`; `va_arg` of a struct, union, sum type or array is an error.
  `thread_create` takes a `fn()->void`, `thread_join` its `*void` handle, and
  `free_closure` a closure. `alloc_with` needs an integer count and an allocator type
  with an `alloc` method (it was a codegen error without a location).
- A sum type is not a bitfield type, and a named bitfield may not have zero width
  (`int x : 0`), as in C. `p - q` needs pointers to the same type.
- An enum bitfield whose enum has no negative member reads back unsigned, as in clang
  and GCC (MS layout keeps it signed): `Col col : 2` holding `B = 2` read back as -2.
- A bitfield whose values all fit an `int` reads as `int`, as in C: `u - 1` of a
  `uint32 u : 3` holding 0 is -1 (it was 4294967295), and `uint64 a : 20` promotes too.
  A postfix `b.f++` keeps the declared type, as in clang.
- A `void` call is not an operand of `&&`, `||` or a comparison, nor an argument passed
  through `...` (`printf("%d", f())`); these were codegen crashes or invalid IR. `return
  f();` of a `void` `f` in a `void` function, and `c ? f() : g()` with `void` arms as a
  statement, compile and run.
- A user operator (`s + s`, `s[i]`, `-s`, `s += s`, an overloaded `==` in a condition)
  is a call, so it ends a global's `?*T` narrowing like a plain call. A lambda body does
  not see a global's narrowing from where the lambda is written (it runs later), and a
  `static` local follows the global rule (a call or a lambda may null it).
- `alloc_with` needs a pointer to the allocator (`alloc_with(b, T, n)` with a by-value
  `b` allocated from a copy), an alloc method of the shape `*void T_alloc(*T self, int64
  size)` (another return type or parameter list emitted invalid IR), and a known, sized
  element type (an unknown type or `void` crashed the C++ compiler).
- `*T[N]` is an array of N pointers in the type checker too, as codegen always lowered
  it: `*int[3] p = &arr` and `*p` of such an array are type errors (they compiled to
  invalid IR). The address of an array is typed `T[N]*`.
- The handle `thread_create` returns is a `*void` in the self-host too, so `int t =
  thread_create(...)` is an error in both compilers. Like any `*void` it converts to
  another pointer, `string` included (a `string` is a byte pointer).
- The arms of a `?:` of two unrelated pointers have no common type in the self-host
  either (`*A p = c ? &a : &b`); it accepted any two pointers.
- `va_arg<T>` of a type the default argument promotions widen is an error that names
  the type to read: `float` arrives as `double`, and `bool`, `char`, `int8`, `int16`,
  `uint8` and `uint16` as `int` (it read the wrong bytes).
- Without `-o`, `eskiuc-esk` writes the object `FILE.o` like `eskiuc` (it printed the
  IR); `--test-codegen` prints the IR.
- A `return` or a `?` inside a `finally` block is an error, in both compilers: it would
  discard the pending exit, including an exception being unwound (C++ hung compiling it,
  the self-host crashed). `break` and `continue` inside a `finally` keep working, and a
  lambda written there is its own function.

- `!` of a void value (`!v()`, also through a method or interface call), `throw v()` of a
  void call and dereferencing a `*void` (`*p`, `*p = v()`) are type errors in both
  compilers; they compiled (the self-host emitted invalid IR for `*p;`).
- Inside a lambda, taking the address of a captured variable's storage (`&n`, `&p.a`,
  `&arr[0]`) or slicing a captured array (`arr[0..2]`) is an error, like assigning to
  it: the address is the closure's copy, so a write through it was silently lost.
- `alloc_with` (a call to the alloc method), `thread_create` and `thread_join` end a
  global's `?*T` narrowing like a call, and the rest of a block after an early-exit guard
  is not narrowed when the branch that falls through assigns the variable
  (`if (p == null) { return 0; } else { p = null; } return p.v;` was accepted).
- **Inferred type arguments no longer come from the first argument alone.** A binding
  from a composite parameter (`*T`, `List<T>*`) wins; by-value `T a, T b` arguments that
  deduce different integer types meet at their common type (`maxof(1, big)` with `int64
  big` is `maxof<int64>`; it was `maxof<int>` and truncated `big`), and any other
  disagreement (`maxof(1.5, (float)2.5)`) is a located error in both compilers.
- **Constant operands are checked through their folded value.** Integer division or
  remainder by zero, the most negative value divided by `-1`, a shift count out of range,
  a floating constant cast to an integer type that cannot hold it and a constant index
  out of bounds are errors when the operand folds through a `const` name, a fixed-size
  `sizeof`, a cast or arithmetic (`5 / Z` with `const int Z = 0`, `x / (int)0.5`,
  `1 << sizeof(int64) * 8`, `(int)D` with `const double D = 1e10`, `M / -1` with `M` the
  most negative `int`, `a[sizeof(int)]` on an `int[4]`). Only the literal forms were
  checked; the others emitted poison. Both compilers; tests `errors/const_*`,
  `errors/float_cast_*`, `errors/index_oob_sizeof`, `const_fold_checked`.

- **String and char escapes follow C.** An octal escape `\NNN` (one to three digits)
  is the byte it denotes (`"\101"` is `A`, `"\012"` a newline; `"\012"` was NUL then
  `12`), `\a`, `\b` and `\?` are recognized, and any other escape (`\q`), an octal
  escape above `\377` and a `\x` with no hex digit are lexer errors located at the
  backslash (an unknown escape used to be the character itself). Both compilers and
  the `#if` evaluator; tests `escape_octal`, `errors/escape_unknown`,
  `errors/escape_octal_range`, `errors/escape_hex_empty`.

- **A type nests at most 1000 levels** (each pointer level, array dimension, template
  argument list and fn type counts one): deeper is a located `type nesting too deep`
  error in both compilers, alongside the 100000-level statement and expression limit.
  Test `errors/type_nesting_too_deep`, `tests/deep/gen.sh` (`types_deep_999`,
  `generic_too_deep`).

### Deprecated
- Stdlib modules built around a struct now use `Type_method` names, as the naming
  convention says: `Rng_*` (`<random>`), `Regex_search`/`Regex_free`/`Match_*`
  (`<regex>`), `Heap_*` (`<sysheap>`), `EventLoop_*` (`<eventloop>`), `Executor_*`
  (`<executor>`), `Chan_*` (`<channel>`), `HpackDecoder_*` and
  `HpackHuff_build`/`HpackHuff_free` (`<hpack>`; `hpack_huff_code`, `hpack_huff_len`,
  `hpack_huff_decode`, `hpack_huff_encode` and `hpack_huff_encoded_len` keep their
  names), `H2Conn_*`/`H2Stream_*` (`<http2>`), `DateTime_to_epoch`/`DateTime_format_iso`
  (`<time>`). Factories keep their names (`el_new`, `executor_new`, `chan_new`,
  `regex_compile`). The old names remain as wrappers and will be removed in a later
  release. `String_is_space` is deprecated in favor of `is_space` from `<ctype>`.

### Fixed
#### Miscompiles of valid code
- A `let` in a nested block overwrote the outer variable of the same name for the rest of
  the function (including inside `for` bodies and across `await`).
- Compound assignment (`a[f()] += 1`, `a[i++] += 5`, `getf().lo += 2` on a bitfield)
  evaluated its left side twice.
- An unbraced `defer` (`if (c) defer ...;`, a `case` body, a loop body) ran even when it
  was never reached; an unbraced body is now its own scope.
- A ternary with a literal wider than 32 bits or an unsigned arm was truncated, and a
  `null` arm crashed LLVM (the `null` arm in C++ only).
- A lambda body inherited the enclosing function's defers, loop targets and unwind
  block, and a generic first instantiated inside `try` produced invalid IR (C++).
- `finally` now runs when a `catch` handler leaves by `return`, `break` or `continue`.
- A `static` local captured by a closure is shared (not copied), and an uninitialized
  `static` starts at zero.
- A range loop's upper bound (`for (i in 0..n())`) is evaluated once.
- Operator overloads inside generic bodies, `for (row in m)` over a 2D array, and
  denormal or overflowing float literals (C++).
- Member, index and method access on rvalues (`mk().a[1]`, `a.add(b).add(c)`); `!` on
  pointers and floats and a unary `operator !` overload; `case K:` with a `const` or a
  constant expression.
- Unions have C alignment; bitfield `++`/`--` works, bitfields use C storage words, and
  their signedness follows the dealiased type.
- Async: `do`/`while`, `defer` and lambdas capturing locals work inside an `async fn`;
  a local named `fr` no longer collides with the frame pointer, and shadowed names inside
  lambda bodies are renamed correctly.
- Self-host: expression temporaries no longer allocate stack on every loop iteration
  (long loops overflowed the stack); pointer arithmetic, promotions and ternaries get the
  right type; ADT payloads with arrays or nested enums, and bitfield structs, have the
  same size as in the C++ compiler; negative and non-`i32` global constants keep their
  value; calling a returned closure directly works; an `extern` followed by its
  definition no longer emits two definitions; chained assignment yields the converted
  value.
- Found by the C oracle: a shadowing initializer (`int64 x = x + 1;`) reads the outer
  variable; a deferred statement uses the names visible where it was written, even at
  an exit where a later declaration shadows one; a braced `defer` body run on an early
  exit no longer crashes codegen (C++); `continue` inside a `switch` case runs only the
  loop's defers (self-host); a `const` initializer using `sizeof(struct)` folds to the
  real size (self-host folded 0); literals and `sizeof` keep their type in mixed-sign
  operations (self-host).
- Built-in operators inside generic bodies get their instance types, so unsigned
  division and comparison in a generic function are unsigned (C++).
- Names synthesized by the async transform and by range loops (`__fr`, `st`, `__end_i`
  and the rest) no longer collide with user identifiers.
- An `extern` prototype next to the program's own definition keeps the Eskiu
  convention, so by-value structs and fn parameters agree between the call and the
  definition (C++).

#### Type checker
- Missing-return analysis is sound for labeled `break`, `break` before a `return`, and
  `switch` fall-through, and no longer flags a case that falls into a returning
  `default`.
- `?*T` narrowing is flow-sensitive (a reassignment or a shadowing declaration ends it)
  and covers `&&`, `||`, `!`, early-exit guards (`if (p == null) return ...;`), loop
  conditions and ternaries. A narrowed pointer can be passed or assigned as `*T`.
- Calls through `fn` values, fields and interface methods, and generic calls, check the
  argument count and types; a generic call also checks its type-argument count and
  reports a type argument it cannot infer. `break`/`continue` escaping a lambda is
  rejected.
- Located errors instead of compiler crashes for cyclic type aliases, `void` variables,
  parameters and fields, float, pointer or over-wide bitfields, a struct containing
  itself by value, unknown types and wrong template argument counts. An unknown type no
  longer triggers a cascade of conversion errors.
- Errors that only codegen used to report (assigning to an rvalue, `&` of an rvalue or a
  bitfield, invalid indexing and casts, a non-constant `case`, `break` outside a loop,
  constant slice bounds out of range) now come from the type checker with a location.
  Integer literals in compound assignments and ternary arms are range-checked.
- `must_use` applies to method-call syntax. An assignment target is no longer reported
  as an uninitialized read. A leading-star pointer to a generic instance (`*List<int>`)
  is accepted as a declared type.
- `-Wall` no longer flags methods used through dot calls, interfaces or operators, or
  the parameters of prototypes (an unused lambda parameter is still reported, like any
  other parameter); the unused-parameter warning points at the parameter. Diagnostics spell operator functions as `operator +(V, V)`.
- Self-host: dot calls to free-function methods (`s.trim()`) are accepted; many invalid
  programs it used to accept (C-style array declarators, `++` on a float, a mismatched
  `?`, misplaced array literals) are rejected like the C++ compiler; its diagnostics
  carry `line:col`.
- `int32 main()` is accepted as an entry point. Generic arguments are inferred through a
  `const Box<T>* self` receiver, and the self-host infers every type parameter of a
  nested generic argument (`HashMap_get(&m, k, &out)` with no `<K, V>`).

#### Front end, driver and tooling
- A C-style local of function type (`fn(int32)->int32 h = f;`) parses (C++).
- `x < y >> 1` parses (a backtracked generic parse left `>>` split), and `(*p)` parses
  as a dereference rather than a cast.
- Macro arguments keep string and character literals whole, nested macro calls in
  arguments expand, and an apostrophe in a comment no longer stops expansion. CRLF
  sources and `\` continuations work.
- Imports are deduplicated by canonical path, so diamond and circular imports work.
- Every diagnostic is `file:line:col`, including parse and lexer errors; an error inside
  an imported file names that file; parse errors recover at the next declaration.
- The `--test-*` modes predefine the same macros as a real build and exit non-zero on
  errors; `--definition-at` resolves by scope.
- `run` exits with `128+signal` when the program is killed, drops a `--` separator and
  accepts flags with values before the script. `-o` naming an input file, a directory
  input and `-O4` are refused. `--help` lists the Eskiu options and subcommands without
  LLVM's internal ones. `$CC` may carry arguments.
- Deeply nested or very long input no longer overflows the compiler's stack. Binary
  operators parse by precedence climbing, operator and `else if` chains are walked by
  loops in every pass (no chain limit), the compiler runs on a 1 GB stack, and more than
  100000 nesting levels is a located `nesting too deep` error.
- Faster builds: `-O0` no longer runs the optimizing backend passes, and symbol lookup
  takes constant time at any scope depth.
- `fmt` indents code after a closing `*/` and preserves CRLF line endings.
- Self-host driver: spawns clang without a shell (safe with any argument), accepts the
  full flag set (`-Wall`/`-Wextra` are accepted and ignored: lint warnings come only from
  the C++ compiler), finds the stdlib through `$ESKIU_ROOT`, and reports a missing import as
  an error.
- VS Code server: full-document sync, unsaved buffers resolve relative imports, and the
  version comes from `package.json` (extension 0.0.27, which also highlights octal and `\a` `\b` `\?` escapes).

#### Standard library: memory safety
- `tls_read_frame` / `tls_read_frame_async` check the peer's frame length (heap
  overflow).
- HPACK: a table-size update above the SETTINGS limit is rejected (heap overflow), a
  literal naming an evicted entry no longer reads freed memory, and integer and string
  decoding stay inside the header block.
- A dropped `Chan_recv` future no longer leaves a dangling waiter.
- `fs_read_all` works on pipes and stdin; `String_concat(&s, &s)` and
  `Bytes_append(&b, &b)` no longer read freed memory; the `Map` hash can't go negative.
- HTTP/2 response headers are sized to fit and split into CONTINUATION frames.

#### Standard library: logic
- Regex: repeated groups (`(a){8}`) match, capture memory is bounded, `a{3,1}` is an
  error, and a leading `]` in a class is literal.
- `Json_int` writes the full `int64`, the builder escapes control characters, and a
  failed parse frees its partial tree.
- HTTP: header values are trimmed of optional whitespace and error bodies are valid
  JSON.
- Multipart no longer matches `name=` inside `filename=` and handles empty fields;
  `net_write_async` no longer leaks; `EventLoop`/`Executor` close their descriptors and
  the timer table grows.
- `FirstFit` (and so `<sysheap>`) coalesces adjacent free blocks; freeing `null` is a
  no-op.
- `String_from_int(INT_MIN)`, `String_init(&s, 0)`, POSIX `dirname`/`basename` edge
  cases, negative ISO years and `env_get_int` fallbacks on non-numeric values.
- HTTP: a body above 2 GiB is copied and its `Content-Length` written in full (`int64`
  throughout).
- The internals of 34 stdlib modules use the current language (dot-calls, `defer`,
  `const`, range loops) with no API change; `tools/gen_hpack_huffman.py` emits `switch`
  tables for the Huffman code.

#### Second audit round
- Exceptions: a `defer` runs when an exception unwinds through its block; a thrown value
  keeps its type and a generic `throw` matches its `catch`; calls through a closure, a
  vtable or a generic instance unwind inside `try`; a `throw` inside a `defer` body ends
  that exit path cleanly; `?` inside a `defer` body is an error.
- Closures and fn values: calls through a fn value parse nested fn types, convert their
  arguments and use `sret` for struct results; arrays of fn values work; lambda
  parameters can be reassigned; a non-escaping closure parameter can be passed on to
  another non-escaping parameter; a named function used as a ternary arm decays to a
  closure (self-host).
- Generics: a prototype before its definition, interface arguments boxed at generic
  call sites, generic instance methods in interface vtables, arrays and slices of
  generic instances, a method call on a source-form instance, an operator overload on a
  generic instance inside a template body, a generic struct containing itself by value
  (now an error), and a type alias cyclic through a type argument (now an error).
- Pointers: `string` arithmetic and `*void` arithmetic step by bytes; `for-in` and member
  access work through a `*struct` pointer.
- `?*T` narrowing ends at an assignment inside a condition and follows pointer
  arithmetic on the narrowed pointer. A nullable non-pointer type is an error.
- Front end: imports see the macro table as of their import line, `__FILE__` is escaped
  as a string, macro comments and arity errors are handled, rescanning reaches a call
  that spans the expansion, `#if` arithmetic wraps and short-circuits like C, an
  out-of-range float constant cast is an error, and 64-bit integer literals and
  exponent digits are checked. Every top-level and use-site diagnostic carries a
  location.
- Driver: nothing is printed on a successful compile; test modes take every input the
  way a build does; importing a directory is an error; `_WIN64` is defined for arm64
  Windows triples; `fmt` keeps line numbers and literal bytes.
- Self-host: temp `.ll` files are created with `mkstemps`, `run` accepts `--` before
  the script, `--test-codegen` type-checks first, parse errors are located, and sema
  matches the C++ checks on lambda bodies, conditions, operands, assignability, match
  arms, variant constructors, array literals, indexes, members, `for-in`, constraints,
  alias targets, `await` operands and writes through a pointer to `const`.
- `alloc_with` returns `null` when `n * sizeof(T)` overflows.
- Stdlib: HTTP/2 frames on a half-closed stream are stream errors and SETTINGS values
  are validated; async servers retry a failed accept; `Chan_free` detaches a parked
  receiver; the executor's self-pipe wakes coalesce; the event loop handles a zero
  capacity and frees callback environments; multipart reads the boundary parameter and
  CRLF delimiters; HTTP/1.1 rejects a truncated body, conflicting lengths, bare LF and a
  NUL in the body; the ALPN list is bounds-checked; HPACK sizes its scratch buffer,
  rejects a late table-size update and a field that overflows; `json` frees its tree
  iteratively; `String` and `Map` use-after-free, substring clamping and `int64` epochs
  are fixed; allocator sizes can't overflow and a tiny `FirstFit` buffer is handled.

#### Third audit round
- Bitfields: a narrower signed value stored into a wide bitfield keeps its sign, and a
  global or `static` initializer of a bitfield struct, a union or a packed struct keeps
  its values (it was zero-filled). Union literals, including a global union set through
  a pointer member, produce valid IR.
- Async functions: a `static` local keeps its value across calls, and `for-in` over a
  slice, `try`/`catch`, array literals and `match` on an enum local work.
- Generics: an instance reached only through a call result (`flip(n).a`,
  `unbox(bx(7))`, `bx(2.5).get()`) is instantiated.
- Interfaces: arrays of interface values, method calls through `*I`, a global interface
  value and a closure taking an interface argument work; returning a large array uses
  `sret`.
- A struct field whose type is declared later in the file gets the right layout (it was
  laid out as `i32`). Member access through `const *T` works.
- Lambdas can return a struct, a pointer or a generic instance; `(*pf)(x)` calls through
  a pointer to a closure; arrays of closures and pointers to closures work in the
  self-host; unary `+` compiles.
- A function-like macro call can span lines, `#pragma` is accepted inside a function or
  struct body, and `fmt` leaves the bytes of a multi-line string literal alone.
- Self-host: rejects casts from an interface, a brace initializer for a non-array, a
  mistyped global initializer, a struct literal with wrong type arguments and a free
  function with the wrong signature for a constraint, as the C++ compiler does.
- Stdlib: regex loops whose body can match empty follow RE2 (`(a?|b)*` on "b" is 0,0), a
  repeat count above 1000 is an error, HTTP/2 rejects CR, LF and NUL in header fields,
  RST_STREAM on an idle stream, a short GOAWAY and a stream that depends on itself, and
  `http_reply` accepts a `null` body.

#### Fourth audit round
- A `finally` runs when a `catch` handler throws (directly or from a call); the new
  exception then propagates.
- A `bool` bitfield at a nonzero bit offset reads back what was stored (it used an `i1`
  storage unit, so it read false or trapped at `-O2`).
- `for (x in E)` evaluates `E` once: a call, or an index by a variable, was evaluated
  again for every iteration (synchronous and async loops). The C++ checker accepts a
  pointer to a generic list returned by a call as the iterable.
- The C++ checker accepts arithmetic, comparisons, `++`/`--` and unary operators on a
  value typed by an alias through a field, element, return value or pointee
  (`type u8 = uint8; s.a + 1`).
- Awaiting a future of a struct or a generic instance no longer prints a spurious
  `0:0: cannot convert` error, and an error in the lowered async code now fails the
  build instead of being ignored.
- Self-host: rejects `free_closure` of a non-closure, a sum type as a bitfield type,
  reading or assigning an inline method as if it were a field (`p.sum`), a string slice
  into a non-`char` slice, and `alloc_with` with a non-integer count or an allocator
  without `alloc` (each emitted invalid IR or garbage before).
- Async: dropping a `select2`, `join2`, `select2v` or `join2v` future before it resolves
  drops its inputs (their producers are cancelled and their wakers unhooked). A later
  completion of an input used to write into the freed combinator, for example when an
  outer timeout cancelled a task that was awaiting a `select2`.
- Sockets: a send to a peer that has closed returns an error (`EPIPE`) instead of raising
  `SIGPIPE` and killing the process. `net_send` passes `MSG_NOSIGNAL` (Linux, macOS),
  macOS sockets from `net_accept`/`net_tcp_connect`/`net_tcp_listen` get `SO_NOSIGPIPE`,
  the async write path sends through `net_send`, and a TLS connection on Linux sets
  `SIGPIPE` to ignored (OpenSSL writes with `write()`).
- HTTP/1.1 (`http_recv`, `HttpRequest_parse`) follows RFC 9112 framing: a chunked body is
  decoded (it was left unread), `Transfer-Encoding` with `Content-Length` is 400, a coding
  other than `chunked` is 501, and whitespace before a header colon, an obsolete line
  fold, a missing or repeated `Host` in HTTP/1.1 and a request line without a valid
  `HTTP/x.y` version are 400 (HTTP/2.0 on the HTTP/1 path is 505). A method must be a
  token.
- `multipart_part` finds the part by the `name` parameter of its `Content-Disposition`
  header only (a `name=` in `Content-Type` matched before), and parameter and header names
  are case-insensitive.
- `url_query_get` decodes each key before comparing it, so `a%20b=1` and `a+b=1` match the
  key `a b`.
- Regex: an invalid bracket range (`[z-a]`, `[a-\d]`) is a compile error as in RE2, and
  `\D` `\W` `\S` inside a bracket class are the complemented shorthands (they were read
  as the letters).
- A cast in a constant expression truncates and sign-extends as in C, so `case (int8)259:`
  and `case 3:` are duplicate labels and `a[(int8)257]` is `a[1]`.
- `fmt` keeps a line after a `\` continuation byte for byte (it re-indented it, changing a
  spliced macro body).
- A variant constructor's integer literal argument must fit its payload type
  (`A(300)` for `A(int8)`), and `null[0]` is a type error (it was a codegen error or
  segfault).
- An interface arm and a struct-pointer arm of `?:` meet as the interface in both
  compilers (C++ codegen crashed, the self-host rejected it).
- Self-host: rejects `match` on a pointer or a struct, casts to and from a sum type, a
  struct literal of an enum, calling an enum member, an array or a non-closure field, a
  method used as a value, a variadic function assigned to a number, a payload variant
  without arguments, `sizeof` of a function, variant or later global, `++` on an enum, a
  by-value cycle through a type alias, an array size naming a later `const`, and an
  uninitialized read in a generic instance, as the C++ compiler does.

#### Fifth audit round
- `List_free` sets `data` to null, so a push after it regrows the list and a second free
  (also through `String_split_free`) is a no-op; it was a use after free and a double free.
- HTTP/2: a malformed request (RFC 9113 §8.1.1, §8.2.2, §8.3) is reset with
  RST_STREAM PROTOCOL_ERROR and the handler is not called; it was answered 200. That
  covers an unknown or response pseudo-header, a repeated one or one after a regular
  field, a missing `:method`, `:scheme` or `:path` (CONNECT: `:authority` and no
  `:scheme`/`:path`), `connection`, `keep-alive`, `proxy-connection`,
  `transfer-encoding` and `upgrade`, `te` other than `trailers`, a content-length the
  DATA frames do not add up to, and a pseudo-header in trailers. Frames the peer had in
  flight on a stream the server reset are ignored instead of drawing a second
  RST_STREAM or a GOAWAY.
- HTTP/2 responses drop the handler's connection-specific fields and its
  `content-length` (it was sent next to the real one).
- A 1xx or 204 response has no Content-Length (RFC 9110 §8.6) in `HttpResponse_render`,
  `http_reply` and HTTP/2, and no body.
- HTTP/1: a NUL in a header value or the request target makes `HttpRequest_parse` and
  `http_parse_head` fail (RFC 9110 §5.5); a header lookup stopped at it.
- Regex: escapes follow RE2. `\b` `\B` (ASCII word boundaries), `\A` `\z`, `\xHH`,
  `\x{HH}`, octal (`\0`, `\012`, `\101`) and `\a` are supported, and any other escaped
  letter or digit (`\q`, `\1`, `\Z`, `[\b]`) is a compile error; they all matched the
  literal letter. In a class, a `-` after a shorthand is a literal (`[\d-z]` is digits,
  `-` and `z`, as in RE2); the fourth round made it an error, wrongly citing RE2.
- `sizeof(var)` in a generic body measured the variable as an `int` (4 bytes) whatever
  its type (`T[4] loc`, `B<T> b`, a lambda's local); `catch (T e)` and `catch (Err<T> e)`
  in a generic body never matched, so the exception terminated the program.
- A slice or array of a struct with an `operator []` indexed through that operator in the
  self-host (`s[i]` on a `V[]`, and `for (v in s)`): the operator's mangled name dropped
  the brackets, so `V[]` named like `V`. Brackets are now part of the name in both
  compilers.
- Self-host: a lambda whose type names a type parameter was rejected in a generic instance
  (`return T() {...}`, `fn(T)->T d = T(T x) {...}`); a call to a generic variadic function
  was emitted non-variadic (garbage arguments); an array of closures as a global or
  `static` gave its lambdas the array as their return type (invalid IR); a union constant
  whose member is a struct or an array was rejected without a location.
- A struct field, a pointee or an array of structs typed through an alias of an array
  (`type AI = int[3]`) compiles in C++ (`q.b[2]` and `(*p)[2]` with `*AI p` were codegen
  errors, a field alias of a struct array crashed), and `*AI` is a pointer to the array in
  the self-host (it lowered as an array of pointers). A struct holding itself through
  such an alias (`type AR = R[2]; struct R { AR a; }`) is rejected by C++ as by the
  self-host (it compiled with a 4-byte field).
- When the target is an interface, each arm of a `?:` is boxed with its own struct's
  vtable, so `I i = c ? &a : &b` with different conforming structs works in declarations,
  assignments, returns and arguments (C++ rejected it, and the self-host called the first
  struct's method on the second).
- An array dimension is an integer constant expression folded like a `const`: casts
  truncate as in C (`int[(uint8)258]` has 2 elements), and arithmetic over numbers,
  `const` ints and enum members works (`int[N + 1]`). Both compilers rejected anything but
  a number or a single name. The self-host resolves an enum member as a struct field's
  dimension (`int[B] a`, it was invalid IR).

#### Sixth audit round
- HTTP/2: `h2_fill_request` leaked 16 bytes per request (`String_from` on the already
  allocated version String).
- HTTP/1: `HttpRequest_parse`, behind `http_serve` and `http_serve_async`, now frames a
  request by RFC 9112 with the code `http_recv` uses: the body is exactly Content-Length
  bytes (it was every byte after the head), an invalid, listed or repeated differing
  Content-Length is rejected, and a request with no blank line after its head or a body
  shorter than its Content-Length is incomplete instead of accepted. The new
  `HttpRequest_parse_status` returns 0, -1 (more bytes needed) or the status (400, 413,
  501, 505). Both servers read until the request is complete (`HttpConnBuf_feed`), so a
  request split across TCP segments reaches the handler whole; a peer that closes early
  gets 400, a head over 64 KiB 400 and a body over 1 MiB 413.
- A Host value must be uri-host [":" port] (RFC 9110 §7.2): `Host: a b`, `Host: a, b`, a
  bad port or `%` escape is 400 in `http_recv` and `HttpRequest_parse`.
- A 304 response has no body and no Content-Length, and a HEAD response keeps the
  Content-Length of its body but sends no body, in HTTP/1 (`HttpResponse_render_head`,
  used by both servers) and HTTP/2; both sent the body.
- HTTP/2 trailers follow the header-field rules: an uppercase name, a CR, LF or NUL in a
  value, a connection-specific field or `te` resets the stream with PROTOCOL_ERROR, as a
  pseudo-header already did.
- Regex: `\s` is `[\t\n\f\r ]` as in RE2 (it also matched `\v`, and `\S` and `[^\s]`
  missed it). POSIX classes in brackets (`[[:alpha:]]`, `[[:^digit:]]`, the 14 RE2
  names) are supported; they were read as a set of letters and a stray `]`. A `{` that
  does not start a `{n}`, `{n,}` or `{n,m}` repeat is a literal (`{`, `a{`, `a{,2}`), as
  in RE2; it was an error.
- `sizeof(T)` in an array dimension is an integer constant, as in C
  (`uint8[sizeof(Header)]`, `int[sizeof(S) / 4]`): a fixed-size scalar folds in the type
  checker, any other type's size comes from the target layout in codegen. Both compilers
  rejected it.
- The self-host accepts `<`, `<<`, `<=`, `>` and `?:` in an array dimension
  (`int[K << 1]`, `int[K < 4 ? 2 : 1]`); its type-spelling scanners read the `<` as a
  generic argument list ("unknown type").
- An array of an array alias (`type Arr = int[3]; type Mat = Arr[2];`) is 2 arrays of 3
  in the self-host (it laid out 3 arrays of 2), and C++ indexes three levels of such
  aliases (`Cube c; c[3][1][2]` was a codegen error).
- A nested brace initializer whose rows are an array alias (`type A = int[2];
  A[2] m = {{1, 2}, {3, 4}};`, also a global) compiles in C++; it was rejected.
- `alloc_with` over an instance of a generic allocator works in both compilers, through
  its inline `alloc` method or a top-level `G_int_alloc(*G<int> self, int64 n)`: C++
  failed in codegen without a location, and the self-host rejected both.
- The self-host lays out 32-bit x86 targets (`--target i686-...`): 4-byte pointers, and
  4-byte alignment of `int64` and `double` on SysV (8 on Windows). It used the 64-bit
  layout, so `sizeof` and struct sizes differed from C++. `cabi_parity.sh` compares the
  sizes of `tests/target_sizes.esk` per target.
- C++ iterates `for (x in *p)` with `p: *A4` (`type A4 = int[4]`); it was rejected.
- The self-host rejects a cast of a void value (`(int)v()`), of a `{...}` literal
  (`((S){1}).a`) and, in a generic instance, of an int to a struct type argument
  (`(T)0` with T = S), and a type argument that makes a parameter `void`
  (`id<void>(v())`), as C++ does.
- x86-64 SysV C ABI: an eightbyte holding a union is as wide as its widest member, as in
  clang. `union { int; double; }` passed or returned by value is an `i64` (it was an
  `i32`, losing the high half), `union { char; int; }` an `i32`, `union { float; double; }`
  a `double`, and `struct { float; union { int; float; double; } }` is `{ float, i64 }`.
  Both compilers; test `c_abi_union` (+ `.c`), also in `cabi_parity.sh`.
- A type alias is its target for every shape check, in both compilers: the expression
  types the type checkers and codegens reason about never name an alias (C++
  `getExpressionType` and `getExprEskiuType`, self-host `sema_infer_type` and `cg_etype`).
  An alias of an interface boxes (`type Sh = Shape; Sh s = &q;` was a bus error in C++, an
  `Sh` parameter a codegen error) and dispatches (`s.area()`, `mk().area()`); an alias of
  a pointer derefs (`PSq[2] ps; ps[0].s`) and dot-calls (`pq.area()`, also on generic
  instances `BI`/`PB`); a field or return typed by an alias of a fn type calls
  (`s.f(1)`, `getf(5)(1)`; invalid IR or a crash in the self-host); a `match` on an alias
  of a classic enum is accepted and checked for exhaustiveness (C++ rejected it, the
  self-host accepted a missing arm); an operator over alias operands (`operator +(VV,
  VV)`) and a `*VV self` receiver resolve in the self-host; `.len` of an element of `IS[2]`
  (`type IS = int[]`) works, and `IS[2]` / `*IS` keep the slice as their element in the
  self-host (they collapsed to `int[2]` / `int*`). Test `alias_shapes`.
- A struct pointer boxes into an interface as a variant payload (`Som<Shape>(&a)`,
  `W(&b)` for `enum Wr { W(Shape) }`; C++ segfaulted, the self-host emitted invalid IR)
  and as a lambda's return value (`Shape() { return &g; }`, a C++ verifier error). Test
  `iface_payload_lambda`.
- A member of an overloaded `[]` result (`w[2].v`) is read from the call's value (a C++
  codegen error, invalid IR in the self-host). Test `index_result_member`.
- A global (or `static`) initialized with a function name (`Op g = add;`, also in an
  array or struct literal) is a constant closure, as in C; both compilers rejected it as
  not a compile-time constant. Test `global_fn_value`.

#### Seventh audit round
- HTTP/2: request bodies were buffered without limit (every DATA frame appended while
  WINDOW_UPDATE gave the credit back, 400 MB over 128 streams). The `H2Server` engine
  now buffers at most `s.max_body` bytes per request (default 1 MiB, as HTTP/1.1) and
  `s.max_buffered` across a connection (default `H2_MAX_BUFFERED`, 4 MiB); a request past
  either, or with a content-length past `max_body`, is answered 413 without the handler
  and its stream reset with NO_ERROR. Test `http2_body_limit`.
- `http_serve_async` sent each answer with a blocking `net_send`, so a client that asked
  for a large body and did not read it stalled the event loop and every other client.
  The connection socket is now non-blocking and the answer goes out through
  `net_write_async`. `net_set_nonblocking` (and the executor's self-pipe setup) did
  nothing on arm64 macOS: `fcntl` is variadic and was declared with a fixed third
  parameter. Test `http_async_slow_reader`.
- `HttpConnBuf_feed` reparsed the request from its first byte on every read, so a
  chunked request fed in small pieces cost time quadratic in its size. It now parses the
  head once and decodes the chunked body as it arrives; a chunk-size or trailer line over
  `HTTP_CHUNK_LINE_MAX` (8192 bytes) is 400. Test `http_conn_feed_incremental`.
- Response header injection: `HttpResponse_header` accepted a CR or LF in a value (and
  any bytes in a name), so a handler echoing input could add a header line. It now
  returns 0 and adds nothing for such a pair (1 otherwise), and the HTTP/2 encoder drops
  a hand-written line with a non-token name or an LF or NUL. A Content-Length the
  handler sets is no longer sent next to the automatic one in HTTP/1 (a 1xx, 204 or 304
  keeps it). Test `http_response_header_inject`.
- `http_chunked_step` spun forever on a chunk size near `INT64_MAX` when the limit
  allowed it (`size + 2` overflowed in the room check). Test `http_chunk_size_max`.
- `&e` evaluates its operand once in C++: `&buf[i++]`, `&A[f()]`, `&getp().x`, `&*gp()`
  and a `for-in` over a List element `ls[f()]` ran the side effect twice. Test
  `addr_of_once`.
- A cast to an alias of an unsigned type (`(u8)-1`) or a call returning one zero-extends
  in the self-host (it sign-extended, so `(u8)-1` was -1 and `f() + 10` with `f` returning
  `u8` 250 was 4). Test `alias_unsigned`.
- The self-host checks every argument of an inferred generic call against the
  instantiated parameter types, as C++ does: only a bare `T` parameter was checked, so
  `swp(&i64, &s.a)` against `*T` corrupted memory and `List_push(&l, 1.5)` on a
  `List<int>` compiled. Tests `errors/generic_ptr_arg_mismatch`,
  `errors/generic_struct_param_arg`, `generic_infer_common`,
  `errors/generic_deduce_conflict`.
- `fmt` follows the preprocessor's view of strings and comments, in both drivers: a
  stray `"` in a skipped `#if 0` branch or inside a `/* */` opened on a `#define` line
  made it think a string was open and reindent a later multi-line string, changing the
  program. Each branch of a conditional starts from the string state at its `#if`, a `#`
  line is a directive even inside a multi-line string, and a directive that leaves a
  comment open opens it for the following lines. Test `fmt_cases/pp_string_state`.
- A field or element of a temporary (`mk().x`, `arr()[0]`, `mp().v`) is not storage:
  taking its address or assigning to it is a located type error, and slicing a
  temporary array (`mp().v[0..2]`) is "cannot slice a temporary array" in both
  compilers (C++ failed in codegen without a location, the self-host accepted
  `&mp().x`). Tests `errors/slice_temporary_array`, `errors/addr_of_rvalue_member`,
  `errors/assign_rvalue_element`.
- The self-host rejects a binary operator on a struct operand when no overload accepts
  the operand types (a pointer argument converts only through `*void`), as C++ does:
  `v + &g.a` for `operator +(V, *int64)` compiled to invalid IR. Test
  `errors/operator_ptr_arg_mismatch`.
- Narrow integers cross the C boundary extended, as clang does: an `extern`'s `int8`,
  `int16`, `uint8`, `uint16`, `bool` and `char` params and results carry `signext` /
  `zeroext`, and an Eskiu function returning one (a callback, or a function C calls by
  name) extends its result. AArch64 Darwin and x86-64 C code relies on it; a C callee
  read garbage high bits. `char` follows the target's C `char`. Both compilers; test
  `c_abi_narrow` (+ `.c`), also in `cabi_parity.sh`.
- A `va_list` passed to a C function (`vprintf`, `vsnprintf`) goes the way the target's
  C `va_list` does: by address on x86-64 System V and AArch64 Linux, as its `char*` on
  Darwin AArch64, Windows x64 and 32-bit ARM. It was passed as a 32-byte aggregate (a
  crash on x86-64). Both compilers; test `va_list_c`.
- Every load and store through a `volatile` variable is volatile: `*p`, `p[i]`, `p.f`,
  `++`/`--` and compound assignment, for locals and globals. C++ marked only the
  variable's own loads (a volatile global not even those), the self-host ignored
  `volatile`. Test `volatile_access` (IR checked in `run.sh` and `cg_parity.sh`).
- The self-host emits inline assembly (it dropped `asm(...)` statements), with inputs
  and clobbers like C++. Test `inline_asm_ext`.
- An async function whose `match` arms or `try`/`catch`/`finally` bodies declare locals
  compiles: the lowering hoists them to frame fields (C++ failed with "has no member",
  the self-host emitted invalid IR). Test `async_arm_locals`.
- The self-host instantiates a generic enum with an alias type argument (`Opt<F>`,
  `type F = int`) as the target's instance (invalid IR). Test `generic_enum_alias_arg`.
- The self-host dot-calls through a pointer to an alias (`IL* l; l.len()` with
  `type IL = List<int>`), as C++ does. Test `alias_ptr_dotcall`.
- A closure param of an async function is retained by the coroutine frame, so the
  call's lambda gets a heap environment even when the body only calls it. C++ rejected
  the program with an internal error. Test `async_closure_param`.

#### Eighth audit round
- Returning the address of a local through a pointer cast or a `?:` arm
  (`return (*void)&x;`, `return c ? &x : &y;`) is the dangling-pointer error, like
  `return &x;`. Both compilers; tests `errors/dangling_cast`, `errors/dangling_ternary`.
- A struct that holds itself by value through an alias of a generic instance
  (`type RA = W<RB>; struct RB { RA a; }`) is rejected; C++ gave it a wrong layout. Both
  compilers; test `errors/struct_cycle_generic_alias`.
- The self-host declares a prototype that the program never defines, so its object links
  with the one that does (separate compilation failed). `driver_parity.sh`
  `separate/prototype`.
- The self-host rejects assigning an array literal (`a = {4, 5, 6};`) and casting a
  function name to a non-pointer (`(int64)f`), as C++ does. Tests
  `errors/assign_array_literal`, `errors/cast_fn_name_to_int`.
- The self-host driver accepts `-O 2` (a separate value), like `eskiuc`.
- A leading UTF-8 byte order mark is skipped instead of rejected, in both compilers.
  Test `utf8_bom`.
- The HTTP/2 engine bounds a request's decoded header list: over
  `H2_MAX_HEADER_LIST` (64 KiB, as `SETTINGS_MAX_HEADER_LIST_SIZE` counts it, now
  advertised) or past what `s.max_buffered` leaves is 431, without keeping the
  fields, and open streams' header lists count toward `max_buffered`. Only the field
  count was limited, so a large dynamic-table entry referenced many times turned
  13 KB of HPACK into 34 MB. Test `http2_header_list_limit`.
- HTTP/2 requests follow the HTTP/1.1 Host rules: an invalid host or `:authority` is
  400, two host fields or a host that differs from `:authority` is a stream error,
  and without a host field the handler sees `:authority` as Host (it was dropped,
  and any host fields with any value were accepted). Test `http2_host_rules`.
- `http_recv` reads a chunked trailer section incrementally: it rescanned it from the
  last chunk on every read and kept it all (quadratic CPU). A trailer section over
  `HTTP_CHUNK_TRAILER_MAX` (64 KiB) is 400 there and in the servers. Test
  `http_recv_trailers`.
- `HTTP_CHUNK_LINE_MAX` also holds for a complete chunk-size or trailer line, so the
  verdict no longer depends on how the request was split into reads (a 9000-byte
  chunk extension was accepted in one read). Test `http_chunk_line_split`.
- `HttpConnBuf_feed` drops chunked input it has decoded, so a body in tiny chunks
  (6 raw bytes per body byte) is no longer 413 below `HTTP_SERVE_MAX_BODY` and the
  buffer stays small. Test `http_chunk_tiny_chunks`.
- The spec's `#define` continuation example declares `printf`.
- A multidimensional array type in generic code keeps C order: `T[2][3]` in a generic
  function or struct was re-spelled `int[3][2]` on substitution, so a non-square local
  crashed C++ codegen, a field was laid out with swapped dimensions (writes out of
  bounds) and an in-range constant index was rejected. Test `generic_multidim`.
- A generic struct instance keeps its bitfields and packing: `struct G<T> { T v;
  uint8 f : 3; }` had full-width fields in both compilers, and the self-host ignored
  `packed` / `#pragma pack` on a generic struct. The instance is laid out like the plain
  struct of its concrete field types. Test `generic_struct_layout` (+ `.c`).
- The self-host's `sizeof(x)` of an async function's local or parameter, and of an
  enclosing local used only by `sizeof` inside a lambda, is the variable's size (it was
  4). Test `sizeof_var_frame`.
- A dot-call through a leading-star pointer to an alias (`*Counter pc; pc.bump(1)`,
  `type Counter = Cnt`) resolves the aliased struct's methods in C++, as the
  trailing-star form did. Test `alias_ptr_dotcall`.
- An array or slice type argument (`Box<int[3]>`, `Box<int[]>`) instantiates a generic
  struct in C++ ("unknown type 'Box_int'"). Test `generic_array_targ`.
- The self-host accepts returning a slice of a slice parameter (`return s[1..s.len];`)
  and the address of an element of a slice parameter or slice field (`&s[1]`,
  `&p.sl[2]`); they were rejected as dangling. Test `slice_return_nonlocal`.
- A struct literal accepts a trailing comma (`P{ x: 5, y: 6, }`), like an array
  literal. Test `struct_lit_trailing_comma`.

#### Final additions
- `-Wall` warns about a scalar local that may be used uninitialized: a read that some
  path through `if`/`else`, loops, `switch` fall-through, `match`, `try`/`catch`/`finally`,
  `defer`, early exits or a lambda capture reaches without an assignment (`&x` assigns;
  aggregates set field by field are not tracked). A read no path assigns stays an error.
  C++ compiler only. Tests `warnings/maybe_uninit`, `warnings/maybe_uninit_ok`.
- The stdlib HTTP servers (`http_serve`, `http_serve_async`, `http2_serve_async`, the
  TLS servers) disconnect a client that sends nothing, trickles its bytes or stops
  reading: a request head (or the HTTP/2 preface and SETTINGS) must arrive within
  `HTTP_HEADER_TIMEOUT_MS` (10 s), a body within `HTTP_BODY_TIMEOUT_MS` (60 s), an idle
  HTTP/2 connection gets GOAWAY after `HTTP_IDLE_TIMEOUT_MS` (60 s), an answer must be
  taken within `HTTP_WRITE_TIMEOUT_MS` (30 s), and the async servers hold at most
  `HTTP_MAX_OPEN_CONNS` (1024) connections. A cut HTTP/1.1 request is answered 408.
  `HttpLimits` and the `*_with` server variants change them; `<net>` gains
  `net_set_timeouts` and `<net_async>` deadline reads and writes. Tests `http_timeouts`,
  `http2_timeouts`, `http2_tls_timeouts`.
- A declaration may follow `case N:` or `default:` directly, without braces. The switch
  body is one scope, as in C: the variable is visible in the later cases, and two cases
  may not declare one name. Tests `case_decl`, `async_case_decl`,
  `errors/case_decl_redefined`, `errors/case_decl_out_of_scope`.
- A `T[]` parameter infers `T` from a slice argument (`sum(a[0..4])`). Test
  `generic_slice_infer`.
- `sizeof(expr)` is the size of the expression's type, not evaluated (`sizeof(*p)`,
  `sizeof(a[0])`, `sizeof(s.f)`); `sizeof(*p)` used to measure a pointer. The type
  checker folds `sizeof` of a pointer, array, struct or union with the target's layout,
  so the constant checks (zero divisor, index out of bounds, duplicate `case`) see it.
  Tests `sizeof_expr`, `errors/index_oob_sizeof_struct`, `errors/switch_dup_sizeof_ptr`,
  `errors/const_div_zero_sizeof_expr`.
- An enum member value may be an integer constant expression over literals, earlier
  members, `const` ints and `sizeof` (`B = A << 2`), folded like any constant. Tests
  `enum_value_expr`, `errors/enum_value_not_const`, `errors/enum_value_expr_range`.
- A bitfield read or written through a `volatile` variable (`r.mode = 5`, `r.mode++`)
  uses volatile loads and stores of its storage word, and a volatile variable's
  initializing store is volatile. Test `volatile_bitfield` (IR count checked in `run.sh`
  and `cg_parity.sh`).
- A nullary variant written with parentheses (`B()`) is an error located at the call,
  "variant 'B' takes no arguments" (write `B`). Test `errors/variant_nullary_parens`.
- A function-like macro whose name is followed by a newline (and blanks or comments)
  before its `(` is expanded, as in C. Test `macro_call_newline`.
- Deep types, member chains and nested ternaries compile in linear time: type spellings
  are parsed and rendered in one pass (C++ `ty::Type`, parser), a member chain is typed
  once (self-host sema and codegen; C++ codegen's volatile-root walk is memoized), and a
  ternary finds its `:` from a table built once (both parsers). `tests/deep/gen.sh`
  gains `member_chain_50k` and `ternary_then_5k`.
- The blocking TLS server's deadlines (`tls_accept_with`, `tls_read_full_deadline`,
  `http2_tls_serve_conn_with`) hold against a peer that trickles bytes inside one TLS
  record. They used `SO_RCVTIMEO`, which restarted with every byte OpenSSL's record loop
  read, so one byte every 200 ms kept a handshake or preface open forever. The socket now
  runs non-blocking and the calls wait with `poll` (`WSAPoll` on Windows) on what is left
  of the deadline; writes take `tls_write_all_deadline`. `<net>` gains `net_wait_ready`.
  Test `tls_trickle_deadline` (+ `.c`, an OpenSSL stand-in with a record layer).
- An HTTP/2 response must be all sent within the new `HttpLimits.write_total_ms`
  (`HTTP_WRITE_TOTAL_MS`, 5 min) of when it was ready. `write_ms` counts from the last
  data sent, so a peer granting one byte of window every 150 ms kept a 2 MB response and
  its buffer alive forever. Past the cap the stream is reset with CANCEL and its response
  freed; the connection stays open (`H2Server_expire`, `H2Server_timed_out_with`, used by
  the h2c and both TLS servers). Test `http2_write_total`.
- `<net>`'s socket timeouts pass a `struct timeval` of two C `long`s with its own size, so
  32-bit ARM Linux gets the 8-byte layout it expects (it was always 16 bytes). Test
  `run_cmd/net_timeval` (IR checked by `run.sh` for armv7 and x86-64).
- An object-like macro that expands to the name of a function-like macro is invoked when
  the `(` is on a following line (`#define G F`, then `G` and `(5)` on the next line), as
  in C. A line inside a multi-line string literal is string text in both preprocessors:
  a `#undef` or other `#` line there is not a directive and no macro expands in it; `fmt`
  keeps such a line's bytes. Tests `pp_string_lines`, `fmt_cases/string_hash_line`.
- The self-hosted compiler types each `?:` of a deeply nested one once, in the type
  checker (memoized per node while no binding, scope or narrowing changes) and in codegen
  (stamped bottom-up when the outermost one is emitted): 5000 levels took about ten
  seconds to type-check, 20000 now compile in about a second. `tests/deep/gen.sh` gains
  `ternary_mixed_20k`.
- Inline `asm` takes output operands, in both compilers: `asm("op" : "=r"(y) : "r"(x))`
  with register outputs (`=r`, `=&r`, several at once), read-write `+r`, and memory
  `=m`/`+m`, numbered like clang (outputs first) and lowered as clang does. An output
  must be a writable lvalue of a non-bool integer, floating-point or pointer type (not a
  bitfield). Tests `inline_asm_out` (x86-64 IR checked by `run.sh` and `cg_parity.sh`),
  `errors/asm_output_*`, `errors/asm_input_output_constraint`.
- `extern` struct and union arguments and results follow the 32-bit x86 C ABI (cdecl) in
  both compilers, as clang lowers them: an argument of at most 16 bytes made only of
  32/64-bit scalars is passed as those scalars, anything else byval; Linux returns every
  aggregate through sret, while Darwin, Windows and the BSDs return one of 1, 2, 4 or 8
  bytes in registers (a lone `float`/`double` or pointer as itself, except a floating
  one under MSVC). `cabi_parity.sh` covers the i686 targets and compares both compilers
  with clang's IR for the C companions. Test `c_abi_x86_32` (+ `.c`).
- `http_serve` sends an answer under one `write_ms` deadline from its first byte and retries a partial send within it, so an answer is no longer cut short by an early partial send.

- A slice or array compares by its element type in the C++ type checker, whatever the
  spelling (`int*[]` is `*int[]`), and a `T[]` parameter binds `T` from a slice of a
  generic instance or of a trailing-star pointer. Test `slice_elem_spelling`.
- The self-host calls a local or parameter of fn type named like an ADT variant (`A(1)`,
  `B()`) instead of building the variant, as C++ does. Test `variant_name_shadow`.
- The self-host emits a double literal too large for double as infinity in LLVM hex form
  (`0x7FF0000000000000`), as C++ does; a global initializer `1e400` was rejected by clang.
  `float_literal_range` now covers the global case, so `corpus_parity.sh` checks it.
- `a * b;` and `a * g();` with `a` a local or parameter are expression statements in both
  compilers, in a block or after a `case` label (C++ read them as declarations, the
  self-host read `a * b;` after a `case` as one). Test `local_times_stmt`.
- `sizeof` of a generic struct instance, a sum type, a struct with bitfields (SysV/AAPCS
  or MS rules, per target) and a struct holding an interface value folds in the type
  checker, so a duplicate `case sizeof(T):` is a located error instead of an LLVM or
  clang failure. Tests `sizeof_case_layouts`, `errors/switch_dup_sizeof_{generic,adt,bitfield,iface}`.
- `await` works in any position of an `async` function, in both compilers: inside a
  `try` body and a `catch` handler (an exception thrown before or after a suspension
  reaches the right handler; `finally` runs exactly once on every exit), a `match` arm, a
  `switch` subject, a `for-in` iterable, a range bound, a compound assignment (its target
  evaluated once, before the await), a condition, a call argument and any larger
  expression (operands with side effects keep their order; `&&`, `||` and `?:` evaluate
  an awaiting operand only when it runs). This replaces the located errors added earlier in this cycle
  in this release. A future dropped while suspended now runs the `defer`s and `finally`
  blocks pending at its await once. An `await` in a `finally` is a located error. Tests
  `async_try`, `async_try_cancel`, `async_await_positions`, `async_await_edges`,
  `async_generic_try`, `errors/await_in_finally`, `run_cmd/await_in_defer`, `run_cmd/await_in_generic_match`.
- `<regex>` reads patterns and texts as UTF-8, with the semantics of Go's regexp (RE2):
  `.`, classes and literals match whole code points (match offsets stay byte offsets, and
  a byte that is not valid UTF-8 reads as U+FFFD), `\x{..}` goes up to `\x{10FFFF}`, and
  it gains `\p{..}` / `\P{..}` (the general categories, their one-letter groups, the
  scripts and `Any`, Unicode 15 tables generated from Go's by
  `tools/gen_regex_unicode.go` into `<regex_unicode>`), the flags `(?i)` (simple case
  folding), `(?m)`, `(?s)` and `(?U)` for the rest of a group or scoped as
  `(?flags:...)`, non-capturing groups `(?:...)`, named groups `(?P<name>...)` and
  `(?<name>...)`, and literal text `\Q...\E`. A differential against Go's regexp over
  60 000 generated patterns and texts agrees on every result. Test `regex_unicode`; the
  stdlib fuzzer's regex dictionary covers the new syntax.
- `<eventloop>` on AArch64 Linux: `struct epoll_event` was declared packed as on x86-64,
  so each event read the wrong descriptor and the async servers spun at full CPU (the
  `http2_*` tests hung). It is packed only on x86-64 now.
- `tests/linux_docker.sh` runs a Linux smoke of a release from a Mac: it cross-compiles
  the run and smoke tests and the self-hosted drivers for AArch64 (or x86-64) Linux,
  links and runs them in `ubuntu:24.04` with gcc, runs the self-hosted type checker and
  code generator over the corpus, and checks the bootstrap fixpoint on Linux. A
  pre-release check, not a CI gate.
- A native build targets the `generic` CPU unless `--mcpu` is given, as clang does. It used the host CPU's name without its feature list, which could select instructions a VM or a masked host does not support (a SIGILL), and made the binary depend on the build machine.

### Known issues
These are open in 0.9.2 and planned for 0.9.3. None of them miscompiles a valid program
without a diagnostic, except where the entry says so.

- The C++ compiler does not treat a type alias inside a composite type as its target, so
  `Box<F>` and `Box<int>` (with `type F = int`) are different types there and a valid
  assignment between them is rejected. Spell the target type.
- A generic variant infers its type arguments only from its payload
  (`Opt<int64> a = Some(5)` needs `Some<int64>(5)`); write the type arguments.
- A `#undef` line inside a multi-line string literal is read as a directive.
- An `await` inside a `finally` or a `defer` is rejected (both run, without suspending,
  when a cancelled future is dropped), and so is one in a generic async function's
  `match` arm that binds a payload, after a side-effecting operand or inside a `?:` arm;
  bind the value to a local first.
- `await` is rejected inside `try`, a `match` arm, a `switch` subject, a range bound and a
  compound assignment; bind the awaited value to a local first.
- There is no spelling for a pointer to a nullable pointer, and `null` does not convert
  to an interface value.
- A method that mutates a captured value inside a lambda acts on the closure's copy.
- The `<json>` methods named `int` and `bool` cannot be called with dot syntax.
- The C ABI lowering of `extern` struct and union arguments is not implemented for 32-bit
  x86 (not a supported C ABI target).
- On AArch64 Linux a variadic function written in Eskiu reads its arguments wrong: both
  compilers lower `va_arg<T>` to LLVM's `va_arg` instruction, which the AArch64 backend
  expands for the Darwin `char*` `va_list`, not the AAPCS64 one Linux uses (a miscompile;
  `variadic` and `generic_variadic` fail there). macOS, x86-64 and calls to C variadic
  functions such as `printf` are not affected.
- Regex does not support `\Q..\E`, `(?:...)`, `(?i)`, `\p{...}` or code points above one
  byte.
- A few diagnostics report a different column in the two compilers.
- The type checker does not fold `sizeof` of a struct under `#pragma pack(N)` with N of 2
  or more, or of one with a `: 0` bitfield, so constant-expression checks do not see it.
- An enum member value cannot use `?:`, `&&`/`||`, or a `const` whose initializer is a
  ternary, although global `const` initializers accept them.

## [0.9.1] - 2026-09-09
### Fixed
A correctness campaign (a multi-front bug hunt) closed a set of latent miscompiles and
type-rule gaps. All are fixed lockstep in the C++ and self-hosted compilers unless noted.

- **Global and `static`-local constant initializers.** These were silently mis-folded in
  several forms: a 64-bit literal was truncated to 32 bits (C++), a `struct` global came
  out zeroed, and a `const`/`enum`/`sizeof`/`~`/`!` value folded to `0`; a `string` global
  came out `null` and a hex literal produced invalid IR (self-host). The constant folder
  now handles all of these. `static` locals also accept any of those constant forms (they
  previously required a bare literal), and an over-full global array literal is rejected.
- **Signed/unsigned integer semantics now follow C.** `float`→unsigned casts use `fptoui`
  (a value above the signed max no longer saturates); a right shift's kind follows the
  value shifted, not the count's signedness; and a mixed-rank signed/unsigned op is
  unsigned only when the unsigned operand's rank is at least the signed one's.
- **`switch` `break` no longer runs enclosing `defer`s twice** (it unwound to the wrong
  cleanup depth), and a **bitfield write through a `*Struct`** now reaches the pointee
  instead of the pointer's own slot (both C++).
- **Scientific-notation float literals** (`3.4e38`, `1e6`, `2.5e-3`) now lex and compile.
- **`--safe` slice construction** bounds-checks `0 <= lo <= hi <= len`: a valid empty
  end-slice (`s[len..len]`) no longer traps, and an out-of-range upper bound is caught.
- **Async (self-host):** the awaited value's type is taken from the `let` variable, so
  awaiting a future from a method call, a variable, or a template call resolves correctly
  (was read through a wrong `int` overlay); and an `await` the transform can't place (in a
  condition or a larger expression) is a clean compile error instead of a crash or silent
  miscompile.

## [0.9.0] - 2026-09-08
### Added
- **Labeled `break` and `continue`.** A loop can be named with a leading label
  (`outer: for (...) { ... }`), and `break outer` / `continue outer` act on that loop
  from inside a nested one, instead of only the innermost. The label must name an
  enclosing `while`, `do`/`while`, `for`, or `for ... in` loop, otherwise the program is
  rejected at compile time. A labeled jump runs the same `defer`/`errdefer` cleanups as
  the unlabeled forms (every deferred statement between the jump and the target loop runs,
  innermost first), may not escape a `defer` body, and is not supported inside an
  `async fn`. Implemented lockstep in both the C++ and the self-hosted compiler.

### Fixed
- **Global array initializers are no longer dropped.** A global (or `static` local) array
  literal such as `int[3] G = {10, 20, 30};` was silently zero-filled: only scalar globals
  kept their value, and an array came out all zeros with no warning. The constant folder
  now builds the array constant from the initializer, with C-style zero-fill for a partial
  list (`int[3] = {7}` gives `{7, 0, 0}`), nested arrays, and numeric **casts** inside the
  initializer (`float[3] = {(float)0.485, …}`, which also emit the correct LLVM f32
  constant form). Fixed in both compilers. Locals were unaffected.

## [0.8.0] - 2026-08-21
### Fixed
- **The release binaries are self-contained.** `eskiuc` picked up z3 and zstd as dynamic
  libraries from the build machine (versioned `/opt/homebrew/...` paths on macOS; `libz3.so`,
  absent on a stock Linux, on Linux), so the shipped tarballs failed to load elsewhere. Both
  platforms now bundle those libs into `lib/deps` and reference them relatively (macOS via
  `@loader_path`, Linux via an `$ORIGIN` RUNPATH), with a build guard that fails if the
  rewrite did not take. (The compiler still shells out to `clang` to link native output, so a
  C toolchain is a runtime requirement, as before.)
- **The async stack runs on Windows.** `<eventloop>` gained a `WSAPoll` reactor (a user-space
  pollfd set rebuilt each wait, since `WSAPoll` keeps no kernel state) alongside kqueue/epoll,
  and `<net_async>` gained a Winsock backend (`ioctlsocket(FIONBIO)` for non-blocking mode,
  `recv`/`send`, `WSAGetLastError` with `WSAEWOULDBLOCK`) beside the `fcntl`/`read`/`write`
  path. A Windows `SOCKET` is not a small sequential fd (handle values in the hundreds are
  normal), so the loop's fd-indexed slot table now grows to fit the largest handle instead of
  silently dropping registrations past a fixed `max_fds`. Validated end to end on a native
  Windows runner: a non-blocking socket round-trip completes over the reactor. POSIX behavior
  is unchanged (small dense fds never trigger a grow).
- **Windows platform shims for the OS-level stdlib.** `<sysheap>` now carves pages with
  `VirtualAlloc`/`VirtualFree` on Windows (it has no `mmap`), and `<time>` uses the Win32
  clocks (`GetTickCount64` for the monotonic clock, `GetSystemTimeAsFileTime` for wall time,
  `Sleep` for `sleep_ms`) instead of `clock_gettime`/`nanosleep` (whose `timespec.tv_nsec` is
  a 32-bit `long` on LLP64 Windows and would not match). `<threading>` already links against
  winpthreads. Validated end to end on a native Windows runner (`sysheap` + a `Mutex` + the
  monotonic clock in one program). Linux/macOS behavior is unchanged.
- **Exceptions work on Windows (mingw).** `try`/`throw`/`catch` previously emitted the
  Itanium/DWARF EH personality (`__gxx_personality_v0`) unconditionally, so a Windows object
  failed to link. The personality is now selected by target: mingw x86-64 uses the SEH
  personality (`__gxx_personality_seh0`); Linux/macOS keep `__gxx_personality_v0`. A native
  build with no `--target` follows its host. The rest of the EH lowering (landingpad IR, the
  `__cxa_*` runtime, `_ZTIPv`) is unchanged. Validated end to end on a native Windows runner
  (`windows.yml`): a `try`/`catch` program compiles, links with the mingw C++ runtime, and
  catches the throw.

### Added
- **`match` on classic (payload-less) enums.** A plain `enum Dir { N, E, S, W }` can now be
  `match`ed with exhaustiveness checking, the same guarantee algebraic enums already had:
  every variant must be covered (or a `_` default), and adding a variant turns every
  unhandled `match` into a compile error. It lowers to a switch on the enum's int value
  (cases respect explicit `= N`). Previously `match` was ADT-only and a plain enum could only
  be dispatched with a non-exhaustive `if`/`switch`. Landed lockstep in both compilers. Test:
  `tests/enum_match.esk`.
- **Operator overloading.** A struct can define `V3 operator +(V3 a, V3 b) { ... }` (and the
  same for `- * / % == != < > <= >= & | ^ << >>`, unary `- ! ~`, and subscript `[]`), so
  `a + b` reads as algebra instead of `v3_add(a, b)`. Resolution is fully static: an operator
  compiles to a normal function under a canonical mangled name, and `a op b` on non-built-in
  operands resolves to it by operand types at compile time (zero runtime cost, no vtable), so
  overloads coexist by operand type (`V3 * V3` and `V3 * double`). Compound assignment
  (`v += w`) desugars to the overloaded binary op. Structural: a type gets `+` simply by
  declaring `operator +`, no `impl` block. Landed lockstep in both compilers, including the
  numeric-argument coercion at the call site (`V3 * 2.0` finds `operator *(V3, float)`).
  Test: `tests/operators.esk`.
- **Self-host mirrors of the two safety features.** The Eskiu-written compiler now
  implements the two features that had shipped C++-only, reaching full feature parity:
  - `--safe` runtime bounds checks (self-host `codegen.esk` emits the same slice/array
    index trap-on-out-of-range as the C++ back-end; off by default, opt-in via `--safe`).
  - the `?*T` checked nullable pointer (self-host `parser.esk`/`sema.esk` reject an
    unchecked deref, narrow through `if (x != null)`, allow `*T` -> `?*T` widening and
    reject `?*T` -> `*T` without a check; `codegen.esk` lowers `?*T` exactly like `*T`).
  Each is gated in CI (`tests/selfhost/safe_parity.sh`, `tests/selfhost/nullable_parity.sh`).

### Changed
- **`alloc<T>` now zero-initializes.** `<mem>`'s `alloc<T>(n)` returns memory that is
  zeroed (hosted mode calls `calloc` instead of `malloc`; the `--freestanding` `esk_alloc`
  contract is now "return zeroed memory", honored by `kernel/alloc.esk`). A freshly
  allocated `*T` is null, an `int` is `0`, a `List` is a valid empty list, so reading a
  field before assigning it is defined rather than a garbage read. This matches C++'s
  `new T()` and removes an uninitialized-read crash class that only surfaced on platforms
  whose raw heap is not already zero (Linux), while working on those whose is (macOS).

## [0.7.0]
### Added
- **`extern` variables.** `extern <type> <name>;` declares a global defined in another
  translation unit (a C global), emitting an external-linkage declaration with no
  initializer. Reads and writes resolve at link time, so Eskiu can share state with C
  libraries (e.g. a libctru global). Previously `extern` accepted only function
  prototypes. Landed in lockstep across both compilers.
- **Cross-compile to Windows (x86-64).** `--target x86_64-pc-windows-gnu` emits a COFF
  object with the Microsoft x64 calling convention (LLVM lowers it from the triple), and
  the preprocessor predefines `_WIN32` (plus `_WIN64` on a 64-bit arch). Link it with
  `lld-link` and an import library generated by `llvm-dlltool` (both ship with LLVM, so no
  Windows SDK is needed to link); the resulting `.exe` runs on Windows or under wine.
- **32-bit ARM backend, cross-compile to the Nintendo 3DS.** The ARM target is now
  registered alongside AArch64 and x86, and three new flags drive it: `--mcpu` (e.g.
  `mpcore` for the 3DS's ARM11), `--mattr` (LLVM feature string, e.g. `+vfp2`), and
  `--reloc` (`static` for the `.3dsx` loader, which applies static relocations and has no
  dynamic loader for a GOT). A hard-float ARM triple (ending in `hf`, e.g.
  `armv6k-none-eabihf`) selects the hard-float ABI, so the emitted object carries the
  `Tag_ABI_VFP_args` build attribute and links against hard-float libraries like libctru.
  An eskiuc object built this way links cleanly into a `.3dsx` homebrew via the devkitARM
  toolchain.

### Fixed
- **Float arguments narrow at the call site.** A `double` literal (Eskiu's default float
  literal type) passed to a `float` parameter was left as `double`, so the argument type
  disagreed with the callee's signature and the reference compiler rejected the call at
  LLVM verification. Direct and method calls now run every argument through the shared
  numeric-coercion path, matching assignment and return (the self-hosted compiler already
  did). This unblocks calling C float APIs, common in 3DS graphics code.
- **Bare-metal targets no longer inherit the host platform macro.** An explicit
  non-hosted triple (OS `none`, e.g. the 3DS's `armv6k-none-eabihf`) now predefines
  neither `__APPLE__` nor `__linux__`; previously it fell through to the build host's
  macro. Native builds and explicit `linux`/`apple` triples are unchanged.
- **Hard-float ABI reached the object emitter.** The `--target …hf` hard-float ABI was
  applied when building the module and running the optimizer but not in the object-emitting
  path, so the written object dropped `Tag_ABI_VFP_args` and a hard-float linker rejected
  it. The three code-emitting paths now share one `TargetMachine` builder, which also
  aligns the x86 backend-init guards (the static Apple release links without x86).

---

## [0.6.2]
### Added
- **Cross-compilation targets the right platform.** The `--target` triple now drives the
  predefined platform macro (`__APPLE__` / `__linux__`), so `eskiuc --target x86_64-linux-gnu`
  on macOS selects the Linux stdlib paths (epoll, Linux `sockaddr_in`) instead of emitting
  unresolved BSD symbols. With no `--target` it still follows the build host.

### Fixed
- **Event-loop timer callbacks are initialized.** `el_new` allocated the timer array but
  only zeroed each timer's `active` flag, leaving the `on_fire` closure as heap garbage
  (`alloc` does not zero). Firing is guarded by `active`, but an uninitialized function
  pointer is exactly the shape of the intermittent Linux CI `SIGILL` in the HTTP/2 tests
  (a garbage closure invoked on a fresh, non-zero heap). It now initializes every field,
  completing the equivalent `on_read` fix from v0.3.1, so the event loop holds no
  uninitialized function pointer.
- **The async transform preserves `escaping` parameters.** Lowering an `async fn` to its
  coroutine constructor dropped the per-parameter `escaping` flags. Because the ctor stores
  each parameter into the heap coroutine frame (which outlives the call), an escaping
  closure argument could then be stack-allocated at the call site, leaving the frame with a
  dangling env. The lowered constructor now carries the original flags, and the post-lowering
  soundness check no longer spuriously flags the frame-stored callback.
- **Interface (vtable) dispatch now coerces arguments.** Calling an interface method
  with an argument that needs widening (e.g. an `int32` where the method declares
  `int64`) emitted a call whose argument type did not match the vtable slot's signature.
  The reference compiler rejected it at LLVM verification (`code generation failed`); the
  self-hosted compiler emitted mistyped IR. Both now widen the argument to the method's
  declared parameter type, exactly like a direct call. Fixed in lockstep across both
  code generators.

---

## [0.6.1]
### Added
- **`\xNN` hex escape** in string and character literals: one or two hex digits decode
  to a raw byte, so byte-precise strings are possible (e.g. `"\xC3\x91"` is `Ñ` in UTF-8,
  `'\x41'` is `'A'`). `\x` with no following hex digit falls back to a literal `x`. In
  lockstep across both lexers.
- **Slices from a raw pointer.** `ptr[lo..hi]` on a `*T` now yields a `T[]` slice over
  the pointer's memory, so heap buffers (`alloc<T>(n)`) can become slices, not just
  fixed arrays. Write-through the slice aliases the backing buffer, and `.len` /
  indexing / `for-in` work as on any slice.

### Fixed
- **Escape sequences in string and character literals.** `\0` now decodes to a NUL
  byte instead of the character `'0'` (a silent trap), character literals accept `\r`
  (and `\f`, `\v`), and string and character literals share one escape set
  (`\n \t \r \f \v \0 \\ \" \' \xNN`); an unrecognized escape still yields the character
  itself. Fixed in lockstep across the C++ and self-hosted lexers.
- **Compiler crash slicing a raw pointer.** `ptr[lo..hi]` on a pointer base crashed
  code generation (a null element type in the slice-construction path); it now builds
  the slice correctly (see the slice-from-pointer support above).
- **Global lambda crashed when called.** A non-capturing lambda assigned to a global
  (`let f: fn(int)->int = int(int x){ return x*2; };`) compiled to a null closure, so
  calling it (`f(6)`) segfaulted; it now folds to a constant closure `{ @lambda, null }`
  and works, matching a lambda written inside a function. Fixed in lockstep across the
  C++ and self-hosted code generators (the self-host also gained direct calls through a
  global closure variable).

### Changed
- Documentation accuracy pass (from an internal audit): corrected AST node counts and
  added the `defer` cleanup-stack to the internals docs, regenerated the real
  `--test-lexer` / `--test-parser` sample output, fixed inline-asm operand syntax
  (`$0`, not `%0`) and noted that output operands are unsupported, added an `intrinsic`
  spec section and a `--safe` flag row, and corrected glossary token-type names.

## [0.6.0]
### Added
- **`<sort>` stdlib module.** Generic in-place `sort<T>(a, n, cmp)` (heapsort, a
  guaranteed O(n log n) worst case) and `bsearch<T>(a, n, key, cmp)` over a `*T` array,
  driven by a `cmp(&x, &y)` function in the C qsort/bsearch convention. Works directly on
  a `List<T>`'s backing data.
- **`<url>` stdlib module.** RFC 3986 percent-encoding (`url_encode` / `url_decode`) and
  form-query lookup (`url_query_get`, decoding `+` as space), all writing into a caller's
  `String`.
- **`<uuid>` stdlib module.** RFC 4122 version-4 UUIDs (`uuid_v4`), formatted in the
  canonical 8-4-4-4-12 hex form. Built on `<random>`, so it is not cryptographically secure.
- **UTC civil calendar in `<time>`.** `DateTime` plus `time_to_utc` / `time_from_utc`
  (exact days<->civil conversion over the whole int64 range) and `time_format_iso` for
  ISO 8601 `YYYY-MM-DDTHH:MM:SSZ` output. `<time>` now imports `<string>` for the
  formatting helpers.
- **`<random>` stdlib module.** A seedable pseudo-random generator (xoshiro256\*\*)
  with no global mutable state: `rng_seed`, `rng_next` (raw 64-bit), `rng_below` and
  `rng_range` (unbiased bounded integers), `rng_double` (`[0.0, 1.0)`), `rng_bool`, and
  `rng_fill`. A given seed always reproduces the same stream. Not cryptographically secure.
- **`<regex>` stdlib module.** A regular-expression engine built as a Thompson NFA run
  as a Pike VM, so matching is linear in the input with no catastrophic backtracking.
  Supports literals, `.`, classes `[a-z]` / `[^...]`, the shorthands `\d \w \s` (and
  `\D \W \S`), quantifiers `* + ? {m} {m,} {m,n}` (greedy or, with a trailing `?`, lazy),
  alternation `|`, capturing groups `( )`, and the anchors `^ $`. API: `regex_match` for a
  quick yes/no, or `regex_compile` + `regex_search` + `match_group` to extract captures.
- **`defer` statement.** `defer stmt;` runs `stmt` (a statement or block) when the
  enclosing block is left, in LIFO order, on every path out: fall-through, `return`,
  `break`, `continue`, and `?`-propagation. Block-scoped, so a `defer` in a loop body
  runs each iteration. It is the ergonomic way to pair an acquisition with its release
  (`*uint8 b = alloc<uint8>(n); defer free(b);`) without leaking on early exits. A defer
  body may not `return` or `break`/`continue` out of itself.
- **`errdefer` statement.** Like `defer`, but runs only on the **error exit** path (when
  the function leaves via `?`-propagation), not on a normal `return` or fall-through. For
  undoing partial work when a fallible step fails while keeping it on success
  (`Conn c = open()?; errdefer close(c); handshake(c)?;`).
- **Slice type `T[]`.** A fat pointer (data + length) that views a contiguous run of `T`.
  Construct by slicing a fixed array with a half-open range (`a[lo..hi]`); it aliases the
  backing array. Supports `s[i]` (read/write), `s.len` (`int64`), passing by value, and
  `for (x in s)`. Because the length travels with the slice, a function taking `T[]` needs
  no separate count argument. Lowers to `{ ptr, i64 }`.
- **Checked nullable pointers (`?*T`).** Opt-in null safety: a `?*T` cannot be
  dereferenced, indexed, or member-accessed until it is proven non-null, and
  `if (x != null) { ... }` narrows it to non-null in that branch. A `*T` widens to `?*T`;
  the reverse needs a check. `*T` stays nullable (C-faithful), so nothing existing
  changes. Same representation as `*T`, checked entirely at compile time. (Self-host
  mirror is on the promotion track.)
- **`must_use` function qualifier.** Prefixing a function with `must_use` makes
  discarding its result a compile error, catching leaked allocations and dropped return
  values. The stdlib's `alloc` is now `must_use`, so a bare `alloc<T>(n);` is rejected.
- **`--safe` mode.** A new compiler flag that inserts runtime safety checks: slice and
  fixed-array indexing is bounds-checked, and an out-of-range index traps (aborts via
  `@llvm.trap`) instead of reading or writing past the end. Off by default, so release
  builds are unaffected. (Self-host mirror of `--safe` is deferred to the promotion
  track; the shipped C++ compiler carries it.)

### Fixed
- **`finally` was skipped on an early `return`/`break`/`continue` from inside a `try`
  body.** The cleanup only ran on fall-through and exception unwind; an early exit jumped
  straight out, leaking whatever the `finally` was meant to release. It now runs on those
  paths too (the same scope-exit cleanup mechanism that powers `defer`).

---

## [0.5.0]
Fills a set of basic C constructs the language was missing, so idiomatic C ports
compile without workarounds. Each lands in lockstep across the C++ and self-hosted
compilers and follows C semantics.

### Added
- **`do`/`while` loops.** `do { ... } while (cond);` runs the body once before testing
  the condition, as in C.
- **Increment and decrement operators.** Prefix and postfix `++`/`--` on integer and
  pointer lvalues; postfix yields the old value, prefix the new, and a pointer steps by
  one element.
- **Array-literal initializers.** `int[N] a = { e0, e1, ... };` initializes an array in
  place; a short list zero-fills the remaining elements, and an over-long list is
  rejected.
- **`static` local variables.** A `static` local has a single instance that persists
  across calls (C storage semantics). Its initializer must be a compile-time constant;
  `static` on a global is rejected.
- **Multidimensional arrays.** `T[N][M]` is N arrays of M in C order (the leftmost
  bracket is the outer dimension). Indexing peels one dimension at a time and each index
  is bounds-checked against its own dimension. Nested brace initializers
  (`int[2][3] a = { {1,2,3}, {4,5,6} }`) zero-fill at every level.
- **Ternary conditional `cond ? a : b`.** Evaluates exactly one arm; the arms take a
  common type (two numerics promote C-style). Right-associative, so `a ? b : c ? d : e`
  chains. It coexists with the postfix `?` Result-propagation operator: a `?` with a
  matching same-level `:` ahead is a ternary, otherwise propagation (parenthesize to
  propagate inside a ternary arm).

### Fixed
- **`switch` on a sub-`int` subject.** A `switch` over a `char` (or other narrow integer)
  with wider case constants failed LLVM verification; the subject and case constants are
  now widened to a common type before lowering.

---

## [0.4.0]
A correctness and type-strictness release. A four-front bug hunt (behavioral
differential, sema soundness, synthesized-default audit, feature edges) across the C++
and self-hosted compilers found a batch of miscompiles, crashes, and soundness holes;
this release fixes them and tightens the type system. The shipped (C++) compiler carries
every fix below; the self-hosted compiler's matching sema checks are tracked for the
promotion track.

### Fixed
- **A catch-less `try`/`finally` aborted when an exception passed through it.** With an
  in-flight exception, the `finally` block was skipped and the runtime aborted
  (`std::terminate`) instead of running the cleanup and propagating the exception. The
  exceptional path now runs `finally` and re-raises via `__cxa_rethrow` (as an invoke
  when an enclosing `try` can catch it), so `try { throw } finally { cleanup }` inside a
  caller's `try`/`catch` runs the cleanup and is caught.
- **A lambda nested inside another lambda miscompiled when it captured a variable two
  scopes up.** The capture was recorded only on the innermost lambda, so the enclosing
  lambda did not thread it through its environment and codegen referenced a value from
  another function (`Referring to an instruction in another function`). Captures are now
  propagated to every enclosing lambda they are outer to.
- **Self-hosted back-end: several codegen bugs on edges the corpus never exercised.**
  Logical-not (`!`) and bitwise-not (`~`) were emitted as no-ops (returning the operand
  unchanged), silently inverting control flow; hexadecimal and octal integer literals
  were passed through as raw source text (invalid LLVM IR for hex, wrong value for octal);
  an integer literal above 2^63 overflowed the width scan and was emitted as a truncated
  i32; and an array sized by a named `const` (`int[CAP]`) copied the name into the LLVM
  array type instead of the value. All now match the C++ back-end. (The C++ compiler was
  already correct on these.)
- **Self-hosted back-end: an async `for-in` over a generic `List<T>` (T != int)
  miscompiled.** The loop-desugar defaulted the element type to `int` when the iterable
  was a generic container, so `for (v in xs)` over a `List<double>`/`List<int64>`
  truncated each element (and emitted invalid IR for `List<Struct>`). The desugar now
  resolves the element type by substituting the container's type argument, matching the
  C++ back-end. (Residual R1 in `PROMOTION_PLAN.md`.)
- **Self-hosted back-end: exceptions were mishandled.** A catch-less `try`/`finally`
  swallowed an in-flight exception (running cleanup but then continuing as if caught), and
  a `throw` from inside a catch handler that had to cross a function boundary was lost
  (the runtime terminated). The self-host now runs `finally` and re-raises via
  `__cxa_rethrow` on the exceptional path, and names a rethrown value's type from its LLVM
  type when it cannot be resolved structurally, matching the C++ back-end.
- **Comparison operators did not type-check their operands.** `==`, `!=`, `<`, `>`,
  `<=`, `>=` were accepted for any pair of types (pointer vs int, struct vs struct,
  string vs int), so the checker reported success and codegen emitted a malformed
  comparison (an assertion under an assertions build, a miscompile otherwise). They
  now require mutually comparable operands: both numeric, both pointer-like (including
  `null`), or the same non-aggregate type. Enum, pointer, and `null` comparisons are
  unaffected.
- **Incompatible non-numeric variable initializers were only a warning.** `int x =
  f();` where `f` returns `void` let a non-value into codegen and hung the back-end;
  `int x = "hello";` crashed at run time. Both are now compile errors, matching the
  assignment path.
- **Taking the address of a `const` produced a mutable pointer.** `const int N; *int
  p = &N; *p = 99;` silently mutated `N`. `&` of a const location now yields a pointer
  to const, so writing through it (or assigning it to a plain pointer) is rejected.
- **`float` compared against `double` or `int` failed to compile.** The comparison
  operators did not promote a mixed float/int (or float/double) pair to a common
  float type, so LLVM rejected the `fcmp`. They now promote like the arithmetic
  operators do.
- **Integer literals in [2^31, 2^32) were truncated to i32 and sign-extended.** A
  decimal literal such as `3000000000` assigned to an `int64` materialized as a
  32-bit constant (high bit set), then sign-extended to a negative value
  (`-1294967296`). The codegen now widens any literal that does not fit a *signed*
  i32 to i64 (matching the self-host), so large literals keep their value.
- **`unsigned -> float` conversions used signed `SIToFP`.** A high-bit-set unsigned
  value (for example `uint32 4000000000`) converted to a negative float. Integer to
  float conversion now selects `UIToFP` vs `SIToFP` by the source's signedness, in
  both back-ends (routed through a single `intToFloat` helper on the C++ side).
- **A generic (template) function could fall off the end without returning.** The
  missing-return analysis skipped template bodies, so a generic like
  `T pick<T>(T a, int c) { if (c > 0) { return a; } }` compiled (returning a synthesized
  zero under the C++ back-end, crashing under the self-host). The definite-return check
  now runs on template bodies too.

### Changed
- **Assigning a floating-point value to an integer needs an explicit cast.** `int x =
  3.9;` (or any `float`/`double` into an integer) is rejected, because it silently drops
  the fraction. Write the cast to keep it: `int x = (int)3.9;`. Integer-width narrowing
  (`int n = strlen(s);`, `int64` into `int`, `int` into `uint8`) and float-width
  narrowing (`float f = aDouble;`) stay implicit, matching C. This is the one numeric
  conversion C itself flags under `-Wall`.
- **An integer literal that does not fit its target type is a compile error.** `int8 x
  = 300;` (out of `int8`'s range) is rejected rather than silently wrapping to 44.
- **Division or remainder by a literal zero is a compile error.** `x / 0` and `x % 0`
  are rejected at compile time instead of trapping at run time.
- **A constant array index proven out of bounds is a compile error.** `int[3] a; a[5]`
  (or a negative constant index) is rejected.
- **Reading an uninitialized scalar local is a compile error.** `int x; return x;` (and
  calling a fn-pointer that was never assigned) is rejected by a conservative
  definite-assignment check over the function's straight-line prefix; taking `&x` or
  assigning `x` first clears it, and branchy code is never a false positive.
- **Returning the address of a local is a compile error.** `*int f() { int x; return &x; }`
  is a dangling pointer and is rejected (`&(*ptr)`/`&ptrParam.field`, which point into
  caller memory, are still fine).
- **Redefining a function is a compile error.** Two definitions of the same name are
  rejected (a body-less forward declaration alongside one definition is still allowed).
- **`main` must return `int`.** Its return value is the process exit code, so `void
  main()` (which left the exit code as an undefined, platform-dependent register value) is
  rejected; write `int main() { ... }`.
- **Falling off the end of a non-void function is now a compile error.** A non-void
  function that returned on no path still compiled before: the C++ back-end
  synthesized an implicit `ret 0`/`ret null`, so `int add(int a, int b) { int r = a
  + b; }` silently returned 0 rather than the computed value, and the last
  expression was never the result. The self-hosted back-end emitted `unreachable`
  for the same code, so the two compilers disagreed and the self-host build crashed
  (SIGILL) on a program the C++ build ran cleanly. Both now reject it in sema with
  `missing return in non-void function`, via a definite-return analysis. `void`
  functions may still fall off the end, `async` functions complete their future
  implicitly, and a body that provably cannot fall through needs no trailing
  `return` (an `if`/`else` where both branches return, an exhaustive `switch`/`match`
  where every arm returns, or an infinite `while (1)` with no `break`).

---

## [0.3.1]
### Fixed
- **`*T[N]` now parses as an array of pointers, not a pointer to an array.** The
  type-string parser (`ty::Type::parse`) peeled a leading `*` before the trailing
  `[N]`, so `*Node[7]` became a *pointer to* `Node[7]` and lowered to a single
  opaque `ptr`, while codegen's `IndexExpr` lowering assumed the array reading.
  The two disagreed: indexing such a value emitted an invalid two-index GEP into a
  scalar pointer, silently corrupting locals and crashing the compiler outright on
  a module-level array (a constant-folded GEP tripped an LLVM assertion). The
  trailing `[N]` now binds outermost, so `*Node[7]` is an array of 7 pointers
  consistently across the type checker and codegen. (This entry used to say a
  pointer to an array is spelled `Node[7]*`; that form does not parse, so point at the
  first element with a `*Node` instead.) Found porting a C program whose
  central data structure was a module-level `Actividad *agenda[7]`.
- **`<eventloop>`: initialize the `on_read` closure of every fd slot.** `el_new`
  zeroed `active`/`gen`/`isWrite` but left the `on_read` fat pointer as `alloc`
  garbage. Dispatch is guarded by `active`, so this was latent, but any stray read
  would invoke a garbage function pointer, a SIGILL on Linux/x86-64 (fresh
  allocations aren't zero there as they happen to be on macOS). Now defaulted to a
  no-op. Defensive hardening for the intermittent HTTP/2-test SIGILL under
  investigation (see the `project-flaky-http2` note).

- **A lambda's return type is reconciled with its target `fn(...)->R` type.** Assigning
  a lambda whose header return type disagrees with the declared closure type (e.g.
  `let f: fn(int)->float = int(int x) { return (float)x * k; }`) emitted a function
  returning the *header* type (`int`): an `fptosi` truncation plus an int/float return-
  register mismatch against the closure's call ABI. It was correct at `-O0` by luck but a
  silent `0.0` miscompile under `-O2`. Sema now sets the lambda's return type to the
  target's `R` so its `return` coerces through the normal path. Surfaced by an `-O0`-vs-`-O2`
  differential over the whole test corpus (`closures.esk` was the only divergence).
- **Incompatible function-type assignments are now rejected.** Assigning a function
  value to a `fn(...)` slot with a different signature (e.g. an `int`-returning function
  to a `fn(int)->float`) was silently accepted: a fn value has a fixed call ABI (param
  and return registers), so there is no implicit adapter, and reinterpreting it
  miscompiled (wrong even at `-O0`, `0.0` under `-O2`). `isValidAssignment` now requires
  fn types to match exactly, and a mismatched fn initializer is a hard error rather than a
  warning. Lambda literals in a `let x: fn(...)->R = ...` still coerce (their return type
  is malleable); only non-adaptable fn *values* are rejected. Test `errors/fn_return_mismatch`.

### Changed
- **New CI gate: `-O0`-vs-`-O2` behavioral differential** (`tests/opt_differential.sh`).
  Compiles the whole corpus at both levels and fails on any exit/stdout divergence,
  guarding against optimization-path miscompiles now that `-O` exists (it caught the
  float-closure bug above). The intermittent HTTP/2 async tests are excluded to keep the
  gate deterministic.
- **Self-hosted parser parity now covers the full corpus (51 → 121).** `parse_main`
  preprocesses the top-level file (matching how the C++ `--test-parser` folds
  preprocessing into the lexer), so `parse_parity.sh --full` no longer excludes
  files whose import closure touches the preprocessor (`#ifdef`/`#define`/`__FILE__`/
  `__LINE__`/shebang). A prerequisite for promoting the Eskiu-written compiler
  (see `selfhost/PROMOTION_PLAN.md`, R2).

---

## [0.3.0]
The self-hosting milestone: the whole compiler (lexer, preprocessor, parser, semantic
analyzer, and code generator) is reimplemented in Eskiu (`selfhost/`), reaches a 3-stage
bootstrap fixpoint, and the self-hosted codegen is **feature-complete against the C++
corpus** (a full feature sweep is clean). All parity/self-host/bootstrap gates are CI-wired.

### Fixed
- **`&&` and `||` now short-circuit.** Code generation evaluated both operands eagerly
  and emitted a logical-and/or, so the right-hand side always ran; a guarded
  dereference like `p != null && p.field` could fault. They now lower to a conditional
  branch + PHI, evaluating the RHS only when the LHS doesn't decide the result. Found
  dogfooding the self-hosted type checker. Regression test `tests/short_circuit.esk`.
- **`--test-parser` no longer crashes on a forward-declared function.** The AST
  printer dereferenced a null `FunctionDecl::body` (a prototype like `int f(int);`),
  segfaulting `eskiuc --test-parser`. It now omits the `Body:` section for a
  body-less function (as `ReturnStmt` already does for a null value). Found
  dogfooding the self-hosted parser. Affected only the debug printer, not codegen.
- **C `size_t` externs now use `int64`, not `int`.** The `<mem>`/`<string>` declarations
  for `memcpy`/`memset`/`memmove`/`memcmp`/`memchr` (size argument) and `strlen` (return)
  were `int` (i32). That is an incorrect ABI on LP64 targets and truncates sizes above
  4 GB. They are now `int64`, matching `size_t`. Behavior is unchanged for the common
  sub-4 GB case (the value zero-extends into the argument register either way).

### Added
- **`-O` optimization levels.** `eskiuc -O1`/`-O2`/`-O3` now runs the LLVM middle-end
  pipeline (mem2reg/SROA/instcombine/inlining/GVN/...) over the module before code
  generation. `-O0` (the default) keeps the prior behavior: naive IR straight to the
  backend. On real code this collapses the per-local stack traffic the front-end emits
  (the `ine_decoder` demo drops from 514 allocas to 25 at `-O2`); its Makefile now builds
  with `-O2`.
- **Clearer diagnostic for a keyword used as a name.** Using a reserved word (`fn`, `in`,
  `match`, a type name, ...) as a variable, parameter, or field name now reports
  `expected a name, found keyword 'fn'` at the cause, instead of a misleading downstream
  error (`Expected ';'`, `Expected expression`). This was the most recurring self-host
  papercut. The self-hosted parser mirror is tracked in `selfhost/PROMOTION_PLAN.md` (R3).
- **`String_free` clears `data`.** It sets `self.data = null` after `free`, so a reused or
  doubly-freed `String` can no longer hand a dangling pointer to `free`.
- **`String` length/capacity are now `int64`.** `%String` went from `{ ptr, i32, i32 }` to
  `{ ptr, i64, i64 }`; the length/index/capacity arithmetic inside `<string>` is `int64`
  throughout. Matches `size_t`, removes the 2 GB object-size cap, and avoids width casts at
  the libc boundary. Public accessors that return a length (`String_len`, `String_index_of`)
  now return `int64`; callers that store into an `int` truncate exactly as before.
- **Self-hosted codegen is now feature-complete against the C++ corpus.** A systematic feature
  sweep (every feature-bearing `tests/*.esk` through `cg_parity.sh`) is clean: each program,
  compiled by the Eskiu-written codegen, runs identically to the C++ build. Closing it required
  11 root-cause fixes the sweep exposed (the bootstrap fixpoint had only ever exercised the
  subset the compiler's own source uses): var-decl/struct-init coercion + signedness-aware
  integer widening; full integer semantics (binary-op operand-width unification, unsigned
  `udiv`/`lshr`/`icmp`, >32-bit literals as `i64`, vararg small-int promotion); type-alias
  resolution; function-as-value decay (a `__fnptr` env-dropping thunk + `{thunk,null}` closure,
  or a bare `@fn` for a pointer cast); generic ADT enum monomorphization; a self-host *parser*
  fix (a parenthesized struct literal `(P{…})` was mis-parsed as a cast); primitive constraint
  dispatch (`a.m(b)` on a primitive → `m(a,b)`); the `alloc_with`/`thread_create`/`thread_join`/
  `free_closure` builtins; user-defined variadic functions + `va_list`/`va_start`/`va_arg<T>`/
  `va_end`; packed structs (`packed` / `#pragma pack(N)`); and the `?` error-propagation
  operator. All parity/self-host/bootstrap gates stay green throughout.
- **Self-hosted codegen: ADT payloads wider than one slot.** An ADT enum's tagged-union
  payload area (`{ i32, [N x i64] }`) is now sized by **bytes with field alignment**
  (`cg_layout_size`, mirroring the C++ DataLayout), not by field count, so a variant
  carrying a struct-by-value larger than 8 bytes (e.g. `Line(Vec3)` where `Vec3` is 12B)
  fits instead of overflowing its slot. Construction + `match` extraction already viewed
  the payload as the variant's `{ fields }` struct, so only the area sizing needed the fix.
  Test `adt_big_payload`. cg_parity 56/56. (A subsequent feature sweep found this was NOT
  the last gap; see below.)
- **Self-hosting feature coverage audited + closed out.** Verified the async combinators
  `select2`/`join2`/`spawn` all work through the self-hosted codegen (added `async_select`/
  `async_spawn` to the parity corpus). The self-hosted drivers (`esk_main`/`cg_main`) now
  route diagnostics to **stderr** (an `eprint` helper over `write(2, …)` + `sprintf`) so an
  error can never contaminate the `.ll` text on stdout. cg_parity 55/55, cg_selfhost 68/68,
  bootstrap fixpoint. (NOTE: a later systematic feature sweep, pushing the C++ test corpus
  through the parity oracle, showed self-host codegen is NOT yet feature-complete; ~8
  root-cause gaps remain, tracked in `selfhost/BACKEND_PLAN.md`. The bootstrap only exercises
  the subset the compiler's own source uses, so it missed them.)
- **Self-hosted codegen: more of the language.** Beyond the bootstrap subset, the
  self-hosted code generator (`selfhost/codegen.esk`) now also lowers: **floating point**
  (`fadd`/`fsub`/`fmul`/`fdiv`, `fcmp`, `fneg`, and int↔float casts via `sitofp`/`fptosi`/
  `fpext`/`fptrunc`, with mixed int/float promotion); **`switch`** (a real LLVM `switch`
  with C-style fall-through + `break`); and **ADT enums + `match`** (the tagged-union
  layout `{ i32 tag, [N x i64] payload }`, variant construction, and `match` lowered to a
  tag switch with payload bindings). Each is behaviorally parity-tested (`cg_parity.sh`).
  Return values are now coerced to the function's return type (e.g. a `bool` result
  zero-extended to `int`). It also lowers **closures/lambdas** (free-variable capture into
  a stack env + a fat pointer `{ fn, env }`, higher-order functions, indirect calls) and
  **exceptions** (the Itanium ABI: `invoke`/`landingpad`, `__cxa_allocate_exception`/
  `__cxa_throw`/`__cxa_begin_catch`, type-name `strcmp` catch matching, `finally`; programs
  with `throw`/`try` link `-lc++abi`) and **atomics** (`atomic_load`/`store`/`swap`/`cas` →
  `load atomic`/`store atomic`/`atomicrmw xchg`/`cmpxchg`). The self-hosted compiler
  reproduces its own IR throughout (bootstrap fixpoint stays green).
- **Self-hosted codegen: unions, bitfields, and dynamic trait dispatch.** The
  self-hosted code generator now also lowers: **unions** (every member overlaps at
  offset 0; the type is `{ [N x i8] }` sized to the largest member, member access GEPs
  to the shared storage); **bitfields** (declared widths packed into `i32` words:
  reads `lshr`+`and`+optional `sext`, writes read-modify-write the word with a cleared
  mask, struct-init fills bit slots, and a normal field in a bitfield struct uses its
  real word index); and **interfaces / dynamic trait dispatch** (an interface value is a
  fat pointer `{ data, vtable }`; passing a struct pointer where an interface is expected
  **boxes** it, building a per-`(interface, struct)` vtable global of the struct's method
  implementations, and a call through an interface value dispatches by loading the method
  pointer from its vtable slot and calling it with `data` as the implicit receiver). Each
  is behaviorally parity-tested; the bootstrap fixpoint stays green.
- **Self-hosting async: all 19 async tests pass.** `for-in` over an await now lowers too
  (desugared to a counted `for`: array `T[N]` indexes `xs[i]`/length N; a list-like struct
  uses `xs.data[i]`/`xs.size`), completing the async transform. (Was 18/19.)
- **Self-hosting async: 18 of 19 async tests pass.** Closures' environments are
  **heap-allocated** (an escaping closure, e.g. an event-loop callback or a future combinator,
  called after its creating frame returns, no longer dangles its captured variables); struct
  literals work as **rvalues** (`x = P{…}`, not just var-decls); and the async constructor emits
  the **`on_drop` cancellation closure** (`future_drop` on a suspended task cascade-drops the
  awaited future). Together these unblock the event-loop / socket / timer / combinator
  (`select`/`join`/`spawn`) / cancellation async tests. (Only async `for-in` remains: it needs
  iterable element-type resolution.)
- **Self-hosting back-end: the async/await lowering pass, in Eskiu** (`selfhost/async_lower.esk`,
  a port of `sema/async_transform.cpp`, run between parse and codegen). Each `async fn` is
  rewritten into a frame struct + a `while(1){if st==0…}` state-machine resume function + a
  constructor that returns a `Future<T>*`; each `await` splits a state (evaluate the future,
  `future_poll` it, suspend, resume reading the value), with completion via `atomic_swap` and
  a waker closure, built on the now-available closures, atomics, and generics. Covers
  sequential bodies, **control flow around an await** (if / while / for with break/continue,
  lowered into the state graph), and a desugar pass (`return await E` / `await E;` /
  `x = await E` → let-bound). `async_basic`/`async_if`/`async_for`/`async_multi`/`async_break`/
  `async_return_await` run to parity. (Tests using generic-argument *inference* (`chan_recv(ch)`
  without `<T>`) or async for-in/switch, or closure→fn-pointer extern callbacks, await those
  orthogonal features.) Code generation also now **de-duplicates extern declarations** (the
  same `extern` in two imported stdlib files no longer emits two `declare`s).
- **Generic-argument inference.** A generic function called without explicit type arguments
  (`chan_recv(ch)`, `unwrap(&b)`, `add(7, 5)`) now has its type parameters solved by unifying
  each parameter's pattern (`Chan<T>*`, `Box<T>*`, `T`) against the actual argument types, then
  monomorphized like an explicit `chan_recv<int>`. Pointer spellings are normalized
  (`Box<T>*` ≡ `*Box<int>`) and `cg_etype` now types literals. This unblocks the channel-based
  async tests (`async_channel`, `async_elseif`) and any generic call relying on inference.
- **Unified self-hosted compiler driver + a three-stage bootstrap.** `selfhost/esk_main.esk`
  runs the full pipeline in Eskiu (preprocess → parse → **type-check** → code-gen),
  rejecting ill-typed input without emitting IR. The new gate `tests/selfhost/cg_bootstrap.sh`
  performs the canonical bootstrap: the C++ `eskiuc` builds the driver (cc0), cc0 builds it
  (cc1), and cc1 builds it (cc2); **cc1 and cc2 emit byte-identical IR** for the compiler
  and for a sample program, and cc2 compiles a runnable binary, i.e. stage2 ≡ stage3, a
  true self-hosting fixpoint. **Wired into CI.**
- **Self-hosting reached a bootstrap fixpoint (Phase D).** The self-hosted code
  generator emits valid LLVM IR for the *entire* self-hosted compiler, and `cg_main`
  compiled **by itself** reproduces the C++-built code generator's IR byte-for-byte
  over all 45 inputs (the whole `selfhost/` tree + the corpus). All five drivers
  (lexer/preprocessor/parser/typechecker/codegen), compiled by the self-hosted codegen,
  produce output identical to the C++-built ones. New gate `tests/selfhost/cg_selfhost.sh`
  (emit-validity + fixpoint, **wired into CI**). Eleven codegen bugs were found and fixed
  at the root by dogfooding the compiler's own source.
- **Self-hosting back-end, Phase C: the code generator, in Eskiu.** `selfhost/codegen.esk`
  (driver `cg_main.esk`) generates **textual LLVM IR** (assembled + linked by `clang`,
  no LLVM library). Covers scalars/arithmetic/control-flow, structs + methods + pointers,
  arrays, plain integer enums, **generics via monomorphization** (`List<T>`,
  `Box<int>` → `%Box_int`, `id<int>` → `@id_int`, with `sizeof`/cast so the stdlib
  `alloc<T>` instantiates), global variables, struct-by-value (sret), pointer
  arithmetic, and short-circuit `&&`/`||`. Behavioral oracle: emit `.ll` → `clang` →
  run → compare exit code + stdout to the C++-built binary, over a synthetic corpus
  (`tests/selfhost/cg_parity.sh`, **wired into CI**). Deferred (unused by the compiler's
  own source): floats, ADT enums/`match`, closures, exceptions, async, atomics.
- **Imported files are now preprocessed.** The self-hosted parser ran `import`ed files
  straight into the lexer, skipping the preprocessor, so an import's `#ifdef` kept both
  branches (e.g. `stdlib/mem.esk` emitted a duplicate `free`). `do_import` now runs
  `pp_run` per imported file, mirroring how the C++ folds preprocessing into the lexer.
- **Self-hosting back-end, Phase B: the type checker, in Eskiu.** `selfhost/sema.esk`
  (driver `tc_main.esk`, full pipeline preprocess → parse → check) now catches **all 19
  semantic error classes** in `tests/errors/`: name resolution, argument counts,
  undefined types & fields, async/await rules, switch/match exhaustiveness & duplicates,
  const-correctness (value, field, and pointer-pointee), interface satisfaction for
  bounded generics, the `?` operator's return type, and closure-escape soundness. Gate
  `tests/selfhost/tc_parity.sh` (**wired into CI**): the verdict matches
  `eskiuc --test-typechecker` on all **121** positive corpus files with zero false
  rejections, and every sema negative test is rejected with the right diagnostic. Types
  flow as strings (no structured Type IR), matching the C++ checker's codegen boundary.
- **Self-hosting back-end, Phase A: AST enrichment.** The self-hosted parser's AST
  (`selfhost/{ast,parser}.esk`) now captures what it previously parsed-and-discarded,
  so it can feed a future sema/codegen: generic **type-params + constraints**, struct
  **methods**, enum **ADT payloads**, **bitfield widths**, the **async** modifier, and
  full **interface method signatures**. Validated by a *lockstep* extension of both the
  C++ `ast/ast_printer.cpp` and the self-hosted printer (new `TypeParams:`/`Methods:`
  sections, `Name = tag(t1, t2)`, `type name : N`, `(async)`, `ret name(t1, t2)`),
  keeping `--test-parser` byte-identical (corpus 50/50, synthetic 11/11). Tooling; the
  production compiler change is the additive, debug-only printer extension.
- **Self-hosting milestone 3: the preprocessor, in Eskiu.** `selfhost/preprocessor.esk`
  ports `lexer/preprocessor.cpp`: object- and function-like `#define`/`#undef`,
  `#ifdef`/`#ifndef`/`#else`/`#endif` conditionals, `#pragma` passthrough, `#error`,
  backslash line splicing, recursive identifier-aware macro expansion, and the
  predefined `__FILE__`/`__LINE__`. Validated **through the lexer** (`pp_main`
  preprocesses + lexes; gate `tests/selfhost/pp_parity.sh`): byte-identical to the
  C++ `--test-lexer` over the whole `tests/` + `stdlib/` corpus (156/156, no
  exclusions), clean and directive-using files alike. **Wired into CI.** Tooling;
  the production compiler is untouched.
- **`List_set` / `List_remove`** in `<list>`: set an element by index, and remove
  one (shifting the tail). Surfaced by the preprocessor's macro table (redefine =
  set, `#undef` = remove); generally useful.
- **Self-hosting milestone 2 complete: the parser, in Eskiu.** The self-hosted
  parser (`selfhost/{ast,parser,parse_main}.esk`) now covers the full grammar (
  expressions, statements, declarations, templates/generics, lambdas/async) and is
  byte-identical to the C++ `--test-parser` AST dump over the import-free corpus
  (42/42 real `tests/*.esk` + synthetic). Gate `tests/selfhost/parse_parity.sh`
  (`--full`) is **wired into CI**. Dogfood/tooling; the production compiler is
  untouched. (v0.2.5 shipped this milestone in progress; it is now finished.)
- **Self-hosted parser follows `import`.** It now resolves and recursively parses
  imports (`<name>` → `stdlib/name.esk`, relative `"path"` against the importing
  file's directory), merging the imported decls in declaration order, with dedup and
  shared type-name registration across files (mirroring the C++ parser). Parity now
  covers every `tests/*.esk` whose **transitive import closure** is preprocessor-free
  (corpus 43 → 50); files whose closure touches the preprocessor stay excluded until
  the preprocessor is self-hosted. The harness computes the closure to gate inclusion.

---

## [0.2.5]
### Added
- **`<ctype>` stdlib module.** Pure-Eskiu ASCII character classification (no libc,
  freestanding-safe): `is_space`, `is_digit`, `is_hex`, `is_alpha`, `is_alnum`,
  `is_ident_start`, `is_ident_cont`. Introduced for the self-hosted lexer; usable
  by any program via `import <ctype>;`.
- **Self-hosting milestone 1: the lexer, in Eskiu** (`selfhost/` +
  `stdlib/ctype.esk`). A full lexer written in Eskiu, byte-identical to the C++
  `--test-lexer` over the entire preprocessor-free corpus. Parity gate
  `tests/selfhost/lex_parity.sh` (`--full` → 114/114; compiles the driver once to a
  native binary, ~seconds) is **wired into CI**. Dogfood/tooling; the production
  compiler is untouched. See `selfhost/README.md`.
- **Self-hosting milestone 2: the parser, in Eskiu** (`selfhost/{ast,parser,parse_main}.esk`),
  in progress. Builds a recursive AST (tagged heap structs) from the self-hosted
  lexer's tokens and prints it byte-identical to `--test-parser`; gate
  `tests/selfhost/parse_parity.sh`. Done so far: the full expression layer
  (precedence chain, unary, postfix call/index/member/`?`, casts, `sizeof`, all
  literal kinds). Statements/declarations/templates next.

### Hardening
- **Resolver consistency check (`ESKIU_RESOLVER_DEBUG`).** Codegen's type
  derivation is split out (`deriveExprEskiuType`) so that, under the env flag, the
  single-resolver table is cross-checked against the structural derivation on every
  hit; a semantic disagreement (the v0.2.4 two-evaluator miscompile class) is
  printed. Across the whole corpus the two agree on every behavior-affecting type
  (the only difference is a benign enum-as-int representation), confirming the
  v0.2.4 reconciliation holds. Behavior-preserving (golden IR 26/26).
- **Fuzzer: backslash-newline in comments & strings.** New generators with a
  self-checking expected-output oracle (the O0/O2 differential is blind to a
  uniformly mis-lexed program), guards the comment-continuation footgun fixed
  below.

### Fixed
- **Preprocessor: a `//` comment ending in `\` no longer swallows the next source
  line.** Backslash-newline line continuation was applied unconditionally, so a
  comment whose last character was a backslash spliced the following line into the
  comment, silently deleting a `return`, an `else` branch, or any statement, with
  no diagnostic. Continuation now fires only when the trailing `\` is genuine code
  (not inside a `//` or `/* */` comment or a string/char literal); legitimate
  `#define` continuation is unaffected. Found by dogfooding the self-hosted lexer.
  Regression test: `tests/comment_backslash_continuation.esk`.

---

## [0.2.4]
Type unification: making the type checker the single resolver, so codegen stops
re-deriving types independently (closing the two-evaluator risk). Internal
soundness work; the only user-visible effect is three latent miscompiles it
surfaced and fixed.

### Single type resolver (kills codegen's independent re-derivation)
- The type checker is re-run on the post-AsyncTransform AST and its resolved
  per-expression types are handed to codegen, which now consumes them instead of
  re-deriving via `getExprEskiuType`. This eliminated the structural condition
  behind the earlier `checkConstraints` bug, and immediately **surfaced and fixed
  three latent miscompiles** where the two evaluators silently disagreed (benign
  only because the affected test values were small):
  - **Float literals are `double` in sema too** (were typed `float`; codegen
    always emitted a `double` constant, so generic inference like `max(1.5, 2.5)`
    now monomorphizes to `double`, matching the emitted value).
  - **Pointer-arithmetic deref width**: `*(arr + n)` for `arr: *int` loaded `i8`
    in some paths (reading 1 byte of a 4-byte int); now correctly `i32`.
  - **`char` widening signedness**: a `char` widened to `int` used `sext` in some
    paths; `char` is unsigned in Eskiu, so it is now `zext`.
  - Plus a `for-in` consumption fix (the resolved iterable type arrives as
    `struct:List_int`; the loop lowering now strips the `struct:` decoration).
- **One grammar interpreter.** `codegen`'s `getTypeFromString` now dispatches on
  `ty::Type::parse`, the same parser the type checker uses, instead of its own
  hand-rolled string matching. The type-string grammar is interpreted in exactly
  one place across both phases. Behavior-preserving (golden-IR identical; existing
  quirks preserved).

---

## [0.2.3]
Completing bounded generics, plus a typed internal type representation that
replaces ad-hoc type-string surgery: the foundation for keeping the compiler
sound as it grows.

### Bounded generics: primitives can satisfy a constraint
- A primitive type now satisfies a constraint through a **free function** named
  like the interface method whose first parameter is that primitive, e.g.
  `int cmp(int, int)` makes `int` satisfy `interface Ord { int cmp(Self) }`. Inside
  a generic body a constrained call `t.cmp(x)` on such a `t` lowers to `cmp(t, x)`.
  So `max<T: Ord>(int…)` and a constraint-bounded map over `int` keys now compile.
  Closes the method-only seam; the fn-pointer `HashMap<K,V>` remains for explicit
  `hash`/`eq`. Sema and codegen are gated in lockstep (scalar primitives only).

### Typed `Type` representation (soundness foundation, no behavior change)
- **`sema/type.{h,cpp}`: a structured `ty::Type` IR** (a tagged value with
  `parse`/`str`/`substitute`/`nominalName`), the typed replacement for the
  `std::string` type surgery that was scattered across ~175 call sites. Types
  still travel as canonical strings at the boundaries; the IR is the manipulation
  form, so invariants are enforced by the type instead of by convention (which is
  what produced the earlier `checkConstraints` `struct:`-ordering bug).
- **Migrated onto it:** `substType` (now `Type::substitute`) and the duplicated
  "strip `struct:` + pointers → bare name" surgery at six sema sites, including
  the exact `checkConstraints` strip that caused that bug, which no longer exists.
- **A golden-IR oracle** (`tests/type_zoo/snapshot.sh`) snapshots the emitted IR
  over a 24-program type-zoo and asserts byte-identical output across the
  migration. Every step above is verified behavior-preserving by it, plus the
  suite, sanitizers, and the differential fuzzer. A standalone round-trip unit
  test (`tests/type_roundtrip/`) checks `parse(s).str() == s` over the grammar.
- The registry-coupled `normalizeType`/`unifyTypeParam` and the cross-phase
  consolidation (codegen consuming resolved `Type`s rather than re-deriving from
  strings) are intentionally left for a later step: they are not string surgery.

---

## [0.2.2]
Exclusively hardening + traits, no new stdlib surface. The goal: make the
compiler trustworthy and close the generics gap before the self-hosting arc.

### Compiler hardening
- **A fuzzer** (`tests/fuzz/eskiu_fuzz.py`): mutates the test corpus and generates
  programs biased toward the known bug classes, using the **LLVM IR verifier** and
  sanitizers as oracles (a crash, a verifier failure, or an asan/ubsan abort is a
  finding). Wired into CI as a bounded, fixed-seed job. Its first run found four
  real bug classes, now fixed:
  - **Unreachable code after a block terminator** (`return`/`break`/…) was emitted
    into the terminated block → "terminator in the middle of a basic block". Codegen
    now stops at the terminator and drops the dead code.
  - **Integer arguments on indirect / closure calls** weren't widened to the
    callee's parameter type → an IR-verifier mismatch. Now coerced (the same family
    as the 0.2.1 sret-argument fix).
  - **`switch` with duplicate case values** reached codegen and produced an invalid
    `switch` → now a clean type error.
  - **A negative array dimension** degraded the variable to a bare pointer that was
    then array-indexed → invalid GEP. Now rejected with a clear message.

### Traits / constraints (bounded generics)
- **`<T: Iface>`** (and `<T: A + B>`) on function and struct type parameters, built
  on the existing structural `interface`. At instantiation the concrete type
  argument must satisfy every listed interface (define its methods); otherwise it's
  a type error at the use site (`type 'X' does not satisfy constraint 'I'`) instead
  of a confusing downstream failure. Inside a generic body, a constrained method
  call resolves to the concrete type's method. Works for both `T f<T: Ord>(…)` and
  `struct Box<T: Ord> { … }`. (Constraints are method-based, so a constrained type
  must be a struct with the methods; primitive keys still use the fn-pointer
  `HashMap<K,V>` from 0.2.1.)

### Hardening + maintainability (0.2.2 re-cut, no behavior change)
- **Fuzzer: O0-vs-O2 differential oracle.** The fuzzer now also builds each
  verifier-clean generated program at `-O0` and `-O2` with clang and compares the
  runtime output; a divergence is a miscompile the IR verifier can't catch
  (valid IR, wrong semantics). Plus four more generators (ADT enums + `match`,
  capturing closures, async/await across control flow, nested structs) and
  mutation of generated programs. Wired into CI.
- **Compiler source modularized.** The three ~2,000-line files were split, with no
  behavior change: `sema/type_checker.cpp` → `typecheck_{decl,stmt,expr,type}.cpp`;
  `codegen/codegen_expr.cpp` → `codegen_{call,closure,adt}.cpp`; `parser/parser.cpp`
  → `parse_{decl,stmt,expr}.cpp`; the lexer's preprocessor pass into
  `lexer/preprocessor.cpp`; and `main.cpp`'s driver utilities into `main_support.cpp`.
  No source file exceeds ~760 lines.

---

## [0.2.1]
Hardening and ergonomics, shaken out by building a real service (an INE-QR HTTP
API) on 0.2.0.

### Compiler
- **`codegen.cpp` split** into `codegen_{module,type,scope,decl,stmt,expr}.cpp`: the 3,400-line file became six; no behavior change (same 243 tests).
- **Sanitizer hardening gate**: `tests/run.sh` gains `SANITIZE=asan|ubsan` (compiles every positive test with the instrumentation and fails on a sanitizer abort), and CI runs the whole suite under both. A new `loop_locals` stress test locks in the entry-block-alloca fix.
- **Fix: integer arguments to sret-returning functions** were matched against the wrong parameter: the coercion loop didn't skip the hidden sret pointer at index 0, so the first scalar argument was checked against the sret pointer and an `int` literal was left unwidened (an IR-verifier error for a >16-byte-returning function called with int literals). Now offset-correct.
- **Fix: `substType` substitutes type parameters inside function types** (`fn(*K)->uint64` → `fn(*int)->uint64`), so generic APIs with callback parameters type-check and monomorphize.

### Standard library
- **`<bytes>`**: `Bytes`, a growable binary-safe byte buffer (`*uint8` + length; embedded NULs are real data, unlike `String`'s NUL-terminated `*char`): `Bytes_init`/`_free`/`_push`/`_append`/`_append_raw`/`_slice` (non-owning view)/`_eq`/`_from_str`/`_cstr`, plus base64 convenience (`Bytes_from_base64`/`Bytes_to_base64`). `<http>` gains `HttpReq_body`: a non-owning `Bytes` view of the request body.
- **`HashMap<K,V>`** (`<map>`): a hash map over any value-type key: you pass `hash`/`eq` function pointers at init (Eskiu has no trait system to synthesise them, the systems-language answer, like C's `qsort` comparator), with built-in `int_hash`/`int_eq`. The string-keyed, key-owning `Map<V>` is unchanged.

---

## [0.2.0]
Backend-services phase: async/await, the full HTTP/2 stack (framing, HPACK with
Huffman, streams and flow control, a multiplexed server, and TLS/ALPN), sum types
with `match`, and a broad async/networking standard library, built on the v0.1.0
systems foundation.

### Language
- **`alloc`/`free` are no longer keywords**: heap allocation moved to the `<mem>` stdlib as the generic functions `alloc<T>(n)` and `free(p)`. Under `--freestanding` they target `esk_alloc`/`esk_free` via the predefined `__ESKIU_FREESTANDING__` macro.
- **`const`**: immutable typed bindings, usable as array sizes; field/element mutation of a `const` value is rejected.
- **Raw C function pointers**: casting a top-level function to a pointer type (`(*void)cmp`) yields its bare C function-pointer address rather than the `{fn, env}` closure fat pointer, so an Eskiu function can be handed to a C API as a callback (`qsort`, OpenSSL's ALPN selector, signal handlers, …). Test: `c_callback`.
- **Pointer constness**: `const T*` is a pointer to const T (the pointee is read-only, the pointer is rebindable); `T* const` is a const pointer (the binding is read-only, the pointee is writable); the two compose as `const T* const`. Reading through and rebinding a `const T*` are allowed; writing through it (`*p`, `p[i]`, `p->field`) is rejected, as is any conversion that drops a const qualifier: at initialization, assignment, call arguments and returns. Adding const (`T*` → `const T*`) is always allowed. const has no ABI effect and is stripped before code generation. Works in locals, parameters, struct fields and return types.
- **Integer ranges in `for`-`in`**: `for (i in A..B)` iterates the half-open range `[A, B)` (a new `..` operator). Desugars to a counted `for`, so it composes with `break`/`continue` and the async transform.
- **User-defined variadic functions**: a fixed parameter list followed by `...`, read inside the body with `va_list` / `va_start(ap)` / `va_arg<T>(ap)` / `va_end(ap)` (C default promotions apply: read float arguments with `va_arg<double>`). Lowers to the LLVM `va_arg` machinery; works on arm64 and x86-64.
- **Preprocessor diagnostics**: `__LINE__` (current source line) and `__FILE__` (current file path) expand in place, and `#error message` aborts compilation (honoring `#ifdef` branches). The VS Code grammar now also highlights all preprocessor directives.
- **Algebraic enums + `match`**: an `enum` variant may carry a payload (`enum Shape { Circle(float), Rect(float, float), Unit }`), making the enum a tagged union. Variants are constructed by name (`Circle(2.0)`, bare `Unit`) and destructured with `match s { Circle(r) -> …; _ -> … }`, which binds payload fields per arm. Classic integer enums are unchanged. `match` is checked for **exhaustiveness** (every variant covered, or a `_` default) and rejects duplicate arms. Algebraic enums may be **generic** (`enum Option<T> { None, Some(T) }`, `enum Either<A,B> { Left(A), Right(B) }`) monomorphized per instantiation like template structs; generic variants infer their type arguments from the payload when it determines them (`Some(5)` → `Option<int>`), or take explicit ones otherwise (`None<int>()`, `Left<int,string>(x)`).
- **`#pragma pack(N)` for N > 1**: caps each field's alignment at N (v0.1.0 honored `pack(1)` only). The layout matches the C `#pragma pack(N)` ABI: padding is inserted to N-capped field offsets and the total size rounds up to the struct's alignment (`min(max-field-alignment, N)`). `pack(push, N)` / `pack(pop)` / `pack()` still apply.
- **`alloc_with(&allocator, T, n)`**: explicit-allocator built-in.
- **`volatile let`**: qualifier-first form (like `const int`), in local and top-level declarations.
- Multi-argument `fn` types (`fn(A,B)->R`); function pointers usable as values, parameters, return types, and struct fields, and capturable in closures.
- **`intrinsic`**: function qualifier for compiler-lowered prototypes (distinct from `extern`): a call lowers to inline IR, not a call to a C symbol, and emits no `declare`. Gated on import, so the names stay un-reserved otherwise.
- **`escaping` + `free_closure`**: closures now escape correctly. A non-escaping closure (only called / passed to a non-`escaping` parameter) keeps its environment on the stack (zero cost); an escaping one (returned, stored, or passed to an `escaping` parameter) gets a heap environment that survives. The `escaping` parameter qualifier marks closure-retaining functions; using a non-`escaping` closure parameter beyond a direct call is a compile error. `free_closure(f)` releases an escaping closure's heap environment. (Previously, a closure that outlived its creating function read a dangling stack environment.)
- Fixed: a cast to a type imported from another file (e.g. `(FutureHdr*)p`) was misparsed; imported type names are now visible for cast detection. Follow-up fix: when that type's defining import was *deduplicated* (already imported via another path), its name was still missing from the importing file's parser, so the cast misparsed again. Declared type names are now shared across all sub-parsers (like the import set), so a cast parses regardless of import order.
- **`async` / `await`**: `async T f(...)` whose call yields `*Future<T>`, and the `await E` expression (`E: *Future<T>` → `T`), legal only inside an async function. A pre-codegen pass lowers an async function to a resumable state machine (frame struct + resume + constructor). Single and multiple awaits run end-to-end (fast path and suspend-over-reactor), including `return await` and `async void`. Cancellation works, **every** control-flow construct around await is supported (`if`/`while`/C-style `for`/`switch`/`for-in`, including `break`/`continue`), and the runtime is leak-free (verified with `leaks`). Fixed: a frame-hoisted local used inside a struct literal / `alloc_with` / `free_closure` after an await was not renamed to its frame field (the transform's expression walkers skipped those nodes). All expression nodes now share one child enumeration (`ast/ast_walk.h`).
- **Capturing closures inside generic functions**: a lambda declared in a template function body now captures correctly. Capture analysis runs on template bodies (type-independent), and a capture typed by the type parameter (e.g. `T` or `*Future<T>`) has its environment field substituted per instantiation. (Previously such a lambda got an empty capture list and miscompiled.) `await` also now accepts a `*Future<T>` returned by a generic function (its type arrives already instantiated). Removed the dead `thread`/`spawn`/`mutex` reserved words (the lexer reserved them but nothing used them), freeing those identifiers.
- **`spawn<T>` / `select2<A,B>` / `join2<A,B>`**: generic future combinators with a typed, cast-free call API (`spawn(task)`, `await select2(a, b)`, `await join2(a, b)`, type args inferred). `spawn` detaches a fire-and-forget task; `select2` completes with the index of whichever of two futures finishes first (loser dropped); `join2` completes once both finish. Thin wrappers over one shared type-erased core (`*_hdr`), so no per-instantiation bloat; leak-free (verified with `leaks`).

### Compiler fixes
- **Unsigned widening cast**: `(int)(uint8)255` (and `uint16`/`uint32`/`char`/`bool` → wider) now **zero-extends** (→ 255), matching the source's unsignedness, instead of always sign-extending (→ -1). Fixes byte-level decoding (parsers, the HTTP/2 frame codec, etc.).
- **Allocas are hoisted to the entry block.** A local declared inside a loop body emitted its `alloca` in the loop block, so the slot was re-reserved every iteration and never reclaimed until the function returned. A long-running loop with locals (e.g. `<base64>` encoding a multi-hundred-KB buffer) overflowed the stack and crashed. All stack-slot reservations now live in the function's entry block (stores stay at their original point; only the reservation hoists). Fixes `<base64>` on large inputs and any temp-heavy hot loop.

### Standard library
- **`<http2>`**: HTTP/2 (RFC 7540):
  - *Stage 1:* the frame-header codec (`h2_write_header`/`h2_read_header`, big-endian length/type/flags/31-bit stream id), frame-type + flag constants (`H2_HEADERS`, `H2_SETTINGS`, …), and the connection preface.
  - *Stage 2, connection lifecycle:* a SETTINGS codec (`h2_write_settings`/`h2_apply_settings`) over the six standard parameters plus an `H2Settings` model with RFC defaults; the SETTINGS ACK; PING/PONG (`h2_write_ping`/`h2_send_pong`); GOAWAY (`h2_write_goaway`/`h2_read_goaway`/`h2_send_goaway`) with the §7 error codes; an `H2Conn` connection-state struct; and async I/O over the event loop: `h2_read_full_async` (partial-read loop), `h2_read_frame_async`, and `h2_server_handshake_async` (preface validation → read + apply the client's SETTINGS → reply with ours + an ACK). Tested by `http2_conn` (codecs) and `http2_handshake` (the async handshake over a socketpair).
  - *Stage 5, TLS / ALPN* (`<tls>`): OpenSSL (libssl) by FFI: a server `SSL_CTX` (`tls_server_ctx`) that loads a cert/key and installs an ALPN callback selecting `h2`, blocking `tls_accept`/`tls_read_full`/`tls_write_all`/`tls_close`, and `http2_tls_serve_conn` running the HTTP/2 frame protocol over the encrypted stream. Plus an **async** TLS server (`http2_tls_serve_async`): a non-blocking SSL pump (`tls_accept_async`/`tls_read_async`/`tls_write_all_async` retrying `SSL_*` on `WANT_READ`/`WANT_WRITE` over the reactor) runs many TLS connections on one thread; verified with 3 concurrent `curl --http2` connections. The ALPN selector is passed to OpenSSL as a raw C function pointer (the new `(*void)fn` cast). Verified end-to-end against `curl --http2` (ALPN → h2, `HTTP/2 200`); see `examples/http2_tls_server.esk`. Also fixed: a server must not advertise `SETTINGS_ENABLE_PUSH=1` (§6.5.2). **HTTP/2 is now complete (stages 1–6).**
  - *Stream multiplexing:* the async server routes interleaved request frames to per-stream slots (`H2PendingStream`) and completes each on its END_STREAM, so a client can run many concurrent streams on one connection (responses stay serialized on the single connection writer: no concurrent socket writes). Verified with an interleaved two-stream test (`http2_multiplex`).
  - *Send-side flow control:* the async server (`h2_respond_async`) now spends the per-stream and connection send windows per DATA frame and parks for WINDOW_UPDATE when a window is exhausted, applying connection-level WINDOW_UPDATE in the dispatch loop too, so responses larger than the 65535-byte default window flow correctly (verified delivering 1 MB bodies over `curl`).
  - *Stage 6, server API* (`<http2_server>`): `http2_serve_async(lp, fd, handler, max_conns)` (and `http2_serve_conn_async`), an h2c server mirroring `<http_async>` and reusing `<http>`'s `HttpRequest`/`HttpResponse`. Handshake → frame-dispatch loop → HPACK-decode the request → handler → encode the response (HEADERS `:status`/`content-length`/headers + DATA, END_STREAM); SETTINGS/PING answered, GOAWAY/EOF ends it. Tested end-to-end over a socketpair (`http2_server`). Streams are multiplexed (interleaved request frames routed per-stream), non-blocking, and flow-controlled; cleartext h2c (TLS is in `<tls>`).
  - *Stage 4, streams & flow control:* the per-stream state machine (`H2Stream`: idle → open → half-closed → closed via `h2_stream_on_recv`/`h2_stream_on_send`), credit-based flow control over per-stream and connection windows (`h2_can_send`/`h2_account_sent`/`h2_account_recv`/`h2_grant_window`), the HEADERS/DATA/WINDOW_UPDATE/RST_STREAM codecs, and async HEADERS+CONTINUATION reassembly (`h2_read_header_block_async`). Tested by `http2_stream`.
- **`<hpack>`**: HPACK header compression (RFC 7541), stage 3 (complete): prefix-integer coding (§5.1), string literals (§5.2), the 61-entry static table, a dynamic table with size-based eviction, the §6 decoder/encoder (indexed; literal with/without/never indexing; dynamic table size update), and **Huffman** coding (§5.2 / Appendix B): a decode trie + bit-packed encode whose 257-symbol table is *generated from the RFC text* by `tools/gen_hpack_huffman.py` (→ `stdlib/hpack_huffman.esk`), never hand-transcribed. Verified against the RFC's own vectors, §C.1.1, §C.3.1, §C.4.1 (Huffman), plus encode→decode round-trips (`hpack`). See `docs/dev/http2-design.md` for the full stack.
- **`<alloc>`**: a toolkit of explicit allocators (`Bump`, `Arena`, `Pool`, `FirstFit`) over a buffer you own, via `alloc_with` (the Zig allocator model). Pure pointer arithmetic, no externs. Not a global `malloc` replacement: the default `alloc<T>` stays libc `malloc` in hosted mode; these manage a region and are the building blocks for a `--freestanding` heap.
- **`<sysheap>`**: a general-purpose heap that sources OS pages via `mmap` and allocates with `FirstFit`, so the allocation path never calls libc `malloc` (the Zig `page_allocator` / jemalloc / Go-runtime approach). `heap_init`/`heap_alloc`/`heap_free`/`heap_destroy`; opt-in (the default `alloc<T>` is unchanged).
- **`<time>`**: `time_now_ms`, `time_now_s`, `time_monotonic_ms`, `sleep_ms`.
- **`<env>`**: `env_get`, `env_has`, `env_get_or`, `env_get_int`.
- **`<base64>`**: RFC 4648 `base64_encode` / `base64_decode` over buffers.
- **`<json>`**: JSON builder (`Json`) plus a recursive-descent parser (`json_parse` → `JsonValue`).
- **`<threading>`**: `Mutex`, `Cond`, `Sem` over pthread.
- **`<http>`**: HTTP/1.1 request parser, response builder, and a threaded worker pool (`http_serve`). Also a binary-safe request reader, `HttpReq` + `http_recv` (loops `recv` until the full Content-Length body arrives, into a `*uint8` body; for uploads a single-recv String body would truncate/corrupt), with `HttpReq_header`, `http_reply`, and `http_reply_error`.
- **`<multipart>`**: extract a named part from a `multipart/form-data` body over raw bytes: `multipart_boundary` (parse the boundary from Content-Type) and `multipart_part(body, len, boundary, name, …)` (returns a slice into the body).
- **`<map>`**: `Map<V>`, a string-keyed hash map (open addressing, linear probing, grows at 0.75 load): `Map_init`/`Map_at` (get-or-insert, returns a `*V` slot)/`Map_get`/`Map_free`. Keys are strings (arbitrary-`K` hashing can't be synthesised generically).
- **`<net>`**: `net_accept_addr(fd, *uint32 peer_ip)` accepts a connection and reports the peer's IPv4 address (the plain `net_accept` drops it), enabling per-client rate limiting and logging.
- **`<string>`**: `starts_with`, `ends_with`, `trim`, `split` (List + streaming token iterator).
- **`<path>`**: `path_join`, `path_basename`, `path_dirname`, `path_extension`, `path_is_absolute`.
- **`<eventloop>`**: readiness reactor over kqueue (macOS) / epoll (Linux): `el_new`, `el_add_read`, **`el_add_write`** (write-readiness: `EVFILT_WRITE`/`EPOLLOUT`; a fd waits for read *or* write at a time), `el_del`, `el_run`, `el_stop`, `el_free`. Callbacks are `fn(EventLoop*, int)->void`. Foundation for async I/O and the HTTP stack. Fixed: a one-shot callback's heap env leaked when its fd was re-armed from inside the callback (a sequential read loop on one fd). `el_run` now frees the env it invoked using a per-registration generation counter, not the post-callback `active` flag.
- **`<atomic>`**: `atomic_load`, `atomic_store`, `atomic_swap`, `atomic_cas` over an `int` cell, lowering to LLVM atomics (acquire / release / acq_rel). Foundation for the async runtime's lock-free `Future` state handshake.
- **`<executor>` / `<net_async>`**: async runtime foundation: an `Executor` (event loop + thread-safe ready-queue + self-pipe wakeup, so wakers always resume on the home thread) and leaf futures (`net_read_async`, `net_accept_async`, **`net_write_async`**: non-blocking write that parks on writability and resumes until the whole buffer is sent) plus **`net_set_nonblocking`**, over `<eventloop>`. The async/await de-risk gate validated the runtime model end-to-end (reactor read, cancel, cross-thread resume). The async HTTP/2 server (`<http2_server>`) is now fully non-blocking: responses go out via `net_write_async`, so a slow client can no longer stall the loop (verified: a 40 KB body over `curl --http2-prior-knowledge`).
- **`<http_async>`**: a non-blocking, **concurrent** HTTP/1.1 server over `<eventloop>`: an async accept loop `spawn`s a detached handler per connection (so a slow request doesn't block the others), with a `<channel>` wait-group joining all handlers for clean bounded shutdown. The Phase-2 async-HTTP goal, end to end.
- **`<timer>`**: `timer_after(lp, ms)`, a leaf `*Future<int>` that completes after `ms` of monotonic time. `<eventloop>` gained a timer wheel (`el_add_timer`/`el_del_timer`); `el_run` now blocks only until the nearest deadline. Enables real read-with-timeout: `select2(net_read_async(...), timer_after(lp, ms))`.
- **`<futureval>`**: value-returning async combinators built on sum types: `select2v<A,B>` resolves with the winner's value as `Either<A,B>` (loser dropped), `join2v<A,B>` resolves with both values as a `Pair<A,B>`. Leak-free. (Also fixed: a `Pair<A,B>{...}`-style template struct literal inside a template body now substitutes the enclosing type params instead of mangling to a bogus `Pair_A_B`.)
- **`<either>`**: the standard sum types built on generic algebraic enums: `Option<T>` (`None`/`Some`) and `Either<A,B>` (`Left`/`Right`), with helpers (`opt_is_some`, `opt_unwrap_or`, `either_is_left`, …). `match` works inside these generic helpers (codegen resolves the enum instance from the subject's type under the active type substitution).
- **`<channel>`**: an async message channel over the `Future` runtime: `chan_recv(ch)` is a `*Future<T>` that completes with the next item (immediately if buffered, else it parks until a `chan_send` hands one off and wakes it); `chan_send(ch, v)` enqueues or hands off directly. v1 is a bounded ring with a single outstanding receiver and non-blocking send. Generic and cast-free; leak-free.

### Tooling
- **`eskiuc run file.esk [args...]`**: compile to a temporary executable, run it (forwarding `[args...]`), then delete it; the program's exit code is propagated. Compiler flags precede the script, program arguments follow it.
- **Shebang scripts**: a leading `#!/usr/bin/env eskiuc run` line is ignored by the preprocessor (line numbers preserved), so a `.esk` file can be made executable (`chmod +x`) and run directly.
- **`eskiuc fmt [--check] file.esk …`**: a conservative, comment-preserving source reindenter: normalizes leading indentation (4 spaces per brace level), trailing whitespace, blank-line runs and the final newline, while preserving every line's content (operators, inner spacing, comments, strings) verbatim. Idempotent and meaning-preserving; braces inside strings/comments are ignored. `--check` reports unformatted files (exit non-zero) without writing.
- **`--asan` / `--ubsan`**: real sanitizer instrumentation via the LLVM pass manager. `--asan` runs AddressSanitizer (heap/stack/global memory errors) and links the matching LLVM compiler-rt runtime; `--ubsan` inserts trapping bounds checks (no runtime needed). Both compose with `eskiuc run`.
- **`-Wextra`**: extra warnings layered on top of `-Wall`: comparison between signed and unsigned integers; off by default.

### Documentation
- **`docs/lang/grammar.md`**: a formal EBNF grammar derived from the parser: lexical structure, preprocessor, declarations, the type grammar, statements, and the full expression-precedence chain.
- **`docs/dev/abi.md`**: the C-ABI contract: scalar/pointer lowering (const has no ABI effect), struct/packed/bitfield/union layout, ADT tagged-union shape, the >16-byte sret rule, varargs + `va_list`, fat pointers for closures/interfaces, and template name mangling.
- **`__FILE__`/`__LINE__` reference**: spec §18 now documents all predefined macros (`__LINE__`, `__FILE__`, OS macros, `__ESKIU_FREESTANDING__`) and the shebang interaction.

### Compiler correctness (consistency audit)
- **Mutable parameters.** Every function parameter now gets a stack slot, so a parameter can be reassigned in the body like a local (`int f(int n) { n = n + 1; … }`). Previously this miscompiled (a store into a non-pointer SSA value, caught by the IR verifier). Consequently the receiver resolution for method calls and interface dispatch was made representation-independent: a value-struct receiver passes its address, a pointer receiver passes the pointer it holds, and an interface value (a pointer to its `{data, vtable}` fat struct) is loaded from its slot, fixing a latent bug in calling a struct method through a pointer parameter. Tests: `param_reassign`, `interfaces`.
- **`bool` → wider integer zero-extends.** Returning (or implicitly widening) a `bool` or comparison result into an `int` now yields `1`, not `-1`. The i1 was being sign-extended. `return a < b;` from an `int` function is correct.
- **Async `else-if` with `await`.** In an `async` function, an `if / else if / else` chain whose branches contain `await` and whose `else` terminates (e.g. `return`) no longer miscompiles. The state-machine lowering treated a plain terminating branch as fall-through and appended a state transition after the `return`, yielding invalid IR ("terminator in the middle of a basic block"). The lowering now recognizes terminating statements. Test: `async_elseif`.
- Unsigned integer types use unsigned div/rem/shift/compare; 64-bit integer literals no longer truncate; mixed-width ops sign/zero-extend by signedness; variadic call args get the C default-argument promotions.
- Member access on a struct-valued temporary; `(Type)`/`(Type*)`/alias/enum casts; type alias as a local pointer/array; `List<StructType>` through helper functions; `T*` (trailing-star) pointer deref; nested template close `>>`; closures no longer capture module globals by value.

---

## [0.1.0]

Eskiu is a systems language built to replace the C + Go + C++ + Python stack that compute-intensive backend services typically require. C-style syntax, structural interfaces, monomorphic templates, and explicit memory, compiled to native via LLVM with no garbage collector and no runtime.

This is the first release. It includes everything needed to write real backend services and bare-metal systems code, validated against a cryptographic pipeline running at 74 ms on arm64 (2.5× faster than the reference C implementation) and a bare-metal ARM64 kernel booting in QEMU without libc.

### Closures and threads

- `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`; variables from the enclosing scope are captured by value automatically
- `thread_create(fn()->void)` and `thread_join(*void)` are language keywords; the fat pointer maps directly to pthread's `(start_routine, arg)`: no trampoline
- Link with `-lpthread` on Linux

```eskiu
int base = 10;
let add: fn(int)->int = int(int x) { return x + base; };  // captures base

*void t = thread_create(void() { printf("hello\n"); });
thread_join(t);
```

### Exception handling

- `try { } catch (T name) { } finally { }` with multiple catch clauses
- `throw expr`: any Eskiu value
- LLVM `invoke`/`landingpad` with `__gxx_personality_v0`; unhandled exceptions re-thrown via `resume`
- Link with `-lc++` (macOS) or `-lstdc++` (Linux)

```eskiu
try {
    throw "division by zero";
} catch (string e) {
    printf("caught: %s\n", e);
} finally {
    printf("cleanup\n");
}
```

### Language completeness

- `sizeof(T)`: compile-time `int64` constant for any type including structs
- `union`: all fields share offset 0; size = sizeof(largest field)
- Typed pointer arithmetic: `p: *int; p + 1` advances 4 bytes, not 1
- `*ptr = value` through pointer parameters works correctly

### Forward declarations and declaration order

- Declaration order no longer matters: a function may call any function defined later, and mutual recursion works
- Body-less forward declarations (`int is_odd(int n);`) parse and are honoured
- Codegen declares every function prototype in a pre-pass before emitting any body, so call-before-define resolves cleanly instead of failing

### Compound assignment

- Bitwise compound assignment operators: `&=`, `|=`, `^=`, `<<=`, `>>=` (alongside the existing `+= -= *= /= %=`)

### Template ergonomics

- Template struct literals: `Pair<int, float> { first: 7, second: 3.14 }` (named and positional)
- Type-argument inference: type parameters are inferred from argument types by structural unification: both when a parameter is the bare type parameter (`max(3, 5)`) and when it appears inside a composite type (`List_get(&nums, i)` infers `T` from the `List<T>*` parameter). Explicit `max<int>(3, 5)` still works
- Mixed-width literal arguments are coerced to the substituted parameter type at the call site (e.g. a `double` literal passed where `T = float`)

### Iteration, error propagation, and richer strings

- **`for`-`in` loops**: `for (x in iterable)` iterates over fixed-size arrays (`T[N]`) and over List-like structs (any struct with `int size` and a `data` pointer, including `List<T>`). The loop variable is a per-iteration copy; `break`/`continue` behave as in a counted loop (it desugars to one, so `continue` still advances the index)
- **`?` error-propagation operator**: postfix `expr?` on a `Result<T, E>` returns the result early if it is `Err`, otherwise evaluates to the unwrapped `T`. Permitted only inside a function that returns the same `Result` type; rejected with a clear diagnostic otherwise
- **Richer mutable `String`**: added `String_push`, `String_char_at`, `String_set`, `String_clear`, `String_index_of`, `String_eq`, `String_eq_cstr`, `String_reverse`, `String_substring`, `String_from_int`, and `String_to_int` to `stdlib/string.esk`, all maintaining the owned, NUL-terminated buffer invariant

### Compiler robustness and fixes

- The parser now reports declaration errors and returns a failure instead of silently dropping unparseable declarations and reporting success; `--test-typechecker` exits non-zero on type errors
- Fixed: writing a `float` member of a `union` stored a full `double` without truncating, so reads returned garbage
- Added a regression test suite (`tests/run.sh`) with exact-output and expected-error checks covering arithmetic, control flow, recursion, pointers, strings, structs, interfaces, templates, closures, lambdas, exceptions, forward declarations, and compound assignment

### One-step linking

- `eskiuc file.esk -o prog` now **compiles and links** in one step, producing a runnable executable: `eskiuc` invokes the system C toolchain (`$CC`, then `cc`/`clang`/`gcc`), the same way `rustc` and `clang` do internally
- `-l<lib>`, `-L<path>`, and `--link-arg=<arg>` are forwarded to the linker (e.g. `eskiuc app.esk -o app -lpthread`)
- Object-only when the `-o` output ends in `.o`, when `-c` is passed, or under `--freestanding` (bare-metal links itself); a C toolchain is the only build dependency besides LLVM

### Enums, type aliases, bitfields, and the preprocessor

- **Enums**: `enum Color { Red, Green = 5, Blue }`: members are `int` constants (implicit or explicit values), the enum type maps to `i32`, and they work in `switch`, comparisons, and as parameter types
- **Type aliases**: `type u8 = uint8;`: a name for any existing type, including pointers and templates; resolved to the underlying type everywhere
- **Bitfields**: `struct F { uint32 a : 1; uint32 b : 3; }`: packed into storage words with masked read-modify-write access; signed fields sign-extend; works with struct literals
- **Preprocessor**: object-like and function-like `#define`/`#undef` (with recursive expansion and backslash-continued **multi-line** macro bodies) plus `#ifdef`/`#ifndef`/`#else`/`#endif` conditional compilation, run as a text pass before lexing; the macro table is shared across `import`/multi-file builds, and source line numbers are preserved
- **Packed structs**: the `packed struct` qualifier removes inter-field padding (back-to-back layout) for matching on-the-wire/on-disk formats and C `__attribute__((packed))` structs. `#pragma pack(push, N)` / `pack(pop)` is honoured for C source compatibility (maintains an alignment stack; `pack(1)` packs subsequent structs); both set the same flag and compose with bitfields and `sizeof`

### Tooling

- **Multi-file compilation**: `eskiuc a.esk b.esk -o prog` compiles several files together, merging their declarations (order-independent thanks to the prototype pre-pass)
- **`-Wall`**: lint-style warnings: unused variables, parameters, and functions, plus assignment used as a condition (`if (x = 0)`); off by default

### Standard library

- `import <name>`: angle-bracket imports resolve stdlib modules from the installation; `import "path"` for local files
- `<fs>`, file I/O: `fs_open`, `fs_close`, `fs_read`, `fs_write`, `fs_seek`, `fs_tell`, `fs_size`, `fs_read_all`, `fs_write_all`, `fs_eof`, `fs_error`
- `<net>`, TCP sockets over the POSIX BSD socket API: `net_tcp_listen`, `net_accept`, `net_tcp_connect`, `net_send`/`net_recv`/`net_send_str`, `net_close`, plus the raw `extern`s and a portable `sockaddr_in`. Pure stdlib; sockets need no compiler support beyond the C FFI and `packed struct`. Includes `examples/http_server.esk` (a working HTTP/1.1 server) and `examples/tcp_echo_server.esk`

### Networking-enabling language features

- **Function-as-value**: a top-level function used as a value (not called) decays to a `fn(T,...)->R`, passable to `thread_create` and callbacks without wrapping it in a lambda. The compiler synthesizes an adapter so the function fits the `{fn_ptr, env_ptr}` closure ABI
- **Predefined OS macros**: the compiler predefines `__APPLE__` (macOS) or `__linux__` (Linux), so stdlib and user code can `#ifdef` per platform; used by `<net>` to pick the correct `sockaddr_in` layout

### Release

- GitHub Actions workflow builds static binaries for macOS arm64 and Linux x86-64 on every `v*` tag
- Tarball: `bin/eskiuc` + `lib/eskiu/stdlib/`

---

## Systems milestone

Bare-metal ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) and prints to serial without libc or a C runtime.

- Inline asm UART driver, bump allocator in `--freestanding` mode
- Cross-compiled with `--target aarch64-unknown-none-elf`
- `volatile` MMIO, `asm(...)` with GCC-compatible constraints

## [0.0.14-alpha]

### Added

**Inline assembly**
- `asm("cli");`: simple form; passes the string verbatim to the assembler with no inputs, outputs, or clobbers
- `asm("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");`: extended form with GCC-compatible constraint syntax; supports input operands and clobber lists (no output operands)
- Lowers to LLVM inline asm nodes; `"memory"` clobber emits a compiler barrier

**Freestanding mode**
- `--freestanding` CLI flag: redirects `alloc` to call `esk_alloc` and `free` to call `esk_free` instead of the libc `malloc`/`free`
- User provides both symbols in their kernel or bare-metal runtime; the compiler emits `declare` stubs and the linker resolves them

**`volatile` qualifier**
- `volatile let reg: *uint8 = (uint8*) ADDR;`: marks all LLVM loads and stores through the pointer as `volatile`, preventing the optimiser from caching, reordering, or eliminating MMIO accesses

**Cross-compilation**
- `--target TRIPLE` CLI flag: sets the LLVM target triple for the output object file
- Both AArch64 and X86 LLVM backends are included in the Eskiu build
- `eskiuc file.esk --target x86_64-pc-linux-gnu` produces an ELF x86-64 object file

**v0.1 prerequisites complete**
- All compiler prerequisites for the kernel-on-QEMU milestone are implemented and tested

## [0.0.13-alpha]

### Fixed
- **Negative literals**: `-1`, `-3.14` etc. now parse as negative literal values directly, not as unary minus applied to a positive literal. Global variable initialisers with negative values (e.g. `int x = -1;`) previously compiled to 0; this is now correct. `evaluateConstantExpr` folds unary minus on numeric constants as a fallback.

---

## [0.0.12-alpha]

### Added

**Lambdas and function pointer types**
- `fn(T,...)->R` function pointer type syntax: first-class function types usable in variable declarations, struct fields, and parameter lists
- Anonymous function expressions: `int(int x) { return x * 2; }`, C-like syntax producing a function pointer value
- Lambda variables: `let double_it: fn(int)->int = int(int x) { return x * 2; };`
- Higher-order functions: lambdas can be passed as arguments to functions expecting `fn(...)` parameters
- `LambdaExpr` AST node; full visitor chain (parser, type checker, codegen, AST printer)
- Codegen: each lambda expression is lowered to a uniquely-named private `llvm::Function`; the expression value is the function pointer

**VS Code extension: real-time diagnostics, hover, and go-to-definition**
- `--hover-at LINE:COL` CLI flag: prints the inferred Eskiu type of the expression at the given source position; consumed by the VS Code extension for hover tooltips
- `--definition-at LINE:COL` CLI flag: prints the `file:line:col` of the definition of the symbol at the given position; consumed for go-to-definition
- VS Code extension (`editor/vscode/`) upgraded from syntax-only to a full language client: spawns `eskiuc --test-typechecker` on save for real-time error squiggles; hover provider calls `--hover-at`; definition provider calls `--definition-at`
- TextMate grammar already present from v0.0.11 provides syntax highlighting

**switch/case type checking**
- The type checker now validates that each `case` value is compatible with the `switch` subject expression type; mismatched case types are reported as errors at the `case` token position

## [0.0.11-alpha]

### Added

**Decoder rewritten in Eskiu (no C pipeline code)**
- `ine_decoder/crypto.esk` (541 lines): AES-256-CBC + RSA-8192 pipeline, hex/base64 decoders, PKCS#1 stripper, 6-bit decoder, WebP reconstruction, `run_no_so_pipeline()`; all in Eskiu calling OpenSSL via `extern`
- `ine_decoder/output.esk` (186 lines): Spanish character table, field splitter, growable JSON buffer, `decode_to_buffers()`; pure Eskiu
- `ine_decoder/crypto.c` and `output_decode.c` removed: replaced entirely by Eskiu
- Only C remaining: `qr_extract.c` shim (12 lines) + `qr_extract_impl.cpp` (CoreGraphics + zxing-cpp)
- Runtime: **80ms** on arm64, identical output to reference

**String literal adjacent concatenation**
- `"abc" "def"` on consecutive lines (or the same line) are now concatenated into a single string at parse time; enables readable multi-line constant definitions

### Fixed (compiler bugs found during Eskiu crypto port)

- **`ptr - ptr` → `int64`**: pointer subtraction for computing byte offsets in buffers
- **Integer widening in arithmetic** (`+ - * /`): `i8 - i32` now ZExts the narrower operand before emitting the instruction
- **`string[i]` → `char`**: indexing a `string` typed value now returns the correct `char` (i8) element instead of computing type `"strin"`
- **Assignment store coercion**: `arr[i] = val` where the array element type is wider than `val` now ZExts/truncates to match (e.g. `int64[0] = 0` was storing i32 into an i64 slot)
- **Return value coercion**: functions that return `int64` but the expression evaluates to `i32` now automatically extend the return value
- **GlobalVariable initializer coercion**: `uint8 X = 0x52` was creating a global with mismatched i32/i8 initializer; now casts to the declared type
- **`validateStructType` multi-level pointers**: `**char` was triggering "undefined struct '*char'"; now strips all pointer levels before checking the base type

---

## [0.0.10-alpha]

### Added

**Global variables**
- `VarDecl` at module scope now emits `llvm::GlobalVariable` instead of `alloca`
- Constant initializers folded at compile time: `int`, `float`, `double`, `bool`, `char`, `string`, `null`
- Non-constant initializers zero-initialize the global (assign in `main()` for complex expressions)
- `globalVarTypes` map tracks Eskiu type strings for globals (complement to function-scoped `varTypeStack`)
- `evaluateConstantExpr()`: folds literal expressions to `llvm::Constant*`
- `visit(IdentExpr*)` now loads from `llvm::GlobalVariable` as well as `AllocaInst`
- `IMAGE_PATH`, `OUT_JSON`, `OUT_WEBP` in `ine_decoder/main.esk` moved to module scope

**sret (large struct return)**
- `needsSret(type)`: returns true for aggregates > 16 bytes (arm64 register limit)
- `visit(FunctionDecl*)` rewrites large-return functions: prepends hidden `ptr sret.ptr` parameter, changes return type to `void`, tracks struct type in `funcSretTypes`
- `visit(ReturnStmt*)` stores result to `currentSretParam` and emits `ret void` for sret functions
- Call sites (regular + template): alloca sret buffer, prepend as arg 0, call, load result
- `currentSretParam` saved/restored across template instantiation

**Integer argument widening at call sites**
- Automatically `SExt`/`Trunc` integer arguments to match function parameter widths
- Fixes `i32 1712` passed to `int64 param` (was LLVM verification error)

### Fixed
- `*int` vs `size_t *` in `extern.esk`: `run_no_so_pipeline` and `decode_to_buffers` now use `int64` for length params to match C's `size_t` (was writing 8 bytes to a 4-byte stack slot → heap corruption)
- ine_decoder `pipeline.esk`: stage signatures updated to `int64` for all size parameters

### Planned
- `argv`/`argc` support: programs can accept CLI arguments natively

---

## [0.0.9-alpha]

### Added
- **`ine_decoder/`, INE QR decoder port**: full pipeline running at **74.4 ms** total
  (QR: 71.7 ms + crypto: 2.8 ms + output decode: <1 ms) vs. 188.9 ms reference C and 3–5 s original target; 2.5× faster than hand-written C
  - `types.esk`: `QRPair`, `NoSoKeys`, `IneResult`, `IneFields` structs
  - `extern.esk`: libc + OpenSSL EVP (AES-256-CBC / RSA-8192) + `ine_qr_extract()` declarations
  - `stage1_qr.esk`: QR extraction wrapper
  - `stage2_crypto.esk`: 3-round AES-256-CBC + RSA-8192 via OpenSSL
  - `stage3_output.esk`: pipe-delimited plaintext → JSON + WebP extraction
  - `main.esk`: orchestration, timing, output
  - `qr_extract.c` / `qr_extract_impl.cpp`: C/C++ shim using CoreGraphics + zxing-cpp 3.x
  - `Makefile`, `README.md`

### Fixed
- **Integer width mismatch in comparisons**: `uint8 == int` (e.g. `plaintext[i] == 124`) crashed LLVM with "Both operands to ICmp instruction are not of the same type"; now `ZExt`s the narrower operand to match the wider before emitting any of the six comparison operators
- **Mixed int/float arithmetic**: `i64 * double` (e.g. timing calculation `(t1 - t0) * 1000.0`) generated invalid IR; now detects int/float type mismatch in `+`, `-`, `*`, `/` and promotes the integer operand with `SIToFP` before emitting the floating-point instruction

---

## [0.0.8-alpha]

### Added
- **`import "file.esk"`**: multi-file support with paths resolved relative to the importing file's directory; recursive imports with deduplication (a file imported more than once is parsed only once); `Parser.basedir` + `importedFiles` set propagated to sub-parsers
- **Interface vtable dispatch**: `interface I { void method(); }` generates `%I_vtable = type { ptr, ... }` and `%I_fat = type { ptr data, ptr vtable }` LLVM types; structs are auto-boxed at call sites via `boxAsInterface()`; method dispatch loads the vtable pointer from the fat pointer and calls indirectly; `getExprEskiuType` extended to handle `UnaryExpr("&", ...)` and `UnaryExpr("*", ...)` for boxing detection
- **`String.append()` and `String.concat()`**: added to `stdlib/string.esk` using `memcpy` + pointer arithmetic

### Fixed
- Pointer comparison (`p == null`, `ptr1 == ptr2`) incorrectly used `FCmpOEQ` (float equality); now uses `ICmpEQ`; all six comparison operators updated to check `isFloatingPointTy()` first
- `i1 → i32` widening used `SExt` (sign-extends `true` → `-1`); now uses `ZExt` for `i1` operands so comparisons correctly store `0` or `1`

---

## [0.0.7-alpha]

### Added
- **Bitwise operators**
  - Binary: `&`, `|`, `^`, `<<`, `>>`, new precedence levels in parser (bitwiseOr → bitwiseXor → bitwiseAnd → equality → shift → comparison → additive)
  - Unary: `~` (bitwise NOT), added to unary operator list in parser and `inferUnaryExprType`
  - Codegen: `CreateAnd`, `CreateOr`, `CreateXor`, `CreateShl`, `CreateAShr`
- **Hex literals**: `0xFF`, `0x0F`; lexer reads hex digits after `0x`/`0X` prefix; `stoll(..., 0)` for auto-base detection in codegen
- **Compound assignments**: `+=`, `-=`, `*=`, `/=`, `%=`; new tokens in lexer; desugared in `parseAssignment()` to `x = x op y`
- **`continue` statement**: `ContinueStmt` AST node; full visitor chain; `continueTarget` saved/restored around `WhileStmt` and `ForStmt` bodies (points to loop condition and step block respectively)
- **For-loop with declaration init**: `for (int i = 0; i < n; i += 1)`, parser wraps the declaration in a `BlockStmt`; type checker processes init items directly in `ForStmt` scope, avoiding the inner-scope pop that previously made `i` undefined in the condition, step, and body
- **Pointer arithmetic**: `ptr + n` → `GEP(i8, ptr, n)`, `ptr - n` → `GEP(i8, ptr, -n)` in `visit(BinaryExpr*)`

### Fixed
- `&` address-of returned a loaded value instead of the alloca pointer, breaking all pointer-argument call patterns
- Unary `-` for floats used integer `CreateNeg`; now uses `CreateFNeg` for floating-point operands
- `!` logical NOT on integers used bitwise `CreateNot` (incorrect for `i32`); now uses `CreateICmpEQ(x, 0)` for non-`i1` types
- `~` unary NOT was missing from the parser's unary operator list, causing a parse failure for `~0` and similar expressions
- `inferUnaryExprType` did not handle `~`; reported "invalid operand" for bitwise NOT on integers
- `inferBinaryExprType` did not handle bitwise or shift operators; reported "invalid operands" for `a & b`, `a << n`, etc.
- For-loop init declarations were scoped to an inner `BlockStmt`, making the loop variable undefined in the loop condition and body

---

## [0.0.6-alpha]

### Added
- **Source locations in errors**: `ASTNode` now carries `line`/`col`; parser stamps all expression and statement nodes; errors now report `file.esk:line:col:` instead of `file.esk:0:0:`; filename taken from the CLI input path
- **`&` address-of operator**: correctly returns the lvalue pointer (alloca), enabling `fn(&localVar)` patterns and template method calls via `self` pointer
- **Template member access on pointer types**: trailing `*` stripped before struct field lookup; `self: List<int>*` now correctly resolves to `structFields["List_int"]` in both `visit(MemberExpr*)` and `getExprEskiuType()`
- **`stdlib/list.esk`**: `List<T>` with `List_init`, `List_push`, `List_get`, `List_len`, `List_free` as template functions; tested end-to-end
- **`stdlib/string.esk`**: `String` struct with `String_init`, `String_from`, `String_cstr`, `String_len`, `String_free`
- **Interface structural check**: `isValidAssignment` now verifies that a struct satisfies an interface's method signatures (vtable codegen deferred to 0.0.8)
- **`isPointerType` unified**: now detects both leading `*T` and trailing `T*` conventions

### Fixed
- `&` operator returned a loaded value instead of the address, breaking all pointer-argument patterns
- Template method parameter types with pointer suffix (e.g. `List<T>*`) were incorrectly mangled to `List_int_*` instead of `List_int*`
- `getExprEskiuType` for member chains through a pointer-to-template-struct now resolves correctly

---

## [0.0.5-alpha]

### Added
- **`switch`/`case`**: `SwitchStmt` AST node with `Case { value, stmts }` list; full visitor chain; parser handles `switch (expr) { case val: stmts break; default: stmts }` with fallthrough support; codegen emits LLVM `switch` instruction with `ConstantInt` case values; `break` branches to `switch.end` via existing `breakTarget` mechanism
- **Function templates**: `fn Name<T, E>(T x) -> RetType<T,E> { ... }`; `FunctionDecl.typeParams` field; `TemplateCallExpr` AST node parsed in `parsePostfix()`; lazy instantiation via `typeParamOverride` map that intercepts all type lookups during template body emission; context save/restore preserves insert point and `currentFunction` when instantiating inside another function's body; `substType` handles `Name<T,E>` nested substitution recursively
- **`InterfaceDecl`**: `interface Speakable { void speak(); }` fully parsed and stored in `interfaceDecls`; codegen generates no IR (vtable dispatch deferred)
- **Stdlib base** (`stdlib/`)
  - `result.esk`: `struct Result<T,E>` + `Ok<T,E>` / `Err<T,E>` template constructors
  - `math.esk`: `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` (extern to libm)
  - `io.esk`: `printf`, `fprintf`, `sprintf`, `scanf`, `puts`, `getchar`, `putchar`
  - `mem.esk`: `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`, `memchr`

### Fixed
- Parser: `int fn<T>(T x)` at top level was silently dropped; `parseDeclaration()` now detects `<` after a name as a function template
- Type checker: template function bodies were visited with unresolved type params; now guarded with `typeParams.empty()` check
- `substType`: did not substitute type params inside `Name<T,E>` template strings; now handles nested template types recursively

---

## [0.0.4-alpha]

### Added
- **Template struct declaration**: `struct Result<T, E> { ... }`; `StructDecl` gains `typeParams` field; template declarations are stored in a separate registry and not emitted to LLVM until first use
- **Template type references**: `Result<int, string>` parsed in `parseType()` via `IDENT < TYPE, ... >` lookahead; stored as `"Result<int,string>"` in the AST
- **Lazy instantiation in type checker**: `normalizeType("Result<int,string>")` detects `<`, looks up the template, substitutes `T→int` / `E→string` in all field types, and registers `"Result_int_string"` as a concrete struct
- **Lazy instantiation in codegen**: `getTypeFromString("Result<int,string>")` calls `ensureTemplateInstantiated()` which creates `%Result_int_string = type { i32, i32, ptr }` on first use
- **Name mangling**: `Result<int,string>` → `Result_int_string` in LLVM IR
- **Type substitution**: `substType` handles `*T` → `*int`, `T*` → `int*`, `T[N]` → `int[N]`, and nested template types recursively
- **`varTypeStack` normalization**: `VarDecl` stores the mangled name so `MemberExpr` resolution finds instantiated struct fields correctly

### Fixed
- Parser: `int fn<T>(...)` at top level was silently dropped; `parseDeclaration()` now detects `<` after a function name as a template parameter list
- Type checker: template bodies were visited with unresolved type parameters; now guarded so only concrete instantiations are type-checked

---

## [0.0.3-alpha]

### Added
- **`alloc(T, N)`**: new `AllocExpr` AST node; emits `call @malloc(i64 N * sizeof(T))`; `sizeof(T)` resolved via `DataLayout` initialized at the start of `generateCode()`
- **`free(ptr)`**: parsed as a regular call; `free` pre-registered in the type checker as variadic; auto-declared in the LLVM module on first use, no explicit `extern` required
- **`getOrDeclareFunc()`**: codegen helper that lazily declares C runtime functions (`malloc`, `free`) into the module without requiring explicit `extern` declarations

### Fixed
- `validateStructType` did not strip the leading `*T` pointer prefix, causing false "undefined struct" errors on `*uint8`, `*Point`, etc.
- `inferBinaryExprType` did not handle the `=` operator for non-numeric types; assignment to pointer fields and struct fields now type-checks correctly
- `isValidAssignment` now allows any-pointer-to-any-pointer assignment for C interop

---

## [0.0.2-alpha]

### Added
- **Struct codegen**: `llvm::StructType::create` per `StructDecl`; `alloca %StructType` for locals; field read/write via `getelementptr`
- **Fixed-size array fields**: `uint8[858]` → `[858 x i8]`; parser now captures the array size (was previously discarded); `IndexExpr` codegen via GEP for both `T[N]` and `*T`
- **Struct literal initialization**: named (`Point { x: 1.5, y: 2.5 }`) and positional (`Point { 1.5, 2.5 }`); fills alloca directly with per-field type coercion
- **Method calls**: methods emitted as `StructName_methodName(ptr self, ...)` mangled functions; `p.method(args)` detects and prepends an implicit `self` pointer
- **`StructInitExpr` AST node**, full visitor chain: parser, type checker, codegen, AST printer
- **`BreakStmt` codegen**: `CreateBr(breakTarget)`; target saved/restored around loop bodies
- **`emitObjectFile()`**: native `.o` via LLVM `TargetMachine` + `legacy::PassManager`; full pipeline `eskiuc file.esk -o file.o` produces a linkable object file
- **Float arithmetic**: `+`, `-`, `*` now emit `fadd`/`fsub`/`fmul` for floating-point operands (previously always emitted integer instructions)

### Fixed
- Type checker: method bodies were not registered and type-checked in the first pass
- Type checker: `*T` (leading-pointer) was not auto-derefed on member access; now strips the leading `*` before struct field lookup
- Type checker: `isValidAssignment` normalizes both sides so `"Point" == "struct:Point"`
- Type checker: function parameter types were stored as parameter names (e.g. `"a"`) instead of type strings (e.g. `"int"`)
- Type checker: variadic functions incorrectly rejected call sites with more arguments than fixed params
- Codegen: `varTypeStack` now scoped alongside the symbol table for correct struct type resolution across nested scopes

---

## [0.0.1-alpha]

### Added
- **Phase 0: Build system and CLI**. CMake build with LLVM 17+ integration; `--version` flag; `--test-lexer`, `--test-parser`, `--test-typechecker`, `--test-codegen` modes; `file:line:col` error reporting
- **Phase 1: Lexer**. Complete tokenizer with line/col tracking; all Eskiu keywords (`int`, `float`, `uint8`–`uint64`, `int8`–`int64`, `struct`, `interface`, `enum`, `alloc`, `free`, `extern`, `thread`, `try`/`catch`/`finally`, and more); `COLON` token for type annotations
- **Phase 2: Parser**. Recursive-descent parser producing a visitor-based AST; functions, variables (`let x: int = 5` and C-style `int x = 5` both accepted), structs with fields and methods, `extern` declarations, full control flow (`if`/`else`, `for`, `while`, `break`, `return`), expressions with correct precedence, cast expressions `(TYPE)expr`
- **Phase 3: Codegen**. LLVM IRBuilder backend; arithmetic, comparison, and logical operators; `if`/`else`, `while`, `for`; function calls; integer and float literals; type coercion on initializers; correct lvalue/rvalue split (`evaluateLValue`) so assignments emit `store` to an `alloca` rather than to a value
- **Phase 4: Type checker**. Scope-aware analysis; type inference for all binary and unary operators; struct field validation; function signature checking; `MemberExpr` member-type resolution; parameters registered before the validation pass
- **Types**: `uint8`/`uint16`/`uint32`/`uint64` and `int8`/`int16`/`int32`/`int64` as first-class types mapped to LLVM `i8`–`i64`; `bool` → `i1`; `char` → `i8`; `string` → `i8*`
- **Pointer types**: both leading `*T` and trailing `T*` syntax accepted throughout lexer, parser, and type checker
- **Examples**: `examples/hello.esk`, `examples/test_struct.esk`, `examples/test_struct_error.esk`

### Fixed
- Lexer: `COLON` token was not recognized, breaking `let`-style type annotations
- Type checker: function parameters were not registered before the body validation pass, causing false "undeclared identifier" errors

### Known limitations (resolved in later releases)
- Struct codegen not wired (Phase 5); `MemberExpr` type-checks but does not emit IR
- No heap allocation (`alloc`/`free`), stack only (Phase 6)
- No interfaces or templates (Phase 5)
- No standard library or `Result<T,E>` (Phase 7)
- No lambdas, threads, or async (Phase 8+)
