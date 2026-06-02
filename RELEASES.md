# Releases

Version history and release notes for Eskiu Lang.

## Current Status

**v0.0.1-alpha** (June 2026)

- Phases 0–4 complete (Lexer, Parser, Type Checker, Codegen)
- Struct types with member access validation working
- LLVM IR backend fully functional
- Comprehensive documentation added

---

## v0.0.1-alpha (June 2026)

**Released:** June 2, 2026

### What's Included

**Compiler Phases:**
- ✅ Phase 0: LLVM integration, CMake build
- ✅ Phase 1: Lexer, tokenization
- ✅ Phase 2: Parser, AST construction
- ✅ Phase 3: Code generation to LLVM IR
- ✅ Phase 4: Type checker, semantic analysis

**Language Features:**
- Variables with explicit types (`let x: i32 = 5`)
- Functions with parameters and return types
- Control flow (`if`/`else`, `for`, `while`, `break`, `return`)
- Primitive types (`i8`-`i64`, `u8`-`u64`, `f32`, `f64`, `bool`)
- Pointers and references (`*T`, `&`)
- Struct types with member access (`.field` syntax)
- C interoperability (`extern fn`)

**Development Tools:**
- Compiler test modes: `--test-lexer`, `--test-parser`, `--test-typechecker`, `--test-codegen`
- LLVM IR inspection with `-emit-llvm`
- Error reporting with file:line:col format

**Documentation:**
- QUICKSTART.md (5-minute first program)
- GETTING_STARTED.md (complete tutorial)
- BUILD.md (installation for macOS, Linux, Alpine)
- DEBUGGING.md (test modes and troubleshooting)
- LANGUAGE_SPEC.md (full syntax specification)
- ARCHITECTURE.md (compiler design)
- DESIGN_DECISIONS.md (philosophy and trade-offs)
- PERFORMANCE.md (optimization guide)
- FAQ.md (common questions)
- GLOSSARY.md (60+ technical terms)
- PHASES.md (development roadmap)
- CONTRIBUTING.md (contributor guide)
- INDEX.md (navigation hub)

### Known Limitations

- **No templates/generics** (Phase 5)
- **No custom allocators** (Phase 6)
- **No standard library** (Phase 7)
- **No interfaces/structural typing** (Phase 5)
- **No async/await** (Phase 11+)
- **Stack allocation only** (heap coming Phase 6)
- **No const correctness** (Phase 5+)
- **No SIMD support** (Phase 5+)

### Bug Fixes

- Fixed Phase 4 type checker parameter registration bug (parameters now correctly registered before validation pass)
- Fixed lexer to recognize COLON token for type annotations

### Contributors

- Eduardo Dorantes (creator, primary development)

---

## Planned Releases

### v0.1.0 (Q4 2026)

**Focus:** Structs, Interfaces, Templates (Phase 5)

**Goals:**
- Implement struct methods
- Go-style structural typing (interfaces)
- Basic generics/templates
- Two-pass compilation for forward references

**Estimated features:**
- `interface` definitions
- Implicit interface satisfaction
- `fn foo<T>(x: T) -> T` syntax
- Virtual method tables for dynamic dispatch

### v0.2.0 (Q2 2027)

**Focus:** Memory Management & Stdlib (Phases 6–7)

**Goals:**
- Explicit heap allocation (`alloc`/`free`)
- String type
- Basic collections (Vec, Map)
- Result<T, E> type

### v1.0.0 (Q4 2027)

**Focus:** Stability and Core Features (Phases 8–10)

**Goals:**
- Lambdas and closures
- Exception handling
- Thread support (pthreads)
- Language stability guarantee

### v2.0.0 (2028+)

**Focus:** Advanced Features (Phase 11+)

**Goals:**
- Async/await
- FFI improvements
- Optimization passes
- Ecosystem maturity

---

## Release Process

**Alpha releases (v0.0.x):**
- Posted on GitHub Releases
- May contain breaking changes
- Documentation updated for each release

**Beta releases (v0.x.0):**
- Feature-complete for the phase
- API relatively stable within the phase
- Backward compatibility attempted

**Stable releases (v1.0.0+):**
- Language stability guarantee
- Semantic versioning (MAJOR.MINOR.PATCH)
- Breaking changes in MAJOR versions only

---

## Changelog Details

See [docs/PHASES.md](docs/PHASES.md) for detailed requirements and status of each phase.

For detailed commit history, see `git log --oneline` or GitHub Commits.

---

## Support and Feedback

- **Bug reports:** File a [GitHub Issue](https://github.com/yourusername/eskiu/issues) with a minimal test case
- **Feature requests:** See [ISSUES.md](.github/ISSUE_TEMPLATE/feature_request.md)
- **Questions:** Check [docs/FAQ.md](docs/FAQ.md) or [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)

---

**Next release: v0.1.0 (Q4 2026) — Structs, Interfaces, Templates**
