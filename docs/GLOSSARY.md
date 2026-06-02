# Glossary

Technical terms used in Eskiu documentation and compiler.

## A

**Abstract Syntax Tree (AST)**  
Tree representation of program structure after parsing. Nodes represent language constructs (functions, variables, expressions). Used by the compiler's semantic analysis and code generation phases.

**Annotation**  
Extra information attached to a declaration (future feature). Example: `#[inline]` in Rust. Not yet in Eskiu.

## B

**Backend**  
The part of the compiler that generates executable code. Eskiu uses LLVM as the backend.

**Binding**  
Association between a name and a value in a scope. Example: `let x: i32 = 5` binds the name `x` to the value 5.

**Borrow Checker**  
Rust's compile-time ownership validation system. Eskiu does not have one (design choice).

## C

**Call Expression**  
Expression that invokes a function. Example: `add(3, 4)` where `add` is a function.

**Cast**  
Explicit type conversion. Example: `(i32)3.14` converts 3.14 to an integer.

**Closure**  
Function that captures variables from its surrounding scope. Not yet in Eskiu (Phase 8).

**Code Generation**  
Phase 3 of the compiler. Converts the AST to LLVM IR.

**Compilation**  
Process of converting source code (`.esk`) to executable binary through lexing, parsing, type checking, and code generation.

**Compiler**  
Program that translates source code to machine code. The Eskiu compiler (`eskiu`) is the main tool.

**Const**  
Read-only constant value. Not yet fully supported in Eskiu.

## D

**Declaration**  
Statement that introduces a new name (function, variable, struct). Example: `fn foo() -> i32 { ... }` declares function `foo`.

**Dereference**  
Access the value pointed to by a pointer. Example: `*ptr` dereferences pointer `ptr`.

**Dispatch**  
Mechanism for calling the right function based on type (static or dynamic).

- **Static dispatch:** Function resolved at compile-time (default)
- **Dynamic dispatch:** Function resolved at runtime through vtable (Phase 5+)

## E

**Expression**  
Code that evaluates to a value. Example: `3 + 4` or `add(5, 2)`.

**Extern**  
Declaration of a C function callable from Eskiu. Example: `extern fn printf(...)`

## F

**Field**  
Named member of a struct. Example: `Point { x: i32, y: i32 }` has fields `x` and `y`.

**Function**  
Named block of code that performs a computation. Example: `fn add(a: i32, b: i32) -> i32 { return a + b; }`

**Function Signature**  
Declaration of a function's name, parameters, and return type (without the body).

## G

**Garbage Collector (GC)**  
Runtime system that automatically frees unused memory. Eskiu has no GC (design choice).

**Generic**  
Function or type parameterized over types. Example: `List<T>`. Not yet in Eskiu (Phase 5).

## I

**Identifier**  
Name of a variable, function, or type. Must start with letter or underscore.

**Inference**  
Automatic deduction of types by the compiler. Eskiu requires explicit type annotations (design choice).

**Interface**  
Set of method signatures defining a contract. Implemented types must provide all methods. Eskiu uses structural typing (Phase 5).

**IR (Intermediate Representation)**  
Low-level code format between source code and machine code. Eskiu generates LLVM IR.

## J

**JIT (Just-In-Time)**  
Compilation at runtime. Not yet in Eskiu.

## K

**Keyword**  
Reserved word with special meaning. Example: `fn`, `let`, `if`, `return`.

## L

**Lexer**  
Phase 1 of the compiler. Converts source code string into tokens.

**Lexical Scope**  
Visibility of a name determined by its position in the source code. Inner scopes shadow outer scopes.

**LLVM**  
Low-Level Virtual Machine. Compiler infrastructure that Eskiu uses as a backend.

## M

**Member**  
Field or method of a struct. Accessed with `.` operator. Example: `point.x`

**Monomorphic**  
Instantiation of a generic function for a specific type. Example: `List<i32>` is a monomorphic instantiation.

**Mutability**  
Whether a variable can be modified after initialization. Current Eskiu: all variables are mutable (const not fully supported).

## N

**Null**  
Invalid pointer value. Dereferencing null causes undefined behavior.

**Null Pointer Dereference**  
Attempting to access `*nullptr`. Undefined behavior in Eskiu.

## O

**Operator**  
Symbol performing an operation. Examples: `+`, `-`, `*`, `/`, `==`, `!=`.

**Operator Precedence**  
Order in which operators are evaluated. Example: `*` before `+`.

## P

**Parser**  
Phase 2 of the compiler. Converts tokens into an Abstract Syntax Tree (AST).

**Parameter**  
Named input to a function. Example: `fn add(a: i32, b: i32)` has parameters `a` and `b`.

**Pointer**  
Address of a value in memory. Type: `*T` (pointer to T). Example: `*i32` points to a 32-bit integer.

**Polymorphism**  
Code that works with multiple types. Achieved through:
- **Parametric:** Generics (Phase 5)
- **Structural:** Interfaces (Phase 5)
- **Not supported:** Inheritance or subtyping

## R

**Reference**  
Address of a value. Created with `&` operator. Example: `&x` is a reference to `x`.

**Recursive Function**  
Function that calls itself. Example: factorial.

**Return Type**  
Type of value returned by a function. Example: `fn foo() -> i32` returns an i32.

## S

**Scope**  
Region of code where a name is visible. Eskiu uses lexical scoping (block-based).

**Semantic Analysis**  
Phase 4 (Type Checker). Validates that the program is well-typed and well-formed.

**Struct**  
Composite type containing named fields. Example: `struct Point { x: i32, y: i32 }`.

**Symbol**  
Name in a program (variable, function, type).

**Symbol Table**  
Data structure mapping names to their declarations. Used during type checking.

## T

**Token**  
Smallest unit of meaning in source code. Examples: `let`, `x`, `:`, `i32`, `=`, `5`, `;`.

**Type**  
Classification of values in the program. Examples: `i32`, `f64`, `bool`, `*i32`, `Point`.

**Type Annotation**  
Explicit declaration of a type. Example: `let x: i32 = 5` annotates `x` as `i32`.

**Type Checker**  
Phase 4 of the compiler. Validates types and reports semantic errors.

**Type Error**  
Compilation error caused by type mismatch. Example: `let x: i32 = 3.14` (float assigned to int).

**Type Inference**  
Compiler deduction of types from context. Limited in Eskiu (explicit annotations required).

**Type Mismatch**  
Incompatible types in an operation. Example: adding `i32` and `*i32`.

## U

**Unary Operator**  
Operator taking one operand. Examples: `-` (negation), `*` (dereference), `&` (reference).

**Undefined Behavior**  
Program behavior not specified by the language. Examples: null pointer dereference, buffer overflow. Eskiu does not prevent these.

**Use-After-Free**  
Accessing memory after it's been freed. Undefined behavior.

## V

**Variable**  
Named storage location for a value. Declared with `let`.

**Visitor Pattern**  
Design pattern for traversing tree structures. Eskiu uses this for AST traversal in the type checker and code generator.

**Vtable (Virtual Method Table)**  
Data structure enabling dynamic dispatch. Used by interfaces for method lookup at runtime.

## W

**Well-Typed**  
Program where all types are consistent and valid.

## Z

**Zero-Cost Abstraction**  
Language feature that compiles to no overhead. Eskiu aims for this (no hidden costs).

---

## Related Documents

- [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) — Full language specification
- [ARCHITECTURE.md](ARCHITECTURE.md) — Compiler architecture
- [DESIGN_DECISIONS.md](DESIGN_DECISIONS.md) — Why certain choices were made
