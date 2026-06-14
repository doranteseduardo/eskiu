# Contributing to Eskiu

Read [architecture.md](architecture.md) before diving in. Understanding the pipeline — lexer → parser → type checker → async transform → type checker re-run (single resolver) → codegen — is the prerequisite for any meaningful contribution.

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
docs/dev/architecture.md, docs/dev/phases.md, ast/ast.h, sema/type_checker.cpp + sema/typecheck_{decl,stmt,expr,type}.cpp, sema/type.{h,cpp} (the `ty::Type` IR), codegen/codegen_*.cpp (the codegen split)

## Development Workflow
Branch naming: feat/ fix/ refactor/
Commit style: imperative subject line, no trailing period
Issue vs PR: issue first when crossing phase boundaries or proposing design; direct PR for scoped fixes

## Adding a New Language Feature
Checklist: Lexer → Parser → AST → ASTVisitor → Type checker → Codegen → Test file
Visitor propagation: declare in ASTVisitor, build failure drives implementation in ASTPrinter/TypeChecker/CodeGen
parseType() and parseBlockStatement() are the single dispatch points for types and statement keywords
New type spellings go in sema/type.cpp (`ty::Type::parse`, the one grammar interpreter shared by sema and codegen) and the typecheck split files (typecheck_type.cpp et al.); do not add a second type-string evaluator
Template note: bodies NOT type-checked at declaration time; deferred to monomorphic instantiation

## Code Style
C++17, no deps beyond LLVM
camelCase methods/members, snake_case locals
No comments unless WHY is non-obvious
shared_ptr for all AST nodes via ExprPtr/StmtPtr/DeclPtr aliases
No RTTI except existing dynamic_cast sites

## Testing
There IS an automated suite — run it before any PR.
- `tests/run.sh` — the regression harness over the `.esk` test corpus (the four `--test-*` modes plus end-to-end compile/run).
- Generative + mutation fuzzer `tests/fuzz/eskiu_fuzz.py` with an **O0-vs-O2 differential oracle**: it compiles each generated program at `-O0` and `-O2` and flags any divergence in output — this is how miscompiles are caught.
- Golden-IR oracle `tests/type_zoo/snapshot.sh` + `tests/type_zoo/golden/` — captures/checks the emitted IR for the type zoo; the codegen-regression guard.
- `--asan` / `--ubsan` gates in CI for runtime memory errors and undefined behavior.
- A formatter-idempotency pass (`eskiuc fmt --check`) over every test.
Manual checks still apply per feature:
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
v0.2.4 — the reinforce-the-language phase. The "backend services" release (v0.2.0) shipped and is tagged; since then the focus has been **hardening and type soundness**, not new language features (feature freeze):
- Source modularization (the codegen/sema/parser splits), the asan/ubsan CI gate, and the generative fuzzer with its O0-vs-O2 differential oracle (0.2.1–0.2.2).
- Bounded generics (`<T: Iface>` / `<T: A + B>`) and primitives-satisfy-constraints (0.2.2–0.2.3).
- The structured `ty::Type` IR (0.2.3) and the type unification that made the type checker the **single resolver** (0.2.4): codegen consumes the resolved per-expression types, and `getTypeFromString` dispatches on `ty::Type::parse`. This closed the old two-evaluator miscompile risk and fixed three latent miscompiles (float-lit double, ptr-deref width, char zext).
Genuinely deferred: a package manager, self-hosting/renderer, and the tighter locals-across-await liveness optimization. See docs/dev/phases.md for the full feature table and roadmap. New feature proposals require an issue.
