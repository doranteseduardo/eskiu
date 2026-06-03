# Eskiu Language Specification

**Version:** 0.1-draft  
**Status:** Phases 0–4 complete. Features marked with a phase note are not yet available in the current build.

---

## 1. Lexical Elements

### 1.1 Keywords

The following identifiers are reserved and may not be used as variable or function names:

```
let  int  int8  int16  int32  int64
uint  uint8  uint16  uint32  uint64
float  double  bool  char  string  void
struct  interface  enum  fn
for  in  while  if  else  switch  case  default  break  return
import  extern  alloc  free  null  true  false
thread  spawn  mutex
try  catch  finally  throw
```

### 1.2 Identifiers

An identifier begins with a letter (`a`–`z`, `A`–`Z`) or underscore (`_`), followed by zero or more letters, digits (`0`–`9`), or underscores. Identifiers are case-sensitive: `point` and `Point` are distinct.

```eskiu
my_var    _internal    Count    x1
```

### 1.3 Literals

**Integer literals** are sequences of decimal digits. Negative values are formed by applying the unary `-` operator.

```eskiu
0    42    1000
```

**Float literals** contain a decimal point. The leading digit is optional.

```eskiu
3.14    0.5    .5
```

**String literals** are sequences of characters enclosed in double quotes. The following escape sequences are recognized:

| Escape | Meaning         |
|--------|-----------------|
| `\n`   | Newline         |
| `\t`   | Horizontal tab  |
| `\\`   | Backslash       |
| `\"`   | Double quote    |
| `\0`   | Null byte       |

```eskiu
"Hello, world!\n"
"path\\to\\file"
```

**Character literals** are a single character or escape sequence enclosed in single quotes.

```eskiu
'a'    '\n'    '\\'
```

**Boolean literals** are the keywords `true` and `false`.

**Null literal** is the keyword `null`, used for null pointer values.

### 1.4 Comments

Single-line comments begin with `//` and extend to the end of the line. Block comments are enclosed in `/* ... */` and may span multiple lines. Comments do not nest.

```eskiu
// This is a single-line comment

/*
   This is a
   block comment.
*/
```

### 1.5 Operators

```
+   -   *   /   %
==  !=  <   >   <=  >=
&&  ||  !
&   |   ^   ~   <<  >>
=
```

The `&` token serves as both the bitwise AND operator and the address-of operator depending on context. The `*` token serves as both the multiplication operator and the pointer dereference operator depending on context.

### 1.6 Delimiters

```
{  }   ( )   [ ]
;  ,   .   :   ->   ...
```

---

## 2. Types

### 2.1 Primitive Types

| Type     | LLVM IR   | Width    | Notes                            |
|----------|-----------|----------|----------------------------------|
| `int`    | `i32`     | 32 bits  | Signed; alias for `int32`        |
| `int8`   | `i8`      | 8 bits   | Signed                           |
| `int16`  | `i16`     | 16 bits  | Signed                           |
| `int32`  | `i32`     | 32 bits  | Signed                           |
| `int64`  | `i64`     | 64 bits  | Signed                           |
| `uint`   | `i32`     | 32 bits  | Unsigned; alias for `uint32`     |
| `uint8`  | `i8`      | 8 bits   | Unsigned                         |
| `uint16` | `i16`     | 16 bits  | Unsigned                         |
| `uint32` | `i32`     | 32 bits  | Unsigned                         |
| `uint64` | `i64`     | 64 bits  | Unsigned                         |
| `float`  | `float`   | 32 bits  | IEEE 754 single-precision        |
| `double` | `double`  | 64 bits  | IEEE 754 double-precision        |
| `bool`   | `i1`      | 1 bit    | `true` or `false`                |
| `char`   | `i8`      | 8 bits   | Single byte; no implicit widening|
| `string` | `i8*`     | pointer  | Immutable C-string literal       |
| `void`   | `void`    | —        | No value; valid only as return type |

Signedness is tracked by the compiler for correct code generation of arithmetic and comparisons. The underlying integer width is the same for signed and unsigned pairs (e.g., `int8` and `uint8` are both `i8`).

### 2.2 Pointer Types

A pointer type is written with a leading `*`:

```eskiu
*int       // pointer to int
*uint8     // pointer to uint8
**char     // pointer to pointer to char
```

Both leading-star (`*T`) and trailing-star (`T*`) syntax are accepted by the parser. The canonical form used throughout this document is `*T`.

```eskiu
let ptr: *int = null;
let buf: *uint8 = null;
```

### 2.3 Struct Types

A struct type is named by its declaration (see §7). Struct variables are declared using the struct name as the type.

```eskiu
let p: Point;
```

> **Phase 5** — Struct initialization expressions and struct codegen are not yet available in the current build. Struct declarations and member type checking are implemented.

### 2.4 Array Types

Fixed-size arrays use the form `T[N]` where `N` is a compile-time constant.

```eskiu
uint8[858] buffer;
int[16] coefficients;
```

> **Phase 5** — Fixed-size array codegen is not yet complete. The syntax is parsed and appears in the AST; backend support is planned for Phase 5.

### 2.5 Type Casting

An explicit cast is written as `(TYPE)expr`. The expression is converted to the named type at the point of the cast.

```eskiu
double x = 3.14;
int n = (int)x;        // truncates to 3
uint8 b = (uint8)n;
float f = (float)n;
```

No implicit widening or narrowing conversions are performed. When a value of one numeric type is passed where another is expected, an explicit cast is required (the compiler emits a warning on implicit mismatch).

---

## 3. Variables

### 3.1 Declaration Syntax

Two syntactic forms are valid and equivalent:

**`let` form (type-annotated):**

```eskiu
let x: int = 5;
let name: string = "Eskiu";
let ptr: *int = null;
```

**C-style form:**

```eskiu
int x = 5;
string name = "Eskiu";
*int ptr = null;
```

Both forms require an explicit type annotation. Type inference is not supported. The initializer is optional; an uninitialized variable holds an undefined value.

```eskiu
int counter;          // declared, value undefined
counter = 0;          // first assignment
```

### 3.2 Scope

Variables are scoped to the enclosing block `{ }`. A variable declared in an inner block shadows a variable of the same name in an outer block. Shadowing is permitted.

```eskiu
int x = 10;
{
    int x = 20;         // shadows outer x
    printf("%d\n", x);  // prints 20
}
printf("%d\n", x);      // prints 10
```

### 3.3 Assignment

Assignment is an expression statement. The left-hand side must be an lvalue (a named variable or a pointer dereference).

```eskiu
x = x + 1;
*ptr = 42;
point.x = 1.0;
```

Compound assignment operators (`+=`, `-=`, etc.) are not currently defined. Use `x = x + 1` instead.

---

## 4. Functions

### 4.1 Definition

A function definition gives the return type, name, parameter list, and body:

```eskiu
int add(int a, int b) {
    return a + b;
}

void reset(int count) {
    count = 0;
}
```

Parameters are passed by value. The parameter list may be empty:

```eskiu
int get_magic() {
    return 42;
}
```

### 4.2 Return Type

Every function must declare a return type. A `void` function may use a bare `return;` or allow control to fall off the end of the body.

```eskiu
void log_event() {
    printf("event\n");
    return;
}
```

### 4.3 Variadic Functions

The ellipsis `...` marks a variadic parameter list. Variadic parameters may only appear in `extern` declarations (see §9); user-defined variadic functions are not yet supported.

### 4.4 Forward Declarations

Functions are registered in a first pass before their bodies are type-checked, so mutual recursion is valid without forward declarations.

```eskiu
int even(int n);   // not required, but the compiler accepts it
int odd(int n) { if (n == 0) return 0; return even(n - 1); }
int even(int n) { if (n == 0) return 1; return odd(n - 1); }
```

---

## 5. Operators

### 5.1 Arithmetic

| Operator | Description       |
|----------|-------------------|
| `a + b`  | Addition          |
| `a - b`  | Subtraction       |
| `a * b`  | Multiplication    |
| `a / b`  | Division          |
| `a % b`  | Modulo (remainder)|

Integer division truncates toward zero. For float operands, standard IEEE 754 semantics apply.

### 5.2 Comparison

| Operator | Description              |
|----------|--------------------------|
| `a == b` | Equal                    |
| `a != b` | Not equal                |
| `a < b`  | Less than                |
| `a > b`  | Greater than             |
| `a <= b` | Less than or equal       |
| `a >= b` | Greater than or equal    |

Comparison operators yield a `bool`.

### 5.3 Logical

| Operator | Description   |
|----------|---------------|
| `a && b` | Logical AND   |
| `a \|\| b` | Logical OR  |
| `!a`     | Logical NOT   |

Short-circuit evaluation applies: in `a && b`, `b` is not evaluated if `a` is false; in `a || b`, `b` is not evaluated if `a` is true.

### 5.4 Bitwise

| Operator | Description     |
|----------|-----------------|
| `a & b`  | Bitwise AND     |
| `a \| b` | Bitwise OR      |
| `a ^ b`  | Bitwise XOR     |
| `~a`     | Bitwise NOT     |
| `a << b` | Left shift      |
| `a >> b` | Right shift     |

Bitwise operators act on the integer bit representation. Right shift on signed types is arithmetic (sign-extended).

### 5.5 Assignment

```eskiu
x = expr;
```

Assignment stores the value of `expr` into the lvalue on the left. The result of the assignment expression is the stored value.

### 5.6 Address-of and Dereference

| Operator | Description                        |
|----------|------------------------------------|
| `&x`     | Address of `x`; yields `*T` where `x: T` |
| `*ptr`   | Dereference `ptr`; yields `T` where `ptr: *T` |

### 5.7 Operator Precedence

Higher rows bind more tightly.

| Level | Operators                                 | Associativity  |
|-------|-------------------------------------------|----------------|
| 1     | `()` `[]` `.`                             | Left to right  |
| 2     | Unary `!` `-` `+` `&` `*` `~` `(TYPE)`   | Right to left  |
| 3     | `*` `/` `%`                               | Left to right  |
| 4     | `+` `-`                                   | Left to right  |
| 5     | `<<` `>>`                                 | Left to right  |
| 6     | `<` `>` `<=` `>=`                         | Left to right  |
| 7     | `==` `!=`                                 | Left to right  |
| 8     | `&` (bitwise)                             | Left to right  |
| 9     | `^`                                       | Left to right  |
| 10    | `\|` (bitwise)                            | Left to right  |
| 11    | `&&`                                      | Left to right  |
| 12    | `\|\|`                                    | Left to right  |
| 13    | `=`                                       | Right to left  |

Use parentheses to override precedence explicitly.

---

## 6. Control Flow

### 6.1 if / else

```eskiu
if (condition) {
    // ...
} else if (other_condition) {
    // ...
} else {
    // ...
}
```

The condition must be a `bool` or an integer (non-zero is true). Braces are required around each branch body.

### 6.2 while

```eskiu
while (condition) {
    // body
}
```

The body executes repeatedly while `condition` is true. Use `break` to exit early.

### 6.3 for

C-style three-part form:

```eskiu
for (int i = 0; i < 10; i = i + 1) {
    printf("%d\n", i);
}
```

Each part is optional:

```eskiu
for ( ; running; ) {    // condition only
    // ...
}
```

The init clause may declare a variable scoped to the loop:

```eskiu
for (int i = 0; i < n; i = i + 1) { }
// i is not in scope here
```

### 6.4 return

Returns a value from the current function. A `void` function may use `return;` with no operand, or omit the return at the end of the body.

```eskiu
int sign(int x) {
    if (x > 0) return 1;
    if (x < 0) return -1;
    return 0;
}
```

### 6.5 break

Exits the innermost enclosing `for` or `while` loop immediately.

```eskiu
while (true) {
    if (done) break;
    // ...
}
```

### 6.6 switch / case

> **Phase 5** — `switch`/`case` statements are lexed and parsed but codegen is not yet implemented.

---

## 7. Structs

### 7.1 Definition

A struct groups named fields of any types. Methods may be defined directly inside the struct body.

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

    float area() {
        return width * height;
    }
}
```

Field types may be any primitive type, pointer type, or another struct type. Fixed-size array fields are declared with the `T[N]` syntax:

```eskiu
struct QRBuffer {
    uint8[858] data;
    int length;
}
```

### 7.2 Field Access

Fields are accessed with the `.` operator:

```eskiu
let p: Point;
p.x = 1.0;
p.y = 2.0;
float sum = p.x + p.y;
```

Method calls use the same `.` syntax:

```eskiu
float a = rect.area();
```

### 7.3 Current Limitations

> **Phase 5** — Struct initialization expressions (`Point { x: 1.0, y: 2.0 }`) and struct codegen (IR generation for struct variables, field loads/stores, and method dispatch) are not yet implemented. Struct declarations are parsed and type-checked; member access is type-checked. Codegen for structs will be completed in Phase 5.

---

## 8. Memory Model

### 8.1 Stack Allocation

All variables declared in a function body are allocated on the stack. Stack memory is automatically reclaimed when the enclosing function or block exits. There is no garbage collector.

```eskiu
int main() {
    int buf[256];   // 256 ints on the stack
    // ...
    return 0;
}   // buf is reclaimed here
```

### 8.2 Heap Allocation

> **Phase 6** — `alloc` and `free` are reserved keywords and will be implemented in Phase 6.

The planned interface is:

```eskiu
*uint8 buf = alloc(uint8, 1024);   // allocate 1024 bytes
// ... use buf ...
free(buf);                          // explicit deallocation required
```

`alloc(T, N)` allocates space for `N` elements of type `T` and returns a `*T`. Every allocation must be paired with a corresponding `free`. Double-free and use-after-free are undefined behavior.

### 8.3 Pointer Arithmetic

> **Phase 6** — Pointer arithmetic (`ptr + n`, `ptr - n`) is planned for Phase 6 alongside heap allocation.

### 8.4 Philosophy

Eskiu uses manual memory management. There is no garbage collector and no borrow checker. The programmer is responsible for ensuring that:

- Every heap allocation is freed exactly once.
- Pointers are not used after the memory they refer to has been freed.
- Stack-allocated variables are not returned by pointer from the function that declares them.

---

## 9. extern / C Interop

### 9.1 extern Declarations

An `extern` declaration makes a C function available to Eskiu code. The declaration specifies the return type, name, and parameter types, matching the C function's ABI exactly.

```eskiu
extern int printf(string fmt, ...);
extern int strlen(string s);
extern *void memcpy(*void dst, *void src, int n);
extern int open(string path, int flags);
```

The `...` ellipsis denotes a variadic C function. Eskiu does not currently support defining user-level variadic functions; `...` is only valid in `extern` declarations.

### 9.2 Calling extern Functions

`extern` functions are called exactly like Eskiu functions:

```eskiu
extern int printf(string fmt, ...);

int main() {
    printf("value: %d\n", 42);
    return 0;
}
```

### 9.3 C ABI Compatibility

`extern` declarations emit standard C-ABI-compatible LLVM IR call instructions. Any function exported by a C library or compiled with a C compiler may be called this way, including OpenSSL and other system libraries.

For functions that return or accept `void*`, use `*void` on the Eskiu side:

```eskiu
extern *void malloc(int size);
extern void free(*void ptr);
```

---

## 10. Planned Features

The table below summarizes features planned for upcoming releases. Features not listed here are out of scope for v1.0.

| Version | Feature                                                                 |
|---------|-------------------------------------------------------------------------|
| v0.1    | Full struct codegen (field load/store, method dispatch)                 |
| v0.1    | Fixed-size array codegen (`T[N]` fields and locals)                     |
| v0.1    | `alloc(T, N)` / `free(ptr)` heap allocation                             |
| v0.1    | Go-style implicit interfaces (structural subtyping)                     |
| v0.1    | Monomorphic template instantiation (`struct List<T>`)                   |
| v0.1    | `switch` / `case` statement codegen                                     |
| v0.2    | `Result<T, E>` error type                                               |
| v0.2    | Lambdas and closures                                                    |
| v0.2    | Thread primitives (`spawn`, `mutex`)                                    |
| v1.0    | Exception handling (`try` / `catch` / `finally` / `throw`)             |
| v2.0    | `async` / `await`                                                       |

Features in the lexer and parser that are not yet backed by codegen are noted throughout this document with a **Phase N** callout. A feature marked as Phase 5 or later will produce a parse-level representation but will not generate correct IR in the current build.
