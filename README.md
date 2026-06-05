<p align="center">
  <img src="assets/logo.png" alt="Eskiu" width="140">
</p>

<h2 align="center">eskiu</h2>
<p align="center">C performance. Go concurrency. Explicit memory.</p>
<p align="center">
  <a href="https://eskiu-lang.org">eskiu-lang.org</a> &nbsp;&middot;&nbsp;
  <a href="docs/lang/getting-started.md">Documentation</a> &nbsp;&middot;&nbsp;
  <a href="QUICKSTART.md">Quickstart</a> &nbsp;&middot;&nbsp;
  <a href="CHANGELOG.md">Changelog</a>
</p>

---

Eskiu is a statically typed systems language that compiles to native code via LLVM. C-style syntax, structural interfaces, monomorphic templates, and explicit memory — no garbage collector, no runtime.

**v0.1:** a bare-metal ARM64 kernel written in Eskiu boots in QEMU without libc.

<p align="center">
  <img src="assets/kernel.png" alt="Eskiu v0.1 kernel running in QEMU" width="320">
</p>

## Get started

```bash
git clone https://github.com/doranteseduardo/eskiu && cd eskiu
cmake -S . -B build && cmake --build build
./build/eskiuc examples/hello.esk -o hello && ./hello
```

## Example

```eskiu
extern int printf(string fmt, ...);

interface Drawable { void draw(); }

struct Circle {
    float radius;
    void draw() { printf("Circle(r=%f)\n", self.radius); }
}

void render(Drawable d) { d.draw(); }

int apply(fn(int)->int f, int x) { return f(x); }

int main() {
    let c: Circle = Circle { radius: 5.0 };
    render(&c);                                     // Circle(r=5.000000)

    let square: fn(int)->int = int(int n) { return n * n; };
    printf("%d\n", apply(square, 6));               // 36

    return 0;
}
```

## Requirements

- LLVM 17+ (tested with LLVM 22)
- C++17 compiler
- CMake 3.20+
- A C toolchain (`cc`/`clang`/`gcc`) — `eskiuc` invokes it to link executables (not needed for `--freestanding`)

## Documentation

| | |
|---|---|
| [QUICKSTART.md](QUICKSTART.md) | Build and run your first program in 5 minutes |
| [docs/lang/getting-started.md](docs/lang/getting-started.md) | Language tutorial |
| [docs/lang/spec.md](docs/lang/spec.md) | Full language reference |
| [docs/dev/phases.md](docs/dev/phases.md) | Feature status and roadmap |

## Licence

MIT — see [LICENSE](LICENSE)
