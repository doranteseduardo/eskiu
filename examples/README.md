# Examples

This directory contains real Eskiu programs demonstrating language features.

## Table of Examples

### [hello.esk](hello.esk)

**What it demonstrates:** Basic program structure, `extern` C functions, `main` entry point

**Difficulty:** Beginner

```bash
cd ../build
./eskiu compile ../examples/hello.esk -o hello
./hello
# Output: Hello, Eskiu!
```

**Key concepts:**
- `extern fn` for declaring C functions
- `printf` for output
- `main` returns exit code

---

### [fibonacci.esk](fibonacci.esk)

**What it demonstrates:** Loops, recursion, function definitions

**Difficulty:** Beginner

```bash
./eskiu compile ../examples/fibonacci.esk -o fib
./fib
```

**Key concepts:**
- `for` loops with explicit counters
- Recursive functions
- Local variables
- Return values

---

### [struct_usage.esk](struct_usage.esk)

**What it demonstrates:** Struct definitions, member access, struct initialization

**Difficulty:** Intermediate

```bash
./eskiu compile ../examples/struct_usage.esk -o structs
./structs
```

**Key concepts:**
- `struct` definition with fields
- Memory layout (fields stored contiguously)
- Accessing members with `.` operator
- Passing structs to functions

---

## How to Run Any Example

From the `build/` directory:

```bash
# Compile to executable
./eskiu compile ../examples/FILENAME.esk -o OUTPUT_NAME

# Run it
./OUTPUT_NAME
```

Or compile and run in one command:

```bash
./eskiu compile ../examples/FILENAME.esk -o /tmp/out && /tmp/out
```

---

## How to Understand an Example

1. **Read the code:** Follow comments
2. **Predict output:** Before running, think about what it should print
3. **Run it:** `./OUTPUT_NAME`
4. **Compare:** Did the output match your prediction?
5. **Modify:** Change the code and recompile—experiment!

---

## Debugging Examples

If an example doesn't compile:

```bash
# See tokens
./eskiu compile ../examples/FILENAME.esk --test-lexer

# See parsed AST
./eskiu compile ../examples/FILENAME.esk --test-parser

# See type checking errors
./eskiu compile ../examples/FILENAME.esk --test-typechecker

# See generated LLVM
./eskiu compile ../examples/FILENAME.esk --test-codegen
```

---

## Progress by Phase

- **Phase 1 (Lexer):** All examples work
- **Phase 2 (Parser):** All examples work
- **Phase 3 (Codegen):** All examples work
- **Phase 4 (Type Checker):** All examples work

---

## Next Steps

1. **Learn the basics:** Run [hello.esk](#helloesk) and modify it
2. **See loops:** Run [fibonacci.esk](#fibonacciesk)
3. **Understand structs:** Run [struct_usage.esk](#struct_usageesk)
4. **Read the spec:** See [LANGUAGE_SPEC.md](../docs/LANGUAGE_SPEC.md)
5. **Get started:** See [GETTING_STARTED.md](../docs/GETTING_STARTED.md)

---

## Contributing Examples

Have a good example? Submit a pull request! Examples should:

- Fit in ~30 lines
- Demonstrate one or two features clearly
- Have comments explaining key concepts
- Actually compile and run without errors
- Follow the naming convention `feature_name.esk`
