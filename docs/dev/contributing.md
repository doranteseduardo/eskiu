# Contributing to Eskiu

Read [architecture.md](architecture.md) before diving in. Understanding the pipeline (lexer → parser → type checker → async transform → type checker re-run (single resolver) → codegen) is the prerequisite for any meaningful contribution.

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
There IS an automated suite: run it before any PR.
- `tests/run.sh`: the regression harness over the `.esk` test corpus (the four `--test-*` modes plus end-to-end compile/run).
- Generative + mutation fuzzer `tests/fuzz/eskiu_fuzz.py` with an **O0-vs-O2 differential oracle**: it compiles each generated program at `-O0` and `-O2` and flags any divergence in output. This is how miscompiles are caught.
- Golden-IR oracle `tests/type_zoo/snapshot.sh` + `tests/type_zoo/golden/`: captures/checks the emitted IR for the type zoo; the codegen-regression guard.
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
v0.5.0, over the v0.4 correctness release and the v0.3 self-hosting milestone. The whole compiler is reimplemented in Eskiu under `selfhost/`, parity-gated against the C++ `eskiuc`, reaching a 3-stage bootstrap fixpoint with a code generator feature-complete against the C++ corpus. The focus stays on correctness and completing the C surface, not open-ended language growth:
- v0.5.0 filled the last common C constructs the language was missing (`do`/`while`, prefix/postfix `++`/`--`, array-literal initializers, `static` locals, multidimensional arrays `T[N][M]`, and the ternary `cond ? a : b`), each landed in lockstep across both compilers so idiomatic C ports compile without workarounds.
- v0.4.0 was a four-front bug hunt (behavioral differential, sema soundness, synthesized-default audit, feature edges) that fixed a batch of miscompiles and crashes across both compilers and tightened the type system (floating-point to integer needs an explicit cast; out-of-range literals, division by a literal zero, uninitialized reads, dangling `&local`, function redefinition, and non-void fall-through are errors). The self-host sema's matching checks are deferred to the promotion track.
- Earlier 0.3.1 correctness work (via the `-O` optimization levels): a float-closure return-type miscompile, a rejected fn-type ABI mismatch, the `*T[N]` parse fix, and correct `size_t` externs.
- A `-O0`-vs-`-O2` behavioral differential (`tests/opt_differential.sh`) guards the whole corpus against optimization-path miscompiles.

The live track toward v1.0 is promoting the Eskiu-written compiler to the primary build; see `selfhost/PROMOTION_PLAN.md`. Genuinely deferred: a package manager. See docs/dev/phases.md for the full feature table and roadmap. New feature proposals require an issue.
