# Eskiu Language Specification

**Version:** v0.2.0 (in development)

---

## 1. Overview

Eskiu is a systems programming language built to address the fragmentation of compute-intensive services. Services in this space commonly reach for C when performance matters, Go for concurrency, C++ for libraries, and Python for glue — each with its own toolchain and interop cost.

Eskiu's goal is to replace that stack with a single language. Phase one establishes a solid systems foundation: native performance, explicit memory, direct C interop. Phase two, once the foundation is stable, will introduce first-class support for the domain types that high-throughput services actually work with, without giving up general systems capability.

Core properties:

- Statically typed with explicit type annotations
- Manual memory management — no garbage collector
- Compiles to native object files via LLVM (arm64 and x86-64)
- Structs with methods, monomorphic templates, and structural interfaces
- Lambdas and `fn(T)->R` function pointer types
- Multi-file programs via `import`
- Error locations reported as `file.esk:line:col: message`

The full compilation pipeline:

```sh
eskiuc file.esk -o file   # compile and link into an executable
./file                    # run
```

`eskiuc` links the program for you by invoking the system C toolchain (`$CC`,
then `cc`/`clang`/`gcc`) — the same approach `rustc` and `clang` use. To stop at
the object file instead, give the output a `.o` name or pass `-c`, then link
yourself:

```sh
eskiuc file.esk -o file.o   # compile to a native object file only
clang file.o -o file        # link it yourself
```

---

## 2. Lexical Elements

### 2.1 Comments

Single-line comments begin with `//` and extend to the end of the line. Block comments are enclosed in `/* ... */` and may span multiple lines. Comments do not nest.

```eskiu
// This is a single-line comment

/*
   This is a
   block comment.
*/
```

### 2.2 Identifiers

An identifier begins with a letter (`a`–`z`, `A`–`Z`) or underscore (`_`), followed by zero or more letters, digits (`0`–`9`), or underscores. Identifiers are case-sensitive: `point` and `Point` are distinct.

```eskiu
my_var    _internal    Count    x1
```

### 2.3 Keywords

The following identifiers are reserved and may not be used as variable or function names:

```
let  int  int8  int16  int32  int64
uint  uint8  uint16  uint32  uint64
float  double  bool  char  string  void
struct  packed  interface  fn  extern  intrinsic  import
if  else  for  while  in  switch  case  default
return  break  continue
true  false  null
alloc_with
const  volatile  escaping  asm
thread_create  thread_join
try  catch  finally  throw
async  await
sizeof  free_closure  union  enum
```

### 2.4 Literals

**Integer decimal literals** are sequences of decimal digits:

```eskiu
0    42    1000
```

**Integer hex literals** are prefixed with `0x` followed by hexadecimal digits (`0`–`9`, `a`–`f`, `A`–`F`):

```eskiu
0xFF    0x0F    0xDEAD    0xBEEF
```

**Negative numeric literals** are written with a leading `-`:

```eskiu
-1    -42    -100
-3.14    -0.5
```

Negative literals are first-class values and can be used in any expression context, including global variable initialisers and struct field initialisers.

**Float literals** contain a decimal point:

```eskiu
3.14    2.0    0.5
```

**String literals** are sequences of characters enclosed in double quotes. The following escape sequences are recognized:

| Escape | Meaning        |
|--------|----------------|
| `\n`   | Newline        |
| `\t`   | Horizontal tab |
| `\\`   | Backslash      |
| `\"`   | Double quote   |

```eskiu
"Hello, world!\n"
"path\\to\\file"
"column\theader"
```

**Character literals** are a single character or escape sequence enclosed in single quotes:

```eskiu
'a'    '\n'    '\\'
```

**Boolean literals** are the keywords `true` and `false`.

**Null literal** is the keyword `null`, used for null pointer values.

---

## 3. Types

### 3.1 Primitive Types

| Type     | LLVM IR  | Width   | Notes                              |
|----------|----------|---------|------------------------------------|
| `int`    | `i32`    | 32 bits | Signed; alias for `int32`          |
| `int8`   | `i8`     | 8 bits  | Signed                             |
| `int16`  | `i16`    | 16 bits | Signed                             |
| `int32`  | `i32`    | 32 bits | Signed                             |
| `int64`  | `i64`    | 64 bits | Signed                             |
| `uint`   | `i32`    | 32 bits | Unsigned; alias for `uint32`       |
| `uint8`  | `i8`     | 8 bits  | Unsigned                           |
| `uint16` | `i16`    | 16 bits | Unsigned                           |
| `uint32` | `i32`    | 32 bits | Unsigned                           |
| `uint64` | `i64`    | 64 bits | Unsigned                           |
| `float`  | `float`  | 32 bits | IEEE 754 single-precision          |
| `double` | `double` | 64 bits | IEEE 754 double-precision          |
| `bool`   | `i1`     | 1 bit   | `true` or `false`                  |
| `char`   | `i8`     | 8 bits  | Single byte                        |
| `string` | `i8*`    | pointer | Immutable C-string literal         |
| `void`   | `void`   | —       | No value; valid only as return type|

Signedness is tracked by the compiler for correct arithmetic and comparison codegen. Signed and unsigned variants of the same width share the same LLVM integer type (e.g., `int8` and `uint8` are both `i8`).

### 3.2 Pointer Types

A pointer type is written with a leading `*`:

```eskiu
*int       // pointer to int
*uint8     // pointer to uint8
**char     // pointer to pointer to char
```

Both leading-star (`*T`) and trailing-star (`T*`) syntax are accepted. The canonical form in this document is `*T`.

```eskiu
let ptr: *int = null;
let buf: *uint8 = null;
```

### 3.3 Array Types

Fixed-size arrays use the form `T[N]` where `N` is a compile-time integer constant — a decimal literal, an `enum` member, or a `const int` (see §4.6). Array types are supported both as struct fields and as local variables:

```eskiu
struct QRBuffer {
    uint8[858] left;
    uint8[858] right;
    int length;
}

int main() {
    int[16] scratch;   // local fixed-size array
    return 0;
}
```

`uint8[858]` lowers to `[858 x i8]` in LLVM IR.

### 3.4 Struct Types

A struct type is named by its declaration (see §8). Variables of struct type are declared using the struct name as the type annotation:

```eskiu
let p: Point;
let r: Rect;
```

### 3.5 Template Types

Template structs and functions are parameterized by one or more type variables. Instantiation is lazy and monomorphic — the compiler generates one concrete definition per unique set of type arguments.

```eskiu
let r: Result<int, string>;
let items: List<float>;
```

`Result<int, string>` lowers to `%Result_int_string` in LLVM IR.

### 3.6 Interface Types

Interface types are structural: any struct that provides all required methods satisfies the interface without an explicit declaration. When a struct is passed as an interface, the compiler auto-boxes the value into a fat pointer `{data_ptr, vtable_ptr}`.

```eskiu
interface Drawable { void draw(); }
// any struct with a draw() method satisfies Drawable
```

### 3.7 Function Pointer Types

A function pointer type is written using the `fn` keyword:

```eskiu
fn(int)->int          // function taking one int, returning int
fn(int, int)->bool    // function taking two ints, returning bool
fn()->void            // function taking no arguments, returning void
```

Function pointer types can be used anywhere a type annotation is expected: variable declarations, struct fields, and function parameters.

```eskiu
let callback: fn(int)->int = int(int x) { return x * 2; };
int apply(fn(int)->int f, int x) { return f(x); }
```

### 3.8 Type Casting

An explicit cast is written as `(TYPE)expr`. The expression is converted to the named type at the point of the cast.

```eskiu
double x = 3.14;
int n = (int)x;        // truncates to 3
uint8 b = (uint8)n;
float f = (float)n;
```

No implicit narrowing or widening conversions are performed. An explicit cast is required when the source and destination types differ.

---

## 4. Variables

### 4.1 let-style Declaration

```eskiu
let x: int = 5;
let name: string = "Eskiu";
let ptr: *int = null;
let pt: Point;
```

### 4.2 C-style Declaration

```eskiu
int x = 5;
string name = "Eskiu";
*int ptr = null;
```

Both forms are equivalent. The type annotation is required in both; type inference is not supported.

### 4.3 Pointer Variables

```eskiu
let p: *int = null;
if (p != null) {
    int val = *p;
}
```

### 4.4 Struct Variables

```eskiu
let pt: Point;
pt.x = 1.5;
pt.y = 2.5;
```

### 4.5 Volatile Variables

The `volatile` qualifier prevents the compiler (via LLVM) from optimising away loads and stores to a variable. It is required for memory-mapped I/O (MMIO) registers whose value may change or have side effects outside the program's control.

```eskiu
volatile let uart: *uint8 = (uint8*) 0x3F8;
*uart = 'A';   // store is always emitted — not eliminated by optimiser
```

`volatile` applies to all LLVM loads and stores that touch the declared pointer. It has no effect on variables that are never accessed through a pointer, but the canonical use is MMIO pointer variables as shown above.

### 4.6 Constants (`const`)

The `const` qualifier declares an immutable, typed binding. It prefixes either declaration form, must be initialised, and may not be reassigned:

```eskiu
const int MAX = 100;
const let step: int = 5;

MAX = 200;   // error: cannot assign to constant 'MAX'
```

A `const` integer can also be used as a **fixed-size array dimension**, in struct fields and in local variables:

```eskiu
const int CAP = 4;

struct Ring { int[CAP] slots; }   // CAP resolves at compile time

int main() {
    int[CAP] xs;                  // local array sized by a constant
    xs[0] = 1;
    return sizeof(Ring);          // 16
}
```

Array dimensions accept a decimal literal, an `enum` member, or a `const int`. `const` bindings are block-scoped like any other variable.

**`const` works on any type** (string, struct, pointer, scalar). Immutability covers both rebinding the variable and mutating a field or element of a `const` value:

```eskiu
const string name = "Eskiu";   // any type may be const
const let p: Point = Point { x: 1.0, y: 2.0 };
p.x = 5.0;                      // error: cannot assign to constant 'p'
```

The one case it does **not** cover is writing *through* a `const` pointer: `const` makes the pointer binding non-reassignable, but the pointee is still writable.

```eskiu
const let q: *int = &v;
q = &w;     // error: cannot assign to constant 'q'  (rebinding the pointer)
*q = 10;    // allowed — writes the pointee, not the binding
```

Eskiu does not distinguish a pointer-to-const from a const-pointer (there is no `const int*` vs `int* const`); `const` always qualifies the binding.

---

## 5. Operators

### 5.1 Arithmetic

| Operator | Description        |
|----------|--------------------|
| `a + b`  | Addition           |
| `a - b`  | Subtraction        |
| `a * b`  | Multiplication     |
| `a / b`  | Division           |
| `a % b`  | Modulo (remainder) |

Integer division truncates toward zero. Float operands use IEEE 754 semantics.

### 5.2 Bitwise

| Operator | Description  |
|----------|--------------|
| `a & b`  | Bitwise AND  |
| `a \| b` | Bitwise OR   |
| `a ^ b`  | Bitwise XOR  |
| `~a`     | Bitwise NOT  |
| `a << b` | Left shift   |
| `a >> b` | Right shift  |

Right shift on signed types is arithmetic (sign-extended).

### 5.3 Comparison

| Operator | Description           |
|----------|-----------------------|
| `a == b` | Equal                 |
| `a != b` | Not equal             |
| `a < b`  | Less than             |
| `a > b`  | Greater than          |
| `a <= b` | Less than or equal    |
| `a >= b` | Greater than or equal |

Comparison works on integer types, floating-point types, and pointer types. The result is always `bool`.

### 5.4 Logical

| Operator   | Description  |
|------------|--------------|
| `a && b`   | Logical AND  |
| `a \|\| b` | Logical OR   |
| `!a`       | Logical NOT  |

Short-circuit evaluation applies: in `a && b`, `b` is not evaluated if `a` is false; in `a || b`, `b` is not evaluated if `a` is true.

### 5.5 Assignment

| Operator  | Description                  |
|-----------|------------------------------|
| `x = e`   | Assign                       |
| `x += e`  | Add and assign               |
| `x -= e`  | Subtract and assign          |
| `x *= e`  | Multiply and assign          |
| `x /= e`  | Divide and assign            |
| `x %= e`  | Modulo and assign            |

The left-hand side must be an lvalue: a named variable, a pointer dereference (`*ptr = value`), or a field access. Assigning through a dereferenced pointer parameter works correctly — `*ptr = value` stores through the pointer as expected.

### 5.6 Address-of and Dereference

| Operator | Description                                  |
|----------|----------------------------------------------|
| `&x`     | Address of `x`; yields `*T` where `x: T`    |
| `*ptr`   | Dereference `ptr`; yields `T` where `ptr: *T`|

`&x` returns the `alloca` pointer for the stack variable `x`.

```eskiu
int val = 42;
*int ptr = &val;
*ptr = 100;       // val is now 100
```

### 5.7 Pointer Arithmetic

Adding or subtracting an integer `n` from a pointer of type `*T` advances the pointer by `n * sizeof(T)` bytes (typed GEP). This matches C semantics: `p + 1` moves to the next element, not the next byte.

```eskiu
*int  pi = alloc<int>(8);
*int  p2 = pi + 1;   // 4 bytes forward — points to element 1
```

The exceptions are `*void` and `*char`, which always use byte-level stride (1 byte per step) to preserve C interop semantics:

```eskiu
*uint8 buf = alloc<uint8>(1024);
*uint8 mid = buf + 512;    // 512 bytes into the buffer
*uint8 back = mid - 512;   // back to start
```

### 5.8 sizeof Expression

`sizeof(T)` is a compile-time constant expression that evaluates to the size of type `T` in bytes as an `int64`. It works for all Eskiu types, including structs and unions.

```eskiu
sizeof(int)    // 4
sizeof(int64)  // 8
sizeof(float)  // 4
sizeof(double) // 8
sizeof(Grid)   // 12  (3 float fields)
```

`sizeof` is resolved entirely at compile time and produces no runtime code.

### 5.9 Operator Precedence

Listed from lowest precedence (loosest binding) to highest (tightest binding):

| Level | Operators                                    | Associativity |
|-------|----------------------------------------------|---------------|
| 1     | `=` `+=` `-=` `*=` `/=` `%=`                | Right to left |
| 2     | `\|\|`                                       | Left to right |
| 3     | `&&`                                         | Left to right |
| 4     | `\|` (bitwise)                               | Left to right |
| 5     | `^`                                          | Left to right |
| 6     | `&` (bitwise)                                | Left to right |
| 7     | `==` `!=`                                    | Left to right |
| 8     | `<` `>` `<=` `>=`                            | Left to right |
| 9     | `<<` `>>`                                    | Left to right |
| 10    | `+` `-`                                      | Left to right |
| 11    | `*` `/` `%`                                  | Left to right |
| 12    | Unary `!` `-` `+` `~` `&` `*` `(TYPE)`      | Right to left |
| 13    | `()` `[]` `.` `?` (postfix)                  | Left to right |

Use parentheses to override precedence explicitly.

---

## 6. Functions

### 6.1 Regular Functions

```eskiu
int add(int a, int b) {
    return a + b;
}

int get_magic() {
    return 42;
}
```

Parameters are passed by value. The return type is declared before the function name.

**Declaration order is irrelevant.** A function may call any other function regardless of where it appears in the file, so call-before-definition and mutual recursion both work without ceremony. A body-less *forward declaration* is also permitted (and optional):

```eskiu
int is_odd(int n);                              // forward declaration

int is_even(int n) {
    if (n == 0) { return 1; }
    return is_odd(n - 1);                       // defined below — fine
}

int is_odd(int n) {
    if (n == 0) { return 0; }
    return is_even(n - 1);
}
```

### 6.2 Void Functions

```eskiu
void log_event(string msg) {
    printf("%s\n", msg);
}
```

A `void` function may use `return;` with no operand or allow control to fall off the end of the body.

### 6.3 Variadic Functions (extern only)

The ellipsis `...` marks a variadic parameter list in `extern` declarations:

```eskiu
extern int printf(string fmt, ...);
extern int sprintf(string buf, string fmt, ...);
```

User-defined variadic functions are not supported. `...` is only valid in `extern` declarations.

### 6.4 Extern Declarations

`extern` declares a C function available to Eskiu code. See §13 for details.

### 6.5 Lambdas and Closures

An anonymous function (lambda) is written with a C-style function body and no name. The syntax is identical to a named function declaration without the name:

```eskiu
int(int x) { return x * 2; }
```

The type of a lambda is the corresponding function pointer type `fn(T,...)->R`. Lambdas are assigned to variables, passed as arguments, or used anywhere a function pointer value is expected.

```eskiu
// Assign to a variable
let double_it: fn(int)->int = int(int x) { return x * 2; };
int result = double_it(5);   // result == 10

// Pass as an argument
int apply(fn(int)->int f, int x) {
    return f(x);
}
int out = apply(double_it, 4);   // out == 8

// Inline (pass directly)
int out2 = apply(int(int x) { return x + 1; }, 9);  // out2 == 10
```

**Closure capture.** A lambda may reference variables from its enclosing scope. Such variables are captured by value at the point the lambda expression is evaluated.

```eskiu
int base = 10;
let add: fn(int)->int = int(int x) { return x + base; };
add(5);   // 15 — 'base' was captured by value
```

Under the hood, `fn(T)->R` is a two-word fat pointer `{fn_ptr, env_ptr}`. When a lambda captures one or more variables, the compiler packages them into an environment struct and stores its address in `env_ptr`. Lambdas that capture nothing have `env_ptr = null` and compile identically to plain function pointers. The representation is fully transparent to user code — the type annotation remains `fn(T)->R` in both cases.

**Escape analysis and closure lifetime.** Where the environment lives depends on whether the closure *escapes* its creating function:

- A **non-escaping** closure — one that is only called, or passed to a parameter that is not marked `escaping` — has its environment allocated on the **stack**. This costs nothing and needs no cleanup (the common `map`/`filter`/callback-invoked-in-place case).
- An **escaping** closure — one that is returned, stored into a struct field / global / through a pointer, or passed to an `escaping` parameter — has its environment allocated on the **heap**, so it remains valid after the creating function returns. Release it with `free_closure(f)` (a no-op for non-capturing closures, whose env is null).

A parameter that retains the closure beyond the call (stores it, returns it, hands it to another `escaping` parameter) must be declared `escaping`:

```eskiu
// stores cb beyond the call -> the parameter is `escaping`
void on_ready(int fd, escaping fn(int)->void cb) { handlers[fd] = cb; }

// only calls f -> no annotation; closures passed here stay on the stack
int apply(fn(int)->int f, int x) { return f(x); }
```

This is checked: using a non-`escaping` closure parameter beyond a direct call is a compile error pointing you at `escaping`, so a closure can never silently outlive its stack environment. `escaping` and `free_closure` are reserved words (§2.3).

**Named functions as values.** A top-level function used as a value (rather than called) decays to a `fn(T,...)->R`, so it can be assigned or passed directly — no lambda wrapper needed:

```eskiu
void worker() { /* ... */ }
int  inc(int x) { return x + 1; }

*void t = thread_create(worker);   // pass the function itself
int r   = apply(inc, 41);          // r == 42
```

The compiler synthesizes a tiny adapter so the function fits the `{fn_ptr, env_ptr}` calling convention (the function does not take an environment); this is transparent to your code.

### 6.6 Template Functions

Template functions are parameterized by one or more type variables declared in angle brackets after the function name:

```eskiu
T identity<T>(T x) {
    return x;
}

T max<T>(T a, T b) {
    if (a > b) return a;
    return b;
}
```

Called with explicit type arguments:

```eskiu
int result = max<int>(3, 5);
float fmax = max<float>(1.5, 2.5);
int same = identity<int>(42);
```

The type arguments may also be **inferred** from the argument types. Inference works both when a type parameter appears directly as a parameter type and when it appears inside a composite parameter type, so type arguments can be omitted in either case:

```eskiu
int r = max(3, 5);            // T inferred as int (direct parameter)
int i = identity(42);         // T inferred as int

let nums: List<int>;
List_init(&nums, 4);          // T inferred as int from the List<int>* argument
List_push(&nums, 7);
int first = List_get(&nums, 0);
```

Inference unifies each parameter type against the concrete argument type
structurally (peeling pointers and matching template instances), so a `List<T>*`
parameter binds `T` from a `List<int>*` argument. If a type parameter cannot be
inferred from any argument, pass the type arguments explicitly. Each unique set
of type arguments generates a separate monomorphic instantiation.

### 6.7 Thread Primitives

`thread_create` and `thread_join` are language keywords that spawn and await OS threads.

```eskiu
thread_create(fn()->void worker) -> *void
thread_join(*void handle) -> void
```

`thread_create` accepts any `fn()->void` value — including a closure — and returns an opaque `*void` thread handle. `thread_join` blocks the calling thread until the spawned thread completes.

```eskiu
extern int printf(string fmt, ...);

int main() {
    *void t = thread_create(void() { printf("hello from thread\n"); });
    thread_join(t);
    return 0;
}
```

**With a capturing closure:**

```eskiu
int id = 1;
let worker: fn()->void = void() { printf("thread %d\n", id); };
*void t = thread_create(worker);
thread_join(t);
```

**Implementation detail.** The closure fat pointer `{fn_ptr, env_ptr}` maps directly to the `(start_routine, arg)` pair expected by `pthread_create` — no trampoline function is generated. On Linux, link the final binary with `-lpthread`.

### 6.8 Exception Handling

Eskiu supports structured exception handling via `try`, `catch`, `finally`, and `throw`.

#### Syntax

```eskiu
try {
    // body — any function calls here are emitted as LLVM invoke
} catch (TYPE name) {
    // handler — receives the thrown value as 'name'
} finally {
    // cleanup — always executes
}
```

Multiple `catch` clauses may be chained. The `finally` clause is optional. Either `catch` or `finally` (or both) must follow `try`.

#### throw

`throw expr` throws the value of `expr` as an exception. Any Eskiu value type may be thrown — `string`, `int`, a pointer, etc.

```eskiu
int divide(int a, int b) {
    if (b == 0) {
        throw "division by zero";
    }
    return a / b;
}
```

#### Catching exceptions

Each `catch` clause names a type and a variable. If the thrown value matches the declared type, control transfers to that clause and the variable holds the thrown value.

```eskiu
try {
    int r = divide(10, 0);
} catch (string e) {
    printf("caught: %s\n", e);
}
```

#### finally

The `finally` block executes unconditionally after the `try` body and any `catch` clause, regardless of whether an exception was raised.

```eskiu
try {
    throw "error";
} catch (string e) {
    printf("caught: %s\n", e);
} finally {
    printf("cleanup\n");
}
```

#### Unhandled exceptions

If no `catch` clause matches the thrown value, the exception propagates up the call stack. If it reaches the top with no handler, the program terminates.

#### Linking

Exceptions use the platform C++ runtime, so link the final program with `-lc++` on macOS or `-lstdc++` on Linux (library flags go straight through to the linker):

```bash
eskiuc file.esk -o file -lc++      # macOS
eskiuc file.esk -o file -lstdc++   # Linux
```

---

## 7. Control Flow

### 7.1 if / else if / else

```eskiu
if (x > 0) {
    printf("positive\n");
} else if (x < 0) {
    printf("negative\n");
} else {
    printf("zero\n");
}
```

The condition must evaluate to a `bool` or integer (non-zero is true). Braces are required around each branch body.

### 7.2 for

C-style three-part form:

```eskiu
for (int i = 0; i < 10; i += 1) {
    printf("%d\n", i);
}
```

The init clause may declare a variable scoped to the loop. Each part is optional:

```eskiu
for ( ; running; ) {
    // condition only
}
```

### 7.2.1 for ... in (for-each)

The `for (x in iterable)` form binds `x` to each element of `iterable` in turn.
The loop variable is a fresh copy each iteration; assigning to it does not modify
the underlying collection. `break` and `continue` work as in any loop.

Two kinds of iterable are supported:

- **Fixed-size arrays** (`T[N]`), including array fields:

  ```eskiu
  int[4] xs;
  xs[0] = 10; xs[1] = 20; xs[2] = 30; xs[3] = 40;
  for (v in xs) {
      printf("%d\n", v);
  }
  ```

- **List-like structs** — any struct with an `int size` field and a `data`
  pointer field, which includes `List<T>` from the standard library:

  ```eskiu
  import <list>;
  let nums: List<int>;
  List_init(&nums, 4);
  List_push(&nums, 1);
  List_push(&nums, 2);
  for (n in nums) {
      printf("%d\n", n);
  }
  ```

The form desugars to an index-counted loop: for an array the bound is its
compile-time length; for a List-like value the bound is its `size` field and
each element is read through `data[i]`.

### 7.3 while

```eskiu
while (condition) {
    // body
}
```

The body executes repeatedly while `condition` is true.

### 7.4 switch / case / default / break

```eskiu
switch (x) {
    case 1:
        printf("one\n");
        break;
    case 2:
        printf("two\n");
        break;
    default:
        printf("other\n");
        break;
}
```

`switch` dispatches on an integer value. `break` exits the enclosing switch. If `break` is omitted, control falls through to the next case. The type checker validates that each `case` value is compatible with the type of the `switch` subject expression.

### 7.5 return

Returns a value from the current function. A `void` function uses `return;` with no operand.

```eskiu
int sign(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}
```

### 7.6 break

Exits the innermost enclosing `for`, `while`, or `switch` immediately.

```eskiu
while (true) {
    if (done) break;
}
```

### 7.7 continue

Skips the remainder of the current loop iteration and proceeds to the next:

```eskiu
for (int i = 0; i < 10; i += 1) {
    if (i % 2 == 0) continue;
    printf("%d\n", i);   // prints odd numbers only
}
```

---

## 8. Structs

### 8.1 Declaration with Fields

```eskiu
struct Point {
    float x;
    float y;
}

struct Rect {
    float x;
    float y;
    float width;
    float height;
}
```

Field types may be any primitive type, pointer type, another struct type, or a fixed-size array type.

An integer field may declare a **bit width** with `: N`, making it a bitfield. Consecutive bitfields pack into storage words of their declared type; reads mask and shift out the field (signed fields sign-extend), and writes are read-modify-write. You cannot take the address of a bitfield.

```eskiu
struct Flags {
    uint32 ready  : 1;
    uint32 mode   : 3;
    uint32 weight : 4;
}

let f: Flags = Flags { ready: 1, mode: 5, weight: 9 };
f.mode = 2;   // leaves ready and weight untouched
```

### 8.2 Methods with Implicit self

Methods are declared inside the struct body. Inside a method body, `self` refers to a pointer to the receiver struct.

```eskiu
struct Counter {
    int count;

    void increment() {
        self.count += 1;
    }

    int get() {
        return self.count;
    }
}
```

Methods are lowered to regular functions with a leading pointer parameter, e.g., `Counter_increment(*Counter self)`.

### 8.3 Struct Initialization

**Named initialization:**

```eskiu
Point p = Point { x: 1.5, y: 2.5 };
```

**Positional initialization:**

```eskiu
Point p = Point { 1.5, 2.5 };
```

Fields are assigned in declaration order in the positional form. All fields must be provided.

### 8.4 Field Access and Mutation

```eskiu
let p: Point;
p.x = 1.0;
p.y = 2.0;
float sum = p.x + p.y;
```

Field assignment and load both use the `.` operator. Method calls use the same syntax:

```eskiu
counter.increment();
int val = counter.get();
```

### 8.5 Fixed-size Array Fields

```eskiu
struct QRFrame {
    uint8[858] left;
    uint8[858] right;
    int size;
}
```

`uint8[858]` lowers to `[858 x i8]` in LLVM IR.

### 8.6 Union Types

A `union` declaration is identical in syntax to `struct`, but all fields share offset 0. The size of the union equals the size of its largest field. Accessing a field reinterprets the underlying bytes as the field's type — no explicit cast is needed.

```eskiu
union Value {
    int    i;
    float  f;
    *uint8 p;
}

let v: Value;
v.i = 42;
printf("%f\n", v.f);  // reinterprets the 4 bytes of v.i as a float
```

A `union` variable is declared exactly like a struct variable:

```eskiu
let u: Value;
u.i = 0x3F800000;   // bit pattern for 1.0f
printf("%f\n", u.f); // prints 1.0
```

`sizeof(Value)` returns the size of the largest field — `sizeof(*uint8)` = 8 on a 64-bit target in this example.

### 8.7 Enums

An `enum` declares a set of named integer constants. Members take consecutive values starting at 0; an explicit `= N` resets the running value, and the next member continues from there. The enum type itself is an `int` (`i32`), so enum values work in arithmetic, comparisons, and `switch`.

```eskiu
enum Color  { Red, Green, Blue }            // 0, 1, 2
enum Status { Ok = 0, Err = 2, Pending }    // 0, 2, 3

let c: Color = Green;            // c == 1
if (c == Red) { /* ... */ }
```

Members are unscoped — `Red` is used directly, as in C. The enum name may be used anywhere a type is expected (it behaves as `int`).

### 8.8 Type Aliases

`type Name = ExistingType;` introduces a name for an existing type. The alias is fully interchangeable with its underlying type — it resolves before type checking and code generation. Aliases work for any type, including pointers and templates.

```eskiu
type u8      = uint8;
type Bytes   = *uint8;
type IntList = List<int>;

let buf: Bytes = alloc<u8>(16);
```

`type` is contextual — it is only a keyword in the form `type Name = ...;`, so it remains usable as an ordinary identifier elsewhere.

### 8.9 Packed Structs

By default a struct is laid out with natural alignment: the compiler inserts padding so each field sits on its required boundary. A **packed** struct removes that padding — fields are placed back-to-back. This matters when a struct must match an exact on-the-wire or on-disk byte layout, or a C struct declared with `#pragma pack` / `__attribute__((packed))`.

Mark a struct packed with the `packed` qualifier:

```eskiu
struct Natural {        // sizeof == 8 (1 byte tag + 3 padding + 4 byte value)
    uint8  tag;
    uint32 value;
}

packed struct Header {  // sizeof == 5 (no padding; value starts at offset 1)
    uint8  tag;
    uint32 value;
}
```

For C source compatibility, `#pragma pack` is also honoured. It maintains an alignment stack and applies to every struct declared while in effect; `pack(1)` packs, `pack()` / `pop` restore:

```eskiu
#pragma pack(push, 1)
struct WireHeader {     // packed (sizeof == 5)
    uint8  kind;
    uint32 length;
}
#pragma pack(pop)       // subsequent structs use natural alignment again
```

`#pragma pack(1)` and `packed struct` are equivalent and set the same flag. Only `pack(1)` changes layout; other alignment values are accepted but not honoured (use natural alignment or `packed`). Packed layout composes with bitfields and is reflected by `sizeof` and by every field access.

---

## 9. Interfaces

### 9.1 Declaration

An interface declares a set of method signatures. No implementation is provided.

```eskiu
interface Drawable {
    void draw();
}

interface Greeter {
    void greet();
    string name();
}
```

### 9.2 Structural Satisfaction

There is no `implements` keyword. A struct satisfies an interface if it provides all required methods with matching signatures. The check is structural and performed at the call site.

```eskiu
struct Circle {
    float radius;

    void draw() {
        printf("Drawing circle r=%.2f\n", self.radius);
    }
}

// Circle satisfies Drawable because it has draw()
```

### 9.3 Calling Interface Methods

Interface methods are called with the `.` operator:

```eskiu
void render(Drawable d) {
    d.draw();
}
```

Dispatch is performed via the vtable pointer in the fat pointer.

### 9.4 Passing Structs as Interfaces

Passing a struct pointer to a function expecting an interface auto-boxes it into a fat pointer `{data_ptr, vtable_ptr}`:

```eskiu
Circle c;
c.radius = 5.0;
render(&c);   // &c is auto-boxed into a Drawable fat pointer
```

### 9.5 Implementation Detail: Fat Pointer

Under the hood, an interface value is a two-word fat pointer:

```
{ i8* data_ptr, i8* vtable_ptr }
```

The vtable is a struct of function pointers, one per interface method, generated per concrete type. This is transparent to user code.

---

## 10. Templates

### 10.1 Template Structs

Template structs are declared with one or more type parameters in angle brackets:

```eskiu
struct Pair<A, B> {
    A first;
    B second;
}

struct Box<T> {
    *T value;
    int valid;
}
```

### 10.2 Template Functions

```eskiu
T identity<T>(T x) {
    return x;
}

T max<T>(T a, T b) {
    if (a > b) return a;
    return b;
}
```

### 10.3 Instantiation

Template instantiation is lazy and monomorphic. The compiler generates one concrete struct or function definition per unique set of type arguments. The generated name uses underscores: `Result<int, string>` becomes `%Result_int_string` in LLVM IR.

```eskiu
let p: Pair<int, float>;
p.first = 1;
p.second = 3.14;

int big = max<int>(10, 20);
```

### 10.4 Using Result<T,E> from stdlib

```eskiu
import "stdlib/result.esk";

int main() {
    let r: Result<int, string> = Ok<int, string>(42);
    if (r.ok) {
        printf("value: %d\n", r.value);
    } else {
        printf("error: %s\n", r.error);
    }
    return 0;
}
```

### 10.5 The `?` error-propagation operator

The postfix `?` operator removes the boilerplate of checking a `Result` after
every fallible call. Applied to a `Result<T, E>` value, `expr?`:

- if the result is `Err`, **returns it unchanged** from the enclosing function;
- otherwise evaluates to the unwrapped success value of type `T`.

It may only appear inside a function whose return type is the *same* `Result<T, E>`
type — the compiler rejects `?` anywhere else, since there would be nothing to
propagate into.

```eskiu
import <result>;

Result<int, string> divide(int a, int b) {
    if (b == 0) { return Err<int, string>("division by zero"); }
    return Ok<int, string>(a / b);
}

// Without `?`, each call would need its own `if (!r.ok) return r;`.
Result<int, string> compute(int a, int b, int c) {
    let x: int = divide(a, b)?;   // unwraps, or returns the Err
    let y: int = divide(x, c)?;
    return Ok<int, string>(y + 1);
}
```

A value is treated as Result-like if it has an `int ok` field and a `value`
field; the standard library's `Result<T, E>` satisfies this.

---

## 11. Memory

### 11.1 Stack Allocation

All variables declared in a function body are allocated on the stack. Stack memory is reclaimed automatically when the enclosing function returns. There is no garbage collector.

```eskiu
int main() {
    int x = 10;
    Point p;
    p.x = 1.0;
    return 0;
}   // x and p are reclaimed here
```

### 11.2 Heap Allocation

Heap allocation lives in the standard library, not the language core: `import <mem>` brings in `alloc<T>` and `free`.

`alloc<T>(N)` allocates space for `N` elements of type `T` and returns a `*T`. In hosted mode (the default) it calls `malloc(N * sizeof(T))`. Under `--freestanding` (see §11.5) it calls the user-provided `esk_alloc` instead — the same source, selected at compile time via the `__ESKIU_FREESTANDING__` macro.

`free(ptr)` releases a heap-allocated pointer (libc `free` hosted, `esk_free` freestanding). It takes a `*void`; any pointer type coerces.

```eskiu
import <mem>;

*uint8 buf = alloc<uint8>(1024);
// ... use buf ...
free(buf);
```

`alloc<T>`/`free` are ordinary generic stdlib functions — there is no `alloc` keyword. (`alloc_with`, the explicit-allocator primitive, *is* a built-in; see §11.5.) Every allocation must be paired with exactly one `free`. Double-free and use-after-free are undefined behaviour.

### 11.3 Pointer Arithmetic

Pointer arithmetic is typed: `p + n` on a `*T` pointer advances by `n * sizeof(T)` bytes. `*void` and `*char` are byte-level (stride 1).

```eskiu
*uint8 buf = alloc<uint8>(256);
*uint8 ptr = buf + 64;    // 64 bytes from start
*uint8 back = ptr - 32;   // 32 bytes back

*int pi = alloc<int>(8);
*int  p2 = pi + 1;        // 4 bytes forward — next int element
```

The subscript operator `ptr[i]` reads or writes the `i`-th element and is exactly equivalent to `*(ptr + i)` (typed by the pointee). It is the idiomatic way to index allocated buffers and array fields:

```eskiu
*int xs = alloc<int>(4);
xs[0] = 10;
xs[3] = xs[0] * 2;        // same as *(xs + 3) = *(xs + 0) * 2
```

### 11.4 Null Checks

```eskiu
let p: *int = null;
if (p != null) {
    int val = *p;
}
```

Dereferencing `null` is undefined behaviour. The compiler does not insert null checks.

### 11.5 Freestanding Mode

Passing `--freestanding` predefines the macro `__ESKIU_FREESTANDING__`, which `<mem>` uses to switch the allocation backend:

| `<mem>` function | Hosted (default) | Freestanding (`--freestanding`) |
|------------------|------------------|---------------------------------|
| `alloc<T>(n)`    | `malloc`         | `esk_alloc`                     |
| `free(p)`        | `free`           | `esk_free`                      |

In freestanding mode the user must provide `esk_alloc` and `esk_free` in their own code (typically in a kernel or bare-metal runtime); `<mem>` declares them `extern` and the linker resolves them from the user-supplied object file. Code that needs heap allocation still just writes `import <mem>` and calls `alloc<T>`/`free` — the same source compiles for both modes.

```eskiu
// user-provided in kernel.esk or a C shim
*void esk_alloc(int size) { return buddy_alloc(size); }
void  esk_free(*void ptr)  { buddy_free(ptr); }
```

Freestanding mode does not remove any other language features. The standard library modules (`stdlib/result.esk`, etc.) remain available but must not import libc functions that are absent from the target.

**Custom allocators (`alloc_with`).** `alloc_with(&allocator, T, n)` is the explicit-allocator form of `alloc`: instead of going to `malloc`/`esk_alloc`, it calls `<Type>_alloc(&allocator, n * sizeof(T))` and returns a `*T`. Any struct that exposes a method `*void <Type>_alloc(<Type>* self, int64 nbytes)` is a valid allocator — so allocation strategy is a plain value, not a global.

```eskiu
import <alloc>;

*uint8 backing = alloc<uint8>(4096);   // one slab from the host (or a static buffer in freestanding)
let a: Bump;  Bump_init(&a, backing, 4096);
*int xs = alloc_with(&a, int, 16);     // 16 ints carved from the slab — no per-object malloc
```

The `<alloc>` module ships four allocators, all built on caller-provided memory (so they work under `--freestanding` with no libc `malloc`):

| Allocator | Strategy | Reclaim |
|-----------|----------|---------|
| `Bump`    | monotonic offset into the buffer | `Bump_reset` frees everything at once; individual frees are no-ops |
| `Arena`   | bump with checkpoints | `Arena_save`/`Arena_restore` free back to a marker; `Arena_reset` frees all |
| `Pool`    | fixed-size blocks, free list threaded through freed blocks | `Pool_free` returns a block for reuse |
| `FirstFit`| general-purpose, first-fit search with region splitting (after Thompson's original) | `FirstFit_free` returns a region (adjacent-region coalescing is a planned refinement) |

### 11.6 MMIO and volatile

See §4.5 for the `volatile` qualifier. In freestanding/kernel contexts, hardware registers are accessed through `volatile` pointer variables:

```eskiu
volatile let uart_dr: *uint8 = (uint8*) 0x3F8;   // UART data register
volatile let uart_sr: *uint8 = (uint8*) 0x3FD;   // UART status register

void uart_putc(uint8 c) {
    while ((*uart_sr & 0x20) == 0) {}   // spin until TX ready
    *uart_dr = c;
}
```

Every load and store through a `volatile` pointer is emitted as a `volatile load` / `volatile store` in LLVM IR, preventing the optimiser from caching, reordering, or eliminating the access.

---

## 12. Multi-file Programs

A project can be split across files two ways: with `import` (below), or by passing several files to the compiler at once — `eskiuc a.esk b.esk -o prog` — which merges the declarations of all inputs into one program. Declaration order across files does not matter.

### 12.1 import Statement

The `import` statement has two forms:

```eskiu
import <result>;              // stdlib module — resolved by the compiler
import "stdlib/result.esk";   // local file — path relative to the importing file
import "../shared/types.esk";
```

`import <name>` resolves the module from the Eskiu installation's stdlib directory. The compiler locates the stdlib either via the `ESKIU_ROOT` environment variable (if set) or by auto-detecting it from the compiler binary's location. No path prefix is required.

`import "path"` resolves the path relative to the directory of the importing file, as before.

All declarations in the imported file (functions, structs, templates, externs) become available in the importing file.

### 12.2 Deduplication

Each file is parsed and processed at most once per compilation, regardless of how many files import it. Circular imports are detected and do not cause infinite loops.

### 12.3 Example

```eskiu
// main.esk
import "stdlib/io.esk";
import "stdlib/result.esk";

int main() {
    let r: Result<int, string> = Ok<int, string>(0);
    printf("ok: %d\n", r.ok);
    return 0;
}
```

---

## 13. extern / C Interop

### 13.1 Extern Function Declarations

An `extern` declaration makes a C function available to Eskiu code. The declaration must match the C function's ABI exactly.

```eskiu
extern int printf(string fmt, ...);
extern int strlen(string s);
extern *void memcpy(*void dst, *void src, int n);
extern int open(string path, int flags);
extern void exit(int code);
```

### 13.2 Calling Extern Functions

`extern` functions are called exactly like Eskiu functions:

```eskiu
extern int printf(string fmt, ...);

int main() {
    printf("Hello, %s!\n", "world");
    return 0;
}
```

### 13.3 C ABI Compatibility

`extern` declarations emit standard C-ABI-compatible LLVM IR `call` instructions. Any function exported from a C library — including system libraries, OpenSSL, zxing-cpp, or any other C-compatible library — may be called this way.

For functions that accept or return `void*`, use `*void` on the Eskiu side:

```eskiu
extern *void malloc(int size);
extern *void memcpy(*void dst, *void src, int n);
extern *void memset(*void ptr, int value, int n);
```

(For heap allocation, prefer `import <mem>` and `alloc<T>`/`free` over declaring `malloc`/`free` as `extern` yourself — see §11.2.)

---

## 14. Stdlib

Eskiu ships a set of standard library files in the `stdlib/` directory. Import them with relative paths from your source file.

| File                  | Contents                                                         |
|-----------------------|------------------------------------------------------------------|
| `stdlib/result.esk`   | `Result<T,E>` template struct; `Ok<T,E>(value)` and `Err<T,E>(err)` constructor functions |
| `stdlib/list.esk`     | `List<T>` template struct; `List_init`, `List_push`, `List_get`, `List_len`, `List_free` |
| `stdlib/string.esk`   | `String` struct; `String_init`, `String_from`, `String_append`, `String_concat`, `String_push`, `String_char_at`, `String_set`, `String_clear`, `String_index_of`, `String_eq`, `String_eq_cstr`, `String_reverse`, `String_substring`, `String_from_int`, `String_to_int`, `String_cstr`, `String_len`, `String_free`, `String_starts_with`, `String_ends_with`, `String_trim`, `String_next_token` (streaming split), `String_split`/`String_split_free` (into a `List<String>`) |
| `stdlib/math.esk`     | `extern` declarations for `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` |
| `stdlib/io.esk`       | `extern` declarations for `printf`, `fprintf`, `sprintf`, `scanf`, `puts` |
| `stdlib/mem.esk`      | Heap allocation `alloc<T>(n)` / `free(p)` (libc, or `esk_alloc`/`esk_free` under `--freestanding`); plus `extern` `memcpy`, `memset`, `memmove`, `memcmp`, `strlen` |
| `stdlib/fs.esk`       | File I/O: `fs_open`, `fs_close`, `fs_flush`, `fs_read`, `fs_readline`, `fs_write`, `fs_puts`, `fs_seek`, `fs_tell`, `fs_size`, `fs_read_all`, `fs_write_all`, `fs_eof`, `fs_error` |
| `stdlib/net.esk`      | TCP sockets: `net_tcp_listen`, `net_accept`, `net_tcp_connect`, `net_send`, `net_recv`, `net_send_str`, `net_close` (plus the raw POSIX `extern`s and a portable `sockaddr_in`) |
| `stdlib/alloc.esk`    | Allocators over caller-provided memory for `alloc_with` (see §11.5): `Bump`, `Arena`, `Pool`, `FirstFit` — each with `_init`/`_alloc` (and `_free`/`_reset`/`_save`/`_restore` as applicable) |
| `stdlib/time.esk`     | `time_now_ms`, `time_now_s`, `time_monotonic_ms`, `sleep_ms` |
| `stdlib/env.esk`      | `env_get`, `env_has`, `env_get_or`, `env_get_int` (process environment; CLI args come from `main`'s `argc`/`argv`) |
| `stdlib/base64.esk`   | `base64_encode` / `base64_decode` over byte buffers, plus `base64_encoded_len` / `base64_decoded_len` and the `base64_value` / `base64_digit` primitives |
| `stdlib/path.esk`     | Unix path manipulation: `path_join`, `path_basename`, `path_dirname`, `path_extension`, `path_is_absolute` |
| `stdlib/http.esk`     | HTTP/1.1: `HttpRequest` + `HttpRequest_parse`/`_header`, `HttpResponse` + `HttpResponse_header`/`_set_body`/`_render`, and a threaded worker pool `http_serve(port, nworkers, handler)` where `handler` is `fn(HttpRequest*, HttpResponse*)->void` |
| `stdlib/threading.esk`| Synchronization over pthread: `Mutex` (`_init`/`_lock`/`_unlock`/`_destroy`), `Cond` (`_init`/`_wait`/`_signal`/`_broadcast`/`_destroy`), `Sem` (`_init`/`_wait`/`_post`/`_destroy`). Pairs with the `thread_create`/`thread_join` built-ins |
| `stdlib/eventloop.esk`| Readiness reactor over kqueue (macOS) / epoll (Linux): `EventLoop`, `el_new`, `el_add_read`, `el_del`, `el_run`, `el_stop`, `el_free`. Callback is `fn(EventLoop*, int)->void` |
| `stdlib/atomic.esk`| Atomic intrinsics on an `int` cell: `atomic_load`/`atomic_store`/`atomic_swap`/`atomic_cas`, lowering to LLVM atomics with fixed acquire/release ordering. Declared with the `intrinsic` qualifier |
| `stdlib/json.esk`     | JSON builder + parser. Builder: `Json` + `Json_init`/`_free`/`_cstr`, `Json_obj_begin`/`_end`, `Json_arr_begin`/`_end`, `Json_key`, `Json_str`, `Json_int`, `Json_bool`, `Json_null` (auto separators). Parser: `json_parse(src) -> *JsonValue` + `JsonValue_kind`/`_len`/`_at`/`_get`/`_as_int`/`_as_double`/`_as_bool`/`_as_cstr`/`_free` |

### Result<T,E>

```eskiu
import "stdlib/result.esk";

Result<int, string> divide(int a, int b) {
    if (b == 0) return Err<int, string>("division by zero");
    return Ok<int, string>(a / b);
}

int main() {
    let r: Result<int, string> = divide(10, 2);
    if (r.ok) {
        printf("result: %d\n", r.value);
    } else {
        printf("error: %s\n", r.error);
    }
    return 0;
}
```

### List<T>

```eskiu
import "stdlib/list.esk";

int main() {
    let items: List<int>;
    List_init(&items);
    List_push(&items, 10);
    List_push(&items, 20);
    List_push(&items, 30);
    printf("len: %d\n", List_len(&items));
    printf("item[1]: %d\n", List_get(&items, 1));
    List_free(&items);
    return 0;
}
```

### String

```eskiu
import "stdlib/string.esk";

int main() {
    let s: String;
    String_from(&s, "Hello");
    String_append(&s, ", world!");
    printf("%s\n", String_cstr(&s));   // Hello, world!
    String_free(&s);
    return 0;
}
```

`String` is a growable, owned, NUL-terminated buffer. Beyond append/concat it
offers per-character access and mutation (`String_char_at`, `String_set`,
`String_push`), comparison (`String_eq`, `String_eq_cstr`), search
(`String_index_of`), `String_substring`, `String_reverse`, `String_clear`, and
integer conversion (`String_from_int`, `String_to_int`):

```eskiu
let n: String;
String_init(&n, 8);
String_from_int(&n, -2026);            // "-2026"
printf("%d\n", String_to_int(&n));     // -2026
String_free(&n);
```

### Networking (`<net>`)

`<net>` wraps the POSIX BSD socket API for TCP. The `net_*` helpers cover the
common path; the raw `extern`s (`socket`, `bind`, …) and a portable
`sockaddr_in` are also exported for anything more specialised. It is a pure
stdlib module — sockets need no compiler support beyond the C FFI.

```eskiu
import <net>;

extern int printf(string fmt, ...);

int main() {
    int fd = net_tcp_listen(8080);            // bind + listen on 0.0.0.0:8080
    if (fd < 0) { return 1; }
    printf("listening on :8080\n");

    *uint8 req = alloc<uint8>(4096);
    while (1) {
        int c = net_accept(fd);               // blocking accept
        if (c < 0) { continue; }
        net_recv(c, (*void)req, 4096);
        net_send_str(c,
            "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nHello from Eskiu.\n");
        net_close(c);
    }
    free(req);
    return 0;
}
```

| Helper | Effect |
|---|---|
| `net_tcp_listen(port) -> int` | Create a socket, set `SO_REUSEADDR`, bind to `0.0.0.0:port`, listen. Returns the fd or `-1` |
| `net_accept(fd) -> int` | Accept the next connection; returns the connection fd or `-1` |
| `net_tcp_connect(host, port) -> int` | Connect to a dotted-quad host (e.g. `"127.0.0.1"`); returns fd or `-1` |
| `net_send(fd, buf, n)` / `net_recv(fd, buf, n)` | Send / receive raw bytes (`int64` count) |
| `net_send_str(fd, s)` | Send a C string (length via `strlen`) |
| `net_close(fd)` | Close a socket |

`sockaddr_in` differs between macOS and Linux; `<net>` selects the right layout
at compile time using the predefined `__APPLE__` / `__linux__` macro (see §18).
A concurrent server combines `<net>` with `thread_create` — handing each
accepted connection to a worker (see §6.5 for passing a function as a value).
Complete programs are in `examples/http_server.esk` and
`examples/tcp_echo_server.esk`.

---

## 15. Inline Assembly

Eskiu supports inline assembly via the `asm(...)` statement, which lowers directly to an LLVM inline asm node.

### 15.1 Simple Form

```eskiu
asm("cli");         // disable interrupts (x86)
asm("sti");         // enable interrupts (x86)
asm("nop");
```

The string is passed verbatim to the assembler. No inputs, outputs, or clobbers are specified.

### 15.2 Extended Form

The extended form follows GCC-compatible inline assembly syntax:

```
asm("template" : outputs : inputs : clobbers);
```

```eskiu
asm("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");
```

- **Template** — the assembly instruction string; `%0`, `%1`, … reference operands by index.
- **Outputs** — list of `"constraint"(lvalue)` pairs; empty in the example above (omitted with `:`).
- **Inputs** — list of `"constraint"(expr)` pairs. Common constraints: `"a"` (eax/rax), `"Nd"` (8-bit immediate or dx), `"r"` (any register), `"m"` (memory).
- **Clobbers** — comma-separated list of clobbered resources. `"memory"` tells the compiler that the asm may read or write arbitrary memory (acts as a compiler barrier).

All four sections are separated by `:`. Trailing sections may be omitted if empty.

### 15.3 Notes

- Inline assembly is only meaningful when targeting a platform whose assembler understands the instructions. Use `--target` to select the appropriate triple (see §16).
- `asm` is a statement, not an expression. It does not produce a value.
- The compiler performs no validation of the assembly template or constraints beyond forwarding them to LLVM.

---

## 16. CLI Flags

| Flag | Action |
|------|--------|
| `eskiuc --version` | Print the compiler and LLVM version |
| `eskiuc file.esk -o prog` | Compile **and link** into the executable `prog` |
| `eskiuc a.esk b.esk -o prog` | Compile several files together (declarations are merged) |
| `eskiuc file.esk -Wall -o prog` | Enable lint warnings: unused vars/params/functions, assignment-in-condition |
| `eskiuc file.esk -o prog -lpthread` | Link, passing library flags through to the linker |
| `eskiuc file.esk -o file.o` | Compile to an object file only (no link) |
| `eskiuc file.esk -c -o name` | Compile to an object file only, any name |
| `eskiuc file.esk` | Compile to `file.esk.o` (object only) |
| `eskiuc file.esk --target TRIPLE` | Cross-compile for the given target triple |
| `eskiuc file.esk --freestanding` | Object only; use `esk_alloc`/`esk_free` instead of `malloc`/`free` |
| `eskiuc file.esk --test-lexer` | Dump token stream |
| `eskiuc file.esk --test-parser` | Dump AST |
| `eskiuc file.esk --test-typechecker` | Type check only; print errors |
| `eskiuc file.esk --test-codegen` | Dump LLVM IR |
| `eskiuc file.esk --hover-at LINE:COL` | Print inferred type at position |
| `eskiuc file.esk --definition-at LINE:COL` | Print definition location of symbol |

**Linking.** When the `-o` output is not an object file (no `.o` suffix) and `-c`
is absent, `eskiuc` links the program into an executable by invoking the system
C toolchain — `$CC`, then `cc`/`clang`/`gcc` on the `PATH` — exactly as `rustc`
and `clang` do internally. `-l<lib>` and `-L<path>` flags, and any `--link-arg=<arg>`,
are forwarded to the linker. A C toolchain must therefore be installed (it is the
only build-time dependency besides LLVM). With `--freestanding` (or a `.o` output)
no linking happens, so bare-metal targets are linked yourself (see the kernel's
`ld.lld` invocation).

`--hover-at` and `--definition-at` accept the format `LINE:COL` with 1-based line and column numbers. They are used by the VS Code extension to provide hover type information and go-to-definition navigation.

### Cross-compilation

`--target TRIPLE` sets the LLVM target triple for the output object file. Both the AArch64 and X86 LLVM backends are included in the Eskiu build.

```bash
eskiuc kernel.esk --target x86_64-pc-linux-gnu --freestanding -o kernel.o
eskiuc kernel.esk --target aarch64-unknown-none --freestanding -o kernel.o
```

Common triples:

| Triple | Description |
|--------|-------------|
| `x86_64-pc-linux-gnu` | ELF x86-64 (Linux) |
| `aarch64-unknown-linux-gnu` | ELF AArch64 (Linux) |
| `aarch64-unknown-none` | Bare-metal AArch64 |
| `x86_64-unknown-none` | Bare-metal x86-64 |

When `--target` is omitted the compiler defaults to the host machine's triple.

---

## 17. Error Reporting

The compiler emits diagnostics with full source location information:

```
file.esk:8:22: undefined variable 'foo'
file.esk:14:5: type mismatch: expected int, got float
```

The format is `file:line:col: message`. Line and column numbers are 1-based.

---

## 18. Preprocessor

A small text pass runs before lexing. It supports **object-like and function-like macros** and **conditional compilation**. Directives occupy their own line (the first non-blank character is `#`), and both directive lines and skipped lines are blanked out so reported line numbers match the original source.

| Directive | Effect |
|---|---|
| `#define NAME value` | Object-like macro; later occurrences of `NAME` are replaced by `value` |
| `#define NAME(a, b) body` | Function-like macro; `NAME(x, y)` substitutes the arguments into `body` |
| `#define NAME` | Define `NAME` with an empty value (useful for `#ifdef`) |
| `#undef NAME` | Remove a definition |
| `#ifdef NAME` / `#ifndef NAME` | Begin a block compiled only if `NAME` is / is not defined |
| `#else` / `#endif` | Else branch / end of a conditional |
| `#pragma pack(...)` | Struct packing directive — see §8.9 |

A line ending in a backslash (`\`) is **continued** onto the next line, so a macro body may span several physical lines (the spliced lines stay counted, so line numbers are preserved):

```eskiu
#define MAX 100
#define SQ(x) ((x) * (x))
#define DEBUG

#define POLY(x)        \
    ((x) * (x)         \
     + 2 * (x) + 1)

int main() {
    int n = MAX;          // 100
    int s = SQ(n);        // ((100) * (100))
    int p = POLY(3);      // ((3) * (3) + 2 * (3) + 1) = 16
#ifdef DEBUG
    printf("debug build\n");
#endif
    return 0;
}
```

Substitution is identifier-aware and leaves string and character literals untouched. Expansion is **recursive** — a macro whose body references other macros is expanded fully (a macro is never re-expanded within its own expansion). The macro table is **shared across files**, so a `#define` propagates into files pulled in by `import` and into the other inputs of a multi-file compile. A function-like macro *invocation* must fit on a single (post-continuation) line.

Unlike the other directives, `#pragma` is not consumed by the preprocessor — it is passed through to the compiler. Only `#pragma pack` is acted upon (§8.9); any other pragma is ignored.

**Predefined macros.** The compiler predefines a macro for the host operating system — `__APPLE__` on macOS, `__linux__` on Linux — so stdlib and user code can branch on platform with `#ifdef`. This is how `<net>` selects the correct `sockaddr_in` layout:

```eskiu
#ifdef __APPLE__
    // macOS-specific layout / constants
#else
    // Linux
#endif
```
