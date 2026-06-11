# Eskiu 0.2.0

This is the backend-services release. v0.1.0 laid the foundation — native
compilation through LLVM, templates, interfaces, manual memory, bare-metal
support. v0.2.0 adds what you need to write servers: async/await, a complete
HTTP/2 stack with TLS, sum types with `match`, and a much larger standard
library.

The base is unchanged. It compiles to native code, you manage memory yourself,
and the output is a standalone binary.

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

Compile and run a file in one step:

```bash
eskiuc run hello.esk
```

---

## What's new

**async/await.** `async T f(...)` returns a `*Future<T>`, and `await e` suspends
until it resolves. The compiler lowers each async function to a resumable state
machine. Every control-flow construct works around an `await` — `if`, `while`,
`for`, `switch`, `for`-`in`, `break`, `continue`, and `return await`. Pair it
with the built-in executor and channels, or write your own scheduler.
Cancellation works, and the runtime is leak-free.

```eskiu
import <future>;
import <channel>;

async int sum_squares(Chan<int>* ch) {
    let a: int = await chan_recv(ch);
    let b: int = await chan_recv(ch);
    let c: int = await chan_recv(ch);
    return a + b + c;
}
```

**HTTP/2, end to end.** A full RFC 7540 / 7541 stack written in Eskiu: the frame
codec, HPACK with Huffman, the per-stream state machine, flow control, and a
multiplexed server. The async server runs many connections on one thread,
handles interleaved streams, and honors send windows so large bodies flow
correctly. TLS comes from OpenSSL, with ALPN negotiating `h2`. Verified against
`curl --http2`, including 1 MB bodies and three concurrent connections. See
`examples/http2_tls_server.esk`.

**Sum types and `match`.** An `enum` variant can carry a payload, turning the
enum into a tagged union. Construct variants by name and destructure them with
`match`, which is checked for exhaustiveness and rejects duplicate arms.

```eskiu
enum ParseResult { Port(int), InvalidRange(int), NotANumber }

int main() {
    match parse_port("8080") {
        Port(p)         -> printf("Listening on :%d\n", p);
        InvalidRange(n) -> printf("%d out of range\n", n);
        NotANumber      -> printf("not a number\n");
    }
    return 0;
}
```

Algebraic enums can be generic too — `Option<T>`, `Either<A,B>` — with type
arguments inferred from the payload (`Some(5)` gives `Option<int>`).

**More language.** `const` bindings and pointer constness (`const T*`,
`T* const`); integer ranges in `for`-`in` (`for (i in 0..n)`); user-defined
variadic functions with `va_list`; raw C function pointers via `(*void)fn` for
C callbacks; escaping closures with `free_closure`; future combinators
(`spawn`, `select2`, `join2`); and `__LINE__` / `__FILE__` / `#error` in the
preprocessor.

---

## Standard library additions

Imported with `import <name>`:

| Module | Contents |
|--------|----------|
| `<http2>` | HTTP/2 framing, lifecycle, streams, flow control (RFC 7540) |
| `<hpack>` | HPACK header compression with Huffman (RFC 7541) |
| `<http2_server>` | multiplexed, non-blocking h2c server |
| `<tls>` | OpenSSL server with ALPN, blocking and async |
| `<eventloop>` | readiness reactor over kqueue / epoll, with timers |
| `<executor>` / `<net_async>` | async runtime — executor and non-blocking sockets |
| `<channel>` | async channel; `chan_recv` is a future |
| `<http_async>` | concurrent, non-blocking HTTP/1.1 server |
| `<either>` / `<futureval>` | `Option<T>`, `Either<A,B>`, `Pair<A,B>` and helpers |
| `<atomic>` | `atomic_load` / `store` / `swap` / `cas` over LLVM atomics |
| `<alloc>` / `<sysheap>` | explicit allocators and an mmap-backed heap |
| `<json>` | JSON builder and parser |
| `<base64>` | RFC 4648 encode / decode |
| `<time>` / `<env>` / `<path>` | clocks, environment, path manipulation |
| `<threading>` | `Mutex`, `Cond`, `Sem` over pthread |

---

## Notable fixes

- **Unsigned widening** — `(int)(uint8)255` now zero-extends to `255` instead of
  sign-extending to `-1`, fixing byte-level decoding throughout.
- **Mutable parameters** — every parameter gets a stack slot, so it can be
  reassigned in the body like a local.
- **`bool` widening** — widening a `bool` or comparison result into an `int`
  yields `1`, not `-1`.
- **Async `else`-`if` with `await`** — a terminating branch no longer produces
  invalid IR.

The full, granular log is in [CHANGELOG.md](CHANGELOG.md).

---

## Documentation

[eskiu-lang.org](https://eskiu-lang.org) &nbsp;·&nbsp; [Getting started](docs/lang/getting-started.md) &nbsp;·&nbsp; [Language reference](docs/lang/spec.md)
