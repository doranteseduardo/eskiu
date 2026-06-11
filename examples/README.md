# Examples

Real Eskiu programs demonstrating language features and the standard library.

## How to Run

Compile and run any example in one step — when the `-o` name has no `.o`
extension, `eskiuc` links the executable for you (via your system C toolchain):

```bash
eskiuc examples/hello.esk -o hello && ./hello
# Hello from Eskiu!
# Result: 8
```

Prefer to keep the object file? Give the output a `.o` name (or pass `-c`) and
link it yourself:

```bash
eskiuc examples/hello.esk -o hello.o && clang hello.o -o hello && ./hello
```

The compiler binary is `eskiuc` — there is no `eskiu compile` subcommand. Examples
that `import <...>` a stdlib module need the compiler to find the stdlib; running
from the repo root works out of the box (or set `ESKIU_ROOT=.`).

---

## What's here

| Example | Demonstrates | stdlib |
|---------|--------------|--------|
| [hello.esk](hello.esk) | `extern` C declarations, plain functions, `int main()`, arithmetic | — |
| [argv.esk](argv.esk) | command-line arguments: `int main(int argc, string* argv)`, `argv[i]` | — |
| [structs.esk](structs.esk) | structs with methods, `self`, field access | — |
| [interfaces.esk](interfaces.esk) | structural interfaces (no `implements` keyword) — any struct with the methods satisfies them | — |
| [generics.esk](generics.esk) | templates: the generic `List<T>`, template functions and structs | — |
| [result.esk](result.esk) | `Result<T, E>` error-as-value with `Ok`/`Err` | `<result>` |
| [json.esk](json.esk) | build a JSON document and parse it back | `<json>` |
| [tcp_echo_server.esk](tcp_echo_server.esk) | a TCP echo server over the POSIX socket API | `<net>` |
| [http_server.esk](http_server.esk) | a concurrent HTTP/1.1 server with a worker pool | `<http>` |
| [async.esk](async.esk) | `async`/`await` — a coroutine that suspends on I/O over the event loop | `<future>` `<net_async>` `<eventloop>` |

The network examples bind a local port — run them in one terminal and connect
from another (e.g. `curl localhost:PORT` or `nc localhost PORT`).

---

## A first walkthrough: `hello.esk`

```bash
eskiuc examples/hello.esk -o hello && ./hello
# Hello from Eskiu!
# Result: 8
```

Key concepts:
- `extern int printf(string fmt, ...)` — declare a C function for use in Eskiu
- defining and calling a plain function (`add`)
- `int main()` as the program entry point

---

## Inspection Modes

Pass one of these flags instead of `-o` to inspect a compilation stage without
producing an object file:

| Flag | What it shows |
|------|---------------|
| `--test-lexer` | Token stream produced by the lexer |
| `--test-parser` | Parsed AST |
| `--test-typechecker` | Type-checker output and any errors (`file.esk:line:col: message`) |
| `--test-codegen` | Generated LLVM IR |

```bash
eskiuc examples/hello.esk --test-codegen
```

For examples of code the compiler should **reject** (and the diagnostics it
emits), see [`tests/errors/`](../tests/errors).

---

## What to try next

- Add a method to a struct that takes another struct and returns a computed value.
- Write a generic container: `struct Box<T> { T value; }` and a `Box_get<T>`.
- Declare an `interface` and satisfy it from two unrelated structs.

Language reference: [docs/lang/spec.md](../docs/lang/spec.md) ·
Getting started: [docs/lang/getting-started.md](../docs/lang/getting-started.md)
