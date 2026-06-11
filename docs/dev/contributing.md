# Contributing to Eskiu

Read [architecture.md](architecture.md) before diving in. Understanding the pipeline — lexer → parser → type checker → codegen — is the prerequisite for any meaningful contribution.

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
docs/dev/architecture.md, docs/dev/phases.md, ast/ast.h, sema/type_checker.cpp

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
v0.2.0 — the "backend services" release. The systems foundation (v0.1.0) is shipped and tagged; v0.2.0 layers the concurrent-backend stack on top: async/await, the full HTTP/2 stack (framing, HPACK, streams + flow control, multiplexed server, TLS/ALPN), sum types with `match`, and the stdlib (allocators, threading, sockets, async runtime, JSON, and more).
Genuinely deferred: a package manager, and the tighter locals-across-await liveness optimization. See docs/dev/phases.md for the full feature table and roadmap. New feature proposals require an issue.
