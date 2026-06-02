# 🦬 Eskiu Lang — Complete Language Plan v0.2

> A systems programming language compiled via LLVM IR, with C-style syntax,
> multiparadigm design (procedural + light functional + OOP), and manual/stack
> memory management. Compiler implemented in C++.

---

## 1. Identity

| | |
|---|---|
| **Name** | Eskiu Lang |
| **Short name** | Eskiu |
| **File extension** | `.esk` |
| **Compiler binary** | `eskiuc` |
| **Backend** | LLVM IR → x86-64, ARM64, WASM, RISC-V (all via LLVM) |
| **Compiler language** | C++ |
| **Paradigm** | Multiparadigm: systems, procedural, light functional, OOP |

---

## 2. Design Principles

1. **Performance-first** — near-metal output, no hidden costs, no GC.
2. **C-style familiarity** — anyone who knows C can read Eskiu immediately.
3. **Honest memory model** — stack allocation is the default; heap is explicit.
4. **No borrow checker complexity** — manual `alloc`/`free` for heap; RC as opt-in.
5. **Multiplataform from day one** — LLVM backend handles all targets.
6. **Practical OOP** — interfaces (Go-style), not inheritance hierarchies.
7. **Errors as values first** — `Result<T,E>` in stdlib; exceptions in v1.0.

---

## 3. Syntax Specification

### 3.1 Variables

```eskiu
let x: int = 5;
let name: string = "hola";
let ptr: *int = null;
```

### 3.2 Primitive Types

| Type | Description |
|---|---|
| `int` | Platform-width signed integer |
| `uint` | Platform-width unsigned integer |
| `int8/int16/int32/int64` | Explicit-width signed |
| `uint8/uint16/uint32/uint64` | Explicit-width unsigned |
| `float` | 32-bit floating point |
| `double` | 64-bit floating point |
| `bool` | Boolean (`true` / `false`) |
| `char` | Single byte character |
| `string` | Immutable C-string literal (`char*`) |
| `String` | Mutable string struct with buffer |
| `void` | No value |

### 3.3 Functions

```eskiu
// Standard function — C-style return type first
int add(int a, int b) {
    return a + b;
}

// Void function
void log(string msg) {
    printf("%s\n", msg);
}

// Variadic
void logf(string fmt, ...) { }

// Extern / C interop
extern int printf(string fmt, ...);
extern void* malloc(int size);
```

### 3.4 Lambdas / Closures

```eskiu
let double = fn(int x) { x * 2 };
let greet  = fn(string name) { printf("Hello %s\n", name) };

// Used inline
let nums = [1, 2, 3].map(fn(int x) { x * 2 });
```

### 3.5 Structs and Methods

```eskiu
// Plain struct (C-style field declaration)
struct Point {
    float x;
    float y;
}

// Struct with methods
struct Point {
    float x;
    float y;

    float distance(Point other) {
        return sqrt((x - other.x) * (x - other.x) +
                    (y - other.y) * (y - other.y));
    }

    void print() {
        printf("(%f, %f)\n", x, y);
    }
}

// Also available as free functions
float distance(Point a, Point b) { ... }
```

### 3.6 Interfaces (Go-style implicit)

```eskiu
interface Speakable {
    void speak();
}

struct Dog {
    string name;

    void speak() {
        printf("Woof!\n");
    }
}

struct Cat {
    string name;

    void speak() {
        printf("Meow!\n");
    }
}

// Dog and Cat implicitly implement Speakable
// (no explicit 'implements' keyword needed)
void makeSpeak(Speakable s) {
    s.speak();
}

makeSpeak(Dog { name: "Rex" });
makeSpeak(Cat { name: "Luna" });
```

> No inheritance. No vtables written by the programmer.
> If a struct satisfies an interface, it implements it.

### 3.7 Templates / Generics

```eskiu
// Monomorphic instantiation only — no template metaprogramming
struct List<T> {
    *T   data;
    int  size;
    int  capacity;

    void push(T item) { ... }
    T    get(int i)   { ... }
    int  len()        { return size; }
}

List<int>    nums;
List<string> words;
List<Point>  points;
```

### 3.8 Enums

```eskiu
enum Direction { North, South, East, West }
enum Color     { Red = 0, Green, Blue }
enum Status    { Ok = 0, Err = 1 }
```

### 3.9 Control Flow

```eskiu
// Conditionals
if (x > 0) {
    // ...
} else if (x == 0) {
    // ...
} else {
    // ...
}

// Switch
switch (color) {
    case Red:   printf("rojo\n");   break;
    case Green: printf("verde\n");  break;
    default:    printf("otro\n");   break;
}
```

### 3.10 Loops

```eskiu
// Classic C-style for
for (int i = 0; i < 10; i++) {
    printf("%d\n", i);
}

// For-in (iterates over collections implementing Iterable)
for item in collection {
    item.print();
}

// While
while (condition) {
    doWork();
}
```

### 3.11 Memory

```eskiu
// Stack allocation (automatic, like C locals)
let p: Point = Point { x: 1.0, y: 2.0 };

// Heap allocation (manual)
let arr: *int = alloc(int, 10);   // malloc(10 * sizeof(int))
arr[0] = 42;
free(arr);

// Pointer operations
let val: int = *ptr;              // dereference
let addr: *int = &val;            // address-of
```

### 3.12 Strings

```eskiu
// Immutable literal (char* under the hood)
let s: string = "hola mundo";
printf("%s\n", s);

// Mutable String (struct with owned buffer)
let msg: String = String.new("hola");
msg.append(" mundo");
printf("%s (len=%d)\n", msg.cstr(), msg.len());
```

### 3.13 Arrays and Slices

```eskiu
// Fixed-size array (stack allocated)
int[10] fixed;
fixed[0] = 1;

// Dynamic slice (heap backed)
[]int dynamic = [1, 2, 3, 4, 5];
int first = dynamic[0];
int count = dynamic.len();
```

### 3.14 Error Handling

```eskiu
// v0.1 — Result<T, E> as stdlib type
Result<int, string> divide(int a, int b) {
    if (b == 0) return Err("division by zero");
    return Ok(a / b);
}

let r = divide(10, 2);
if (r.ok) {
    printf("Result: %d\n", r.value);
} else {
    printf("Error: %s\n", r.error);
}

// v1.0 — Exceptions (LLVM EH)
try {
    let f = open("file.txt");
    process(f);
} catch (IOError e) {
    printf("IO Error: %s\n", e.message);
} catch (Exception e) {
    printf("Error: %s\n", e.message);
} finally {
    cleanup();
}
```

### 3.15 Concurrency (v0.2)

```eskiu
// Threads (pthreads wrapper)
thread t = spawn(fn() {
    doWork();
});
t.join();

// Mutex
let m: Mutex = Mutex.new();
m.lock();
sharedState += 1;
m.unlock();
```

### 3.16 Modules

```eskiu
import math
import io
import "utils/vec"    // relative path
```

### 3.17 Comments

```eskiu
// Single line comment

/*
   Multi-line block comment
*/
```

### 3.18 Type Casting

```eskiu
let x: double = 3.14;
let n: int    = (int)x;     // C-style explicit cast
let b: uint8  = (uint8)n;
```

---

## 4. Project Target — INE QR Decoder

The **concrete program** that Eskiu v0.1 must be able to compile is a
reimplementation of the C core of `ine-qr-re` — a reverse-engineered
decoder for the cryptographic QR codes on Mexican voter ID cards
(`libPersonalCode.so`, ARM64, 4.6 MB, Chilkat 9.5 statically linked).

### 4.1 What the program does

Given a photo of a Mexican INE credential, it:

1. Detects and extracts two 858-byte QR payloads from the image
2. Runs a 7-layer cryptographic pipeline (3-round AES-256-CBC + RSA-8192)
3. Decodes the plaintext into 18 biographical fields + a 96×129 WebP photo

### 4.2 Three pipeline stages

```
[Image bytes]
    │
    ▼
Stage 1: qr_extract        — zxing-cpp detects two QR codes → QRPair (2×858 bytes)
    │
    ▼
Stage 2: no_so_crypto      — AES key derivation + 3-round AES+RSA → plaintext
    │
    ▼
Stage 3: output_decode     — parses pipe-delimited text + WebP → JSON + image
```

### 4.3 Key data types to implement in Eskiu

```eskiu
struct QRPair {
    uint8[858] left;
    uint8[858] right;
    int        ok;
    char[256]  err;
}

struct IneResult {
    int    ok;
    char[256] err;
    *char  json;
    int    json_len;
    *uint8 webp;
    int    webp_len;
    double qr_ms;
    double crypto_ms;
    double decode_ms;
}

struct NoSoKeys {
    char[33] aes_iv1;
    char[65] aes_key1;
    char[33] aes_iv2;
    char[65] aes_key2;
    char[33] aes_iv3;
    char[65] aes_key3;
}
```

### 4.4 Features required by this target

| Feature | Used by |
|---|---|
| Structs with fixed-size array fields | `QRPair`, `IneResult`, `NoSoKeys` |
| `alloc`/`free` | crypto pipeline buffers |
| Pointers + pointer arithmetic | `*uint8`, `*char` |
| `extern` + C ABI | OpenSSL, zxing-cpp |
| Fixed arrays `uint8[858]` | QR payloads |
| Free functions (no closures needed) | all three pipeline stages |
| `Result<T,E>` | error propagation between stages |
| `string` / `char*` | hex strings, error messages |

**No closures, templates, interfaces, or threads needed for v0.1.**
This means a working real-world program is achievable with just the core language.

---

## 5. Compiler Architecture

```
eskiuc/
├── lexer/
│   ├── lexer.h
│   └── lexer.cpp          ← tokenizer
├── parser/
│   ├── parser.h
│   └── parser.cpp         ← recursive descent parser
├── ast/
│   ├── ast.h              ← AST node definitions
│   └── ast_printer.cpp    ← debug pretty-printer
├── sema/
│   ├── type_checker.h
│   └── type_checker.cpp   ← type inference, null checks, cast validation
├── codegen/
│   ├── codegen.h
│   └── codegen.cpp        ← LLVM IR emission
├── runtime/
│   ├── alloc.esk          ← alloc/free wrappers
│   └── thread.esk         ← thread/mutex stdlib (v0.2)
├── stdlib/
│   ├── result.esk         ← Result<T, E>
│   ├── string.esk         ← String mutable type
│   ├── list.esk           ← List<T>
│   ├── math.esk
│   └── io.esk
└── main.cpp               ← CLI entry point: eskiuc <file.esk> -o <output>
```

---

## 6. Development Roadmap

### Phase 0 — Environment Setup (1–2 weeks)

**Goal:** compiler project compiles and links cleanly against LLVM.

- Install LLVM 17+ (`brew install llvm` / `apt install llvm-17-dev`)
- Set up CMake with `llvm-config --cxxflags --ldflags --libs`
- Create project skeleton (directories, empty `.h`/`.cpp` files)
- Write a `main.cpp` that initializes `LLVMContext` and prints the LLVM version
- **Read:** [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/) — complete all chapters

**Deliverable:** `eskiuc --version` prints `Eskiu 0.0.1 (LLVM 17.x)`

---

### Phase 1 — Lexer (2–3 weeks)

**Goal:** tokenize any `.esk` file into a stream of tokens.

**Token categories:**

| Category | Examples |
|---|---|
| Keywords | `let`, `int`, `float`, `fn`, `for`, `in`, `while`, `if`, `else`, `switch`, `case`, `default`, `break`, `return`, `struct`, `interface`, `enum`, `import`, `extern`, `alloc`, `free`, `null`, `true`, `false`, `void`, `char`, `string` |
| Operators | `+`, `-`, `*`, `/`, `%`, `=`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`, `&`, `\|`, `^`, `~`, `<<`, `>>` |
| Delimiters | `{`, `}`, `(`, `)`, `[`, `]`, `;`, `,`, `.`, `->`, `...` |
| Literals | `INT_LIT`, `FLOAT_LIT`, `STRING_LIT`, `CHAR_LIT` |
| Identifiers | `IDENT` |
| Special | `EOF`, `UNKNOWN` |

**Deliverable:** Given `int add(int a, int b) { return a + b; }` →
prints a clean token list with type + value + line/column.

---

### Phase 2 — Parser + AST (3–4 weeks)

**Goal:** produce an Abstract Syntax Tree from a token stream.

**Strategy:** handwritten recursive descent parser (no yacc/bison).
Reasons: better error messages, easier to extend, more educational.

**Minimum AST node set:**

```
Program
├── FunctionDecl (name, params, return_type, body)
├── VarDecl (name, type, initializer)
├── StructDecl (name, fields, methods)
├── InterfaceDecl (name, signatures)
├── EnumDecl (name, variants)
├── TemplateDecl<T> (wraps StructDecl or FunctionDecl)
├── ExternDecl (name, params, return_type)
├── ImportDecl (path)
│
├── Statements
│   ├── BlockStmt
│   ├── IfStmt (condition, then, else)
│   ├── ForStmt (init, condition, step, body)
│   ├── ForInStmt (var, collection, body)
│   ├── WhileStmt (condition, body)
│   ├── ReturnStmt (value?)
│   ├── BreakStmt
│   └── ExprStmt
│
└── Expressions
    ├── BinaryExpr (op, left, right)
    ├── UnaryExpr (op, operand)
    ├── CallExpr (callee, args)
    ├── IndexExpr (base, index)
    ├── MemberExpr (base, field)
    ├── CastExpr (type, expr)
    ├── LambdaExpr (params, body)
    ├── LiteralExpr (int | float | string | bool | null)
    └── IdentExpr (name)
```

**Deliverable:** AST pretty-printer that renders any `.esk` file as a
labeled tree. No LLVM yet.

---

### Phase 3 — Basic Codegen (4–5 weeks)

**Goal:** compile a minimal subset to a real executable.

**Subset for this phase:**
- Arithmetic expressions (`+`, `-`, `*`, `/`)
- Variable declarations and assignments
- Function declarations and calls
- `if`/`else` and `while`
- `extern` declarations (to call `printf`)
- Integer and float literals
- Produce a `.o` file linkable with `clang`

**Key LLVM APIs:**
- `LLVMContext`, `Module`, `IRBuilder<>`
- `Function`, `BasicBlock`, `Value`
- `AllocaInst` for local variables (promote to registers with `mem2reg` pass)
- `llvm::Type::getInt32Ty()`, `getDoubleTy()`, `getInt8PtrTy()`

**Deliverable:**
```eskiu
// hello.esk
extern int printf(string fmt, ...);

int main() {
    printf("Hello from Eskiu!\n");
    return 0;
}
```
Compiles and runs: `eskiuc hello.esk -o hello && ./hello`

---

### Phase 4 — Type Checker (3–4 weeks)

**Goal:** catch type errors before codegen. Pass over the AST.

- Type inference for binary expressions
- Function call argument type checking
- `null` only assignable to pointer types
- Explicit cast `(T)x` validation
- Undefined variable / function detection
- **Error format:** `file.esk:12:5: error: cannot assign int to string`

**Deliverable:** type errors are reported with file, line, column,
and a clear message. Codegen is gated on zero type errors.

---

### Phase 5 — Structs, Interfaces, Templates (5–6 weeks)

**Goal:** the type system handles composite types and polymorphism.

**Structs:**
- `llvm::StructType::create()` for each struct definition
- Method calls → functions with implicit `self` first parameter
- Field access via `GEP` (getelementptr)

**Interfaces:**
- Implemented as a struct of function pointers (a vtable)
- Compiler auto-generates the vtable when a struct is passed as an interface
- No explicit `implements` keyword — structural typing

**Templates:**
- Monomorphic instantiation: `List<int>` and `List<string>` are
  two independent struct types generated at compile time
- No partial specialization, no template metaprogramming
- Instantiation triggered on first use

**Deliverable:** `QRPair`, `IneResult`, and `NoSoKeys` structs from the INE
target compile correctly with field access and basic method calls.

---

### Phase 6 — Heap Memory + Strings (3–4 weeks)

**Goal:** `alloc`/`free` work, both string types usable.

- `alloc(T, N)` → `malloc(N * sizeof(T))` + bitcast to `*T`
- `free(ptr)` → `free(ptr)`
- `string` literal → `i8*` global constant
- `String` mutable type implemented in stdlib as:
  ```eskiu
  struct String {
      *char data;
      int   len;
      int   cap;

      void   append(string s) { ... }
      *char  cstr()           { return data; }
      int    len()            { return len; }
  }
  ```

**Deliverable:** heap-allocated arrays and `String` work in the INE
decoder's error path.

---

### Phase 7 — Result\<T,E\> + Stdlib Base (2–3 weeks)

**Goal:** error handling without exceptions; core stdlib available.

**`Result<T, E>` in stdlib:**
```eskiu
struct Result<T, E> {
    int  ok;
    T    value;
    E    error;
}

Result<T, E> Ok<T, E>(T val)  { ... }
Result<T, E> Err<T, E>(E err) { ... }
```

**Minimum stdlib modules:**

| Module | Contents |
|---|---|
| `result` | `Result<T,E>`, `Ok()`, `Err()` |
| `string` | `String` mutable type |
| `list` | `List<T>` dynamic array |
| `math` | `sqrt`, `abs`, `min`, `max`, `pow` |
| `io` | `print`, `println`, `read_file`, `write_file` |
| `mem` | `alloc`, `free`, `memcpy`, `memset` |

**Deliverable:** the three INE pipeline stages can propagate errors
using `Result<T,E>` without any exceptions.

---

### Phase 8 — Lambdas + Closures (4–5 weeks)

**Goal:** functions as first-class values; closures capture environment.

This is the most technically complex phase. Closures require generating
an environment struct that captures all referenced outer variables,
then passing it as a hidden parameter.

**Implementation strategy:**
1. Lambda with no captures → plain function pointer
2. Lambda with captures → `{ fn_ptr, *env_struct }` pair
3. Env struct fields are copies (by value) of captured variables
4. For-in loops and `map`/`filter`/`fold` become usable

**Deliverable:**
```eskiu
let nums = [1, 2, 3, 4, 5];
let doubled = nums.map(fn(int x) { x * 2 });
let evens   = nums.filter(fn(int x) { x % 2 == 0 });
```

---

### Phase 9 — Threads (v0.2) (3–4 weeks)

**Goal:** native thread support via pthreads.

- `spawn(fn() { ... })` → `pthread_create` with the lambda as the thread body
- `thread.join()` → `pthread_join`
- `thread.detach()` → `pthread_detach`
- `Mutex.new()` / `.lock()` / `.unlock()` → `pthread_mutex_t` wrapper
- Link with `-lpthread`

**Deliverable:** parallel INE decode over multiple images using a thread pool.

---

### Phase 10 — Exceptions (v1.0) (4–5 weeks)

**Goal:** C++-compatible exception handling via LLVM EH.

**Implementation notes:**
- Every `call` that can throw becomes `invoke` in LLVM IR
- Exception landing pads use `llvm.eh.typeid.for`
- `finally` blocks use `cleanup` landing pads
- Base class `Exception` in stdlib with `message: String`
- Study: `llvm/docs/ExceptionHandling.rst`

**Deliverable:**
```eskiu
try {
    let data = readFile("cred.heic");
} catch (IOError e) {
    printf("File error: %s\n", e.message);
}
```

---

### Phase 11 — Async/Await (v2.0) (6–8 weeks)

**Goal:** async functions and await expression using LLVM coroutines.

- Async functions return a `Promise<T>` implicitly
- `await` suspends the coroutine and resumes on completion
- Backed by a simple event loop in the runtime
- `async`/`await` interops with threads from Phase 9
- Study: LLVM coroutine intrinsics (`llvm.coro.*`)
- Consider using `libuv` as event loop base instead of building from scratch

---

## 7. Version Roadmap

| Feature | v0.1 | v0.2 | v1.0 | v2.0 |
|---|:---:|:---:|:---:|:---:|
| Primitive types, structs, enums | ✅ | | | |
| Functions, extern, C ABI | ✅ | | | |
| Fixed arrays + dynamic slices | ✅ | | | |
| Pointers + manual alloc/free | ✅ | | | |
| Type checker + explicit casts | ✅ | | | |
| Monomorphic templates | ✅ | | | |
| Interfaces (Go-style structural) | ✅ | | | |
| Result\<T,E\> + stdlib base | ✅ | | | |
| **INE QR decoder compiles** | ✅ | | | |
| Lambdas + closures | | ✅ | | |
| Threads + Mutex | | ✅ | | |
| Exceptions (LLVM EH) | | | ✅ | |
| Async/await (LLVM coroutines) | | | | ✅ |
| Inheritance | — | — | — | evaluate |

---

## 8. Risk Register

| Risk | Severity | Mitigation |
|---|---|---|
| Scope creep — wanting everything in v0.1 | High | Use the INE decoder as the gating criterion: if the program doesn't need it, it's not v0.1 |
| Interface dispatch implementation | Medium | Implement as a struct of function pointers; avoid virtual dispatch complexity |
| Template instantiation explosion | Medium | Limit to one level of nesting in v0.1; no recursive templates |
| Closures — environment capture in IR | High | Isolate in Phase 8; everything else works before this |
| LLVM EH (exceptions) | Very High | `landingpad` / `invoke` is not covered by Kaleidoscope; budget extra time and read `ExceptionHandling.rst` separately |
| Async/await runtime design | Very High | Consider `libuv` as event loop base; building a scheduler from scratch is a research project |
| LLVM API changes between versions | Medium | Pin to LLVM 17; test on 18 only after v0.1 ships |

---

## 9. Reading List

### Essential (do before writing code)
- [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/) — complete all 8 chapters
- [Crafting Interpreters](https://craftinginterpreters.com/) — free online; chapters 1–19 cover lexer, parser, and AST
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html) — reference for IR instructions

### For specific phases
- `llvm/docs/ExceptionHandling.rst` — Phase 10
- `llvm/docs/Coroutines.rst` — Phase 11
- [LLVM IRBuilder API docs](https://llvm.org/doxygen/classllvm_1_1IRBuilder.html) — throughout codegen

### Reference implementations (read the source)
- [Clang](https://github.com/llvm/llvm-project/tree/main/clang) — how a real C/C++ frontend works
- [Zig compiler](https://github.com/ziglang/zig) — modern systems language, similar goals

---

## 10. Quick Start Checklist

```bash
# 1. Install LLVM 17
brew install llvm@17                        # macOS
sudo apt install llvm-17-dev clang-17       # Ubuntu/Debian

# 2. Clone and scaffold
mkdir eskiuc && cd eskiuc
git init
mkdir -p lexer parser ast sema codegen runtime stdlib

# 3. Write CMakeLists.txt
# cmake_minimum_required(VERSION 3.20)
# project(eskiuc)
# find_package(LLVM 17 REQUIRED CONFIG)
# include_directories(${LLVM_INCLUDE_DIRS})
# add_definitions(${LLVM_DEFINITIONS})
# add_executable(eskiuc main.cpp)
# llvm_map_components_to_libnames(llvm_libs support core irreader)
# target_link_libraries(eskiuc ${llvm_libs})

# 4. Verify setup
cmake -B build && cmake --build build
./build/eskiuc --version
# → Eskiu 0.0.1 (LLVM 17.x)

# 5. Start with the Lexer
# lexer/lexer.h  → Token struct + TokenType enum
# lexer/lexer.cpp → Lexer::next_token()
```

---

*Eskiu Lang — Language Design Specification v0.2*
*Last updated: June 2026*
