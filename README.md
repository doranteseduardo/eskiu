<p align="center">
  <img src="assets/logo.jpg" alt="Eskiu" width="640">
</p>

A systems programming language compiled to LLVM IR. C-style syntax, no garbage collector, no borrow checker. Built to port a real INE QR decoder from 3-5 seconds to under 1 second.

## Quick Start

```bash
git clone https://github.com/yourusername/eskiu && cd eskiu
cmake -S . -B build && cmake --build build -j$(nproc)
./build/eskiuc examples/hello.esk --test-codegen
```

## Example

```eskiu
extern int printf(string fmt, ...);

int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(5, 3);
    printf("Hello from Eskiu!\n");
    printf("Result: %d\n", result);
    return 0;
}
```

## Architecture

```
Stage            Component                Status
-----            ---------                ------
Source           .esk file                --
Lexer            lexer/lexer.cpp          complete
Parser           parser/parser.cpp        complete
Type Checker     sema/type_checker.cpp    complete
Code Generator   codegen/codegen.cpp      complete
LLVM Backend     LLVM IRBuilder           complete
Structs/Templates Phase 5                 in progress
Heap/Alloc       Phase 6                  planned
stdlib/Result<T> Phase 7                  planned
```

## CLI Flags

| Flag                   | Action                          |
|------------------------|---------------------------------|
| `--test-lexer`         | Print token stream              |
| `--test-parser`        | Print AST                       |
| `--test-typechecker`   | Type check and report errors    |
| `--test-codegen`       | Print LLVM IR                   |
| `--version`            | Print version                   |

## Roadmap

| Version | Focus                                                        |
|---------|--------------------------------------------------------------|
| v0.1    | Structs with array fields, `alloc`/`free`, extern C ABI, `Result<T,E>` |
| v0.2    | Templates (monomorphic), Go-style interfaces, full stdlib foundation |
| v1.0    | Exceptions, complete stdlib, self-hosting bootstrap target   |
| v2.0    | Lambdas, async/await, threads, package manager               |

## Type System

| Eskiu type               | LLVM type |
|--------------------------|-----------|
| `int`, `int32`           | `i32`     |
| `int8`, `int16`, `int64` | `i8` `i16` `i64` |
| `uint`, `uint8` … `uint64` | unsigned equivalents |
| `float`                  | `float`   |
| `double`                 | `double`  |
| `bool`                   | `i1`      |
| `char`                   | `i8`      |
| `string`                 | `i8*`     |
| `*T` or `T*`             | pointer   |

## Requirements

- LLVM 17+ (tested with LLVM 22)
- C++17 compiler (clang++ or g++)
- CMake 3.20+

## Documentation

- Language reference and syntax: [docs/lang/](docs/lang/)
- Contributor and internals guide: [docs/dev/](docs/dev/)
- Architecture walkthrough: [docs/dev/architecture.md](docs/dev/architecture.md)
- Build instructions (macOS, Linux, Alpine): [docs/lang/build.md](docs/lang/build.md)
- Phase roadmap detail: [docs/dev/phases.md](docs/dev/phases.md)
