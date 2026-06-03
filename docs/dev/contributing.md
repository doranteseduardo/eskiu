# Contributing to Eskiu

Read [architecture.md](../ARCHITECTURE.md) before diving in. Understanding the pipeline — lexer → parser → type checker → codegen — is the prerequisite for any meaningful contribution.

## Getting Started

### Clone, build, verify

```bash
git clone <repo-url> eskiu
cd eskiu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
make -C build -j4
```

Verify the build:

```bash
./build/eskiuc --version
```

### Run the test modes

All four flags must work on `examples/hello.esk` before you touch anything:

```bash
./build/eskiuc examples/hello.esk --test-lexer
./build/eskiuc examples/hello.esk --test-parser
./build/eskiuc examples/hello.esk --test-typechecker
./build/eskiuc examples/hello.esk --test-codegen
```

The `--test-codegen` output should be valid LLVM IR containing `@add` and `@main`.

### Recommended reading

1. `docs/ARCHITECTURE.md` — pipeline, data flow, symbol table, type mappings
2. `docs/PHASES.md` — what each phase owns and what is still open
3. `ast/ast.h` — all node types and the `ASTVisitor` base class
4. `sema/type_checker.cpp` — how the visitor pattern is used in practice

---

## Development Workflow

### Branch naming

```
feat/short-description       # new capability
fix/short-description        # bug fix
refactor/short-description   # internal cleanup with no behavior change
phase5/short-description     # phase-scoped work
```

### Commit message style

One imperative line, no trailing period, no body required for trivial changes:

```
Add struct GEP emission in codegen
Fix pointer type normalization in type checker
Refactor parseType to handle trailing * syntax
```

For non-trivial changes, add a blank line then bullet points — but keep the subject line self-contained.

### When to open an issue vs just a PR

Open an issue first when:
- The change touches more than one phase boundary
- You are proposing a design decision (new syntax, new type rule, new codegen strategy)
- You found a bug but do not yet have a fix

Skip the issue and go straight to a PR when:
- The fix is clearly scoped (a wrong LLVM type, a missing visitor case, a typo in an error message)
- The work is already tracked in PHASES.md

---

## Adding a New Language Feature

Every language feature touches at least four files. Work in this order — do not skip ahead.

### Checklist

- [ ] **Lexer** — add the new keyword or operator to `TokenType` and recognize it in `Lexer::nextToken()`
- [ ] **Parser** — parse the new construct; update `parseType()` if you are adding a type keyword, update `parseBlockStatement()` if you are adding a statement form
- [ ] **AST** — define the new node in `ast/ast.h`; add `accept(ASTVisitor* v)` override
- [ ] **ASTVisitor** — add a pure `visit()` overload for the new node to the `ASTVisitor` base class; then implement it in every existing implementor: `ASTPrinter`, `TypeChecker`, `CodeGen`
- [ ] **Type checker** — implement `visit()` in `sema/type_checker.cpp`; emit a clear error with file:line:col if types do not match
- [ ] **Codegen** — implement `visit()` in `codegen/codegen.cpp`; emit correct LLVM IR using `IRBuilder`
- [ ] **Test file** — add a `.esk` file in `test/` that exercises the feature end-to-end

### How the visitor pattern propagates

`ASTVisitor` in `ast/ast.h` is the central contract. Adding a new node means:

1. Declare `virtual void visit(YourNode* node) = 0;` in `ASTVisitor`.
2. The compiler will fail to build until every concrete visitor (`ASTPrinter`, `TypeChecker`, `CodeGen`) implements it. Use that build failure as a checklist.
3. Each `accept()` implementation is always one line: `visitor->visit(this);`.

### parseType() and parseBlockStatement()

`parseType()` is the single place where type keywords map to AST type strings. If your feature introduces a new primitive or parameterized type (e.g., `Result<T,E>` in Phase 7), add the token recognition there — nowhere else.

`parseBlockStatement()` dispatches to sub-parsers based on the current token. If your feature introduces a new statement-level keyword (e.g., `spawn` in Phase 9), add the branch there.

---

## Code Style

- C++17. No external dependencies beyond LLVM.
- `camelCase` for methods and class members. `snake_case` for local variables.
- No comments unless the WHY is non-obvious. Code that explains what it does is a smell; code that explains why it does it is documentation.
- Error messages must include context: file, line, column, and the name of the thing that was wrong. "type mismatch" is not enough; "type mismatch in argument 2 of call to 'decode': expected uint8, got int" is.
- All AST node ownership is through `shared_ptr`. Use the aliases already defined: `ExprPtr`, `StmtPtr`, `DeclPtr`. Do not introduce raw owning pointers.
- No RTTI except at existing `dynamic_cast` sites. If you need to distinguish node kinds, add a virtual method or use the visitor.

---

## Testing

There is no automated test suite. Verification is manual using the four `--test-*` modes.

### For every new feature

Add a `.esk` file in `test/` that compiles clean through all four modes. Name it after the feature:

```
test/struct_field_access.esk
test/pointer_arithmetic.esk
```

### For type errors

Add a `*_error.esk` file alongside the happy-path file and confirm that `--test-typechecker` emits the expected error. The error text should appear in a comment at the top of the file:

```eskiu
// expect: error: type mismatch: expected int, got float
int x = 3.14;
```

### Verifying codegen

Run `--test-codegen` and read the IR. Check that:
- Each variable has exactly one `alloca` in the entry block
- Every branch has a terminator (`br`, `ret`)
- There are no dangling `undef` values where concrete values are expected
- Pointer types match (`i8*` for `string`, `i32*` for `*int`, etc.)

---

## Commit Checklist

Before committing:

- [ ] Build passes: `make -C build -j4`
- [ ] `--test-lexer` works on `examples/hello.esk`
- [ ] `--test-parser` works on `examples/hello.esk`
- [ ] `--test-typechecker` works on `examples/hello.esk`
- [ ] `--test-codegen` works on `examples/hello.esk`
- [ ] New `.esk` test file added if applicable
- [ ] `docs/PHASES.md` updated if phase status changed

---

## Phase Boundaries

- Do not implement Phase N+2 while Phase N+1 is open.
- **Phase 5 is the current target.** Work on structs, interfaces, and templates. The AST nodes and type checker groundwork exist; codegen is not yet wired.
- Do not add heap allocation (`alloc`/`free` plumbing, `alloca`-to-heap promotion) until struct codegen is complete and Phase 5 is closed.
- Do not add stdlib (`Result<T,E>`, collections, `io` module) until Phase 6 memory primitives are done.

Phase boundaries exist because each phase produces a compiler that is self-consistent. A compiler that half-supports structs and half-supports heap allocation is harder to debug than one that fully supports neither.
