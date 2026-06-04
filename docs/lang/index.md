# Eskiu Language Reference

Eskiu is a statically typed systems language built to address a specific problem: compute-intensive services typically pull in C for performance-critical work, Go for concurrency, C++ for libraries, and Python for glue — each with its own toolchain and interop cost.

The goal is a single language that replaces that stack. Phase one establishes a solid systems foundation: native performance, explicit memory, direct access to any C library. Phase two, once that foundation is stable, will introduce first-class support for the domain types that high-throughput services actually work with. The syntax is deliberately C-style; the language adds monomorphic templates, structural interfaces, lambdas, and an explicit heap model via `alloc`/`free`.

**Current version: v0.1.1-alpha** — closures with capture-by-value semantics and `thread_create`/`thread_join` thread primitives. v0.1 milestone complete.

---

## Documentation Map

| Document | What it covers | Start here if… |
|---|---|---|
| [getting-started.md](getting-started.md) | Hands-on tutorial — build, hello world, all features | You're new to Eskiu |
| [spec.md](spec.md) | Complete language reference | You need exact syntax or semantics |
| [build.md](build.md) | Install the compiler on macOS / Linux | You need to set up a dev environment |
| [../../docs/GLOSSARY.md](../GLOSSARY.md) | Terminology definitions | You encounter an unfamiliar term |

---

## Feature Summary

| Category | Features |
|---|---|
| **Types** | `int`, `int8`–`int64`, `uint`–`uint64`, `float`, `double`, `bool`, `char`, `string`, `void`, `*T` pointers, `T[N]` fixed arrays |
| **Literals** | Decimal, hex `0xFF`, float, string (with adjacent concat), char, bool, null |
| **Operators** | Arithmetic `+ - * / %`, bitwise `& \| ^ ~ << >>`, comparison, logical, compound `+= -=` etc., pointer arith `ptr+n` |
| **Control flow** | `if`/`else`, `for` (with decl init), `while`, `switch`/`case`, `break`, `continue`, `return` |
| **Functions** | C-style, `extern` C ABI, variadic, template `fn<T>(T x)` |
| **Lambdas / Closures** | `int(int x) { return x * 2; }` — anonymous functions; `fn(T,...)->R` fat-pointer types; closure capture by value; higher-order functions |
| **Threads** | `thread_create(fn()->void)` / `thread_join(*void)` — OS thread keywords; closure fat pointer maps directly to pthread ABI |
| **Structs** | Fields, methods with `self`, struct literal init `Point { x: 1, y: 2 }` |
| **Templates** | `struct Result<T,E>`, `fn Ok<T,E>(T v)` — monomorphic instantiation |
| **Interfaces** | `interface Drawable { void draw(); }` — structural typing, vtable dispatch |
| **Memory** | Stack default, `alloc(T,N)` / `free(ptr)`, pointer arithmetic |
| **volatile** | `volatile let reg: *uint8 = (uint8*) 0x3F8;` — MMIO-safe loads/stores |
| **Inline asm** | `asm("cli");` simple form; `asm("outb %0, %1" :: "a"(v), "Nd"(p) : "memory");` extended form |
| **Freestanding** | `--freestanding` flag — `alloc`/`free` call `esk_alloc`/`esk_free`; user-supplied in kernel |
| **Cross-compile** | `--target TRIPLE` — AArch64 and X86 backends included |
| **Multi-file** | `import "path/to/file.esk"` — relative to importing file |
| **Errors** | `file.esk:8:22: message` — real line/col from parser |
| **Stdlib** | `result.esk`, `list.esk`, `string.esk`, `math.esk`, `io.esk`, `mem.esk` |

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

| Flag | Action |
|---|---|
| `-o file.o`                 | Compile to native object file                   |
| `--target TRIPLE`           | Cross-compile for the given target triple        |
| `--freestanding`            | Use `esk_alloc`/`esk_free` instead of libc      |
| `--test-lexer`              | Print token stream                              |
| `--test-parser`             | Print AST                                       |
| `--test-typechecker`        | Type check and report errors                    |
| `--test-codegen`            | Print LLVM IR                                   |
| `--hover-at LINE:COL`       | Print inferred type of expression at position   |
| `--definition-at LINE:COL`  | Print definition location of symbol at position |
| `--version`                 | Print version                                   |
