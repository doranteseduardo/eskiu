# Eskiu 0.6.2

A patch release focused on the async runtime and CI stability. It removes the last
uninitialized function pointer from the event loop (the shape of an intermittent Linux
CI `SIGILL` in the HTTP/2 tests), fixes a soundness gap in the async transform, corrects
interface (vtable) argument coercion, and makes `--target` cross-compilation select the
right platform. Existing code keeps compiling unchanged.

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

Or build from source (LLVM 17+ and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## What's new

- **Cross-compilation targets the right platform.** The `--target` triple now drives the
  predefined platform macro (`__APPLE__` / `__linux__`), so `eskiuc --target x86_64-linux-gnu`
  on macOS selects the Linux stdlib paths (epoll, Linux `sockaddr_in`) instead of emitting
  unresolved BSD symbols. With no `--target` it still follows the build host.

## What's fixed

- **Event-loop timer callbacks are initialized.** `el_new` allocated the timer array but
  only zeroed each timer's `active` flag, leaving the `on_fire` closure as heap garbage
  (`alloc` does not zero). Firing is guarded by `active`, but an uninitialized function
  pointer is exactly the shape of the intermittent Linux CI `SIGILL` in the HTTP/2 tests.
  It now initializes every field, completing the equivalent `on_read` fix from v0.3.1, so
  the event loop holds no uninitialized function pointer.

- **The async transform preserves `escaping` parameters.** Lowering an `async fn` to its
  coroutine constructor dropped the per-parameter `escaping` flags. Because the constructor
  stores each parameter into the heap coroutine frame (which outlives the call), an escaping
  closure argument could then be stack-allocated at the call site, leaving the frame with a
  dangling env. The lowered constructor now carries the original flags.

- **Interface (vtable) dispatch coerces arguments.** Calling an interface method with an
  argument that needs widening (e.g. an `int32` where the method declares `int64`) emitted a
  call whose argument type did not match the vtable slot's signature: the reference compiler
  rejected it at LLVM verification, and the self-hosted compiler emitted mistyped IR. Both
  now widen the argument to the method's declared parameter type, exactly like a direct call.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in. No language or standard-library API changes; recompiling picks up the fixes.
