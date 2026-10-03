# Eskiu 0.9.3

A patch release on top of 0.9.2. It fixes the problems a review of the whole documentation
turned up in the compiler and closes the known issues 0.9.2 shipped with. Every fix lands in
both the C++ and the self-hosted compiler, each with a regression test. The documentation was
reviewed against the compiler, and its examples compile and print what they say.

---

## Install

**macOS (Apple Silicon) and Linux (x86-64, arm64)**

```bash
curl -fsSL https://eskiu-lang.org/install.sh | sh
eskiuc --version
```

Or download a tarball below and unpack it into a prefix (`tar -xzf eskiuc-<platform>.tar.gz -C /usr/local`).

**Windows (x86-64, experimental)**

```powershell
Expand-Archive eskiuc-windows-x86_64.zip -DestinationPath C:\eskiu   # then add C:\eskiu\bin to PATH
eskiuc --version
```

Or build from source (LLVM 21 or newer, LLVM 22 recommended, and CMake 3.20+):

```bash
git clone https://github.com/doranteseduardo/eskiu
cd eskiu && cmake -S . -B build && cmake --build build
```

---

## Added

- **`await` inside `finally` and `defer`.** In an async function a cleanup may await: it
  suspends and resumes like any other await, keeps a `return` value, and rethrows an
  exception that unwinds through the try once the `finally` ends. A future cancelled while
  it waits runs its pending cleanup as a detached task that frees itself when it ends.
- **Unnamed bitfields.** `uint32 : 3;` reserves three bits and `uint32 : 0;` starts the
  next bitfield in a new unit, laid out as the target's C compiler does, so C headers with
  padding bitfields can be ported as they are. They work in structs and unions.

## Fixed

- **Inline asm with GCC register constraints.** On x86-64 and 32-bit x86, `"a"`, `"b"`,
  `"c"`, `"d"`, `"S"` and `"D"` (with `=`, `+`, `&`, and in combinations such as `"Nd"`)
  are translated to LLVM's `{ax}` form, as clang does. Before, LLVM could not allocate them.
- **`await` in generic async functions.** An `await` in a `match` arm that binds a payload,
  after an operand with a side effect in the same expression, or inside a `?:` arm now works
  in a generic async function, as it already did in a plain one.
- **macOS linker warning.** On a new macOS release every link printed "object file was built
  for newer 'macOS' version". The native target now uses the macOS product version.
- **`.obj` output.** `-o name.obj` writes an object file, like `.o`, instead of trying to
  link an executable.
- **Diagnostics** name types as you write them (`'Box'`, not `'struct:Box'`).
- **Closure environments.** A capturing lambda bound to a local of an async function leaked
  its environment on every call; the coroutine frame now frees it. Outside async functions a
  lambda bound to a local that is only called keeps its environment on the stack, as
  documented, instead of an unfreed heap block.
- **Generic bitfields.** A bitfield typed by a struct's type parameter (`T a : 3;`) is checked
  with each instance's type argument.
- **Self-hosted compiler speed.** Code generation for very deeply nested binary expressions
  was quadratic in the depth (26 s at depth 4000); it is linear now (0.06 s).

## Upgrade

- **A captured value is read-only inside a lambda for method calls too.** Captures are by
  value, and assigning a captured variable or taking its address was already an error.
  Calling a method with a plain `*T self` receiver on a captured value (`c.inc()`, or
  `c.inner.inc()`) is now an error as well; before, the method silently changed the
  closure's copy. Declare the method `const T* self` if it only reads, or capture a pointer
  if the lambda should change the original.

The full list of changes is in [CHANGELOG.md](CHANGELOG.md).
