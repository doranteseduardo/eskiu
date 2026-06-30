# Eskiu 0.3.0

The self-hosting milestone. The whole compiler (lexer, preprocessor, parser,
semantic analyzer, and code generator) is now reimplemented in Eskiu under
`selfhost/`, reaches a 3-stage bootstrap fixpoint, and the self-hosted code
generator is feature-complete against the C++ corpus. For users, this release is
a correctness fix plus a debug-printer crash fix; there is no new language surface.

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

**Fixed: `&&` and `||` now short-circuit.** Code generation evaluated both operands
eagerly and emitted a logical-and/or, so the right-hand side always ran. A guarded
dereference like `p != null && p.field` could fault. They now lower to a conditional
branch plus PHI, evaluating the right-hand side only when the left doesn't already
decide the result. Regression test added. Found by dogfooding the self-hosted type
checker.

**Fixed: `--test-parser` no longer crashes on a forward-declared function.** The AST
printer dereferenced a null `FunctionDecl::body` for a prototype like `int f(int);`,
segfaulting `eskiuc --test-parser`. It now omits the `Body:` section for a body-less
function. Affected only the debug printer, not code generation.

**The self-hosting milestone is complete.** The entire compiler pipeline is written
in Eskiu (`selfhost/`): lexer, preprocessor, parser, semantic analyzer, async
lowering, and an LLVM-IR code generator that emits textual IR (no LLVM library is
linked; `clang` assembles and links). Three things are proven and CI-gated:

- **Per-pass parity** against the C++ `eskiuc`. The front-end is byte-identical to
  the reference debug dumps; the type checker matches its accept/reject verdict and
  catches all 19 semantic error classes; the code generator is checked behaviorally
  (compile both ways, run, compare exit code and output).
- **A 3-stage bootstrap fixpoint.** The C++ compiler builds the self-hosted compiler
  (cc0), cc0 builds it again (cc1), cc1 builds it a third time (cc2), and cc1 and cc2
  emit identical IR for the compiler's own source.
- **Feature-completeness.** The full C++ feature corpus, pushed through the behavioral
  code-gen oracle, passes a clean sweep. Floats, `switch`, sum types and `match`,
  closures, exceptions (the Itanium ABI), atomics, generics with inference, async/await,
  unions, bitfields, interfaces, type aliases, function-as-value, packed structs,
  variadics, and the `?` operator all generate correct code.

This is dogfood and tooling. The production C++ compiler is untouched apart from one
additive, debug-only printer extension used as a parity gate.

Behavior-preserving for user programs apart from the short-circuit fix, verified by
the test suite, sanitizers, the golden-IR snapshot oracle, and the O0-vs-O2
differential fuzzer.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.2.x. The only behavior change is the short-circuit fix: if you relied
on the right-hand side of `&&` or `||` always running (for a side effect), it now runs
only when the left operand doesn't already decide the result. This is the standard C
semantics; correct programs are otherwise unaffected.
