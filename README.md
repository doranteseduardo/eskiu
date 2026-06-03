<p align="center">
  <img src="assets/logo.png" alt="Eskiu" width="180">
</p>

<h1 align="center">Eskiu</h1>
<p align="center">A systems language with C-style syntax, Go-style interfaces, and explicit memory — compiled to native via LLVM.</p>

## Quick Start

```bash
git clone https://github.com/doranteseduardo/eskiu && cd eskiu
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

## Real-World Benchmark

Eskiu was built to port a cryptographic image processing pipeline from a 3–5 second reference implementation to under 1 second. The v0.1 decoder implements the full pipeline in Eskiu — image decoding via CoreGraphics + zxing-cpp (extern), multi-round AES-256-CBC + RSA-8192 cryptography (OpenSSL externs), and structured data extraction — with no C pipeline code.

| Stage | Eskiu | Reference C |
|---|---|---|
| Image / QR extraction | 78 ms | 185 ms |
| Crypto pipeline | 2 ms | 3 ms |
| Output decode | < 1 ms | 1 ms |
| **Total** | **80 ms** | **188 ms** |

**2.4× faster** than the reference, **37–75× faster** than the original target.  
The crypto pipeline matches hand-written C within 1 ms.

## Architecture

```
Stage              Component                  Status
-----              ---------                  ------
Source             .esk file                  --
Lexer              lexer/lexer.cpp            complete
Parser             parser/parser.cpp          complete
Type Checker       sema/type_checker.cpp      complete
Code Generator     codegen/codegen.cpp        complete
Object File        emitObjectFile()           complete
Structs / Methods  Phase 5                    complete — fields, init, methods
Interfaces         Phase 5.5                  complete — vtable dispatch + structural typing
Templates          Phase 5.5                  complete — struct + function templates
Heap / alloc/free  Phase 6                    complete
stdlib             Phase 7                    complete — Result, List, String, math/io/mem
Lambdas            Phase 8                    complete — fn(T,...)->R type, anonymous functions
VS Code extension  tooling                    complete — syntax highlighting, errors, hover, goto-def
```

## Language Features

```eskiu
// Structs with methods
struct Point {
    float x;
    float y;
    float dist(Point other) { ... }
}

// Templates
struct Result<T, E> { int ok; T value; E error; }
Result<int, string> r = Ok<int, string>(42);

// Interfaces (structural typing, no implements keyword)
interface Drawable { void draw(); }
void render(Drawable d) { d.draw(); }
render(&myCircle);  // auto-boxed

// Lambdas and function pointers
let double_it: fn(int)->int = int(int x) { return x * 2; };
int result = double_it(5);  // 10

int apply(fn(int)->int f, int x) { return f(x); }
apply(double_it, 4);  // 8

// Multi-file
import "stdlib/result.esk";

// Heap allocation
let buf: *uint8 = alloc(uint8, 1024);
buf[0] = 0xFF;
free(buf);
```

## CLI Flags

| Flag                      | Action                                    |
|---------------------------|-------------------------------------------|
| `--test-lexer`            | Print token stream                        |
| `--test-parser`           | Print AST                                 |
| `--test-typechecker`      | Type check and report errors              |
| `--test-codegen`          | Print LLVM IR                             |
| `-o name`                 | Compile and link to executable            |
| `--hover-at LINE:COL`     | Print type of expression at position      |
| `--definition-at LINE:COL`| Print definition location of symbol       |
| `--version`               | Print version                             |

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
