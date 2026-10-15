<p align="center">
  <img src="assets/logo.png" alt="Eskiu, a kitten with a ball of yarn" width="140">
</p>

<h2 align="center">eskiu</h2>
<p align="center">A self-hosting systems language with the power of C and the immediacy of a scripting language.</p>
<p align="center"><sub>Part of the <a href="https://reactvision.xyz">ReactVision</a> family</sub></p>
<p align="center">
  <a href="https://eskiu-lang.org">eskiu-lang.org</a> &nbsp;&middot;&nbsp;
  <a href="docs/lang/getting-started.md">Documentation</a> &nbsp;&middot;&nbsp;
  <a href="QUICKSTART.md">Quickstart</a> &nbsp;&middot;&nbsp;
  <a href="CHANGELOG.md">Changelog</a>
</p>

<p align="center">
  <a href="https://github.com/doranteseduardo/eskiu/actions/workflows/ci.yml"><img src="https://github.com/doranteseduardo/eskiu/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="CHANGELOG.md"><img src="https://img.shields.io/badge/version-0.11.0-6448d4" alt="Version 0.11.0"></a>
  <img src="https://img.shields.io/badge/LLVM-21%2B-orange" alt="LLVM 21+">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License: MIT"></a>
</p>

---

Eskiu is a systems language with C's control over memory and calling conventions, and a
quicker feel for everyday work. It compiles to native code through LLVM and calls C
libraries directly, and `eskiuc run file.esk` (or a `#!/usr/bin/env -S eskiuc run` shebang)
runs a source file the way you would run a Python or Ruby script.

It is used in production at [ReactVision](https://reactvision.xyz): the
[ViroReact](https://eskiu-lang.org/case-study-reactvision.html) renderer moved its AR/VR
rendering modules from C++ to Eskiu and uses about 85% less memory, and a
[Nintendo 3DS](https://eskiu-lang.org/case-study-3ds.html) runs an on-device AR demo whose
logic is written in Eskiu.

- **C-style and C-compatible.** C syntax and integer rules, structs, unions, bitfields and
  `extern` declarations laid out and called the way the target's C compiler does it.
- **More than C.** Generics, structural interfaces, sum types with exhaustive `match`,
  closures, operator overloading and `async`/`await`.
- **Explicit memory with guard rails.** No garbage collector: `alloc`/`free`, allocators,
  `defer`, slices, checked nullable pointers and an opt-in bounds-checked build (`--safe`).
- **A batteries-included stdlib.** Collections, strings, JSON, regex, an async runtime and
  HTTP/1.1, HTTP/2 and TLS servers.
- **Self-hosted.** The compiler is also written in Eskiu (`selfhost/`) and builds itself.

It runs on macOS, Linux and Windows, cross-compiles to 32-bit ARM and x86, and runs bare
metal: the screenshot below is an Eskiu kernel booting in QEMU with no libc.

<p align="center">
  <img src="assets/kernel.png" alt="Eskiu kernel running in QEMU" width="320">
</p>

## A script

```eskiu
#!/usr/bin/env -S eskiuc run
import <fetch>;
import <json>;
import <io>;

int main() {
    let r: Response;
    int rc = fetch("https://api.github.com/repos/llvm/llvm-project", &r);
    defer Response_free(&r);
    if (rc != 0) { return 1; }
    *JsonValue repo = Response_json(&r);
    if (repo == null) { return 1; }
    defer JsonValue_free(repo);
    printf("%s has %lld stars\n", JsonValue_as_cstr(json_get(repo, "full_name")),
           JsonValue_as_int(json_get(repo, "stargazers_count")));
    return 0;
}
```

[`examples/scripts/`](examples/scripts/) has longer ones: a to-do list in a JSON file, a
GitHub repository report and a downloads folder organizer.

## Systems code

```eskiu
import <mem>;
import <io>;

enum Shape {
    Circle(float),
    Rect(float, float)
}

float area(Shape s) {
    match s {
        Circle(r)  -> { return 3.14159 * r * r; }
        Rect(w, h) -> { return w * h; }
    }
    return 0.0;
}

int apply(fn(int)->int f, int x) { return f(x); }

int main() {
    *Shape shapes = alloc<Shape>(2);
    defer free(shapes);                          // runs on every exit path
    shapes[0] = Circle(1.0);
    shapes[1] = Rect(2.0, 3.0);
    for (i in 0..2) {
        printf("area %d = %.2f\n", i, area(shapes[i]));
    }

    int k = 10;
    printf("%d\n", apply(int(int x) { return x + k; }, 5));   // closure capturing k
    return 0;
}
```

```
$ eskiuc run shapes.esk
area 0 = 3.14
area 1 = 6.00
15
```

## Status

Eskiu is pre-1.0. The language is stable enough for real work. What can change between
releases, and how a renamed function is deprecated before it goes, is written down in
[docs/lang/stability.md](docs/lang/stability.md); [MIGRATION.md](MIGRATION.md) maps every
removed name to its replacement.

## Install

On macOS or Linux:

```bash
curl -fsSL https://eskiu-lang.org/install.sh | sh
eskiuc --version
```

The script picks the prebuilt binary for your platform (macOS arm64, Linux x86-64 or arm64),
verifies its checksum and installs it. The Windows x86-64 build and the tarballs are on the
[releases page](https://github.com/doranteseduardo/eskiu/releases). `eskiuc` links programs
with your system C toolchain (`cc`, `clang` or `gcc`), so have one installed.

To build from source you need LLVM 21 or newer (22 recommended), CMake 3.20+ and a C++17
compiler:

```bash
git clone https://github.com/doranteseduardo/eskiu && cd eskiu
cmake -S . -B build && cmake --build build
./build/eskiuc examples/hello.esk -o hello && ./hello
```

## Documentation

| | |
|---|---|
| [Quickstart](QUICKSTART.md) | Install and run your first programs |
| [Tutorial](docs/lang/getting-started.md) | The language, step by step |
| [The Book of Eskiu](https://eskiu-lang.org/the-book-of-eskiu.html) | A longer guide, from basics to async and C interop |
| [Language reference](docs/lang/spec.md) | Every construct and the standard library |
| [Examples](examples/) | Small programs, from hello world to an HTTP/2 server |
| [Stability](docs/lang/stability.md) | What can change between releases |
| [Migration](MIGRATION.md) | Renamed and removed names, old to new |
| [Compiler internals](docs/dev/index.md) | Architecture, ABI notes and design decisions |
| [Changelog](CHANGELOG.md) | Every release and what changed |

## Contributing

Bug reports are welcome; please include the program and the `eskiuc --version` output.
Open an issue before working on a new language feature or a design change. Scoped fixes can
go straight to a pull request. [docs/dev/contributing.md](docs/dev/contributing.md) covers
the build, the test suite and the two compilers that every language change has to keep in
step.

## License

MIT. See [LICENSE](LICENSE). A [ReactVision](https://reactvision.xyz) project.
