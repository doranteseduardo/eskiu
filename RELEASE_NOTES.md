# Eskiu 0.1.0

Eskiu is a systems language built to replace the C + Go + C++ + Python stack that compute-intensive backend services typically require. C-style syntax, structural interfaces, monomorphic templates, and explicit memory — compiled to native via LLVM with no garbage collector and no runtime.

This is the first release. It includes everything needed to write real backend services and bare-metal systems code, validated against a cryptographic pipeline running at 74 ms on arm64 — 2.5× faster than the reference C implementation — and a bare-metal ARM64 kernel booting in QEMU without libc.

---

## Install

**macOS (Apple Silicon)**

```bash
tar -xzf eskiuc-macos-arm64.tar.gz -C /usr/local
eskiuc --version
```

**Linux (x86-64)**

```bash
tar -xzf eskiuc-linux-x86_64.tar.gz -C /usr/local
eskiuc --version
```

Or build from source — requires LLVM 17+ and CMake 3.20+:

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## What's in this release

### Closures

`fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`. Variables from the enclosing scope are captured by value — no annotation required.

```eskiu
int base = 10;
let add: fn(int)->int = int(int x) { return x + base; };
add(5);  // 15
```

### Threads

`thread_create` and `thread_join` are language keywords. The closure fat pointer maps directly to pthread's `(start_routine, arg)` — no trampoline needed. Link with `-lpthread` on Linux.

```eskiu
*void t = thread_create(fn() { printf("hello from thread\n"); });
thread_join(t);
```

### Exception handling

`try`/`catch`/`finally`/`throw` via LLVM `invoke`/`landingpad` (Itanium ABI). Multiple catch clauses supported; unhandled exceptions re-thrown via `resume`. Link with `-lc++` on macOS or `-lstdc++` on Linux.

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

- `sizeof(T)` — compile-time size constant for any type including structs
- `union` — all fields share offset 0; size = sizeof(largest field)
- Typed pointer arithmetic — `p: *int; p + 1` advances 4 bytes, not 1

### Standard library

`import <name>` resolves stdlib modules from the installation. `import "path"` for local project files.

| Module | Contents |
|--------|----------|
| `<result>` | `Result<T,E>`, `Ok`, `Err` |
| `<list>` | `List<T>` with auto-resize |
| `<string>` | `String` with append, concat |
| `<fs>` | File I/O — open, read, write, seek, read_all, write_all |
| `<math>` | sqrt, pow, fabs, floor, ceil |
| `<io>` | printf, scanf, puts |
| `<mem>` | memcpy, memset, strlen |

---

## Documentation

[eskiu-lang.org](https://eskiu-lang.org) &nbsp;·&nbsp; [Getting started](docs/lang/getting-started.md) &nbsp;·&nbsp; [Language reference](docs/lang/spec.md)
