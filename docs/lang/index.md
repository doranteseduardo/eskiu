# Eskiu Language Reference

Eskiu is a statically typed systems language with the power of C and the immediacy of a scripting language: it compiles to native code through LLVM, and `eskiuc run file.esk` runs a source file directly, like Python or Ruby. This is the full language reference. New to Eskiu? Start with the [tutorial](getting-started.md).

**Current version: v0.3.1.** The self-hosting milestone: the compiler is reimplemented in
Eskiu itself (`selfhost/`), reaching a 3-stage bootstrap fixpoint and a code generator
feature-complete against the reference C++ compiler.

---

## Documentation Map

| Document | What it covers | Start here if… |
|---|---|---|
| [getting-started.md](getting-started.md) | Hands-on tutorial — build, hello world, all features | You're new to Eskiu |
| [spec.md](spec.md) | Complete language reference | You need exact syntax or semantics |
| [grammar.md](grammar.md) | Formal EBNF grammar | You need the precise concrete syntax |
| [build.md](build.md) | Install the compiler on macOS / Linux | You need to set up a dev environment |
| [../GLOSSARY.md](../GLOSSARY.md) | Terminology definitions | You encounter an unfamiliar term |

---

## Feature Summary

| Category | Features |
|---|---|
| **Types** | `int`, `int8`–`int64`, `uint`–`uint64`, `float`, `double`, `bool`, `char`, `string`, `void`, `*T` pointers, `T[N]` fixed arrays, `union` |
| **Literals** | Decimal, hex `0xFF`, float, string (with adjacent concat), char, bool, null |
| **Operators** | Arithmetic `+ - * / %`, bitwise `& \| ^ ~ << >>`, comparison, logical, compound `+= -=` etc., typed pointer arith `ptr+n` (advances by `sizeof(*ptr)`), `sizeof(T)` |
| **Control flow** | `if`/`else`, `for` (with decl init), `while`, `switch`/`case`, `break`, `continue`, `return` |
| **Functions** | C-style, `extern` C ABI, variadic, template `fn<T>(T x)` |
| **Lambdas / Closures** | `int(int x) { return x * 2; }` — anonymous functions; `fn(T,...)->R` fat-pointer types; closure capture by value; higher-order functions |
| **Threads** | `thread_create(fn()->void)` / `thread_join(*void)` — OS thread keywords; closure fat pointer maps directly to pthread ABI |
| **Exceptions** | `try`/`catch`/`finally`/`throw` — LLVM `invoke`/`landingpad`, Itanium ABI; multiple `catch` clauses; link `-lc++` (macOS) or `-lstdc++` (Linux) |
| **Async / await** | `async` function lowered to a resumable state-machine coroutine; a call yields `*Future<T>`; `await` suspends until ready. Runtime in `<future>`/`<executor>`/`<eventloop>` |
| **Enums / match** | `enum` of named integer constants; payload-bearing variants form algebraic data types (tagged unions), destructured with exhaustive `match`; generic enums (`Option<T>`, `Either<A,B>`) |
| **Structs** | Fields, methods with `self`, struct literal init `Point { x: 1, y: 2 }`, bitfields `f: N`, `packed` layout |
| **Templates** | `struct Result<T,E>`, `fn Ok<T,E>(T v)` — monomorphic instantiation; bounded generics `<T: Iface>` / `<T: A + B>` (a primitive satisfies a constraint via a free function) |
| **Collections** | `<list>` `List<T>`, `<map>` `Map<V>` (string-keyed) / `HashMap<K,V>`, `<string>` `String`, `<bytes>` binary-safe `Bytes` buffer |
| **Interfaces** | `interface Drawable { void draw(); }` — structural typing, vtable dispatch |
| **Memory** | Stack default; heap via `<mem>` `alloc<T>(n)` / `free`; explicit allocators `<alloc>` via `alloc_with`; typed pointer arithmetic |
| **volatile** | `volatile let reg: *uint8 = (uint8*) 0x3F8;` — MMIO-safe loads/stores |
| **Inline asm** | `asm("cli");` simple form; `asm("outb %0, %1" :: "a"(v), "Nd"(p) : "memory");` extended form |
| **Freestanding** | `--freestanding` flag — `<mem>` `alloc<T>`/`free` call `esk_alloc`/`esk_free`; user-supplied in kernel |
| **Cross-compile** | `--target TRIPLE` — AArch64 and X86 backends included |
| **Multi-file** | `import <result>` stdlib modules · `import "file.esk"` relative local files |
| **Errors** | `file.esk:8:22: message` — real line/col from parser |
| **HTTP/2** | `<http2>` frame codec + connection/stream state machine, `<hpack>` header compression, `<tls>` (h2 over OpenSSL with ALPN), `<http2_server>` (h2c) — full multiplexed HTTP/2 stack |
| **Stdlib** | Core (`<result>`, `<list>`, `<string>`, `<math>`, `<io>`, `<mem>`, `<fs>`, `<path>`, `<base64>`, `<json>`, `<time>`, `<env>`); memory (`<alloc>`, `<sysheap>`); concurrency (`<threading>`, `<atomic>`); the async runtime (`<eventloop>`, `<future>`, `<executor>`, `<net_async>`, `<timer>`, `<channel>`, `<futureval>`); sum types (`<either>`); networking (`<net>`, `<http>`, `<http_async>`); and the HTTP/2 stack (`<http2>`, `<hpack>`, `<tls>`, `<http2_server>`) |

---

## Quick Reference

### Type → LLVM

| Eskiu | LLVM | Eskiu | LLVM |
|---|---|---|---|
| `int` / `int32` | `i32` | `uint` / `uint32` | `i32` |
| `int8` | `i8` | `uint8` | `i8` |
| `int16` | `i16` | `uint16` | `i16` |
| `int64` | `i64` | `uint64` | `i64` |
| `float` | `float` | `double` | `double` |
| `bool` | `i1` | `char` | `i8` |
| `string` | `i8*` | `*T` / `T*` | `ptr` |

### Operator precedence (lowest → highest)

```
assignment  = += -= *= /= %=
logical     || &&
bitwise     | ^ &
equality    == !=
relational  < > <= >=
shift       << >>
additive    + -
multiplicative * / %
unary       ! - ~ & * (TYPE)
postfix     f() a[i] a.b
```

### CLI flags

| Flag / subcommand | Action |
|---|---|
| `-o prog` / `-o file.o`     | Link an executable / emit an object file (by suffix) |
| `-c`                        | Compile to an object file only                  |
| `-l<lib>` / `-L<path>`      | Pass library / search-path flags to the linker  |
| `eskiuc run file.esk`       | Compile to a temp executable and run it         |
| `eskiuc fmt file.esk`       | Reformat source in place                        |
| `-Wall` / `-Wextra`         | Lint warnings / signed-unsigned comparison warnings |
| `--asan` / `--ubsan`        | AddressSanitizer / trapping bounds checks       |
| `--target TRIPLE`           | Cross-compile for the given target triple        |
| `--freestanding`            | Use `esk_alloc`/`esk_free` instead of libc      |
| `--test-lexer`              | Print token stream                              |
| `--test-parser`             | Print AST                                       |
| `--test-typechecker`        | Type check and report errors                    |
| `--test-codegen`            | Print LLVM IR                                   |
| `--hover-at LINE:COL`       | Print inferred type of expression at position   |
| `--definition-at LINE:COL`  | Print definition location of symbol at position |
| `--version`                 | Print version                                   |
