# Design Decisions

This document explains key architectural and language design choices in Eskiu.

## Why No Garbage Collection?

**Decision:** Eskiu has no garbage collector.

**Rationale:**

1. **Performance predictability** — No GC pauses, no stop-the-world collections. For systems with hard real-time constraints (embedded, QR decoders, networking), GC unpredictability is unacceptable.

2. **Control** — Developers decide when memory is allocated and freed. This is essential for resource-constrained environments.

3. **Simplicity** — GC adds complexity. Manual management is simpler to reason about and debug.

4. **Target audience** — Eskiu targets systems programmers who are comfortable with C-style memory management.

**Trade-off:** You must avoid use-after-free, double-free, and memory leaks. Eskiu doesn't prevent these—the compiler trusts you. If memory safety is critical, use Rust.

---

## Why No Borrow Checker?

**Decision:** Eskiu uses manual memory management without a borrow checker.

**Rationale:**

1. **Simplicity** — Borrow checkers are complex to implement and confusing for newcomers. Eskiu trades memory safety for simplicity.

2. **Learning curve** — C programmers immediately understand manual memory. Rust's borrow checker has a steep learning curve.

3. **Pragmatism** — The QR decoder use case doesn't require memory safety—it requires speed and control.

4. **Development velocity** — Fewer compiler restrictions mean faster prototyping.

**Alternative considered:** We could add a borrow checker later (v1.0+) as an optional feature.

---

## Why C-Style Syntax?

**Decision:** Eskiu uses C-like syntax for functions, variables, types, and control flow.

**Rationale:**

1. **Familiarity** — C developers can read Eskiu immediately. No mental overhead learning a new syntax.

2. **Industry standard** — C syntax is familiar to ~90% of systems programmers (C, C++, Java, Go, Rust all use it).

3. **Readability** — C syntax is proven to be readable and concise. Why invent a new syntax?

**Differences from C:**

- **Explicit types in declarations:** `let x: i32 = 5;` (not `int x = 5;`)
- **No implicit type conversions:** Must cast explicitly
- **Modern keywords:** `fn` instead of no keyword, `let` for variables
- **Struct initialization:** `Point { x: 1, y: 2 }` (not `Point {1, 2}`)

These differences improve clarity without breaking familiarity.

---

## Why Structural Typing (Interfaces)?

**Decision:** Eskiu uses Go-style structural typing instead of inheritance.

**Rationale:**

1. **No inheritance** — Inheritance hierarchies are fragile, hard to maintain, and encourage tight coupling. Composition is simpler.

2. **Implicit satisfaction** — Any type with matching methods implicitly satisfies an interface. No boilerplate.

3. **Decoupling** — You can define interfaces after implementing the types. Interfaces don't force structure.

4. **Simplicity** — Easier to understand than virtual function tables or runtime type information.

**Example:**

```esk
interface Reader {
    fn read(buf: *i8, len: i32) -> i32;
}

struct File {
    fn read(buf: *i8, len: i32) -> i32 { ... }
}

// File implicitly implements Reader; no declaration needed
```

**Trade-off:** Dynamic dispatch requires vtables or reflection. This is implemented in Phase 5.

---

## Why Separate Compilation Phases?

**Decision:** Eskiu architecture has distinct Lexer → Parser → Type Checker → Codegen phases.

**Rationale:**

1. **Clarity** — Each phase has a single responsibility. Easy to understand and debug.

2. **Testing** — Each phase can be tested independently. We expose `--test-lexer`, `--test-parser`, `--test-typechecker` for developers.

3. **Maintainability** — Bugfixes are localized. A type checker bug doesn't affect the parser.

4. **Future features** — Separate phases make it easier to add new features (e.g., macro expansion, optimization passes).

5. **Educational** — Clear phases make the compiler easier to understand for newcomers.

**Alternative:** Single-pass compilation (like traditional C). Harder to understand, harder to extend.

---

## Why Explicit Type Annotations?

**Decision:** Eskiu requires explicit type annotations on variable declarations.

**Rationale:**

1. **Clarity** — You see the type immediately. No surprises from inference.

2. **Determinism** — Type inference can be ambiguous. Explicit types remove ambiguity.

3. **Compiler simplicity** — No need for sophisticated type inference algorithms.

4. **For systems code** — Type safety is critical. Explicit types catch mistakes early.

**Example:**

```esk
let x: i32 = 5;           // Clear: x is i32
let y = 5;                // ERROR: type required
let z: i32 = 5 + 3;       // OK: RHS type doesn't need annotation
```

**Future:** Partial inference (for complex expressions) may be added in Phase 6+.

---

## Why Two-Pass Type Checking?

**Decision:** Type checking happens in two passes: registration → validation.

**Rationale:**

1. **Forward references** — First pass registers all declarations (functions, structs). Second pass validates usage. Allows calling functions defined later.

2. **Recursive structures** — Structs can reference themselves (e.g., linked lists) if types are registered first.

3. **Simplicity** — Easier to implement than single-pass type checking.

**Passes:**

1. **Registration pass:** Walk AST, register all functions, structs, global variables in symbol table
2. **Validation pass:** Walk AST, validate all types, report errors

---

## Why LLVM IR Backend?

**Decision:** Eskiu compiles to LLVM IR, not directly to assembly.

**Rationale:**

1. **Optimization** — LLVM's optimizer suite (LLVM passes) optimizes the code automatically.

2. **Portability** — LLVM supports x86-64, ARM64, RISC-V, WebAssembly, and more. One backend, many targets.

3. **Maintenance** — We don't maintain a code generator. LLVM does.

4. **Future features** — LLVM's infrastructure supports JIT, debugging info, profiling, etc.

**Trade-off:** Compilation is slower than direct assembly generation (not a problem for Eskiu's use case).

---

## Why Stack Allocation by Default?

**Decision:** Local variables are stack-allocated by default.

**Rationale:**

1. **Performance** — Stack allocation is faster than heap allocation (one increment vs. complex allocation algorithm).

2. **Automatic cleanup** — Variables disappear when scope ends. No manual deallocation for locals.

3. **Simplicity** — Developers don't worry about freeing temporary variables.

4. **Safety** — Stack memory is automatically reclaimed, avoiding memory leaks for local variables.

**Example:**

```esk
fn foo() -> i32 {
    let x: i32 = 5;        // Stack-allocated, auto-freed when foo exits
    let ptr: *i8 = "hi";   // Pointer to read-only string literal
    return x;
}
```

**Future:** Phase 6 will add `alloc()`/`free()` for explicit heap allocation.

---

## Why No Overloading?

**Decision:** Eskiu doesn't support function overloading.

**Rationale:**

1. **Simplicity** — Function resolution is trivial: one name → one function.

2. **Predictability** — No surprises from which overload gets called.

3. **For systems code** — Performance matters. Overloading adds complexity without performance benefit.

4. **C compatibility** — Easier to call C functions that aren't overloaded.

**Workaround:** Use different names: `i32_add`, `f32_add`. Or use generics (Phase 5+).

---

## Why Errors as Values?

**Decision:** Phase 7+ will use `Result<T, E>` for error handling, not exceptions.

**Rationale:**

1. **Performance** — Returning a value is cheaper than unwinding the stack.

2. **Visibility** — You see in the function signature whether it can fail.

3. **Composability** — Errors can be transformed and propagated easily.

4. **Simplicity** — Exceptions require runtime support. `Result` is just a type.

**Future:** Exceptions (Phase 10+) for truly exceptional cases (e.g., out-of-memory).

---

## Why No Inheritance Hierarchies?

**Decision:** No `class Parent { ... } class Child : Parent { ... }`.

**Rationale:**

1. **Fragile base class problem** — Changes to parent break children. Inheritance couples code.

2. **Composition is simpler** — Embed one struct in another instead of inheriting.

3. **Interfaces replace inheritance** — Structural typing (Phase 5) provides polymorphism without coupling.

**Example:**

```esk
// Instead of inheriting from Vehicle:
struct Vehicle {
    speed: i32
}

struct Car {
    vehicle: Vehicle
    doors: i32
}
```

---

## Trade-Offs Summary

| Feature | Eskiu | Trade-off |
|---------|-------|-----------|
| Memory safety | No | Must be careful; no GC/borrow checker |
| Type safety | Yes | Explicit annotations required |
| Performance | Excellent | Manual memory management required |
| Learning curve | Low | Less powerful than Rust (safety) |
| Ecosystem | Small | Building it now |
| Portability | High | Through LLVM |

---

## Questions?

These decisions are not final. If you disagree with one:

1. Open an issue on GitHub explaining your perspective
2. Reference this document and explain the trade-off
3. Propose an alternative with rationale

Good design decisions are debatable. Let's discuss!
