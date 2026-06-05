# Examples

This directory contains real Eskiu programs demonstrating language features.

## How to Run

Compile any example with `eskiuc`, link with `clang`, then run:

```bash
eskiuc examples/hello.esk -o hello.o && clang hello.o -o hello && ./hello
```

Substitute the filename and output names as needed. The compiler binary is `eskiuc` — there is no `eskiu compile` subcommand.

---

## Examples

### [hello.esk](hello.esk)

Demonstrates `extern` C function declarations, top-level functions, integer types, and basic arithmetic.

```bash
eskiuc examples/hello.esk -o hello.o && clang hello.o -o hello && ./hello
# Hello from Eskiu!
# Result: 8
```

Key concepts:
- `extern int printf(string fmt, ...)` — declare a C function for use in Eskiu
- Defining and calling a plain function (`add`)
- `int main()` as the program entry point

---

### [test_struct.esk](test_struct.esk)

Demonstrates struct definitions with float fields and `let`-style variable declarations with member access.

```bash
eskiuc examples/test_struct.esk -o test_struct.o && clang test_struct.o -o test_struct && ./test_struct
```

Key concepts:
- `struct` with named float fields
- `let p: Point` — typed variable declaration using a struct type
- `.` operator for reading struct members

---

### [test_struct_error.esk](test_struct_error.esk)

Demonstrates the type checker catching an access to a field that does not exist on a struct. This example is intentionally invalid — it does **not** compile successfully.

```bash
eskiuc examples/test_struct_error.esk --test-typechecker
# error: file.esk:8: unknown field 'z' on struct 'Point'
```

Key concepts:
- Struct field validation at compile time
- Reading type checker error output

---

## Inspection Modes

Pass one of these flags instead of `-o` to inspect a compilation stage without producing an object file:

| Flag | What it shows |
|------|---------------|
| `--test-lexer` | Token stream produced by the lexer |
| `--test-parser` | Parsed AST printed to stdout |
| `--test-typechecker` | Type checker output and any errors |
| `--test-codegen` | Generated LLVM IR |

Example:

```bash
eskiuc examples/hello.esk --test-codegen
```

---

## What to Try Next

Once the basics work, try writing:

- A struct with methods — define a function that takes a `Point` and computes its distance from the origin
- A generic container — use a template type parameter: `struct Box<T> { T value; }`
- An interface — declare an interface and implement it on a struct to see structural typing in action

Language reference: [docs/lang/spec.md](../docs/lang/spec.md)
