# Quickstart

You have LLVM installed. You will have a native binary running in under 5 minutes.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/eskiuc --version
```

Expected output: `Eskiu 0.0.12-alpha (LLVM ...)`

## Hello, Eskiu

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

Compile to a native binary and run it:

```bash
./build/eskiuc hello.esk -o hello.o && clang hello.o -o hello && ./hello
```

Output:

```
Hello from Eskiu!
Result: 8
```

To inspect the generated LLVM IR instead of producing an object file:

```bash
./build/eskiuc hello.esk --test-codegen
```

## Struct with a method

Eskiu structs hold data. Methods are plain functions that take a pointer to the struct as their first argument.

```eskiu
extern int printf(string fmt, ...);

struct Point {
    int x;
    int y;
}

int Point_sum(Point* self) {
    return self.x + self.y;
}

int main() {
    let p: Point;
    p.x = 3;
    p.y = 7;
    int s = Point_sum(&p);
    printf("sum = %d\n", s);
    return 0;
}
```

```bash
./build/eskiuc point.esk -o point.o && clang point.o -o point && ./point
```

Output:

```
sum = 10
```

## alloc / free with pointer arithmetic

`alloc(T, n)` allocates `n` elements of type `T` and returns a typed pointer. Index with `[]`. Call `free` when done.

```eskiu
extern int printf(string fmt, ...);
extern void free(*void ptr);

int main() {
    *int buf = alloc(int, 4);
    buf[0] = 10;
    buf[1] = 20;
    buf[2] = 30;
    buf[3] = 40;
    int sum = buf[0] + buf[1] + buf[2] + buf[3];
    printf("sum = %d\n", sum);
    free(buf);
    return 0;
}
```

```bash
./build/eskiuc mem.esk -o mem.o && clang mem.o -o mem && ./mem
```

Output:

```
sum = 100
```

## Lambda (anonymous function)

Functions are first-class values. Write a lambda with the same syntax as a regular function, minus the name:

```eskiu
extern int printf(string fmt, ...);

int apply(fn(int)->int f, int x) {
    return f(x);
}

int main() {
    let double_it: fn(int)->int = int(int x) { return x * 2; };
    printf("%d\n", double_it(5));           // 10
    printf("%d\n", apply(double_it, 4));    // 8
    return 0;
}
```

`fn(int)->int` is the function pointer type. The lambda `int(int x) { ... }` produces a value of that type.

---

## What's next

[Language guide →](docs/lang/getting-started.md)
