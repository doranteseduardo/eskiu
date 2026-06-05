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

Compile and run in one step — `eskiuc` links the executable for you (it invokes
your system C toolchain, the same way `rustc` does):

```bash
eskiuc hello.esk -o hello && ./hello
```

---

## What you get

**A complete systems language.** C-style syntax, no surprises. If you know C, you can read Eskiu immediately.

```eskiu
extern int printf(string fmt, ...);

interface Shape { float area(); }

struct Circle {
    float radius;
    float area() { return self.radius * self.radius * 3.14159; }
}

void render(Shape s) { printf("area = %f\n", s.area()); }

int main() {
    let c: Circle = Circle { radius: 5.0 };
    render(&c);                                    // structural dispatch, no implements

    let square: fn(int)->int = int(int n) { return n * n; };
    printf("%d\n", square(6));                     // 36, closure over captured state

    *void t = thread_create(void() { printf("hello\n"); });
    thread_join(t);

    try {
        throw "something went wrong";
    } catch (string e) {
        printf("caught: %s\n", e);
    }

    return 0;
}
```

**Direct access to C libraries.** Declare with `extern`, call directly. OpenSSL, POSIX, CoreGraphics — no binding layer.

**Explicit memory.** `alloc(T, N)` / `free(ptr)`. You control what goes on the heap. No GC, no pause times.

**Ergonomic where it counts.** Template type arguments are inferred — `List_get(&nums, i)`, not `List_get<int>(&nums, i)`. `for (x in nums)` iterates arrays and lists. The `?` operator propagates `Result` errors: `let x = divide(a, b)?;` returns the `Err` or unwraps the value.

**Networking.** The `<net>` module gives you TCP sockets — listen, accept, connect, send, recv — over the POSIX socket API, portable across macOS and Linux. A concurrent server is `<net>` plus `thread_create`. A complete HTTP server fits on a screen:

```eskiu
import <net>;

int main() {
    int fd = net_tcp_listen(8080);
    *uint8 req = alloc(uint8, 4096);
    while (1) {
        int c = net_accept(fd);
        net_recv(c, (*void)req, 4096);
        net_send_str(c, "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nHello from Eskiu.\n");
        net_close(c);
    }
    return 0;
}
```

**Standard library** — imported with `import <name>`:

| Module | Contents |
|--------|----------|
| `<result>` | `Result<T,E>`, `Ok`, `Err` — error-as-value |
| `<list>` | `List<T>` with auto-resize |
| `<string>` | rich mutable `String` — append/concat, char access, equality, search, substring, reverse, int conversion |
| `<fs>` | File I/O — open, read, write, seek, read_all, write_all |
| `<net>` | TCP sockets — `net_tcp_listen`/`accept`/`connect`/`send`/`recv`/`close` |
| `<math>` | sqrt, pow, fabs, floor, ceil |
| `<io>` | printf, scanf, puts |
| `<mem>` | memcpy, memset, strlen |

**Bare-metal capable.** `--freestanding`, `--target TRIPLE`, inline asm with GCC-compatible constraints, `volatile` for MMIO, and `packed struct` (or `#pragma pack`) for exact on-the-wire / register-map layouts. The kernel in `kernel/` boots on QEMU without libc.

---

## Documentation

[eskiu-lang.org](https://eskiu-lang.org) &nbsp;·&nbsp; [Getting started](docs/lang/getting-started.md) &nbsp;·&nbsp; [Language reference](docs/lang/spec.md)
