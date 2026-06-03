# Contributing to Eskiu

Read [architecture.md](../ARCHITECTURE.md) before diving in. Understanding the pipeline — lexer → parser → type checker → codegen — is the prerequisite for any meaningful contribution.

## Getting Started

### Clone, build, verify
...
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
make -C build -j4
./build/eskiuc --version

### Run the test modes
All four flags on examples/hello.esk before touching anything:
--test-lexer / --test-parser / --test-typechecker / --test-codegen

### Recommended reading
docs/ARCHITECTURE.md, docs/PHASES.md, ast/ast.h, sema/type_checker.cpp

## Development Workflow
Branch naming: feat/ fix/ refactor/
Commit style: imperative subject line, no trailing period
Issue vs PR: issue first when crossing phase boundaries or proposing design; direct PR for scoped fixes

## Adding a New Language Feature
Checklist: Lexer → Parser → AST → ASTVisitor → Type checker → Codegen → Test file
Visitor propagation: declare in ASTVisitor, build failure drives implementation in ASTPrinter/TypeChecker/CodeGen
parseType() and parseBlockStatement() are the single dispatch points for types and statement keywords
Template note: bodies NOT type-checked at declaration time; deferred to monomorphic instantiation

## Code Style
C++17, no deps beyond LLVM
camelCase methods/members, snake_case locals
No comments unless WHY is non-obvious
shared_ptr for all AST nodes via ExprPtr/StmtPtr/DeclPtr aliases
No RTTI except existing dynamic_cast sites

## Testing
No automated suite — four --test-* modes, manual
Per feature: test/<feature>.esk
Multi-file: test/<feature>/ directory with entry + imports
Type errors: *_error.esk with // expect: error: ... comment header
Codegen checks: single alloca per var, every branch terminated, no undef, correct pointer types

## Commit Checklist
- [ ] make -C build -j4 passes
- [ ] All four test modes work on examples/hello.esk
- [ ] New .esk test file added if applicable
- [ ] Docs updated if behavior changed

## Current Focus
v0.0.8-alpha, all core features complete.
Active milestone: INE QR decoder port (v0.1) — application port, no new language work needed.
Near-term: String.append realloc strategy, interface return type dispatch.
Phases 5.5/6/7 complete; no active language development. New feature proposals require an issue referencing the v0.1 context.
