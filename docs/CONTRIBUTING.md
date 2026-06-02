# Contributing to Eskiu

Thank you for your interest in contributing to Eskiu! This document outlines the development workflow and guidelines.

## Getting Started

1. **Fork and clone** the repository
2. **Set up** the environment (see [BUILD.md](BUILD.md))
3. **Create a feature branch**: `git checkout -b feature/your-feature`
4. **Make your changes** following the guidelines below
5. **Test thoroughly** using the testing modes
6. **Commit with clear message** (see [Commit Guidelines](#commit-guidelines))
7. **Submit a pull request**

### Before You Start

Familiarize yourself with:
- [ARCHITECTURE.md](ARCHITECTURE.md) — How the compiler is organized
- [PHASES.md](PHASES.md) — What each phase requires
- [GETTING_STARTED.md](GETTING_STARTED.md) — How to use the language

## Development Workflow

### Understanding the Pipeline

The compiler pipeline has distinct phases, each building on the previous:

```
Source → Lexer → Parser → Type Checker → Codegen → LLVM
```

**Pick a phase** and work on completing it. Don't skip ahead.

### Phases Overview

| Phase | Status | Focus |
|-------|--------|-------|
| **0** | ✅ Complete | LLVM integration, CMake build |
| **1** | ✅ Complete | Lexer, tokenization |
| **2** | ✅ Complete | Parser, AST construction |
| **3** | ✅ Complete | Code generation to LLVM IR |
| **4** | ✅ Complete | Type checker, semantic analysis |
| **5** | 🔄 In Progress | Structs, interfaces, templates |
| **6** | ⏳ Planned | Memory: `alloc`/`free`, String |
| **7** | ⏳ Planned | Stdlib: collections, Result<T,E> |
| **8** | ⏳ Planned | Lambdas, closures, first-class functions |
| **9** | ⏳ Planned (v0.2) | Threads: pthreads integration |
| **10** | ⏳ Planned (v1.0) | Exceptions: try/catch/finally |
| **11** | ⏳ Planned (v2.0) | Async/await: coroutines |

See [PHASES.md](./PHASES.md) for detailed requirements for each phase.

## Code Style

### C++ Guidelines

- Use **C++17** features
- Follow **LLVM naming conventions**:
  - Classes: `PascalCase`
  - Functions/methods: `camelCase`
  - Variables: `camelCase`
  - Constants: `UPPER_SNAKE_CASE`
- Prefer `std::unique_ptr` over raw pointers
- Use const-correctness
- Keep functions small and focused

### Example

```cpp
class Lexer {
public:
    explicit Lexer(const std::string& source);
    Token nextToken();

private:
    std::string source;
    size_t current = 0;
    
    bool isAtEnd() const;
    char peek() const;
};
```

## Testing Your Changes

Use the four test modes to validate your work (from the `build/` directory):

### Test Lexer
```bash
./eskiu compile ../examples/hello.esk --test-lexer
```
Check that tokens are correctly identified. See [DEBUGGING.md](DEBUGGING.md) for output examples.

### Test Parser
```bash
./eskiu compile ../examples/hello.esk --test-parser
```
Check that the AST is correctly structured and well-nested.

### Test Type Checker
```bash
./eskiu compile ../examples/hello.esk --test-typechecker
```
Check that type checking passes without errors.

### Test Codegen
```bash
./eskiu compile ../examples/hello.esk --test-codegen
```
Check that valid LLVM IR is generated.

### Full Compilation Test
```bash
./eskiu compile ../examples/hello.esk -o hello
./hello
```

For detailed guidance, see [DEBUGGING.md](DEBUGGING.md).

## Commit Guidelines

Write clear, descriptive commit messages. Use this format:

```
Phase X: Brief description of feature

- Detailed bullet point explaining what changed
- Each bullet is one logical change
- Reference any related issues
- Verify all test modes pass before committing

Fixes #ISSUE_NUMBER
```

### Example

```
Phase 5: Add struct member access validation

- Implement type normalization for struct references
- Add member lookup in type checker
- Report error if struct lacks requested member
- Update AST visitor for member expressions
- All test modes pass (lexer, parser, typechecker, codegen)

Fixes #15
```

### Best Practices

- **One feature per commit** — Easier to review and revert if needed
- **Test before committing** — Run `--test-*` modes to verify all stages pass
- **Reference issues** — Link to GitHub issues with `Fixes #N` or `Addresses #N`
- **Use imperative mood** — "Add support" not "Added support"
- **Keep messages focused** — Don't mix Phase 5 work with bug fixes in one commit

## Pull Request Process

1. **Create a PR** with a clear title: `[Phase X] Feature description`
2. **Link related issues** (if any)
3. **Describe your changes** in the PR body:
   - What was added/changed
   - Why this approach
   - Any trade-offs or limitations
4. **Link test results** (output of `--test-*` commands)
5. **Request review** from maintainers

### Example PR Description

```markdown
## Description
Implements Phase 4: Type Checker

## Changes
- Add `TypeChecker` visitor class
- Implement type inference for binary expressions
- Add error reporting with file:line:col format
- Validate function arguments against declared types

## Testing
All tests pass:
- `--test-lexer` ✅
- `--test-parser` ✅
- `--test-codegen` ✅

## Example Output
```
error: file.esk:12:5: type mismatch: expected int, got float
```

## Design Decisions

When contributing, keep these principles in mind:

1. **Performance-First** — No hidden costs, no allocations
2. **C-Style Familiarity** — Readable by C programmers
3. **Honest Memory Model** — Explicit allocation/deallocation
4. **No Borrow Checker** — Manual memory is simpler
5. **Practical OOP** — Structural typing, no inheritance
6. **Errors as Values** — `Result<T,E>` before exceptions

Discuss design decisions in PR comments if unsure.

## Documentation

When adding features, update:

- **Code comments** — Explain non-obvious logic in the compiler
- **LANGUAGE_SPEC.md** — Document new language syntax and semantics
- **PHASES.md** — Update phase status and requirements
- **API.md** — Document public C++ interfaces if you add them
- **ARCHITECTURE.md** — Update if you change compiler structure
- **DEBUGGING.md** — Add examples if you add new test modes
- **README.md** — Only if the feature is user-facing and changes how Eskiu is used
- **CONTRIBUTING.md** — Only if you change development workflow

After significant changes, update the architecture diagram in README.md.

## Questions?

Open an issue with:
- Clear question title
- Context (what you're working on)
- What you've already tried
- What documentation you checked

---

**Thank you for contributing to Eskiu!**
