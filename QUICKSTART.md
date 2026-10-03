# Quickstart

Two ways to get `eskiuc` running. Pick the one that suits you.

---

## Option A: Pre-built binary (recommended)

On macOS or Linux, the install script picks the right binary, verifies its checksum and
installs it under `/usr/local` (or `~/.eskiu` when `/usr/local` is not writable):

```bash
curl -fsSL https://eskiu-lang.org/install.sh | sh
eskiuc --version
```

Expected output: `Eskiu 0.9.2 (LLVM ...)`

To install by hand, download a tarball from
[github.com/doranteseduardo/eskiu/releases](https://github.com/doranteseduardo/eskiu/releases)
(macOS arm64, Linux x86-64, Linux arm64) and unpack it into a prefix:

```bash
sudo tar -xzf eskiuc-linux-x86_64.tar.gz -C /usr/local
```

The tarball holds `bin/eskiuc` (the compiler) and `lib/eskiu/stdlib/` (the standard
library), so `import <result>` and the other stdlib imports work right away. On Windows,
unpack `eskiuc-windows-x86_64.zip` and add its `bin` folder to `PATH`.

`eskiuc` links programs with your system C toolchain (`cc`, `clang` or `gcc`). On macOS
that is `xcode-select --install`.

---

## Option B: Build from source

Requires LLVM 21+ (LLVM 22 recommended), CMake 3.20+, and a C++17 compiler.

```bash
cmake -S . -B build
cmake --build build
./build/eskiuc --version
```

Expected output: `Eskiu 0.9.2 (LLVM ...)`

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

Compile to a native binary and run it. When the `-o` name has no `.o`
extension, `eskiuc` links it into an executable for you (it invokes your
system C toolchain, the same way `rustc` and `clang` do):

```bash
./build/eskiuc hello.esk -o hello && ./hello
```

Need just the object file? Use a `.o` name (or `-c`), then link yourself:

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
./build/eskiuc point.esk -o point && ./point
```

Output:

```
sum = 10
```

## alloc / free with pointer arithmetic

`alloc<T>(n)` (from the `<mem>` stdlib) allocates `n` elements of type `T` and returns a typed pointer. Index with `[]`. Call `free` when done.

```eskiu
import <mem>;
extern int printf(string fmt, ...);

int main() {
    *int buf = alloc<int>(4);
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
./build/eskiuc mem.esk -o mem && ./mem
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
