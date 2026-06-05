---
# Getting Started with Eskiu

A hands-on introduction to the Eskiu language. You will go from zero to writing
and inspecting real compiled programs in about 30 minutes.

All code blocks in this document compile and run with **Eskiu v0.1.0**.
---

## Installation

### Prerequisites

| Tool         | Minimum version | Notes                             |
| ------------ | --------------- | --------------------------------- |
| LLVM         | 17+             | Headers and libraries required    |
| CMake        | 3.20+           | Build system                      |
| C++ compiler | C++17           | GCC 7+, Clang 5+, or Apple Clang  |
| clang        | any recent      | Used to link the final binary     |

### macOS

```bash
brew install llvm cmake
export LLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm
```

Add the `export` line to `~/.zshrc` to make it permanent. Then clone and build:

```bash
git clone https://github.com/doranteseduardo/eskiu.git
cd eskiu
cmake -S . -B build
cmake --build build -j$(sysctl -n hw.ncpu)
```

### Linux (Ubuntu / Debian)

```bash
sudo apt-get install -y cmake llvm-17-dev clang-17 build-essential
cmake -S . -B build
cmake --build build -j$(nproc)
```

### Verify

```bash
./build/eskiuc --version
# Eskiu 0.1.0 (LLVM 22.1.6)   — exact LLVM version depends on your install
```

Add `./build` to your `PATH` so you can type `eskiuc` from any directory.

---

## Your First Program

Create `hello.esk`:

```eskiu
extern int printf(string fmt, ...);

int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(5, 3);
    printf("Hello from Eskiu!\n");
    printf("Result: %d\n", result);
    return 0;
}
```

Compile and run. When the `-o` name has no `.o` extension, `eskiuc` links the
program into an executable for you (it calls your system C toolchain under the
hood, exactly as `rustc` does):

```bash
eskiuc hello.esk -o hello
./hello
```

Prefer the two steps — for example to inspect or reuse the object file? Give the
output a `.o` name (or pass `-c`) and link it yourself:

```bash
eskiuc hello.esk -o hello.o   # object only
clang hello.o -o hello        # link
./hello
```

Expected output:

```
Hello from Eskiu!
Result: 8
```

### Peek at the generated IR

Before producing an object file you can ask the compiler to print the LLVM IR it
would emit:

```bash
eskiuc hello.esk --test-codegen
```

Abridged output:

```llvm
@0 = private unnamed_addr constant [19 x i8] c"Hello from Eskiu!\0A\00"
@1 = private unnamed_addr constant [12 x i8] c"Result: %d\0A\00"

declare i32 @printf(ptr, ...)

define i32 @add(i32 %a, i32 %b) {
entry:
  %0 = add i32 %a, %b
  ret i32 %0
}

define i32 @main() {
entry:
  %result = alloca i32, align 4
  %0 = call i32 @add(i32 5, i32 3)
  store i32 %0, ptr %result, align 4
  ...
  ret i32 0
}
```

String literals become private globals. Local variables are stack slots
(`alloca`). The `add` function compiles down to a single `add i32` instruction.

---

## Variables and Types

### Declaring variables

Eskiu supports two equivalent declaration styles:

```eskiu
// C-style
int x = 42;
float pi = 3.14;
bool flag = true;

// let-style
let x: int = 42;
let pi: float = 3.14;
let flag: bool = true;
```

Both styles compile to the same IR. Use whichever reads more clearly in context.

### Primitive types

| Type     | Width    | Notes                           |
| -------- | -------- | ------------------------------- |
| `bool`   | 1 bit    | `true` / `false`                |
| `char`   | 8-bit    | Single character                |
| `int`    | 32-bit   | Signed                          |
| `int8`   | 8-bit    | Signed                          |
| `int16`  | 16-bit   | Signed                          |
| `int32`  | 32-bit   | Alias for `int`                 |
| `int64`  | 64-bit   | Signed                          |
| `uint`   | 32-bit   | Unsigned                        |
| `uint8`  | 8-bit    | Unsigned (byte)                 |
| `uint16` | 16-bit   | Unsigned                        |
| `uint32` | 32-bit   | Unsigned                        |
| `uint64` | 64-bit   | Unsigned                        |
| `float`  | 32-bit   | IEEE 754 single-precision       |
| `double` | 64-bit   | IEEE 754 double-precision       |
| `string` | pointer  | Null-terminated C string        |
| `void`   | —        | Used as function return type    |

### Integer and hex literals

```eskiu
int a = 255;
int b = 0xFF;    // same value — hex prefix supported
uint8 mask = 0x0F;
int64 big = 1000000;
int neg = -42;   // negative literals work anywhere, including global scope
float f = -3.14;
```

### Pointer types

Prefix `*` to make a type a pointer:

```eskiu
let p: *int = null;
int x = 10;
p = &x;           // address-of
int val = *p;     // dereference
```

---

## Operators

### Arithmetic

```eskiu
int a = 10;
int b = 3;
int sum  = a + b;   // 13
int diff = a - b;   // 7
int prod = a * b;   // 30
int quot = a / b;   // 3
int rem  = a % b;   // 1
```

### Comparison

All comparison operators work on integers, floats, and pointers:

```eskiu
bool eq  = (a == b);
bool neq = (a != b);
bool lt  = (a <  b);
bool gt  = (a >  b);
bool lte = (a <= b);
bool gte = (a >= b);
```

### Logical

```eskiu
bool x = true;
bool y = false;
bool both = x && y;   // false
bool either = x || y; // true
bool inv = !x;        // false
```

### Bitwise

```eskiu
int flags = 0xFF;
int low   = flags & 0x0F;   // AND  → 15
int hi    = flags | 0x100;  // OR   → 511
int xord  = flags ^ 0x55;   // XOR  → 170
int shl   = 1 << 4;         // shift left  → 16
int shr   = flags >> 4;     // shift right → 15
int inv2  = ~0;              // bitwise NOT → -1
```

### Compound assignment

```eskiu
int x = 10;
x += 5;    // x = 15
x -= 3;    // x = 12
x *= 2;    // x = 24
x /= 4;    // x = 6
x %= 4;    // x = 2
x = x >> 1; // x = 1  (compound bitwise assignment like >>= is not supported)
```

### Address-of and dereference

```eskiu
int n = 42;
let ptr: *int = &n;   // address-of
int copy = *ptr;      // dereference → 42
```

### Pointer arithmetic

```eskiu
extern int printf(string fmt, ...);

int main() {
    let buf: *uint8 = alloc(uint8, 4);
    *buf = 10;
    *(buf + 1) = 20;
    *(buf + 2) = 30;
    printf("%d %d %d\n", *buf, *(buf + 1), *(buf + 2));
    free(buf);
    return 0;
}
```

`ptr + n` advances by `n * sizeof(*ptr)` bytes — typed arithmetic. Adding `1` to a `*uint8` moves one byte; adding `1` to a `*int` moves four bytes. `*void` and `*char` are the exceptions: they always use byte-level stride.

### Cast

```eskiu
float f = 3.99;
int   i = (int)f;      // truncates → 3
uint8 b = (uint8)255;
```

---

## Control Flow

### if / else

```eskiu
extern int printf(string fmt, ...);

int main() {
    int x = 7;
    if (x > 10) {
        printf("big\n");
    } else if (x > 4) {
        printf("medium\n");
    } else {
        printf("small\n");
    }
    return 0;
}
```

Output: `medium`

### while

```eskiu
extern int printf(string fmt, ...);

int main() {
    int i = 1;
    while (i <= 5) {
        printf("%d\n", i);
        i += 1;
    }
    return 0;
}
```

### for with declaration init

The loop variable can be declared directly in the `for` header:

```eskiu
extern int printf(string fmt, ...);

int main() {
    for (int i = 0; i < 5; i += 1) {
        printf("i = %d\n", i);
    }
    return 0;
}
```

### break and continue

```eskiu
extern int printf(string fmt, ...);

int main() {
    for (int i = 0; i < 10; i += 1) {
        if (i == 3) { continue; }
        if (i == 6) { break; }
        printf("%d\n", i);
    }
    return 0;
}
```

Output: `0 1 2 4 5` (one per line).

### switch / case

```eskiu
extern int printf(string fmt, ...);

int main() {
    int day = 3;
    switch (day) {
        case 1:
            printf("Monday\n");
            break;
        case 2:
            printf("Tuesday\n");
            break;
        case 3:
            printf("Wednesday\n");
            break;
        default:
            printf("Other\n");
            break;
    }
    return 0;
}
```

Output: `Wednesday`

---

## Functions

### Defining and calling

Return type comes first, followed by the name and parameter list:

```eskiu
extern int printf(string fmt, ...);

int square(int n) {
    return n * n;
}

bool isEven(int n) {
    return n % 2 == 0;
}

int main() {
    printf("4^2 = %d\n", square(4));
    if (isEven(10)) {
        printf("10 is even\n");
    }
    return 0;
}
```

### extern for C interop

`extern` declares a function that lives in a C library. The compiler emits an
LLVM `declare` and the linker resolves it:

```eskiu
extern int printf(string fmt, ...);
extern int puts(string s);
extern double sqrt(double x);
```

The `...` marks a variadic function — required for `printf`, `scanf`, etc.

### Declaration order

Definition order doesn't matter: a function can call another that's defined later in the file, and mutual recursion works out of the box. You can also write a body-less forward declaration if you prefer to state a signature up front:

```eskiu
int is_odd(int n);                  // forward declaration (optional)

int is_even(int n) {
    if (n == 0) { return 1; }
    return is_odd(n - 1);           // defined below — works
}

int is_odd(int n) {
    if (n == 0) { return 0; }
    return is_even(n - 1);
}
```

---

## Structs

### Defining and using

```eskiu
extern int printf(string fmt, ...);

struct Point {
    float x;
    float y;
}

int main() {
    let p: Point = Point { x: 1.5, y: 2.5 };
    printf("x=%f y=%f\n", p.x, p.y);
    p.x = 10.0;
    printf("x=%f\n", p.x);
    return 0;
}
```

Struct literals accept either named fields (`Point { x: 1.0, y: 2.0 }`) or
positional fields (`Point { 1.0, 2.0 }`).

### Methods with self

Methods are defined inside the struct body and receive a pointer to the instance
as `self`:

```eskiu
extern int printf(string fmt, ...);

struct Rect {
    float w;
    float h;

    float area() {
        return self.w * self.h;
    }

    void print() {
        printf("Rect(%f x %f)\n", self.w, self.h);
    }
}

int main() {
    let r: Rect = Rect { w: 4.0, h: 3.0 };
    r.print();
    printf("area = %f\n", r.area());
    return 0;
}
```

The compiler lowers `r.area()` to `Rect_area(ptr %r)` — the receiver is passed
as the first argument.

### Fixed-size array fields

A struct field can be a fixed-size array:

```eskiu
struct Packet {
    uint8[858] left;
    uint8[858] right;
    int        len;
}
```

In LLVM IR this becomes `[858 x i8]` — no heap allocation required.

---

## Templates

### Template structs

Use angle brackets to parameterize a struct over one or more types:

```eskiu
struct Pair<A, B> {
    A first;
    B second;
}
```

Instantiate by supplying concrete types:

```eskiu
extern int printf(string fmt, ...);

struct Pair<A, B> {
    A first;
    B second;
}

int main() {
    let p: Pair<int, float> = Pair<int, float> { first: 7, second: 3.14 };
    printf("first=%d second=%f\n", p.first, p.second);
    return 0;
}
```

The compiler performs lazy monomorphic instantiation: `Pair<int, float>` becomes
`%Pair_int_float` in IR.

### Template functions

```eskiu
extern int printf(string fmt, ...);

T max<T>(T a, T b) {
    if (a > b) { return a; }
    return b;
}

int main() {
    printf("%d\n", max<int>(3, 5));      // explicit type argument
    printf("%f\n", max<float>(1.2, 0.8));
    printf("%d\n", max(7, 2));            // inferred: T = int
    return 0;
}
```

The return type is `T` — the function returns the same type it is instantiated with. The type argument can be explicit (`max<int>(3, 5)`) or inferred from the arguments (`max(7, 2)`) when the type parameter appears directly as a parameter type.

### Result<T, E> from stdlib

`<result>` provides an error-as-value type ready to use:

```eskiu
import <result>;

extern int printf(string fmt, ...);

Result<int, string> divide(int a, int b) {
    if (b == 0) {
        return Err<int, string>("division by zero");
    }
    return Ok<int, string>(a / b);
}

int main() {
    let r: Result<int, string> = divide(10, 2);
    if (r.ok == 1) {
        printf("result = %d\n", r.value);
    } else {
        printf("error: %s\n", r.error);
    }
    return 0;
}
```

---

## Interfaces

Interfaces declare a set of method signatures. Any struct that provides matching
methods satisfies the interface — no `implements` keyword is needed.

### Declaring an interface

```eskiu
interface Drawable {
    void draw();
}
```

### Implementing structurally

```eskiu
extern int printf(string fmt, ...);

interface Drawable {
    void draw();
}

struct Circle {
    float radius;

    void draw() {
        printf("Circle(r=%f)\n", self.radius);
    }
}

struct Square {
    float side;

    void draw() {
        printf("Square(s=%f)\n", self.side);
    }
}
```

`Circle` and `Square` both satisfy `Drawable` automatically because they
implement a `draw()` method with a matching signature.

### Passing a struct as an interface

Pass a pointer to the struct where an interface parameter is expected. The
compiler auto-boxes it into a fat pointer `{data_ptr, vtable_ptr}` and performs
the vtable dispatch at the call site:

```eskiu
void render(Drawable d) {
    d.draw();
}

int main() {
    let c: Circle = Circle { radius: 5.0 };
    let s: Square = Square { side: 3.0 };
    render(&c);
    render(&s);
    return 0;
}
```

Output:

```
Circle(r=5.000000)
Square(s=3.000000)
```

---

## Memory

### Stack allocation (default)

All local variables and struct instances live on the stack automatically:

```eskiu
int x = 42;
let p: Point = Point { x: 1.0, y: 2.0 };
```

### Heap allocation with alloc / free

`alloc(T, N)` allocates `N` items of type `T` on the heap and returns `*T`.
`free(ptr)` releases the memory:

```eskiu
extern int printf(string fmt, ...);

int main() {
    let buf: *int = alloc(int, 8);
    for (int i = 0; i < 8; i += 1) {
        *(buf + i) = i * i;
    }
    for (int i = 0; i < 8; i += 1) {
        printf("%d ", *(buf + i));
    }
    printf("\n");
    free(buf);
    return 0;
}
```

Output: `0 1 4 9 16 25 36 49`

### Null checks

```eskiu
extern int printf(string fmt, ...);

int main() {
    let p: *int = null;
    if (p == null) {
        printf("pointer is null\n");
    }
    return 0;
}
```

Pointer comparisons use integer equality, not floating-point equality.

### Dereference

```eskiu
int n = 100;
let ptr: *int = &n;
*ptr = 200;       // write through pointer
int val = *ptr;   // read through pointer — val == 200
```

---

## Multi-file Projects

Eskiu has two import forms:

```eskiu
import <result>       // stdlib module — resolved by the compiler
import "utils.esk"    // local file — relative to the current file
```

`import <name>` looks up the module in the Eskiu installation's stdlib directory. No path required. `import "path"` is relative to the importing file, as before.

Each file is parsed only once regardless of how many times it is imported.

### Example layout

```
project/
  main.esk
  utils.esk
```

`utils.esk`:

```eskiu
extern int printf(string fmt, ...);

void greet(string name) {
    printf("Hello, %s!\n", name);
}

int clamp(int val, int lo, int hi) {
    if (val < lo) { return lo; }
    if (val > hi) { return hi; }
    return val;
}
```

`main.esk`:

```eskiu
import "utils.esk";

int main() {
    greet("Eskiu");
    int v = clamp(150, 0, 100);
    return 0;
}
```

Compile the entry point only — the compiler follows imports automatically:

```bash
eskiuc main.esk -o main
./main
```

### Using stdlib modules

Import stdlib modules with angle brackets — no path needed:

```eskiu
import <result>;
import <io>;
import <math>;
```

Available modules:

| Module              | Contents                                              |
| ------------------- | ----------------------------------------------------- |
| `<result>` | `Result<T,E>`, `Ok<T,E>()`, `Err<T,E>()`             |
| `<list>`   | `List<T>` — `List_init`, `push`, `get`, `len`, `free` |
| `<string>` | `String` — `init`, `from`, `append`, `concat`, `cstr`, `len`, `free` |
| `<math>`   | `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs`         |
| `<io>`     | `printf`, `fprintf`, `sprintf`, `scanf`, `puts`       |
| `<mem>`    | `memcpy`, `memset`, `memmove`, `memcmp`, `strlen`     |
| `<fs>`     | `fs_open`, `fs_close`, `fs_read`, `fs_write`, `fs_puts`, `fs_seek`, `fs_tell`, `fs_size`, `fs_read_all`, `fs_write_all`, `fs_eof`, `fs_error` |

Note: when using `<math>` link with `-lm`. Library flags are passed straight
through to the linker, so the one-command form works too:

```bash
eskiuc file.esk -o file -lm
```

---

## Lambdas

Eskiu supports anonymous functions (lambdas) as first-class values. The syntax
mirrors a regular function declaration without a name.

### Function pointer types

Use the `fn(T,...)->R` syntax to declare a function pointer type:

```eskiu
let f: fn(int)->int;
let g: fn(int, int)->bool;
```

### Writing a lambda

```eskiu
extern int printf(string fmt, ...);

int main() {
    let double_it: fn(int)->int = int(int x) { return x * 2; };
    printf("%d\n", double_it(5));   // 10
    return 0;
}
```

### Passing lambdas as arguments

```eskiu
extern int printf(string fmt, ...);

int apply(fn(int)->int f, int x) {
    return f(x);
}

int main() {
    let square: fn(int)->int = int(int n) { return n * n; };
    printf("%d\n", apply(square, 6));        // 36
    printf("%d\n", apply(int(int n) { return n + 1; }, 9));  // 10
    return 0;
}
```

### Closures — capturing from the enclosing scope

A lambda can reference variables declared in the surrounding scope. Those variables are captured by value at the point the lambda is created.

```eskiu
extern int printf(string fmt, ...);

int main() {
    int base = 10;
    let add: fn(int)->int = int(int x) { return x + base; };
    printf("%d\n", add(5));   // 15
    printf("%d\n", add(7));   // 17
    return 0;
}
```

Closures can be passed to higher-order functions exactly like plain lambdas — the `fn(T)->R` type is the same in both cases.

```eskiu
int apply(fn(int)->int f, int x) { return f(x); }

int main() {
    int offset = 100;
    let shift: fn(int)->int = int(int x) { return x + offset; };
    printf("%d\n", apply(shift, 5));   // 105
    return 0;
}
```

Under the hood, `fn(T)->R` is a fat pointer `{fn_ptr, env_ptr}`. Non-capturing lambdas have `env_ptr = null` and behave identically to before.

---

## Threads

`thread_create` and `thread_join` are language keywords. Pass any `fn()->void` value — including a closure — to spawn a thread.

### Basic usage

```eskiu
extern int printf(string fmt, ...);

int main() {
    *void t = thread_create(void() { printf("hello from thread\n"); });
    thread_join(t);
    return 0;
}
```

### Capturing outer state

```eskiu
extern int printf(string fmt, ...);

int main() {
    int id = 42;
    let worker: fn()->void = void() { printf("thread id = %d\n", id); };
    *void t = thread_create(worker);
    thread_join(t);
    return 0;
}
```

The closure fat pointer maps directly to pthread's `(start_routine, arg)` pair — no trampoline is generated. On Linux, link with `-lpthread`:

```bash
eskiuc threads.esk -o threads -lpthread
./threads
```

---

## Exception Handling

Use `try`, `catch`, `finally`, and `throw` for structured exception handling.

### Basic try/catch

```eskiu
extern int printf(string fmt, ...);

int divide(int a, int b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}

int main() {
    try {
        int r = divide(10, 0);
        printf("result: %d\n", r);
    } catch (string e) {
        printf("caught: %s\n", e);
    }
    return 0;
}
```

Output: `caught: division by zero`

### With finally

The `finally` block always executes, whether or not an exception was raised:

```eskiu
extern int printf(string fmt, ...);

int main() {
    try {
        throw "error";
    } catch (string e) {
        printf("caught: %s\n", e);
    } finally {
        printf("cleanup\n");
    }
    return 0;
}
```

Output:
```
caught: error
cleanup
```

### Linking

Exception handling uses the Itanium C++ ABI personality function. Link with `-lc++` on macOS or `-lstdc++` on Linux:

```bash
eskiuc file.esk -o file -lc++      # macOS
eskiuc file.esk -o file -lstdc++   # Linux
./file
```

---

## IDE Tooling (VS Code)

The `editor/vscode/` directory contains a VS Code extension for Eskiu.

### Install

```bash
ln -s $(pwd)/editor/vscode ~/.vscode/extensions/eskiu-language
```

Restart VS Code. `.esk` files will have syntax highlighting immediately.

### Features

| Feature | How it works |
|---------|-------------|
| Syntax highlighting | TextMate grammar (`eskiu.tmLanguage.json`) |
| Real-time error squiggles | Extension runs `eskiuc --test-typechecker` on save and parses `file:line:col: message` output |
| Hover type info | Extension calls `eskiuc --hover-at LINE:COL` and shows the result in a tooltip |
| Go-to-definition | Extension calls `eskiuc --definition-at LINE:COL` and jumps to the returned location |

### CLI flags used by the extension

```bash
# Print the inferred type of the expression at line 10, column 5
eskiuc file.esk --hover-at 10:5

# Print the definition location of the symbol at line 10, column 5
eskiuc file.esk --definition-at 10:5
```

Both flags accept `LINE:COL` with 1-based line and column numbers and print a
single line of output. They can also be used directly on the command line for
scripting.

---

## Using the Test Modes

The compiler exposes diagnostic flags that stop compilation after a specific
phase and print what was produced. None of them produce an object file.

| Flag                      | Phase        | Output                       | When to use                                   |
| ------------------------- | ------------ | ---------------------------- | --------------------------------------------- |
| `--test-lexer`            | Lexer        | Token stream with line/col   | Debugging unexpected parse errors             |
| `--test-parser`           | Parser       | Indented AST                 | Checking whether syntax is parsed correctly   |
| `--test-typechecker`      | Type checker | Errors, then `Type checking succeeded!` / `failed!` | Catching type mismatches before codegen |
| `--test-codegen`          | Code gen     | LLVM IR                      | Inspecting what IR the compiler produces      |
| `--hover-at LINE:COL`     | Type checker | Type at position             | Hover type info (used by VS Code extension)   |
| `--definition-at LINE:COL`| Type checker | Definition location          | Go-to-definition (used by VS Code extension)  |

### --test-lexer

```bash
eskiuc hello.esk --test-lexer
```

Produces one token per line with its kind, text, and `line:col` position.
Useful when you see a parse error and want to check if the tokenizer is splitting
tokens correctly.

### --test-parser

```bash
eskiuc hello.esk --test-parser
```

Prints the full AST as an indented tree. Each node shows its kind and the
relevant identifiers or literals. Use this to verify that operator precedence,
struct literals, and template arguments are parsed the way you expect.

### --test-typechecker

```bash
eskiuc hello.esk --test-typechecker
```

Runs type inference and struct field validation. If a struct field name is wrong
you see the error here, before any IR is generated:

```
hello.esk:5:14: struct 'Point' has no member 'z'
```

### --test-codegen

```bash
eskiuc hello.esk --test-codegen
```

Prints the full LLVM IR to stdout. Useful for verifying that a cast, bitwise
operation, or pointer dereference lowered the way you intended.

---

## Inline Assembly and volatile

These features are primarily useful for bare-metal and kernel programming. Both are available in any Eskiu program.

### volatile

Mark a pointer variable `volatile` to prevent the compiler from optimising away loads and stores through it. This is essential for memory-mapped I/O (MMIO) registers:

```eskiu
volatile let uart: *uint8 = (uint8*) 0x3F8;

// Both the read and the write are always emitted — never cached or removed
uint8 status = *uart;
*uart = 'A';
```

Without `volatile`, the compiler may eliminate or reorder accesses to addresses it considers side-effect-free.

### Inline assembly — simple form

```eskiu
asm("cli");   // disable interrupts
asm("sti");   // enable interrupts
asm("nop");
```

The string is passed verbatim to the assembler. Use this form for instructions that take no operands.

### Inline assembly — extended form

The extended form passes values in and out of the asm template using GCC-compatible constraints:

```eskiu
// Write a byte to an x86 I/O port
void outb(uint8 val, uint16 port) {
    asm("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");
}
```

Syntax: `asm("template" : outputs : inputs : clobbers);`

- Inputs and outputs are `"constraint"(expression)` pairs.
- `"memory"` in the clobber list acts as a compiler barrier.
- Common constraints: `"a"` → rax/eax, `"Nd"` → 8-bit immediate or dx, `"r"` → any register.

### Freestanding mode

When targeting bare metal or a kernel, pass `--freestanding` to the compiler. This redirects the built-in `alloc` and `free` to user-provided `esk_alloc` / `esk_free` symbols instead of the libc `malloc` / `free`:

```bash
eskiuc kernel.esk --target x86_64-pc-linux-gnu --freestanding -o kernel.o
```

You must provide `esk_alloc` and `esk_free` in your own source or a C shim:

```eskiu
// kernel_alloc.esk — linked together with kernel.esk
*void esk_alloc(int size) { return bump_alloc(size); }
void  esk_free(*void ptr)  { bump_free(ptr); }
```

---

## sizeof

`sizeof(T)` is a compile-time constant expression that returns the size of a type in bytes as an `int64`. It works for any Eskiu type, including structs.

```eskiu
extern int printf(string fmt, ...);

struct Grid {
    float x;
    float y;
    float z;
}

int main() {
    printf("sizeof(int)    = %lld\n", sizeof(int));    // 4
    printf("sizeof(int64)  = %lld\n", sizeof(int64));  // 8
    printf("sizeof(Grid)   = %lld\n", sizeof(Grid));   // 12
    return 0;
}
```

`sizeof` is resolved entirely at compile time and produces no runtime code.

---

## Unions

A `union` is declared like a `struct`, but all fields share offset 0. The size of the union equals the size of its largest field. Reading a field reinterprets the stored bytes as that field's type.

```eskiu
extern int printf(string fmt, ...);

union Value {
    int   i;
    float f;
}

int main() {
    let v: Value;
    v.i = 0x3F800000;            // IEEE 754 bit pattern for 1.0
    printf("%f\n", v.f);         // 1.000000 — same bytes, read as float
    v.f = 3.14;
    printf("%d\n", v.i);         // raw integer bits of 3.14
    return 0;
}
```

---

## Typed Pointer Arithmetic

Pointer arithmetic scales by the pointed-to type's size, matching C behaviour. Adding `1` to a `*int` advances 4 bytes; adding `1` to a `*Grid` advances `sizeof(Grid)` bytes.

```eskiu
extern int printf(string fmt, ...);

int main() {
    let buf: *int = alloc(int, 4);
    *buf = 10;
    *(buf + 1) = 20;   // +4 bytes (one int)
    *(buf + 2) = 30;   // +8 bytes
    *(buf + 3) = 40;   // +12 bytes
    for (int i = 0; i < 4; i += 1) {
        printf("%d\n", *(buf + i));
    }
    free(buf);
    return 0;
}
```

`*void` and `*char` are exceptions: they always step one byte at a time.

---

## What's Next

- Full language reference: `docs/lang/spec.md`
- Standard library source: `stdlib/`
- Worked examples: `examples/`
- Contributing guide: `docs/dev/`
