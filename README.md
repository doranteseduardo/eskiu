<p align="center">
  <img src="assets/logo.png" alt="Eskiu" width="140">
</p>

<h2 align="center">eskiu</h2>
<p align="center">A systems language with C-style syntax, structural interfaces, and explicit memory — compiled to native via LLVM.</p>

<p align="center">
  <a href="https://eskiu-lang.org">eskiu-lang.org</a> &nbsp;&middot;&nbsp;
  <a href="docs/lang/getting-started.md">Documentation</a> &nbsp;&middot;&nbsp;
  <a href="QUICKSTART.md">Quickstart</a> &nbsp;&middot;&nbsp;
  <a href="CHANGELOG.md">Changelog</a>
</p>

---

## What it looks like

```eskiu
extern int printf(string fmt, ...);

interface Drawable { void draw(); }

struct Circle {
    float radius;
    void draw() { printf("Circle(r=%f)\n", self.radius); }
}

int apply(fn(int)->int f, int x) { return f(x); }

int main() {
    // Structural interface dispatch — no implements keyword
    let c: Circle = Circle { radius: 5.0 };
    render(&c);

    // Lambda — anonymous function, C-like syntax
    let square: fn(int)->int = int(int n) { return n * n; };
    printf("%d\n", apply(square, 6));  // 36

    // Heap allocation
    let buf: *uint8 = alloc(uint8, 1024);
    buf[0] = 0xFF;
    free(buf);

    return 0;
}
```

## Get started

```bash
git clone https://github.com/doranteseduardo/eskiu && cd eskiu
cmake -S . -B build && cmake --build build -j$(nproc)
./build/eskiuc examples/hello.esk -o hello.o && clang hello.o -o hello && ./hello
```

Full installation guide: [QUICKSTART.md](QUICKSTART.md)

## Performance

A real-world cryptographic pipeline — AES-256-CBC + RSA-8192 decryption, image processing, structured output — running entirely in Eskiu via `extern` C interop.

| Stage | Eskiu | Reference C |
|---|---|---|
| Image / QR extraction | 71.7 ms | 185.5 ms |
| Crypto pipeline | 2.8 ms | 2.9 ms |
| Output decode | < 1 ms | 0.5 ms |
| **Total** | **74.4 ms** | **188.9 ms** |

**2.5× faster** than reference C. The crypto stage matches hand-written C within 0.1 ms.

## Language features

| Category | What's included |
|---|---|
| **Types** | `int/8/16/32/64`, `uint`, `float`, `double`, `bool`, `char`, `string`, `void`, `*T` pointers |
| **Functions** | C-style, `extern` C ABI, variadic, template `fn<T>(T x)` |
| **Lambdas** | `int(int x) { return x * 2; }` · `fn(T)->R` function pointer types · higher-order functions |
| **Structs** | Fields, methods with implicit `self`, named/positional initialisers |
| **Interfaces** | Structural typing, vtable dispatch, no `implements` keyword |
| **Templates** | `Result<T,E>`, `List<T>` — monomorphic instantiation, zero overhead |
| **Memory** | `alloc(T, N)` / `free(ptr)` · pointer arithmetic · no GC |
| **Control flow** | `if/else`, `for`, `while`, `switch/case` (with type checking), `break`, `continue` |
| **Multi-file** | `import "path/to/file.esk"` — relative, parsed once |
| **stdlib** | `Result`, `List<T>`, `String`, `math`, `io`, `mem` |
| **argv/argc** | `int main(int argc, string* argv)` works out of the box |

## CLI

```bash
eskiuc hello.esk -o hello.o            # compile to object file
eskiuc hello.esk --test-lexer          # dump token stream
eskiuc hello.esk --test-parser         # dump AST
eskiuc hello.esk --test-typechecker    # type check only
eskiuc hello.esk --test-codegen        # dump LLVM IR
eskiuc hello.esk --hover-at 8:12       # type at cursor (VS Code)
eskiuc hello.esk --definition-at 8:12  # go-to-definition (VS Code)
```

## Compiler pipeline

```
.esk source  →  Lexer  →  Parser  →  TypeChecker  →  CodeGen  →  .o
                                           ↑
                                    sema/type_checker.cpp
                                    codegen/codegen.cpp
```

All phases complete. Targets arm64 and x86-64 via LLVM.

## VS Code extension

```bash
ln -s $(pwd)/editor/vscode ~/.vscode/extensions/eskiu-language
```

Provides syntax highlighting, real-time error squiggles, hover type info, and go-to-definition — powered by the compiler, no separate language server.

## Requirements

- LLVM 17+ (tested with LLVM 22)
- C++17 compiler (clang++ or g++)
- CMake 3.20+

## Documentation

| Guide | Contents |
|---|---|
| [QUICKSTART.md](QUICKSTART.md) | Build the compiler and run your first program in 5 minutes |
| [docs/lang/getting-started.md](docs/lang/getting-started.md) | Hands-on tutorial covering all language features |
| [docs/lang/spec.md](docs/lang/spec.md) | Complete language reference |
| [docs/dev/architecture.md](docs/dev/architecture.md) | Compiler internals walkthrough |
| [docs/dev/phases.md](docs/dev/phases.md) | Feature status and roadmap |

## Licence

MIT — see [LICENSE](LICENSE)
