## Eskiu Language Documentation

Eskiu is a systems programming language that compiles to LLVM IR. It targets developers who need C-level performance and direct memory control without a garbage collector or borrow checker. The syntax is deliberately C-style, so code reads like existing systems code while gaining a cleaner module model and a gradual type system. The primary motivation is real workloads — starting with an INE QR decoder that needs to drop from 3–5 s to under 1 s — and the language is designed around that constraint: stack allocation by default, explicit heap via `alloc`/`free`, zero-cost extern C ABI, and monomorphic template instantiation.

**Current version:** v0.0.1-alpha — compiler front-end and codegen complete; struct codegen and heap allocation are the active development areas.

---

## Documentation Map

| Document | Covers | Best for |
|---|---|---|
| [getting-started.md](getting-started.md) | Build the compiler, write and run your first program, annotated examples | New users — start here |
| [spec.md](spec.md) | Full language reference: types, operators, statements, expressions, scoping rules | Reference while writing code |
| [build.md](build.md) | Compiler prerequisites, CMake configuration, LLVM version requirements, troubleshooting | Anyone building from source |
| [../../docs/GLOSSARY.md](../../docs/GLOSSARY.md) | Definitions of terms used across the compiler and documentation | When a term is unfamiliar |

---

## Quick Reference

### Types

| Eskiu type | LLVM type | Width | Notes |
...

### Operator Precedence
...

### CLI Flags
...

---

## Current Capabilities (v0.0.1-alpha)

### Works today
...

### Planned for v0.1
...

### Further out
| v0.2 | v1.0 | v2.0 |
