# Eskiu 0.2.1

A hardening-and-ergonomics patch on 0.2.0, shaken out by building a real service
(an INE-QR HTTP API) on top of it. No language changes — the base is the same
0.2.0: compiled to native through LLVM, you manage memory yourself, standalone
binaries.

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

**A sanitizer CI gate.** `tests/run.sh` gains `SANITIZE=asan|ubsan`, and CI now
runs the whole suite three ways — plain, AddressSanitizer, and UBSan — on every
push. This is the standing guard against the class of codegen bug that 0.2.0's
`<base64>` crash turned out to be (a stack overflow from allocas leaking inside a
loop). A `loop_locals` stress test locks that fix in.

**`<bytes>` — a binary-safe byte buffer.** `String` is `*char` and
NUL-terminated, so it truncates on embedded NULs and isn't safe for binary.
`Bytes` is length-prefixed over `*uint8` — push/append/slice (non-owning
view)/eq/from_str/cstr, plus `Bytes_from_base64`/`Bytes_to_base64`. `<http>`
gains `HttpReq_body`, a non-owning `Bytes` view of the request body.

**`HashMap<K,V>` — a map over any key type.** The string-keyed `Map<V>` is
unchanged; the new `HashMap<K,V>` keys on any value type by taking `hash`/`eq`
function pointers at init (Eskiu has no trait system to synthesise them — the
systems-language answer, like C's `qsort` comparator), with built-in
`int_hash`/`int_eq`.

**Two compiler fixes** the new gate and the IR verifier surfaced:
- Integer arguments to functions that return via sret (a >16-byte struct) were
  matched against the wrong parameter, leaving an `int` literal unwidened — an
  IR-verifier error. Now offset-correct.
- `substType` now substitutes type parameters inside function types
  (`fn(*K)->uint64` → `fn(*int)->uint64`), so generic APIs with callback
  parameters (like `HashMap<K,V>`'s hash/eq) type-check and monomorphize.

**Internal:** the 3,400-line `codegen.cpp` was split into six files
(`codegen_{module,type,scope,decl,stmt,expr}.cpp`) — no behavior change.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.2.0 — no source changes required. Existing code keeps compiling;
`<bytes>` and `HashMap<K,V>` are additive.
