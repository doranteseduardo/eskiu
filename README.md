# Eskiu Lang — Systems Programming Language

> A performance-first systems language with C-style syntax, compiled to LLVM IR. No garbage collector, no borrow checker—just honest memory management.

**Current Status:** v0.0.1-alpha · Phases 0–3 complete · Lexer, Parser, Codegen working

---

## Why Eskiu?

Years of building high-performance, resource-constrained systems exposed a problem: **language fragmentation**. You need C for raw speed, Python for iteration, Go for concurrency—but switching contexts constantly costs time and introduces subtle bugs. Systems programming shouldn't require learning three type systems.

Eskiu unifies this with a single language optimized for systems work:

- **Familiar** — C-style syntax, anyone can read it immediately
- **Fast** — compiles to bare LLVM IR, zero hidden costs
- **Explicit** — stack allocation by default, heap allocation is your choice
- **Practical** — Go-style structural typing, no inheritance hierarchies

### The Proof of Fire

Eskiu's validity is measured on a concrete problem: **porting our INE QR decoder from 3–5 seconds to sub-second latency** on the same constrained hardware. Our production decoder currently uses a patchwork of languages and techniques. Eskiu's goal is to achieve the performance target *in a single, unified language*—eliminating fragmentation and proving that a systems language can be both accessible and performant.

This is not theoretical. It's a shipping product with measurable requirements.

---

## Quick Start

### Installation

```bash
# macOS
brew install llvm@17 cmake

# Linux (Ubuntu/Debian)
sudo apt-get install llvm-17-dev clang-17 cmake

# Verify
llvm-config-17 --version  # → 17.x.x
cmake --version            # → 3.20+
```

### Build Eskiu

```bash
cd ~/Documents/Github/eskiu
mkdir -p build && cd build
cmake ..
cmake --build .

# Test
./eskiuc --version
# → Eskiu 0.0.1 (LLVM 22.1.5)
```

---

## Testing the Compiler

Eskiu exposes three testing modes to inspect the compilation pipeline:

### 1. Tokenization (`--test-lexer`)

See how the lexer breaks your code into tokens:

```bash
./eskiuc examples/hello.esk --test-lexer
```

Output:
```
Tokenizing: examples/hello.esk
========================================================
  Line   1, Col   1           EXTERN  'extern'
  Line   1, Col   8              INT  'int'
  Line   1, Col  12            IDENT  'printf'
  ...
========================================================
Total tokens: 58
```

### 2. AST Parsing (`--test-parser`)

Inspect the Abstract Syntax Tree:

```bash
./eskiuc examples/hello.esk --test-parser
```

Output:
```
Program
  ExternDecl: printf -> int
    Parameters:
      string fmt
      ... ...
  FunctionDecl: add -> int
    Parameters:
      int a
      int b
    Body:
      BlockStmt
        ReturnStmt
          BinaryExpr: +
            Left:
              IdentExpr: a
            Right:
              IdentExpr: b
```

### 3. LLVM Code Generation (`--test-codegen`)

See the generated LLVM IR:

```bash
./eskiuc examples/hello.esk --test-codegen
```

Output:
```llvm
; ModuleID = 'eskiu'
source_filename = "eskiu"

declare i32 @printf(ptr, ...)

define i32 @add(i32 %a, i32 %b) {
entry:
  %0 = add i32 %a, %b
  ret i32 %0
}
```

---

## Language Tour

### Variables and Types

```eskiu
let x: int = 42;
let name: string = "Eskiu";
let pi: float = 3.14;
let ptr: *int = null;

// Stack-allocated structs
let point: Point = Point { x: 1.0, y: 2.0 };
```

### Functions and Extern

```eskiu
// Function declaration
int multiply(int a, int b) {
    return a * b;
}

// Extern (C interop)
extern int printf(string fmt, ...);

// Calling both
int main() {
    int result = multiply(5, 3);
    printf("Result: %d\n", result);
    return 0;
}
```

### Control Flow

```eskiu
// If/else
if (x > 0) {
    printf("positive\n");
} else {
    printf("non-positive\n");
}

// While
while (count < 10) {
    count = count + 1;
}

// For
for (int i = 0; i < 10; i++) {
    printf("%d\n", i);
}

// Switch
switch (status) {
    case 0: printf("ok\n"); break;
    case 1: printf("error\n"); break;
    default: printf("unknown\n"); break;
}
```

### Structs (No Inheritance)

```eskiu
struct Person {
    string name;
    int age;

    void greet() {
        printf("Hello, %s!\n", name);
    }
}

struct Employee {
    Person person;
    string role;
}
```

### Interfaces (Go-style Implicit)

```eskiu
interface Drawable {
    void draw();
}

struct Circle {
    float radius;

    void draw() {
        printf("Drawing circle with radius %f\n", radius);
    }
}

struct Square {
    float side;

    void draw() {
        printf("Drawing square with side %f\n", side);
    }
}

// Circle and Square implicitly satisfy Drawable
void renderAll([]Drawable objects) {
    for obj in objects {
        obj.draw();
    }
}
```

---

## Compiler Architecture

```
┌─────────────────────────────────────────────────┐
│                  Source Code (.esk)             │
└────────────────────┬────────────────────────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   LEXER (Phase 1) ✅   │
        │  Tokenizes into tokens │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   PARSER (Phase 2) ✅  │
        │  Builds Abstract Tree  │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │ TYPE CHECKER (Phase 4) │
        │  Validates types       │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   CODEGEN (Phase 3) ✅ │
        │ Emits LLVM IR          │
        └────────────┬───────────┘
                     │
                     ▼
        ┌────────────────────────┐
        │   LLVM Backend         │
        │  Optimizes & compiles  │
        └────────────┬───────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────┐
│  Native Binary (x86-64, ARM64, WASM, RISC-V)   │
└─────────────────────────────────────────────────┘
```

---

## Development Roadmap

| Phase | Status | Focus | Deliverable |
|-------|--------|-------|-------------|
| **0** | Complete | Setup | LLVM integration, CMake build |
| **1** | Complete | Lexer | Tokenizer for all Eskiu syntax |
| **2** | Complete | Parser | AST construction, error recovery |
| **3** | Complete | Codegen | LLVM IR generation, basic types |
| **4** | Next | Type Checker | Type inference, validation |
| **5** | Planned | Composites | Structs, interfaces, templates |
| **6** | Planned | Memory | `alloc`/`free`, String type |
| **7** | Planned | Stdlib | `Result<T,E>`, collections |
| **8** | Planned | Lambdas | Closures, first-class functions |
| **9** | Planned (v0.2) | Threads | pthreads, Mutex |
| **10** | Planned (v1.0) | Exceptions | try/catch/finally (LLVM EH) |
| **11** | Planned (v2.0) | Async | Coroutines, event loop |

---

## File Structure

```
eskiu/
├── CMakeLists.txt           ← Build configuration
├── SETUP.md                 ← Installation guide
├── README.md                ← This file
│
├── lexer/
│   ├── lexer.h              ← Token definitions
│   └── lexer.cpp            ← Tokenizer implementation
│
├── parser/
│   ├── parser.h             ← Parser interface
│   └── parser.cpp           ← Recursive descent parser
│
├── ast/
│   ├── ast.h                ← AST node types
│   ├── ast.cpp              ← Visitor implementation
│   └── ast_printer.cpp      ← Debug pretty-printer
│
├── codegen/
│   ├── codegen.h            ← LLVM IR generation
│   └── codegen.cpp          ← Visitor-based codegen
│
├── sema/                    ← (Phase 4) Type checker
├── runtime/                 ← (Phase 6) Runtime support
├── stdlib/                  ← (Phase 7) Standard library
│
└── examples/
    └── hello.esk            ← Simple test program
```

---

## Design Philosophy

### 1. Performance-First
Near-metal output, no hidden costs. What you write is what you get.

### 2. C-Style Familiarity
Familiar syntax means less mental overhead. Anyone who knows C can read Eskiu immediately.

### 3. Honest Memory Model
- **Stack allocation** is the default (fast, automatic cleanup)
- **Heap allocation** is explicit (`alloc`/`free`)
- No garbage collection overhead

### 4. No Borrow Checker
Manual memory management is simpler than fighting a borrow checker. Trade memory safety for simplicity.

### 5. Practical Polymorphism
Go-style **structural typing** (interfaces) instead of inheritance hierarchies. If it quacks, it's a duck.

### 6. Errors as Values
Use `Result<T, E>` for error handling. Exceptions come later (v1.0).

---

## Building Your First Program

Create `hello.esk`:

```eskiu
extern int printf(string fmt, ...);

int main() {
    printf("Hello, Eskiu!\n");
    return 0;
}
```

Test the pipeline:

```bash
# Tokenize
./eskiuc hello.esk --test-lexer

# Parse
./eskiuc hello.esk --test-parser

# Generate IR
./eskiuc hello.esk --test-codegen
```

---

## Contributing

Contributions welcome! See [CONTRIBUTING.md](./CONTRIBUTING.md) (coming soon).

**Development workflow:**
1. Pick a phase from the roadmap
2. Write a test case first
3. Implement the feature
4. Ensure tests pass
5. Commit with clear message

---

## License

MIT

---

**Status:** v0.0.1-alpha (Phases 0–3 complete)  
**Last Updated:** June 2026  
**Compiler:** LLVM 22+  
**Language:** C++17

For the full language specification, see [eskiu-lang-plan.md](./docs/language-spec.md).
