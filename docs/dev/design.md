# Eskiu Design Decisions

This document records the reasoning behind the architectural and language choices in Eskiu. Each section states the decision, lists the alternatives considered, and explains why they were rejected.

---

## Origin and vision

Eskiu was built in response to a concrete problem: compute-intensive work usually gets split across several languages, each strong at one thing. A typical pipeline uses C for performance-critical code, Go for concurrent services, C++ for libraries, and Python for glue, and every seam between them adds a toolchain, its own idioms, and an interop cost.

Eskiu's answer is a single language with the power of C and the immediacy of a scripting language: it compiles to native code through LLVM, yet `eskiuc run file.esk` runs a `.esk` file directly, the way you would a Python or Ruby script. It starts from solid systems foundations and grows upward into the domain, so it can cover that ground without trading native performance for ergonomics.

**Foundation phase.** Establish a language that can do everything C can do: native performance, explicit memory, direct access to any C library. Validate it against real production code before claiming it works. This phase is complete: a cryptographic pipeline runs entirely in Eskiu, 2.5× faster than the reference C.

**v0.1 milestone, shipped.** Booting Eskiu code on bare metal in QEMU without libc, the classic proof-of-concept for a systems language. Delivered in v0.1.0 via inline assembly, freestanding mode, and `volatile`: an ARM64 kernel written in Eskiu boots in QEMU (`-M virt`) on the PL011 UART, with no libc or C runtime.

**Domain specialisation phase.** Once the systems foundation is stable, make the domain types that high-throughput services actually work with first-class in the language, without losing general systems capability.

| Stage | Eskiu | Reference C |
|---|---|---|
| QR extraction | 71.7 ms | 185.5 ms |
| Crypto (AES+RSA) | 2.8 ms | 2.9 ms |
| Output decode | < 1 ms | 0.5 ms |
| Total | **74.4 ms** | 188.9 ms |

2.5× faster than the reference C. The crypto stage matches hand-written C within 0.1 ms.

Every design decision below was evaluated against two questions: does it get the decoder working correctly, and does it serve the long-term domain specialisation goal?

---

## 1. C-Style Syntax

**Decision:** Eskiu uses C-style syntax: braces for blocks, semicolons as statement terminators, type-before-name declarations (`int x = 5`), `for (init; cond; step)`, explicit return types before function names.

**Alternatives considered:**

- *Rust-style syntax*: `let` bindings everywhere, `fn` keyword, `->` return types, `impl` blocks for methods, `pub`/`mod` visibility. Rejected because the target audience is engineers already writing C, OpenSSL calls, and `extern` function declarations. Switching to `fn main() -> i32` and `impl Struct { ... }` adds cognitive load with no performance payoff.
- *Go-style syntax*: `:=` short declarations, `func` keyword, no semicolons, package declarations. Rejected for the same familiarity reason. Go's `func (s *Struct) Method()` receiver syntax is unfamiliar to C programmers and requires a different mental model for method ownership.
- *Python-like indentation-sensitive syntax*. Rejected immediately: indentation-sensitive grammars are hostile to a hand-written recursive-descent parser and to generated code with templates.

**Why C-style was chosen:** The decoder target requires calling `extern` C functions (`EVP_DecryptInit_ex`, `zxing::ImageView`), declaring C-compatible structs (`uint8[858]` fixed arrays, pointer fields), and reasoning about memory layout in terms the target audience already holds. C syntax is the least surprising bridge between "the language I'm writing" and "the C ABI I'm calling into."

---

## 2. No Garbage Collector

**Decision:** Eskiu has no garbage collector. Memory is managed explicitly with `alloc<T>(n)` (which emits `malloc`) and `free(ptr)` from the `<mem>` stdlib. There is no reference counting and no tracing GC. (As of v0.2.0 the stdlib does ship arena/bump/pool allocators in `<alloc>` over the `alloc_with` primitive (see below), but they are opt-in library types, not built into the language runtime.)

**Alternatives considered:**

- *Tracing GC (Boehm GC or custom)*: A conservative tracing GC can be linked as a library and used transparently. Rejected because GC pauses (even short ones) are unacceptable when the target is a 74 ms pipeline where total runtime matters. A GC collection triggered mid-pipeline would ruin the benchmark.
- *Reference counting (like Swift ARC)*: Automatic reference counting eliminates pause times but adds retain/release overhead on every pointer assignment. For a pipeline that allocates a handful of fixed buffers at startup and frees them at the end, ARC overhead is pure waste.
- *Arena/bump allocator built into the language*: Would require the compiler to reason about object lifetimes, which is a significant semantic addition. Kept out of the language; instead v0.2.0 ships `<alloc>` (Bump/Arena/Pool/FirstFit) as ordinary stdlib types over the `alloc_with(&a, T, n)` primitive: the strategy is a plain value, not a language feature.

**Why no GC:** The pipeline allocates known-size buffers (`alloc<uint8>(858)`, `alloc<uint8>(4096)`), uses them, and frees them in a deterministic sequence. Explicit `alloc<T>`/`free` matches the mental model exactly and produces the same object code as hand-written C.

---

## 3. No Borrow Checker

**Decision:** Eskiu does not enforce ownership or borrowing rules at compile time. Pointers (`*T`) are raw; aliasing is permitted; there is no lifetime annotation system.

**Alternatives considered:**

- *Rust-style borrow checker*: Eliminates use-after-free and data races at compile time, but requires the programmer to annotate lifetimes on every function signature that passes pointers. For a project with a single-threaded pipeline and no concurrent access patterns in v0.1, the annotation burden far exceeds the safety benefit.
- *Affine types / single-ownership without explicit lifetimes*: Simpler than Rust, but still requires the compiler to track ownership through assignment, function calls, and return values. Implementing this correctly for structs-with-pointer-fields (as the decoder uses) requires substantial semantic infrastructure.

**Why rejected:** The decoder code pattern is: allocate a buffer, pass a pointer to a C library function, check the result, free the buffer. This pattern is safe by inspection, and enforcing it mechanically would require the programmer to teach the borrow checker about C library semantics via annotations, which is the same work as writing the C directly. The explicit-control goal means the programmer is trusted to manage pointers correctly, the same assumption made in C.

---

## 4. LLVM as Backend

**Decision:** Eskiu emits LLVM IR via `IRBuilder<>` and uses `TargetMachine` + `legacy::PassManager` to produce native `.o` files. No custom backend.

**Alternatives considered:**

- *Emit C source*. Simplest possible backend: emit a `.c` file and call a C compiler. Rejected because it makes it impossible to control codegen precisely (integer width coercion, GEP arithmetic, struct layout), adds a runtime dependency on a C compiler, and prevents a future path to LLVM optimization passes.
- *Emit x86-64 assembly directly*: Maximum control, but requires implementing register allocation, instruction selection, and calling conventions for every target. The decoder target already needed arm64 (Apple Silicon) and x86-64 support simultaneously.
- *QBE backend*: QBE is a lightweight compiler backend with a clean IR. Rejected because it lacks WASM output and has limited optimization passes compared to LLVM. For a project whose v0.1 goal is speed, LLVM's `-O2` optimizations are valuable.
- *Cranelift*: The backend used by Wasmtime and rustc's debug builds. Rejected because the LLVM C++ API was more familiar to the initial developer and because LLVM's ecosystem (target support, sanitizers, DWARF debug info) is more mature.

**Why LLVM:** A single LLVM IR module covers arm64 (Apple Silicon), x86-64 (Linux/Windows), RISC-V, and WASM with no changes to the compiler front end. `getDefaultTargetTriple()` + `InitializeNativeTarget()` means the same compiler binary produces correct code on the machine it runs on. The `emitObjectFile()` function is 15 lines; LLVM does the rest.

---

## 5. Visitor Pattern for Passes

**Decision:** Every compiler pass (type checker, code generator, AST printer) implements the `ASTVisitor` abstract class. Each AST node holds a pure-virtual `accept(ASTVisitor*)` method. Dispatch is double-dispatch: `node->accept(pass)` calls `pass->visit(node)` with the node's concrete type resolved at compile time through the node's own `accept` override.

**Alternatives considered:**

- *`dynamic_cast` chains in each pass*: `if (auto* e = dynamic_cast<BinaryExpr*>(node)) { ... } else if (auto* e = dynamic_cast<CallExpr*>(node)) { ... }`, works but scatters the type-switch pattern throughout each pass, making it easy to miss a node type silently.
- *`std::variant` + `std::visit`*: Using `std::variant<BinaryExpr, CallExpr, ...>` for the node union gives exhaustive static dispatch. Rejected because the AST hierarchy is deep (46 concrete node types across three inheritance branches, including `Program`), variant nesting becomes unwieldy, and pointer-based polymorphism is needed because some node subtrees are co-owned (codegen shares a method/template `FunctionDecl`'s `body` across the synthesized and instantiated copies; see the smart-pointer note in `architecture.md`).
- *Embedding pass logic in AST nodes (like `codegen()` on each node)*: Rejected because it couples the AST definition to specific passes. Adding a new pass (e.g. a future linter or a dataflow analysis) would require modifying every node class.

**Why visitor:** Adding a new pass means implementing a new `ASTVisitor` subclass. Existing AST nodes and existing passes are not touched. The compiler has three pass implementations today (`ASTPrinter`, `TypeChecker`, `CodeGen`); each was added without modifying the other two. The `ASTVisitor` base class declares one pure-virtual `visit()` overload per concrete node type (42 today), so the compiler enforces coverage: a new node type added to `ast.h` that lacks a `visit()` overload causes a compile-time error in all three passes simultaneously.

---

## 6. `BlockItem = std::variant<DeclPtr, StmtPtr>`

**Decision:** `BlockStmt::items` is `std::vector<std::variant<DeclPtr, StmtPtr>>` rather than two separate lists or a common `Stmt` base for declarations.

**Alternatives considered:**

- *Require all declarations at the top of a block (Pascal/early C style)*: Forces the programmer to reorganize code to hoist declarations, which conflicts with the C-familiarity goal. Modern C (C99+) allows declarations anywhere in a block.
- *Treat `VarDecl` as a `Stmt` subclass*. The simplest change: `VarDecl` inherits from `Stmt`, `BlockStmt::items` is `vector<StmtPtr>`. Rejected because it conflates semantic roles: declarations introduce names into the scope; statements produce side effects or control flow. The distinction matters in the type checker's two-pass strategy: the first pass registers all struct and function declarations; if `VarDecl` were a `Stmt`, the registration pass would need to inspect statement subtrees, not just the top-level declaration list.
- *Two separate lists `decls` and `stmts` in `BlockStmt`*: Loses the ordering information. If a programmer writes `int x = f(); use(x); int y = x + 1;`, the declaration of `y` must come after `use(x)` is evaluated. Two separate lists cannot represent this correctly.

**Why `variant`:** `std::variant<DeclPtr, StmtPtr>` preserves both the ordering (a single `vector` iterated in sequence) and the semantic distinction (visitors dispatch to the declaration or statement path with `std::holds_alternative` / `std::get`). The type checker's `visit(BlockStmt*)` iterates `items` in order and calls `decl->accept(this)` or `stmt->accept(this)` based on the active alternative. This matches C semantics exactly: a variable declared in the middle of a block is visible to all code after it in the same block, but not before.

---

## 7. Monomorphic Templates (Not Generic/Polymorphic)

**Decision:** Template structs and template functions are instantiated monomorphically: `Result<int, string>` and `Result<bool, string>` are two separate LLVM struct types (`%Result_int_string` and `%Result_bool_string`), emitted independently. There is no polymorphic representation (no type-erased value + vtable, no boxing).

**Alternatives considered:**

- *Type-erased generics (Java/Go-style)*: A single `Result` struct holds a `void*` value and a type tag. Accesses go through casts. Rejected because it requires boxing primitives (`int` becomes a heap-allocated `Integer*`) and defeats the purpose of a `uint8[858]` fixed-size array field, which the decoder relies on having a predictable stack layout.
- *Reified generics with a single compiled copy (Haskell `Data.Map` style)*: Emit one copy of the function with `ptr` arguments and rely on the caller to cast. Same problem as type erasure: struct field access becomes a byte-offset calculation, not a GEP with known field types.
- *C++ template-like deferred compilation*: Full C++ template semantics, including SFINAE and partial specialization. Rejected as premature: the decoder target needed `Result<T,E>` and `List<T>`, not a metaprogramming system. The complexity of C++ template instantiation (two-phase name lookup, dependent type resolution) is substantial and incompatible with the goal of keeping the type checker simple.

**Why monomorphic:** Monomorphization is the simplest implementation that produces correct, fast code. Each instantiation is a concrete LLVM struct type with known field offsets. `getTypeFromString("Result<int,string>")` calls `ensureTemplateInstantiated("Result_int_string", "Result", {"int","string"})`, which creates `%Result_int_string = type { i32, i32, ptr }` the first time it is needed. Subsequent uses find it in `structTypes`. The cost is code size (multiple copies of a template function body), which is acceptable for the decoder's small number of instantiations.

---

## 8. Go-Style Structural Interfaces

**Decision:** An interface is satisfied implicitly by having all the required methods. No `implements` keyword, no declaration that a struct satisfies an interface. If `struct Point` has a method `float sum()` and `interface Summable` declares `float sum()`, then `Point` satisfies `Summable`, automatically, structurally.

**Alternatives considered:**

- *Explicit `implements` declaration (Java-style)*: Requires the programmer to declare conformance. This creates a coupling between the struct definition and the interface definition. If the struct is in an imported file and the interface is defined later, the imported file must be modified. For a language designed to call C libraries via `extern`, there will always be structs whose methods are defined separately from the interface that describes them.
- *Rust-style `impl Trait for Type`*: More explicit than Java (the `impl` block is separate from both the struct and the trait), but still requires an explicit linking declaration. Requires the programmer to know both the struct and the interface at the point of writing the `impl` block.
- *C++ pure virtual base classes*: Requires the struct to inherit from the interface, which forces a vtable pointer into the struct's memory layout. This changes the struct's ABI and makes it impossible to use the struct as a plain C-compatible type.

**Why structural:** The type checker's `structSatisfiesInterface(functionSignatures, structName, iface)` function iterates the interface's method signatures and checks whether `structName_methodName` exists in `functionSignatures`. This check requires no annotation from the programmer; it falls out of the method registration that already happens in the type checker's first pass. Structural interfaces also let future stdlib code define interfaces that existing structs satisfy without modifying those structs.

---

## 9. `Result<T,E>` Before Exceptions

**Decision:** Error propagation in Eskiu is done via `Result<T,E>` values (`struct Result<T,E> { int ok; T value; E error; }`). This was implemented before exceptions, and the decoder was written against it (checking `result.ok` and branching manually). Both now ship in v0.1.0: exceptions (`try`/`catch`/`finally`/`throw`) landed as well, and the postfix `?` operator was added to make `Result`-based code ergonomic: `divide(a, b)?` returns the `Err` early or unwraps the value, removing the manual `if (!r.ok) return r;` boilerplate. The design rationale below (why `Result<T,E>` came first) still holds.

**Alternatives considered:**

- *Exceptions first (C++ style)*: `throw` / `catch` / `finally` with LLVM `invoke` / `landingpad`. Rejected for v0.1 because implementing correct EH requires an EH personality function, DWARF unwind tables, and correct stack unwinding through `extern` C frames (OpenSSL, zxing-cpp). This is non-trivial and was not needed to hit the benchmark target.
- *Panic and abort (Go-style, no recovery)*: `panic("msg")` that terminates the process. Simpler to implement but eliminates the ability to handle errors gracefully. The decoder must distinguish "QR code not found" (user error, return a message) from "malloc failed" (fatal).
- *Error codes via return value (C style, no struct)*: Return `int` from functions and check `!= 0`. Works but loses the error value (you know it failed, not why). `Result<T,E>` encodes both the success value and the error value in the same return, with `ok` as a discriminant.

**Why `Result<T,E>` first:** `Result<T,E>` is zero-cost when `ok == 1`: the caller accesses `result.value` directly via GEP without any dynamic dispatch or heap allocation. The struct layout is known at compile time. Error propagation is explicit in the code (the `?` operator is still a visible, checkable control-flow point, not a hidden unwind), which matches the explicit-control philosophy of the language. Exceptions were added alongside it without removing `Result<T,E>`; they serve different use cases (recoverable domain errors vs. unrecoverable runtime failures).

---

## 10. Hand-Written Recursive-Descent Parser

**Decision:** The parser is a hand-written recursive-descent parser with no parser generator. The grammar is encoded as a call graph: `parseProgram` calls `parseDeclaration`, which calls `parseFunctionDecl`, `parseStructDecl`, etc.

**Alternatives considered:**

- *yacc/bison (LALR(1) parser generator)*: Adds a build dependency on yacc/bison, requires a separate `.y` grammar file, and produces error messages that are difficult to customize. LALR(1) grammars cannot express certain constructs that are natural in recursive descent (e.g. the `IDENT { ... }` struct-init ambiguity with block statements, or the `ident<T>(args)` template call ambiguity with `<` as less-than).
- *ANTLR*: A more powerful parser generator (LL(\*)), but adds a Java runtime dependency and generates C++ code that is harder to debug than hand-written code. Customizing error recovery requires understanding ANTLR's error listener API.
- *PEG / Packrat parser*: Guaranteed linear time with backtracking via memoization. Packrat parsers are elegant but consume memory proportional to input size × grammar size (the memoization table). For source files that are small (the decoder is a few hundred lines), this is not a problem in practice, but the implementation complexity is higher than recursive descent.

**Why recursive descent:** Error messages are the primary reason. When `consume(SEMICOLON, "expected ';'")` fails, the throw site is the exact function that expected the semicolon, one function call away from the code the user wrote. The parser can provide "expected ';' after variable declaration" rather than "syntax error." Backtracking is controlled and explicit: three call sites use `size_t savePos = current` and reset on failure. The parser also handles the template-call ambiguity (`ident<T>(args)` vs `ident < T`) with speculative parsing that resets `current` on failure; this pattern is awkward in a grammar table but natural in recursive descent.

---

## 11. For-Loop Declaration Scope Fix

**Decision:** The `init` clause of a `for` loop can be a variable declaration (`for (int i = 0; i < n; i++)`), and that variable is scoped to the entire loop (condition, step, and body), not just the init clause.

**Problem:** The parser wraps a for-loop init declaration in a `BlockStmt` (a single-item block containing the `DeclPtr`). If the type checker treats this `BlockStmt` as an ordinary block, it pushes a scope for the init, evaluates the declaration, then pops the scope before checking the condition, making `i` invisible in `i < n`.

**How it was fixed:** `visit(ForStmt*)` in the type checker detects when `init` is a `BlockStmt` and iterates its items directly in the for-loop's own scope rather than delegating to `visit(BlockStmt*)`. The scope push/pop wraps the entire for-loop body:

```
pushScope()
  process init BlockStmt items (defines i)
  visit condition (i < n, sees i)
  visit body (sees i)
  visit step (i++, sees i)
popScope()
```

**Alternatives considered:**

- *Special `ForInitDecl` AST node*: A dedicated node type that the type checker handles differently from `BlockStmt`. Cleaner semantically but requires a new AST node, a new visitor method on all three passes, and parser changes. The `BlockStmt` wrapper approach reuses existing infrastructure.
- *Hoist the init declaration to the enclosing scope*: The init variable becomes visible outside the loop, matching pre-C99 behavior. This is what early C compilers did and what MSVC did until it was fixed. It is surprising and non-standard; rejected.

**Why the fix matters:** The decoder code uses `for (int i = 0; i < len; i++)` throughout. Without the scope fix, every for-loop with a declaration init fails at the type checker with "undefined variable i."

---

## 12. Lazy Template Instantiation

**Decision:** Template structs and functions are not instantiated when they are declared. They are instantiated the first time they are used. `struct Result<T,E>` in the source file adds an entry to `templateDecls["Result"]` but emits no LLVM IR. The first `let r: Result<int, string>` triggers `ensureTemplateInstantiated("Result_int_string", "Result", {"int","string"})` which creates `%Result_int_string = type { i32, i32, ptr }`.

**Alternatives considered:**

- *Eager instantiation at declaration*: Instantiate all template combinations at declaration time by scanning all uses in a pre-pass. Requires a two-phase compilation model where the use sites are known before the template is processed. This is the C++ header model: the template definition must be visible at every use site, and the compiler instantiates it per translation unit. For a single-file or explicitly-imported model, eager instantiation means scanning the entire program before generating any IR.
- *JIT instantiation at codegen, no type-checker involvement*: The type checker ignores templates; codegen handles everything. Rejected because type errors in template usage (wrong number of type arguments, field access on a non-existent field in a substituted type) would not be caught until codegen or link time.

**Why lazy:** Lazy instantiation means the template machinery is activated only when a concrete type is needed. The type checker's `normalizeType("Result<int,string>")` performs substitution and registers `StructInfo` for `"Result_int_string"` the first time the type appears. Codegen's `ensureTemplateInstantiated` is idempotent: it checks `structTypes.count(mangled)` before doing any work. Both the type checker and codegen interpret the type spelling through the same `ty::Type::parse` grammar (see Decision 14), so the substitution and instantiation logic is shared rather than reimplemented per phase. Template functions follow the same pattern: `visit(TemplateCallExpr*)` mangles the name, saves/restores the insert point, sets `typeParamOverride`, and re-visits the `FunctionDecl` body exactly once per unique type argument combination.

---

## 13. Integer Width Coercion in Comparisons

**Decision:** When a comparison (`==`, `!=`, `<`, etc.) is emitted for operands of different integer widths, the narrower operand is zero-extended (`ZExt`) or sign-extended (`SExt`) to match the wider type before the LLVM `ICmp` instruction is emitted.

**Problem:** LLVM's `CreateICmpEQ(left, right)` requires both operands to have the same type. The decoder computes with `uint8` values (from a `uint8[858]` buffer) and compares them to `int` constants. Without coercion, `ICmpEQ(i8 %byte, i32 0x30)` causes an LLVM assertion failure at IR verification time.

**Implementation:** `visit(BinaryExpr*)` checks whether the comparison involves integer operands of different widths. If `left` is `i8` and `right` is `i32`, it calls `CreateZExt(left, i32)` before emitting `ICmpEQ`. Zero-extension is used (not sign-extension) because the values being compared are byte values from a buffer, treated as unsigned. The same coercion is applied in `visit(VarDecl*)` for assignments and in `emitStructInitInto()` for struct field initialization.

**Alternatives considered:**

- *Require explicit casts in the source (`(int)byte == 0x30`)*: Correct but verbose. The decoder code iterates byte buffers and compares to ASCII constants; requiring an explicit cast on every comparison would make the code harder to read than equivalent C.
- *Truncate the wider operand to the narrower width*: `ICmpEQ(i8 %byte, trunc i32 0x30 to i8)` works for equality but is incorrect for relational comparisons: `(uint8)200 < (int)256` should be true, but truncating 256 to `i8` gives 0, making the comparison `200 < 0` which is false.

**Why ZExt:** Zero-extension from `i8` to `i32` correctly represents unsigned byte values in the range 0–255 as `i32` values in the range 0–255, preserving the ordering relationship for all relational operators.

---

## 14. Structured `ty::Type` IR and a Single Type Resolver

**Decision:** Types are represented internally by a structured `ty::Type` IR (`sema/type.{h,cpp}`) with one grammar interpreter, `ty::Type::parse`, and the type checker is the **single resolver**: it resolves every expression's type into a per-expression table that codegen consumes. Codegen does not re-derive expression types; its `getTypeFromString` dispatches on `ty::Type::parse`, the same grammar interpreter the type checker uses.

**Alternatives considered:**

- *Two independent string evaluators (the prior state)*: The type checker resolved expression types from type strings, and codegen separately re-derived an expression's Eskiu type at codegen time from its own `varTypeStack`/`structFields` walk. Both consumed the same surface spellings (`"Result<int,string>"`, `"List<int>*"`, `"*Point"`) but interpreted them with **two different ad-hoc string parsers**. Any divergence between the two (a corner of the type grammar one handled and the other didn't) was a silent miscompile: a program that type-checked could lower to wrong IR. Three latent miscompiles (a float literal kept as `double`, a pointer-deref width, a `char` zero-extension) were exactly this class of bug.
- *Keep string-typing but share one helper*: Cheaper than a full IR, but a flat string is the wrong representation to share: substitution, pointer peeling, nominal-name extraction, and constraint checking all want structure, and every call site re-parsed the string ad hoc (the bug origin).

**Why a structured IR + one resolver:** A `ty::Type` value carries the structure (nominal name, type arguments, pointer levels) directly, with `parse` / `str` / `substitute` / `nominalName` as the operations every phase needs, so substitution and nominal-name extraction happen once, in one place, instead of being re-derived by each ad-hoc string strip. Making the type checker the sole resolver and having codegen consume its table means there is exactly **one** answer for any expression's type. `ty::Type::parse` is the single grammar interpreter both phases share, which structurally closes the two-evaluator divergence risk. The migration was behavior-preserving (gated by the golden-IR oracle) and, once the single resolver landed, fixed the three latent miscompiles above.

---

## 15. Opt-In Memory Safety (Zig-Flavored)

**Decision:** Make the language safer with compile-time checks and *opt-in* runtime guards, on the Zig side of the line, not the Rust side. Bare `*T` stays C-nullable and existing code compiles unchanged; the new safety surface is reached for deliberately. The batch: `defer` / `errdefer` for cleanup that runs on every exit path (a codegen cleanup-stack, LIFO); the slice type `T[]`, a fat pointer that carries its length so bounds are knowable; the `must_use` qualifier that rejects a discarded result (the stdlib's `alloc` is marked, so a forgotten allocation is a compile error); `--safe`, an off-by-default build mode that bounds-checks array and slice indexing and traps on a violation; and the checked nullable pointer `?*T`, which cannot be dereferenced until proven non-null (`if (x != null)` narrows it) and lowers to a bare pointer, so the safety is entirely at compile time.

**Alternatives considered:**

- *A borrow checker / ownership + lifetimes (the Rust model)*: Rejected. It fights the two things Eskiu is built on: C-faithfulness (a borrow checker changes how you write and pass pointers) and scripting immediacy (lifetime annotations are a real cognitive tax). Arenas already cover most use-after-free in the target workloads; ownership would be a large, ill-fitting mechanism for a marginal additional gain.
- *A garbage collector*: Rejected for the same reason as decision #2 (no GC): it breaks predictable, C-comparable performance and complicates the C ABI.
- *Making the checks mandatory (non-null by default, always-on bounds checks)*: Rejected as breaking. Flipping `*T` to non-null would reject working C-style code, and always-on bounds checks would cost every release build. Opt-in keeps the zero-cost default and lets a codebase adopt safety incrementally.

**Why opt-in compile-time safety:** It composes with the existing model instead of replacing it. `defer` / `errdefer` / `must_use` / `?*T` are all resolved at compile time with no runtime cost, and `--safe` is the one runtime guard, off by default. This continues the v0.4 correctness arc (tightening the type system) rather than introducing a new ownership discipline, so a C programmer can pick up each feature independently without relearning how to hold a pointer.

---

## Summary

The decisions above form a consistent position: Eskiu is an explicit-control systems language that targets programmers who already think in C, need to call C libraries, and want near-C performance without writing C. The compiler is designed to be read and modified by one to three people, so implementation simplicity (hand-written parser, recursive-descent, visitor pattern, no parser generator) is a real constraint. LLVM handles the hard parts of code generation; the front end's job is to produce correct IR quickly.

The INE decoder benchmark is the north star. Every decision that made the benchmark harder to hit was rejected; every decision that kept the code close to C semantics while adding safety margins (structural interfaces, `Result<T,E>`, two-pass type checking) was accepted.
