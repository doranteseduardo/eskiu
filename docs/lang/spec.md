# Eskiu Language Specification

**Version:** v0.0.8-alpha

---

## 1. Overview

Eskiu is a systems programming language with C-style syntax, manual memory management, and an LLVM backend. It targets native machine code (arm64 and x86-64) via LLVM and is designed for direct interoperability with C libraries through standard C ABI calling conventions.

Key properties:

- Statically typed with explicit type annotations
- Manual memory management — no garbage collector
- Compiles to native object files via LLVM
- Structs with methods, templates, and structural interfaces
- Multi-file programs via `import`
- Error locations reported as `file.esk:line:col: message`

The full compilation pipeline:

```sh
eskiuc file.esk -o file.o   # compile to native object file
clang file.o -o file        # link
./file                       # run
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
struct  interface  fn  extern  import
if  else  for  while  switch  case  default
return  break  continue
true  false  null
alloc  free
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

Fixed-size arrays use the form `T[N]` where `N` is a compile-time integer constant. Array types are supported as struct fields:

```eskiu
struct QRBuffer {
    uint8[858] left;
    uint8[858] right;
    int length;
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

### 3.7 Type Casting

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

The left-hand side must be an lvalue (a named variable, a pointer dereference, or a field access).

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

Adding or subtracting an integer from a pointer produces a new pointer offset by that many bytes (byte-level GEP):

```eskiu
*uint8 buf = alloc(uint8, 1024);
*uint8 mid = buf + 512;    // 512 bytes into the buffer
*uint8 back = mid - 512;   // back to start
```

### 5.8 Operator Precedence

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
| 13    | `()` `[]` `.`                                | Left to right |

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

### 6.5 Template Functions

Template functions are parameterized by one or more type variables declared in angle brackets after the function name:

```eskiu
T identity<T>(T x) {
    return x;
}

int max<T>(T a, T b) {
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

Each unique set of type arguments generates a separate monomorphic instantiation.

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

`switch` dispatches on an integer value. `break` exits the enclosing switch. If `break` is omitted, control falls through to the next case.

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

int max<T>(T a, T b) {
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

`alloc(T, N)` allocates space for `N` elements of type `T` and returns a `*T`. Internally this calls `malloc(N * sizeof(T))`.

`free(ptr)` deallocates a heap-allocated pointer. Internally this calls `free(ptr)`.

```eskiu
*uint8 buf = alloc(uint8, 1024);
// ... use buf ...
free(buf);
```

Every allocation must be paired with exactly one `free`. Double-free and use-after-free are undefined behavior.

### 11.3 Pointer Arithmetic

Pointer arithmetic operates at the byte level (GEP on `i8`):

```eskiu
*uint8 buf = alloc(uint8, 256);
*uint8 ptr = buf + 64;    // 64 bytes from start
*uint8 back = ptr - 32;   // 32 bytes back
```

### 11.4 Null Checks

```eskiu
let p: *int = null;
if (p != null) {
    int val = *p;
}
```

Dereferencing `null` is undefined behavior. The compiler does not insert null checks.

---

## 12. Multi-file Programs

### 12.1 import Statement

The `import` statement includes another Eskiu source file. The path is relative to the directory of the importing file.

```eskiu
import "stdlib/result.esk";
import "stdlib/list.esk";
import "../shared/types.esk";
```

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
extern void free(*void ptr);
extern *void memset(*void ptr, int value, int n);
```

---

## 14. Stdlib

Eskiu ships a set of standard library files in the `stdlib/` directory. Import them with relative paths from your source file.

| File                  | Contents                                                         |
|-----------------------|------------------------------------------------------------------|
| `stdlib/result.esk`   | `Result<T,E>` template struct; `Ok<T,E>(value)` and `Err<T,E>(err)` constructor functions |
| `stdlib/list.esk`     | `List<T>` template struct; `List_init`, `List_push`, `List_get`, `List_len`, `List_free` |
| `stdlib/string.esk`   | `String` struct; `String_init`, `String_from`, `String_append`, `String_concat`, `String_cstr`, `String_len`, `String_free` |
| `stdlib/math.esk`     | `extern` declarations for `sqrt`, `fabs`, `pow`, `floor`, `ceil`, `abs` |
| `stdlib/io.esk`       | `extern` declarations for `printf`, `fprintf`, `sprintf`, `scanf`, `puts` |
| `stdlib/mem.esk`      | `extern` declarations for `memcpy`, `memset`, `memmove`, `memcmp`, `strlen` |

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
    printf("%s\n", String_cstr(&s));
    String_free(&s);
    return 0;
}
```

---

## 15. Error Reporting

The compiler emits diagnostics with full source location information:

```
file.esk:8:22: undefined variable 'foo'
file.esk:14:5: type mismatch: expected int, got float
```

The format is `file:line:col: message`. Line and column numbers are 1-based.
