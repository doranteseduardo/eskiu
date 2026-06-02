# Getting Started with Eskiu

This guide walks you through installing Eskiu, understanding its philosophy, and writing your first real programs.

## Table of Contents

1. [Installation](#installation)
2. [Your First Program](#your-first-program)
3. [Understanding the Language](#understanding-the-language)
4. [Working with Memory](#working-with-memory)
5. [Debugging Common Errors](#debugging-common-errors)
6. [Next Steps](#next-steps)

---

## Installation

### System Requirements

- **macOS:** Xcode Command Line Tools, LLVM 14+
- **Linux:** GCC/Clang, LLVM 14+
- **CMake:** 3.10 or later

### Install LLVM

**macOS:**
```bash
brew install llvm
export LLVM_CONFIG=$(brew --prefix llvm)/bin/llvm-config
```

**Ubuntu/Debian:**
```bash
apt-get install llvm-14 llvm-14-dev
```

**Fedora/RHEL:**
```bash
dnf install llvm-devel llvm-libs
```

### Build Eskiu

```bash
git clone https://github.com/yourusername/eskiu.git
cd eskiu
mkdir build
cd build
cmake ..
make -j$(nproc)
```

Verify the build:
```bash
./eskiu --version
```

---

## Your First Program

### Hello World

Create `hello.esk`:

```esk
extern fn printf(format: *i8, ...) -> i32;

fn main() -> i32 {
    printf("Hello, Eskiu!\n");
    return 0;
}
```

Compile and run:
```bash
cd build
./eskiu compile ../hello.esk -o hello
./hello
```

Output:
```
Hello, Eskiu!
```

### Understanding What Happened

1. **`extern fn printf(...)`** — Declares a C function we'll link against
2. **`fn main() -> i32`** — Defines the entry point, returns an exit code
3. **`printf(...)`** — Calls the C function (variadic, so `printf` requires manual type annotation)
4. **`return 0;`** — Exit with code 0 (success)

The compiler translates `hello.esk` to LLVM IR, then links it with the C library. Final binary is executable.

---

## Understanding the Language

### Variables and Types

Eskiu is C-inspired with explicit types and no hidden type conversions.

```esk
fn main() -> i32 {
    let x: i32 = 42;
    let y: i64 = 100;
    let name: *i8 = "Eskiu";
    let ptr: *i32 = &x;
    return 0;
}
```

**Supported Types:**
- **Integers:** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`
- **Floats:** `f32`, `f64`
- **Pointers:** `*T` (unsafe, no bounds checking)
- **Structs:** `struct Name { fields... }`
- **Booleans:** `bool` (true/false)

### Functions

```esk
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    let result = add(5, 3);
    return result;
}
```

All parameters require explicit types. No default arguments or function overloading.

### Control Flow

```esk
fn sign(x: i32) -> i32 {
    if (x > 0) {
        return 1;
    } else if (x < 0) {
        return -1;
    } else {
        return 0;
    }
}

fn countdown(n: i32) -> i32 {
    let i = n;
    while (i > 0) {
        printf("%d\n", i);
        i = i - 1;
    }
    return 0;
}

fn factorial(n: i32) -> i32 {
    let result = 1;
    for (let i: i32 = 1; i <= n; i = i + 1) {
        result = result * i;
    }
    return result;
}
```

---

## Working with Memory

Eskiu is **honest about memory**: no garbage collection, explicit pointers, manual management.

### Pointers and References

```esk
fn swap(a: *i32, b: *i32) -> i32 {
    let temp = *a;
    *a = *b;
    *b = temp;
    return 0;
}

fn main() -> i32 {
    let x: i32 = 10;
    let y: i32 = 20;
    swap(&x, &y);
    printf("x=%d, y=%d\n", x, y);  // x=20, y=10
    return 0;
}
```

### Structs

```esk
struct Point {
    x: i32
    y: i32
}

fn distance_squared(p: Point) -> i32 {
    return p.x * p.x + p.y * p.y;
}

fn main() -> i32 {
    let p: Point = {0};  // Initialize to zero
    p.x = 3;
    p.y = 4;
    let d_sq = distance_squared(p);
    printf("distance squared: %d\n", d_sq);  // 25
    return 0;
}
```

### Memory Errors (and How to Avoid Them)

Eskiu **won't prevent** these errors — you must avoid them:

**Use-after-free:**
```esk
let ptr: *i32 = ...;
free(ptr);
let x = *ptr;  // UNDEFINED BEHAVIOR
```

**Buffer overflow:**
```esk
let arr: [10]i32 = {0};
arr[100] = 5;  // UNDEFINED BEHAVIOR (no bounds check)
```

**Uninitialized memory:**
```esk
let x: i32;
printf("%d\n", x);  // x is uninitialized
```

**Best practices:**
- Initialize variables when declared
- Track pointer lifetime manually
- Don't dereference null pointers
- Validate array bounds yourself

---

## Debugging Common Errors

### Compilation Errors

#### "unknown identifier 'foo'"

The compiler can't find the name `foo`. Check:

1. Spelling and case (Eskiu is case-sensitive)
2. Variable declared in an outer scope? (Only outer scopes are visible)
3. Using a C function? Use `extern` to declare it

Example:
```esk
extern fn strlen(s: *i8) -> i32;  // Now you can call strlen
```

#### "type mismatch: expected i32, got i64"

Eskiu doesn't auto-convert between types. Be explicit:

```esk
let x: i64 = 100;
let y: i32 = (i32)x;  // Cast explicitly
```

#### "struct 'Point' has no member 'z'"

The struct is missing that field. Check the definition and access only existing members.

### Linker Errors

#### "undefined reference to 'printf'"

Your binary didn't link the C standard library. Ensure LLVM is configured to link libc:

```bash
./eskiu compile program.esk -o program
# Should automatically link libc; if not, your LLVM setup needs investigation
```

### Runtime Errors

#### Segmentation fault / crash

You likely hit undefined behavior. Common causes:

- Dereferencing a null pointer
- Dereferencing a freed pointer
- Buffer overflow
- Stack overflow (infinite recursion)

Add `printf` statements to narrow down where it crashes.

### Using Test Modes

Run the compiler with flags to debug early stages:

```bash
# See tokenization
./eskiu compile program.esk --test-lexer

# See parsed AST
./eskiu compile program.esk --test-parser

# See type checking
./eskiu compile program.esk --test-typechecker

# See LLVM IR before linking
./eskiu compile program.esk -emit-llvm -o program.ll
```

---

## Next Steps

### Explore Examples

Look at real programs in `examples/`:

- `hello.esk` — Calls C functions
- `fibonacci.esk` — Recursion and loops
- `struct_usage.esk` — Working with structs

Run them to see what's possible:
```bash
cd build
./eskiu compile ../examples/fibonacci.esk -o fib
./fib
```

### Read the Language Spec

[Language Spec](LANGUAGE_SPEC.md) documents every feature, operator precedence, and syntax rule.

### Understand the Compiler

[Architecture](ARCHITECTURE.md) explains how Eskiu is built:
- Lexer (tokenization)
- Parser (syntax tree)
- Type Checker (semantic analysis)
- Code Generator (LLVM IR)
- Linker (final binary)

### Contribute

Want to add features? See [Contributing](CONTRIBUTING.md) for development workflow.

### Ask Questions

- Check [FAQ](FAQ.md) for common questions
- See [Debugging](DEBUGGING.md) for troubleshooting
- File a bug on GitHub with error output and minimal test case

---

## Cheat Sheet

```esk
// Variables and types
let x: i32 = 10;
let ptr: *i32 = &x;

// Functions
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Control flow
if (x > 0) {
    // ...
} else {
    // ...
}

while (x > 0) {
    x = x - 1;
}

for (let i: i32 = 0; i < 10; i = i + 1) {
    // ...
}

// Structs
struct Point {
    x: i32
    y: i32
}

let p: Point = {0};
p.x = 5;

// Calling C functions
extern fn printf(format: *i8, ...) -> i32;
printf("Hello\n");

// Casts
let y: i64 = (i64)x;
```

---

You're ready to start! Pick an example from `examples/`, modify it, and compile. Good luck!
