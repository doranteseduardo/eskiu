# Eskiu 0.8.0

A release focused on Windows parity and language surface. The whole standard library and
both networking stacks (blocking and async) now run on a native Windows runner, operator
overloading covers every operator, and `match` is exhaustive over payload-less enums.
Existing code keeps compiling unchanged.

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

- **Operator overloading.** A type can define `operator +`, `operator ==`, `operator []`,
  and the rest (binary, unary, comparison, subscript) as ordinary static methods resolved
  by operand type. The dispatch is structural and zero-cost: an overloaded operator lowers
  to a direct call, with the same numeric coercion the built-in operators use. Landed in
  lockstep across both compilers.

- **`match` on classic (payload-less) enums.** A plain `enum Dir { N, E, S, W }` can be
  matched arm by arm, and the compiler checks the match is exhaustive (a missing variant is
  an error, unless a `_` arm is present). The enum still behaves as an integer everywhere
  else; the nominal name is kept on variables and parameters so exhaustiveness is
  recoverable.

## Windows

The compiler already emitted Windows COFF objects (v0.7.0); this release brings the runtime
and standard library up to parity, validated end to end on a native Windows runner
(`.github/workflows/windows.yml`):

- **Exceptions** use the mingw SEH EH personality (`__gxx_personality_seh0`) instead of the
  Itanium `__gxx_personality_v0`, so `try`/`throw`/`catch` links and unwinds under the mingw
  C++ runtime.
- **Platform shims:** `<sysheap>` maps pages with `VirtualAlloc`/`VirtualFree` (no `mmap`),
  `<time>` uses the Win32 clocks (`GetTickCount64`, `GetSystemTimeAsFileTime`, `Sleep`), and
  `<threading>` links against winpthreads.
- **Blocking sockets:** `<net>` runs on Winsock (`WSAStartup`, `closesocket`, `send`/`recv`,
  link `-lws2_32`), so a thread-per-connection server works.
- **Async:** `<eventloop>` gained a `WSAPoll` reactor and `<net_async>` a Winsock backend
  (`ioctlsocket(FIONBIO)`, `recv`/`send`, `WSAGetLastError`). A Windows `SOCKET` is not a
  small sequential fd, so the loop's fd-indexed slot table grows to fit large handles.

See [`docs/dev/cross-compile.md`](docs/dev/cross-compile.md) for the platform macros and the
Windows recipe. The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in. No breaking language or standard-library changes; recompiling picks up the fixes,
and the new operators, plain-enum matching, and Windows backends are all additive.
