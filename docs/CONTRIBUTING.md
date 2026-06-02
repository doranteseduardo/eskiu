# Contributing to Eskiu

Thank you for your interest in contributing to Eskiu! This document outlines the development workflow and guidelines.

## Getting Started

1. **Fork and clone** the repository
2. **Set up** the environment (see [SETUP.md](../SETUP.md))
3. **Create a feature branch**: `git checkout -b feature/your-feature`
4. **Make your changes** following the guidelines below
5. **Test thoroughly** using the testing modes
6. **Submit a pull request**

## Development Workflow

### Understanding the Pipeline

The compiler pipeline has distinct phases, each building on the previous:

```
Source → Lexer → Parser → Type Checker → Codegen → LLVM
```

**Pick a phase** and work on completing it. Don't skip ahead.

### Phases Overview

- **Phase 0** ✅ Setup (done)
- **Phase 1** ✅ Lexer (done) — tokenizes `.esk` files
- **Phase 2** ✅ Parser (done) — builds abstract syntax tree
- **Phase 3** ✅ Codegen (done) — emits LLVM IR
- **Phase 4** ⏳ Type Checker — validates types before codegen
- **Phase 5** 🔮 Structs & Interfaces — composite types
- **Phase 6** 🔮 Memory Management — `alloc`/`free`, String
- **Phase 7** 🔮 Standard Library — collections, Result<T,E>
- **Phase 8** 🔮 Lambdas & Closures — first-class functions
- **Phase 9** 🔮 Threads (v0.2) — pthreads integration
- **Phase 10** 🔮 Exceptions (v1.0) — try/catch/finally
- **Phase 11** 🔮 Async/Await (v2.0) — coroutines

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

Use the three test modes to validate your work:

### Test Lexer
```bash
./eskiuc examples/hello.esk --test-lexer
```
Check that tokens are correctly identified.

### Test Parser
```bash
./eskiuc examples/hello.esk --test-parser
```
Check that the AST is correctly structured.

### Test Codegen
```bash
./eskiuc examples/hello.esk --test-codegen
```
Check that valid LLVM IR is generated.

## Commit Guidelines

Write clear, descriptive commit messages:

```
Phase 1: Add lexer support for string literals

- Implement read_string() method in Lexer class
- Handle escape sequences (\n, \t, \\, \")
- Add STRING_LIT token type
- Test with examples/hello.esk

Fixes #42
```

**Format:**
```
[Phase X]: Brief description

- Bullet points with details
- One feature per bullet
- Reference issues if applicable

Fixes/Closes #issue_number
```

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
- **Code comments** for complex logic
- **PHASES.md** for phase-specific details
- **LANGUAGE_SPEC.md** for new syntax
- **API.md** for public interfaces
- **README.md** if the feature is user-facing

## Questions?

Open an issue with:
- Clear question title
- Context (what you're working on)
- What you've already tried
- What documentation you checked

---

**Thank you for contributing to Eskiu!**
