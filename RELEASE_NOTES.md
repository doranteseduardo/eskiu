# Eskiu 0.2.5

A correctness fix plus the first self-hosting milestone. The headline is a
preprocessor bug fix; alongside it, the lexer is now also written in Eskiu and
proven byte-identical to the C++ lexer. No new language surface.

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

**Fixed — a `//` comment ending in `\` no longer eats the next line.** The
preprocessor applied C-style backslash-newline line continuation unconditionally,
so a comment whose last character was a backslash silently spliced the following
source line into the comment — deleting a `return`, an `else` branch, or any
statement, with no diagnostic. Continuation now fires only when the trailing `\`
is genuine code (not inside a `//` or `/* */` comment or a string/char literal);
legitimate `#define` continuation is unaffected. Regression test added. This was
found by dogfooding the new self-hosted lexer.

**Self-hosting milestone 1 — the lexer, in Eskiu.** A full lexer written in Eskiu
(`selfhost/`) now produces a token stream byte-identical to the C++ `--test-lexer`
over the entire preprocessor-free corpus (114/114), checked by a parity gate wired
into CI. This is dogfood/tooling — the production compiler is untouched. The parser
(milestone 2) is underway; its expression layer is done.

**Hardening.** A `ESKIU_RESOLVER_DEBUG` mode cross-checks the v0.2.4 single resolver
against codegen's structural type derivation on every table hit; across the whole
corpus the two agree on every behavior-affecting type, confirming the unification is
sound. The fuzzer gained generators for backslash-newline inside comments and
strings (with a self-checking expected-output oracle, which the O0/O2 differential
can't see) — guarding the footgun fixed above.

Behavior-preserving apart from the preprocessor fix, verified by the test suite,
sanitizers, the golden-IR snapshot oracle, and the O0-vs-O2 differential fuzzer.

The full log is in [CHANGELOG.md](CHANGELOG.md).

---

## Upgrade

Drop-in over 0.2.x. The only behavior change is the preprocessor fix: if you had a
`//` comment ending in `\`, it was silently swallowing the next line — that line now
runs. Correct programs are otherwise unaffected.
