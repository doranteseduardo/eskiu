# Compiler Development Phases

Detailed requirements and implementation notes for each phase of the Eskiu compiler.

## Phase 0: Environment Setup [COMPLETE]

**Status:** Complete

**Deliverable:** `eskiuc --version` works

### Requirements
- [x] LLVM 17+ integration
- [x] CMake build system
- [x] Main CLI entry point
- [x] Version printing

### Key Files
- `main.cpp`
- `CMakeLists.txt`

---

## Phase 1: Lexer [COMPLETE]

**Status:** Complete

**Deliverable:** Tokenize any `.esk` file into a token stream

### Requirements
- [x] Recognize all keywords (let, int, float, fn, for, while, if, etc.)
- [x] Identify operators (+, -, *, /, ==, !=, &&, ||, etc.)
- [x] Parse literals (integers, floats, strings with escape sequences, chars)
- [x] Track line and column numbers for errors
- [x] Handle comments (// and /* */)
- [x] Support variadic parameters (...)

### Testing
```bash
./eskiuc file.esk --test-lexer
```

Output shows: line, column, token type, token value

### Key Files
- `lexer/lexer.h`
- `lexer/lexer.cpp`

---

## Phase 2: Parser [COMPLETE]

**Status:** Complete

**Deliverable:** Build AST from token stream, with pretty-printer

### Requirements
- [x] Parse function declarations with parameters
- [x] Parse variable declarations with optional initializers
- [x] Parse struct definitions
- [x] Parse extern declarations (C interop)
- [x] Parse statements (if/else, for, while, return, break, blocks)
- [x] Parse expressions with correct precedence
- [x] Handle function calls, array indexing, member access
- [x] Recursive descent parser (hand-written)
- [x] Error recovery (skip to next semicolon on error)

### Expression Precedence (highest to lowest)
1. Postfix: `f()`, `a[i]`, `a.b`
2. Unary: `!`, `-`, `+`, `&`, `*`, casts
3. Multiplicative: `*`, `/`, `%`
4. Additive: `+`, `-`
5. Relational: `<`, `>`, `<=`, `>=`
6. Equality: `==`, `!=`
7. Logical AND: `&&`
8. Logical OR: `||`
9. Assignment: `=`

### Testing
```bash
./eskiuc file.esk --test-parser
```

Output shows: AST with indentation

### Key Files
- `parser/parser.h`
- `parser/parser.cpp`
- `ast/ast.h`
- `ast/ast.cpp`
- `ast/ast_printer.cpp`

---

## Phase 3: Codegen [COMPLETE]

**Status:** Complete

**Deliverable:** Generate valid LLVM IR from AST

### Requirements
- [x] Create LLVM module and functions
- [x] Emit type conversions (int32, float, double, i8*)
- [x] Implement arithmetic operators (+, -, *, /, %)
- [x] Implement comparison operators (==, !=, <, >, <=, >=)
- [x] Implement logical operators (&&, ||, !)
- [x] Implement unary operators (-, !)
- [x] Implement assignment (=)
- [x] Handle variable declarations (alloca)
- [x] Handle function calls
- [x] Implement if/else (branches, merge blocks)
- [x] Implement while loops
- [x] Implement for loops (init, condition, step)
- [x] Implement function returns
- [x] Verify generated module

### Type Mappings
```
int     → i32
int64   → i64
float   → float
double  → double
bool    → i1
char    → i8
string  → i8*
*T      → ptr to T
```

### Testing
```bash
./eskiuc file.esk --test-codegen
```

Output shows: LLVM IR module

### Known Limitations
- No heap allocation (alloc/free)
- No string operations
- No structures
- No template instantiation
- No closure captures

### Key Files
- `codegen/codegen.h`
- `codegen/codegen.cpp`

---

## Phase 4: Type Checker [IN PROGRESS]

**Status:** In progress

**Deliverable:** Type validation before codegen with error reporting

### Requirements
- [ ] Type inference for binary expressions
- [ ] Type inference for function calls
- [ ] Validate function arguments match parameters
- [ ] Check return statements match function return type
- [ ] Error reporting: `file.esk:line:col: message`
- [ ] Warn on type mismatches
- [ ] Support explicit casts: `(int)x`
- [ ] Handle operator overloading rules

### Type Rules

**Binary Operators:**
- `int + int → int`
- `float + float → float`
- Mixed: promote to wider type
- Comparison operators: `→ bool`

**Function Calls:**
- Argument count must match
- Argument types must match parameter types (or be castable)
- Return type is function's declared return type

**Assignments:**
- LHS must be lvalue (variable)
- RHS type must match LHS type (or be castable)

### Error Format
```
error: file.esk:12:5: type mismatch: expected int, got float
error: file.esk:15:10: undefined function 'foo'
error: file.esk:20:3: return type mismatch: expected int, got float
```

### Key Files (to create)
- `sema/type_checker.h`
- `sema/type_checker.cpp`

---

## Phase 5: Structs, Interfaces, Templates [PLANNED]

**Status:** Not started

**Deliverable:** Composite types and polymorphism

### Requirements
- [ ] Struct field access (`.`)
- [ ] Struct initialization: `Point { x: 1, y: 2 }`
- [ ] Methods on structs
- [ ] Go-style implicit interfaces
- [ ] Generic templates: `List<T>`, `Tree<T>`
- [ ] Monomorphic instantiation (no partial specialization)
- [ ] VTable generation for interface dispatch

### Example
```eskiu
struct Point { float x; float y; }

interface Drawable {
    void draw();
}

struct Circle {
    float radius;
    void draw() { printf("circle\n"); }
}
```

---

## Phase 6: Heap Memory & Strings [PLANNED]

**Status:** Not started

**Deliverable:** Manual memory management with `alloc`/`free`

### Requirements
- [ ] `alloc(T, N)` → `malloc(N * sizeof(T))`
- [ ] `free(ptr)` → `free(ptr)`
- [ ] String literal → `i8*` global constant
- [ ] Mutable `String` type (buffer + len + capacity)
- [ ] String methods: `append()`, `len()`, `cstr()`
- [ ] Pointer arithmetic: `ptr[i]`, `*ptr`

### Example
```eskiu
let arr = alloc(int, 10);
arr[0] = 42;
free(arr);

let s = String.new("hello");
s.append(" world");
printf("%s\n", s.cstr());
```

---

## Phase 7: Result<T,E> & Stdlib [PLANNED]

**Status:** Not started

**Deliverable:** Error handling and standard library

### Requirements
- [ ] `Result<T, E>` type in stdlib
- [ ] `Ok(value)` constructor
- [ ] `Err(error)` constructor
- [ ] `if (result.ok)` pattern
- [ ] `List<T>` dynamic array
- [ ] `math` module (sqrt, abs, min, max, pow)
- [ ] `io` module (print, println, read_file, write_file)
- [ ] `mem` module (memcpy, memset)

### Example
```eskiu
Result<int, string> divide(int a, int b) {
    if (b == 0) return Err("division by zero");
    return Ok(a / b);
}

let r = divide(10, 2);
if (r.ok) {
    printf("%d\n", r.value);
}
```

---

## Phase 8: Lambdas & Closures [PLANNED]

**Status:** Not started

**Deliverable:** Functions as first-class values

### Requirements
- [ ] Lambda syntax: `fn(int x) { x * 2 }`
- [ ] Closure captures (by value)
- [ ] Function pointers
- [ ] Higher-order functions (map, filter, fold)
- [ ] For-in loops over collections

### Example
```eskiu
let nums = [1, 2, 3, 4, 5];
let doubled = nums.map(fn(int x) { x * 2 });
let evens = nums.filter(fn(int x) { x % 2 == 0 });
```

---

## Phase 9: Threads (v0.2) [PLANNED]

**Status:** Not started

**Deliverable:** Native thread support

### Requirements
- [ ] `spawn(fn() { ... })` → `pthread_create`
- [ ] `thread.join()` → `pthread_join`
- [ ] `Mutex` type with `.lock()` / `.unlock()`
- [ ] Thread-safe primitives
- [ ] Link with `-lpthread`

### Example
```eskiu
thread t = spawn(fn() {
    printf("Hello from thread!\n");
});
t.join();
```

---

## Phase 10: Exceptions (v1.0) [PLANNED]

**Status:** Not started

**Deliverable:** C++-compatible exception handling

### Requirements
- [ ] `try` block
- [ ] `catch (ErrorType e)` handlers
- [ ] `finally` block
- [ ] `throw` statement
- [ ] LLVM EH (invoke/landingpad)
- [ ] Base `Exception` type in stdlib
- [ ] Custom exception types

### Example
```eskiu
try {
    let data = readFile("cred.heic");
} catch (IOError e) {
    printf("File error: %s\n", e.message);
} finally {
    cleanup();
}
```

---

## Phase 11: Async/Await (v2.0) [PLANNED]

**Status:** Not started

**Deliverable:** Coroutine-based async support

### Requirements
- [ ] `async` function keyword
- [ ] `await` expression
- [ ] `Promise<T>` return type
- [ ] Event loop runtime
- [ ] LLVM coroutine intrinsics
- [ ] Interop with threads from Phase 9

---

## Implementation Priority

1. **Phase 4** (Type Checker) — Critical before advancing
2. **Phase 5** (Structs/Interfaces) — Required for realistic programs
3. **Phase 6** (Memory Management) — Enables real-world code
4. **Phase 7** (Stdlib) — Provides basic utilities

Phases 8–11 are opt-in for advanced features.

---

## Tips for Implementers

- **Write tests first** before implementing each phase
- **Use `--test-*` modes** to validate at each step
- **Keep phases orthogonal** — Phase 4 should not break Phase 3
- **Reference C semantics** when in doubt
- **Read LLVM docs** before generating IR

See [ARCHITECTURE.md](./ARCHITECTURE.md) for details on each component.
