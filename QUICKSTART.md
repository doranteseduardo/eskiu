# Quickstart

## Build

```bash
cmake -S . -B build
cmake --build build
./build/eskiuc --version
```

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

Emit LLVM IR:

```bash
./build/eskiuc hello.esk --test-codegen
```

Output:

```llvm
; ModuleID = 'eskiu'
source_filename = "eskiu"

@0 = private unnamed_addr constant [19 x i8] c"Hello from Eskiu!\0A\00", align 1
@1 = private unnamed_addr constant [12 x i8] c"Result: %d\0A\00", align 1

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
  %1 = call i32 (ptr, ...) @printf(ptr @0)
  %2 = load i32, ptr %result, align 4
  %3 = call i32 (ptr, ...) @printf(ptr @1, i32 %2)
  ret i32 0
}
```

## Count to 5

```eskiu
extern int printf(string fmt, ...);

int main() {
    int i = 1;
    while (i <= 5) {
        printf("%d\n", i);
        i = i + 1;
    }
    return 0;
}
```

```llvm
@0 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %i = alloca i32, align 4
  store i32 1, ptr %i, align 4
  br label %while

while:
  %0 = load i32, ptr %i, align 4
  %1 = icmp sle i32 %0, 5
  br i1 %1, label %while_body, label %while_exit

while_body:
  %2 = load i32, ptr %i, align 4
  %3 = call i32 (ptr, ...) @printf(ptr @0, i32 %2)
  %4 = load i32, ptr %i, align 4
  %5 = add i32 %4, 1
  store i32 %5, ptr %i, align 4
  br label %while

while_exit:
  ret i32 0
}
```

## What's next

[Language guide →](docs/lang/getting-started.md)
