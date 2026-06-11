# Eskiu Compiler — Code Review v2

**Branch:** `develop`
**Scope:** Full audit of all subsystems added or significantly changed since v1 review.
**Audited by:** Claude (Anthropic), June 2026
**Files reviewed:** `sema/async_transform.cpp`, `ast/ast.h`, `ast/type_qual.h`, `sema/type_checker.cpp` (new visitors), `codegen/codegen.cpp` (new visitors), `parser/parser.cpp` (new syntax), `main.cpp`

---

## Summary

The `develop` branch is an enormous leap: async/await coroutines, algebraic data types with `match`, intrinsics and atomics, pointer constness, the `const` declaration qualifier, escape analysis for closures, `for (i in A..B)` ranges, and the full `eskiuc` driver (`run`, `fmt`, `--asan`, `--ubsan`). The overall engineering quality is high — the async transform in particular is a sophisticated, well-structured piece of work.

Two critical bugs require attention before shipping:

1. **Null dereference in `visit(MatchStmt*)`** — `ed` is not guarded before use in a lambda, causing a segfault on malformed/unresolved enum names.
2. **Atomic operations hardcoded to 4-byte alignment** — `MaybeAlign(4)` is wrong for any `*int64`/`*uint64` cell; this is a silent ABI error.

---

## Findings

Severity scale: 🔴 Critical | 🟠 High | 🟡 Medium | 🔵 Low | ℹ️ Info

---

### #V1 — Null dereference in `visit(MatchStmt*)` 🔴 Critical

**File:** `codegen/codegen.cpp:3016–3024`

```cpp
EnumDecl* ed = nullptr;
// ... populated only if enumName is found in adtEnumDecls or enumInstanceArgs ...

auto variantIndex = [&](const std::string& v) -> int {
    for (size_t i = 0; i < ed->members.size(); ++i)   // ← ed may be nullptr
        if (ed->members[i].first == v) return (int)i;
    return -1;
};
auto payloadTypes = [&](int idx) {
    std::vector<llvm::Type*> v;
    for (const auto& ft : ed->payloads[idx]) ...      // ← same
    return v;
};
```

If `enumName` derivation (lines 2986–3002) produces an empty string or a name not present in either registry, `ed` stays `nullptr`. Both lambdas and the arm-body loop at line 3063 (`ed->payloads[vi][b]`) will segfault.

**Scenarios that reach this:**
- A `match` expression on a value whose type cannot be inferred by `getExprEskiuType` (e.g., a complex expression, a call return, a pointer dereference in a non-trivial path).
- Any enum name that exists in the type checker's `enumDecls` map but not in codegen's `adtEnumDecls` (registration skew).

**Fix:** Guard the lambdas and the arm-body, or assert before defining them:

```cpp
if (!ed) {
    error(node, "internal: could not resolve enum declaration for match on '" + enumName + "'");
    return;
}
```

---

### #V2 — Atomic operations hardcoded to 4-byte alignment 🟠 High

**File:** `codegen/codegen.cpp:1550, 1558`

```cpp
// atomic_swap
return builder->CreateAtomicRMW(
    llvm::AtomicRMWInst::Xchg, cell, v, llvm::MaybeAlign(4),   // ← hardcoded
    llvm::AtomicOrdering::AcquireRelease);

// atomic_cas
llvm::Value* cx = builder->CreateAtomicCmpXchg(
    cell, expected, desired, llvm::MaybeAlign(4),               // ← hardcoded
    ...);
```

`MaybeAlign(4)` is correct only for `i32`. If the user passes a `*int64` or `*uint64` cell (e.g., a 64-bit counter), LLVM will emit an atomic with 4-byte alignment, which is:
- A miscompile on x86_64 (lock CMPXCHG8B requires 8-byte alignment).
- A SIGBUS on strict-alignment architectures (AArch64 with strict alignment enabled).

The comment at line 1530 says "Atomics operate on a `*int` (i32) cell", but this constraint is not enforced in the type checker — nothing prevents passing `*int64`.

**Fix:** Derive alignment from the pointee type:

```cpp
llvm::Type* cellTy = cell->getType()->getPointerElementType(); // or derive from known type
unsigned align = module->getDataLayout().getABITypeAlign(cellTy).value();
MaybeAlign(align)
```

Or constrain the intrinsic signature in the type checker to `*int` only and document it.

---

### #V3 — async fn with no `await` throws `runtime_error` 🟠 High

**File:** `sema/async_transform.cpp:274` (approximately)

```cpp
if (awaits.empty())
    throw std::runtime_error("async function '" + fn->name + "' has no await expressions");
```

An `async fn` body with no `await` is arguably a user error, but it crashes the compiler with an unformatted `runtime_error` caught in `main.cpp`'s catch-all and printed as:

```
error: async function 'foo' has no await expressions
```

without a file/line reference. Worse, this fires during `AsyncTransform::run()`, which happens after type-checking, so there is no mechanism to produce a structured diagnostic (with source location) from here.

**Fix:** Detect this in the type checker (`visit(FunctionDecl*)`) where source location is available, and emit a proper error there. The transform can then skip await-free async functions or convert them to a trivially-resolved `Future`.

---

### #V4 — Coroutine frame field types are source-form strings 🟡 Medium

**File:** `sema/async_transform.cpp` (frame struct construction)

The async transform records each local variable's type as `vd->type` — the string as it was parsed (e.g., `"List<int>"`, `"MyAlias"`, a type parameter `"T"`). These strings are used verbatim as field types in the synthesized `__<name>_frame` struct declaration.

Codegen resolves them via `getTypeFromString`/`getTypeAlias`, which works for most concrete types. However:

- **Type aliases** that haven't been expanded yet will produce wrong `llvm::Type*` if the alias map lookup fails silently.
- **Template type parameters** (`T`, `U`) inside a generic async function will carry the raw parameter name into the frame struct. If the function is monomorphized later, the frame struct is not re-generated with the concrete type — it reuses the one with `T`, which will be resolved incorrectly.

This is a latent coupling assumption: the transform works today because all tested async functions are concrete, but it will break for generic async functions.

**Recommendation:** Either (a) normalize types to their stripped canonical form in the transform, or (b) document that `async fn` cannot be a template function and add a sema check.

---

### #V5 — `parseBlockStatement` catch-all still swallows errors silently 🟡 Medium

**File:** `parser/parser.cpp:881–883`

```cpp
} catch (...) {
    current = savePos;
}
```

This is carry-over issue #4 from the v1 review, still deferred. A genuine parse error inside a block-level declaration is silently swallowed and retried as a statement. If the statement parse also fails, the user gets the statement-level error message (often misleading) rather than the declaration error.

This now affects new constructs: a malformed `intrinsic fn` or `async fn` inside a block will fail silently and produce a confusing downstream message.

**Status:** Still deferred post-v0.2.0.

---

### #V6 — Match arm ordering: wildcard `_` not required to be last 🔵 Low

**File:** `parser/parser.cpp:1035–1052`

The parser accepts `_` (wildcard arm) at any position in a `match` block. Arms after the wildcard are unreachable, but neither the parser nor the sema exhaustiveness check warns about this.

```
match x {
    _    -> return 0;
    Some(v) -> return v;   // unreachable, no warning
}
```

**Fix:** In `parseMatchStatement`, if a `_` arm is seen and it is not the last arm, emit a parser warning. Alternatively, handle this in the exhaustiveness checker in sema.

---

### #V7 — `noStructLiteral` flag not exception-safe in `parseMatchStatement` 🔵 Low

**File:** `parser/parser.cpp:1030–1032`

```cpp
bool savedNSL = noStructLiteral; noStructLiteral = true;
ExprPtr subject = parseExpression();
noStructLiteral = savedNSL;
```

If `parseExpression()` throws (e.g., from a `consume()` failure inside the subject expression), `noStructLiteral` remains `true` for the remainder of the parse session. Subsequent struct literals anywhere in the file will be incorrectly rejected.

**Fix:** Use RAII:

```cpp
struct Guard { bool& flag; bool saved; ~Guard() { flag = saved; } };
Guard g{noStructLiteral, noStructLiteral}; noStructLiteral = true;
```

---

### #V8 — `testCodegen` runs `AsyncTransform` without prior type-check 🔵 Low

**File:** `main.cpp` (`testCodegen` function)

```cpp
AsyncTransform().run(program.get());
CodeGen codegen;
// ...
```

The `--test-codegen` mode skips the type checker entirely. For any source with `async fn`:

1. `TypeChecker` stamps `node->resolvedType` on `AwaitExpr` nodes — without this, the type is empty.
2. `AsyncTransform` calls `typeChecker.check()` internally? No — it operates on the raw AST.
3. Codegen's `visit(AwaitExpr*)` throws unconditionally (by design, as a guard that the transform ran). But if the transform ran on an un-type-checked AST, the generated resume function may reference unresolved types, causing a codegen crash.

Similarly, `testTypeChecker` does not run `AsyncTransform`, so the type-checked AST is never transformed — a full pipeline test requires running all three stages in sequence.

**Fix:** For `--test-codegen`, run type-check first:

```cpp
TypeChecker tc; tc.sourceFile = filename;
if (!tc.check(program.get())) { /* error */ return; }
AsyncTransform().run(program.get());
```

---

### #V9 — `tyq::isPtr` leading-star branch is dead or wrong 🔵 Low

**File:** `ast/type_qual.h:52–54`

```cpp
inline bool isPtr(const std::string& s) {
    std::string t = strip(s);
    return !t.empty() && (t.front() == '*' || t.back() == '*');
}
```

The canonical type encoding uses a trailing star (`"int*"`, `"char**"`). A leading star (`"*int"`) is not produced by the parser's `parseType()`. The `t.front() == '*'` branch appears to be dead code; if somehow reached, it would incorrectly classify `"*int"` as a pointer when it isn't a valid type string.

**Fix:** Remove the leading-star branch, or add a comment explaining when it would fire. Confirm via grep that no type string with a leading star enters the type system.

---

### #V10 — `lowerStmt` throw for `ForInStmt` is dead code 🔵 Low

**File:** `sema/async_transform.cpp:~564`

```cpp
throw std::runtime_error("await inside ForInStmt not lowered yet");
```

The async transform's for-in desugar pass (which runs before `lowerStmt`) converts every `ForInStmt` into a `ForStmt` when `resolvedElemType` is set. So `lowerStmt` should never see a `ForInStmt`. The throw is therefore dead code.

The risk is that a future refactor removes or conditionalizes the desugar, causing this throw to fire in production with a confusing message. This is a correctness landmine masquerading as a safety guard.

**Fix:** Replace with an `assert(false, "ForInStmt must be desugared before async transform")` and a comment explaining the dependency.

---

### #V11 — AArch64 target initialized unconditionally; no warning when X86 target requested without support ℹ️ Info

**File:** `codegen/codegen.cpp:generateCode()` and `CMakeLists.txt`

AArch64 target support is initialized unconditionally. X86 is gated behind `#ifdef ESKIU_HAS_X86`. If the compiler is built without X86 support and the user passes `--target x86_64-pc-linux-gnu`, the target machine creation will fail silently or produce a misleading LLVM error.

**Recommendation:** After target machine creation fails, check if the requested triple looks like an X86 target and emit a friendly message: `"error: X86 target not compiled in; rebuild with ESKIU_HAS_X86"`.

---

### #V12 — `eskiuc fmt` string/char literal skip doesn't handle escaped delimiters correctly ℹ️ Info

**File:** `main.cpp` (`formatSource`)

```cpp
if (c == '"' || c == '\'') {
    char q = c; ++i;
    while (i < t.size() && t[i] != q) { if (t[i] == '\\') ++i; ++i; }
    continue;
}
```

The escape handling increments `i` by 1 for `\\`, then the outer loop increments again — so `\\` (escaped backslash) is handled correctly. However, a string ending in `\\` (backslash at end of content) will cause the loop to overshoot the closing quote. Additionally, a `/*` inside a string literal will still set `inBlock = true` because the block-comment check runs before the string-skip in the nesting-update loop.

**Recommendation:** Run the string-skip before the comment-skip in the nesting-update pass, or use a unified mini-lexer.

---

## Summary Table

| # | Severity | File | Description |
|---|----------|------|-------------|
| V1 | 🔴 Critical | `codegen/codegen.cpp:3016` | Null deref on `ed` in `visit(MatchStmt*)` |
| V2 | 🟠 High | `codegen/codegen.cpp:1550,1558` | `MaybeAlign(4)` wrong for 64-bit atomic cells |
| V3 | 🟠 High | `sema/async_transform.cpp:274` | Async fn with no awaits throws `runtime_error` instead of sema error |
| V4 | 🟡 Medium | `sema/async_transform.cpp` | Frame field types are source-form strings; breaks for generic async fns |
| V5 | 🟡 Medium | `parser/parser.cpp:881` | `catch(...)` silently swallows block-decl parse errors (carry-over #4) |
| V6 | 🔵 Low | `parser/parser.cpp:1035` | Wildcard `_` arm not required to be last; unreachable arms get no warning |
| V7 | 🔵 Low | `parser/parser.cpp:1030` | `noStructLiteral` flag not RAII-protected; leaks on exception |
| V8 | 🔵 Low | `main.cpp` | `--test-codegen` skips type-check; async codegen on un-typed AST |
| V9 | 🔵 Low | `ast/type_qual.h:53` | `isPtr` leading-star branch is dead or incorrect |
| V10 | 🔵 Low | `sema/async_transform.cpp:564` | `ForInStmt` throw in `lowerStmt` is dead code, should be `assert` |
| V11 | ℹ️ Info | `codegen/codegen.cpp` | No friendly error when X86 target requested without compiled-in support |
| V12 | ℹ️ Info | `main.cpp` | `eskiuc fmt` string/comment interaction edge case |

---

## Positive Observations

- **Async transform architecture** (`sema/async_transform.cpp`) is well-designed: state graph, break/continue target stacks, `rewrite()` / `rewritePlain()` split, `loopEscapes()` / `stmtTerminates()` helpers. The desugar-before-lower pipeline is the right approach.
- **`tyq::` namespace** is clean and well-commented. The `dropsConst` semantic (only fires for same-stripped-shape pointer pairs) is explicitly documented and correct.
- **Exhaustiveness checking** in the type checker for `match` is real and correct. Variant index lookup and payload type substitution for generic enums is handled consistently between sema and codegen.
- **Escape analysis** for closures is sound-by-default (`LambdaExpr::escapes = true`) and correctly conservative.
- **`for (i in A..B)` desugar** at parse time (into a standard `ForStmt`) is the right choice — it inherits all downstream machinery for free.
- **`eskiuc run/fmt`** are useful and well-integrated. The shebang use case is explicitly documented.
- **`#ifdef __APPLE__` / `#ifdef __linux__`** host OS macros pre-injected into the macro map is a clean way to make stdlib portable without parser changes.
- **`-Wall` / `-Wextra` / `--asan` / `--ubsan`** flags are wired correctly through the whole pipeline.

---

*End of CODE_REVIEW_V2.md — eskiu v0.2.0-dev, develop branch*
