# Quickstart: Your First Eskiu Program (5 minutes)

## Prerequisites

- macOS with Xcode Command Line Tools or Linux with GCC/Clang
- LLVM installed (`brew install llvm` on macOS)
- CMake 3.10+

## 1. Install and Build Eskiu (2 minutes)

```bash
cd ~/projects
git clone https://github.com/yourusername/eskiu.git
cd eskiu
mkdir build && cd build
cmake ..
make -j4
```

Your compiler is ready at `./eskiu` (relative to build directory).

## 2. Write Your First Program (1 minute)

Create a file `hello.esk`:

```esk
fn main() -> i32 {
    return 0;
}
```

## 3. Compile It (1 minute)

```bash
./eskiu compile ../hello.esk -o hello
./hello
```

Success! No output because we haven't added anything yet.

## 4. Make It Do Something (1 minute)

Update `hello.esk` to call a C function:

```esk
extern fn printf(format: *i8, ...) -> i32;

fn main() -> i32 {
    printf("Hello, Eskiu!\n");
    return 0;
}
```

Recompile and run:

```bash
./eskiu compile ../hello.esk -o hello
./hello
```

Output: `Hello, Eskiu!`

## What's Next?

- Read the **[Getting Started Guide](docs/GETTING_STARTED.md)** for a deeper walkthrough
- Check **[examples/](examples/)** for more real programs
- Learn the language in **[Language Spec](docs/LANGUAGE_SPEC.md)**
- Understand the compiler in **[Architecture](docs/ARCHITECTURE.md)**

## Troubleshooting

**Error: "LLVM not found"**  
Install LLVM: `brew install llvm` (macOS) or `apt-get install llvm-14-dev` (Ubuntu)

**Error: "unknown identifier 'printf'"**  
Use `extern` to declare C functions. See examples/fibonacci.esk for more.

**Compiler crashes?**  
File a bug at GitHub. Run with `--test-lexer` to see if lexing works, `--test-parser` to test parsing.

---

That's it! You've compiled your first Eskiu program. Head to [Getting Started](docs/GETTING_STARTED.md) for the full tour.
