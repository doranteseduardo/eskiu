# Eskiu Language Specification

**Version:** 0.2  
**Status:** v0.0.1-alpha (Phases 0–4 complete)

---

## 1. Lexical Elements

### 1.1 Keywords

```
let int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64
float double bool char string void
struct interface enum fn
for in while if else switch case default break return
import extern alloc free
null true false
thread spawn mutex
try catch finally throw
```

### 1.2 Token Types

**Literals:**
- Integer: `42`, `0`, `-5`
- Float: `3.14`, `0.5`, `.5`
- String: `"hello\nworld"` (with escape sequences)
- Character: `'a'`, `'\n'`

**Identifiers:**
- Start with letter or underscore
- Followed by alphanumerics or underscores
- Case-sensitive: `x` ≠ `X`

**Operators:**
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- Bitwise: `&`, `|`, `^`, `~`, `<<`, `>>`
- Assignment: `=`
- Other: `.`, `,`, `;`, `:`, `->`, `...`

**Delimiters:**
- Braces: `{`, `}`
- Parentheses: `(`, `)`
- Brackets: `[`, `]`

---

## 2. Types

### 2.1 Primitive Types

| Type | Size | Description |
|------|------|-------------|
| `int` | platform-dependent | Signed integer |
| `int8` | 8 bits | Signed 8-bit integer |
| `int16` | 16 bits | Signed 16-bit integer |
| `int32` | 32 bits | Signed 32-bit integer |
| `int64` | 64 bits | Signed 64-bit integer |
| `uint` | platform-dependent | Unsigned integer |
| `uint8` | 8 bits | Unsigned 8-bit integer |
| `uint16` | 16 bits | Unsigned 16-bit integer |
| `uint32` | 32 bits | Unsigned 32-bit integer |
| `uint64` | 64 bits | Unsigned 64-bit integer |
| `float` | 32 bits | Single-precision floating point |
| `double` | 64 bits | Double-precision floating point |
| `bool` | 1 bit | Boolean (true/false) |
| `char` | 8 bits | Single-byte character |
| `string` | pointer | Immutable C-string literal |
| `void` | N/A | No value |

### 2.2 Derived Types

**Pointers:**
```eskiu
let ptr: *int = null;        // Pointer to int
let str: *char = null;       // Pointer to char (used for strings)
let ptr_ptr: **int = null;   // Pointer to pointer
```

**Arrays (Phase 3 codegen only):**
```eskiu
int[10] fixed_array;         // Fixed-size array (stack)
[]int dynamic_array = [1,2,3]; // Dynamic array (will be heap in Phase 6)
```

**Structs (Phase 5):**
```eskiu
struct Point {
    float x;
    float y;
}
```

**Interfaces (Phase 5):**
```eskiu
interface Drawable {
    void draw();
}
```

**Templates (Phase 5):**
```eskiu
struct List<T> {
    *T data;
    int size;
    int capacity;
}
```

---

## 3. Variables and Declarations

### 3.1 Variable Declaration

```eskiu
let x: int = 5;
let name: string = "Eskiu";
let ptr: *int = null;

// Type inference is NOT supported in v0.0.1
// You must always specify the type
```

### 3.2 Scope

Variables are scoped to the block they're declared in:

```eskiu
{
    let x = 5;
    {
        let x = 10;     // Shadows outer x
        printf("%d\n", x); // Prints 10
    }
    printf("%d\n", x);  // Prints 5
}
```

### 3.3 Lifetime

**Stack variables** are automatically freed when leaving scope:
```eskiu
{
    let point: Point = Point { x: 1, y: 2 };
} // point is freed here
```

**Heap variables** (Phase 6) are manually freed:
```eskiu
let arr = alloc(int, 10);  // Allocate on heap
// Use arr
free(arr);                 // Manually deallocate
```

---

## 4. Functions

### 4.1 Function Declaration

```eskiu
int add(int a, int b) {
    return a + b;
}

void print_line() {
    printf("---\n");
}

// Variadic parameters (C-style)
extern int printf(string fmt, ...);
```

### 4.2 Parameters

- Parameters are passed by value
- No default parameters (v0.0.1)
- Variadic parameters (`...`) only for extern functions

### 4.3 Return Types

Functions must declare their return type:

```eskiu
int get_number() { return 42; }
void do_nothing() { return; }
```

### 4.4 Extern Functions

Declare C library functions:

```eskiu
extern int printf(string fmt, ...);
extern void* malloc(int size);
extern void free(*void ptr);
extern int strlen(string str);
```

---

## 5. Statements

### 5.1 Block Statement

```eskiu
{
    let x = 5;
    printf("%d\n", x);
}
```

### 5.2 If Statement

```eskiu
if (x > 0) {
    printf("positive\n");
} else if (x == 0) {
    printf("zero\n");
} else {
    printf("negative\n");
}
```

### 5.3 Switch Statement

```eskiu
switch (status) {
    case 0:
        printf("ok\n");
        break;
    case 1:
        printf("error\n");
        break;
    default:
        printf("unknown\n");
        break;
}
```

### 5.4 While Loop

```eskiu
while (count < 10) {
    printf("%d\n", count);
    count = count + 1;
}
```

### 5.5 For Loop

```eskiu
for (int i = 0; i < 10; i++) {
    printf("%d\n", i);
}

// For-in loop (Phase 5+)
for item in collection {
    item.print();
}
```

### 5.6 Return Statement

```eskiu
int get_five() {
    return 5;
}

void do_nothing() {
    return;
}
```

### 5.7 Break Statement

```eskiu
for (int i = 0; i < 100; i++) {
    if (i == 10) break;  // Exit loop
    printf("%d\n", i);
}
```

---

## 6. Expressions

### 6.1 Literals

```eskiu
42           // int literal
3.14         // float literal
"hello"      // string literal
'a'          // char literal
true, false  // bool literals
null         // null pointer
```

### 6.2 Binary Operators

**Arithmetic:**
```eskiu
a + b   // addition
a - b   // subtraction
a * b   // multiplication
a / b   // division (integer or float)
a % b   // modulo
```

**Comparison:**
```eskiu
a == b  // equality
a != b  // inequality
a < b   // less than
a > b   // greater than
a <= b  // less than or equal
a >= b  // greater than or equal
```

**Logical:**
```eskiu
a && b  // logical AND
a || b  // logical OR
!a      // logical NOT
```

**Bitwise:**
```eskiu
a & b   // bitwise AND
a | b   // bitwise OR
a ^ b   // bitwise XOR
~a      // bitwise NOT
a << b  // left shift
a >> b  // right shift
```

### 6.3 Unary Operators

```eskiu
-x      // negation
!x      // logical NOT
&x      // address-of (get pointer)
*x      // dereference (follow pointer)
(int)x  // explicit cast
```

### 6.4 Function Calls

```eskiu
result = add(5, 3);
printf("Result: %d\n", result);
```

### 6.5 Member Access (Phase 5)

```eskiu
point.x = 1.0;
point.y = 2.0;
distance = point.distance(other_point);
```

### 6.6 Array Indexing

```eskiu
arr[0] = 42;
int val = arr[5];
```

### 6.7 Type Casting

```eskiu
let x: double = 3.14;
let n: int = (int)x;     // Explicit cast (truncates to 3)
let b: uint8 = (uint8)n;
```

---

## 7. Structs (Phase 5)

### 7.1 Definition

```eskiu
struct Person {
    string name;
    int age;
    string email;
}
```

### 7.2 Initialization

```eskiu
let person: Person = Person {
    name: "Alice",
    age: 30,
    email: "alice@example.com"
};
```

### 7.3 Methods

```eskiu
struct Person {
    string name;
    int age;

    void greet() {
        printf("Hello, I'm %s and I'm %d years old\n", name, age);
    }

    int get_age() {
        return age;
    }
}

person.greet();
```

---

## 8. Interfaces (Phase 5)

### 8.1 Definition

```eskiu
interface Shape {
    float area();
    void draw();
}
```

### 8.2 Implicit Implementation

```eskiu
struct Circle {
    float radius;

    float area() {
        return 3.14 * radius * radius;
    }

    void draw() {
        printf("Drawing circle\n");
    }
}

// Circle implicitly satisfies Shape interface
// No explicit 'implements' keyword needed
```

### 8.3 Using Interface Types

```eskiu
void render_shape(Shape s) {
    printf("Area: %f\n", s.area());
    s.draw();
}

let circle: Circle = Circle { radius: 5.0 };
render_shape(circle);  // Works! Circle is a Shape
```

---

## 9. Templates (Phase 5)

### 9.1 Definition

```eskiu
struct List<T> {
    *T data;
    int size;
    int capacity;

    void push(T item) {
        // Implementation
    }

    T pop() {
        // Implementation
    }
}
```

### 9.2 Instantiation

```eskiu
let numbers: List<int> = List<int> { /* ... */ };
let strings: List<string> = List<string> { /* ... */ };

numbers.push(42);
strings.push("hello");
```

**Note:** Monomorphic instantiation only. Each type parameter combination creates a new type at compile time.

---

## 10. Memory Management (Phase 6)

### 10.1 Stack Allocation

```eskiu
let point: Point = Point { x: 1.0, y: 2.0 };  // Automatic cleanup on scope exit
```

### 10.2 Heap Allocation

```eskiu
// Allocate
let arr: *int = alloc(int, 10);    // Allocate array of 10 ints

// Use
arr[0] = 42;
arr[5] = 100;

// Deallocate
free(arr);
```

### 10.3 String Type

```eskiu
// Immutable string literal
let msg: string = "Hello";

// Mutable String type (Phase 6)
let mut_msg: String = String.new("Hello");
mut_msg.append(" World");
printf("%s\n", mut_msg.cstr());
```

---

## 11. Error Handling

### 11.1 Result Type (Phase 7)

```eskiu
Result<int, string> divide(int a, int b) {
    if (b == 0) {
        return Err("division by zero");
    }
    return Ok(a / b);
}

let result = divide(10, 2);
if (result.ok) {
    printf("Result: %d\n", result.value);
} else {
    printf("Error: %s\n", result.error);
}
```

### 11.2 Exceptions (Phase 10+)

```eskiu
try {
    let data = readFile("data.txt");
    process(data);
} catch (IOError e) {
    printf("IO Error: %s\n", e.message);
} catch (Exception e) {
    printf("Unexpected error: %s\n", e.message);
} finally {
    cleanup();
}
```

---

## 12. Concurrency (Phase 9)

### 12.1 Threads

```eskiu
thread worker = spawn(fn() {
    printf("Running in thread\n");
});

worker.join();  // Wait for thread to finish
```

### 12.2 Synchronization

```eskiu
let mutex: Mutex = Mutex.new();

mutex.lock();
// Critical section
shared_counter = shared_counter + 1;
mutex.unlock();
```

---

## 13. Comments

```eskiu
// Single-line comment

/*
   Multi-line comment
   Can span multiple lines
*/
```

---

## 14. Operator Precedence (Highest to Lowest)

| Precedence | Operator | Associativity |
|------------|----------|---------------|
| 1 | `()`, `[]`, `.` | Left-to-right |
| 2 | `!`, `-`, `+`, `&`, `*`, casts | Right-to-left |
| 3 | `*`, `/`, `%` | Left-to-right |
| 4 | `+`, `-` | Left-to-right |
| 5 | `<<`, `>>` | Left-to-right |
| 6 | `<`, `>`, `<=`, `>=` | Left-to-right |
| 7 | `==`, `!=` | Left-to-right |
| 8 | `&` (bitwise) | Left-to-right |
| 9 | `^` | Left-to-right |
| 10 | `\|` (bitwise) | Left-to-right |
| 11 | `&&` | Left-to-right |
| 12 | `\|\|` | Left-to-right |
| 13 | `=` | Right-to-left |

---

## 15. Type Conversion Rules

**Automatic (Implicit):**
- `int8` → `int16` → `int32` → `int64`
- `float` → `double`
- `int` → `float` (if no precision loss)

**Explicit (Cast Required):**
```eskiu
let x: double = 3.14;
let n: int = (int)x;     // Truncate
let b: bool = (bool)n;   // Non-zero → true
let c: char = (char)n;   // Take low 8 bits
```

---

## 16. Built-in Functions (Phase 7+)

**Math:**
```eskiu
float sqrt(float x);
float abs(float x);
float pow(float x, float exp);
float min(float a, float b);
float max(float a, float b);
```

**I/O:**
```eskiu
void print(string msg);
void println(string msg);
string read_file(string path);
void write_file(string path, string content);
```

**Memory:**
```eskiu
void memcpy(*void dest, *void src, int size);
void memset(*void ptr, int value, int size);
```

---

## 17. Examples

### Hello World
```eskiu
extern int printf(string fmt, ...);

int main() {
    printf("Hello, World!\n");
    return 0;
}
```

### Fibonacci
```eskiu
int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib(i));
    }
    return 0;
}
```

### Struct Usage (Phase 5)
```eskiu
struct Rectangle {
    float width;
    float height;

    float area() {
        return width * height;
    }
}

int main() {
    let rect: Rectangle = Rectangle { width: 10.0, height: 5.0 };
    printf("Area: %f\n", rect.area());
    return 0;
}
```

---

For implementation details, see [PHASES.md](./PHASES.md) and [ARCHITECTURE.md](./ARCHITECTURE.md).
