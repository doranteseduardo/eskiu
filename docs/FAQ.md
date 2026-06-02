# Frequently Asked Questions

Quick answers to common questions about Eskiu Lang.

## Getting Started

### How do I get Eskiu running?

See [QUICKSTART.md](../QUICKSTART.md) for a 5-minute setup, or [BUILD.md](BUILD.md) for detailed instructions.

### I get "LLVM not found" error

Install LLVM:
- **macOS:** `brew install llvm`
- **Linux:** `apt-get install llvm-14-dev` (Ubuntu) or `dnf install llvm-devel` (Fedora)

Then set the LLVM path before building:
```bash
export LLVM_CONFIG=$(brew --prefix llvm)/bin/llvm-config  # macOS
# or find it on Linux: which llvm-config-14
```

### Can I use Eskiu on Windows?

Not yet. Windows support is planned for v0.2. For now, use WSL2 (Windows Subsystem for Linux).

---

## Language and Features

### What makes Eskiu different from C?

Eskiu is C-inspired but adds:
- **Explicit types everywhere** (no implicit conversions)
- **Structs with member access** (`.field` syntax)
- **Go-style interfaces** (structural typing, no inheritance)
- **Type checking before compilation** (catches errors early)
- **LLVM IR backend** (better optimization than traditional C compilers)

### Can I call C functions from Eskiu?

Yes! Use `extern` to declare C functions:

```esk
extern fn strlen(s: *i8) -> i32;
extern fn printf(format: *i8, ...) -> i32;

fn main() -> i32 {
    printf("Hello\n");
    return 0;
}
```

### Can I use Eskiu to compile C code?

No. Eskiu is not a C compiler. But Eskiu can call C functions via `extern`, so you can link Eskiu code with C libraries.

### When will [feature] be implemented?

Check [PHASES.md](PHASES.md) for the roadmap. Current status:
- ✅ Phase 0-4: Lexer, Parser, Type Checker, Codegen
- 🔄 Phase 5: Structs, Interfaces, Templates (in progress)
- ⏳ Phase 6+: Memory management, stdlib, async, exceptions

### Is Eskiu memory safe?

**No.** Eskiu is honest about memory: no garbage collection, no borrow checker, no safety guarantees. You manage memory explicitly, which gives you control but requires discipline. Use pointers carefully; Eskiu won't prevent:
- Use-after-free
- Buffer overflows
- Null pointer dereferences

If memory safety is critical, use Rust instead.

### Does Eskiu have generics/templates?

Generics are planned for Phase 5. Right now, use `extern` to call generic C functions or write monomorphic (single-type) versions.

### Can I use inheritance and polymorphism?

No inheritance. Instead, use **Go-style interfaces** (planned for Phase 5):

```esk
interface Reader {
    fn read(buffer: *i8, size: i32) -> i32;
}

// Any struct with a matching read() method is a Reader
```

---

## Compilation and Performance

### How fast is Eskiu?

Eskiu compiles to LLVM IR with zero hidden costs. Performance is as good as optimized C. However, the language is young, so the optimizer is not as mature as GCC/Clang. Benchmark your code.

### Can I optimize my Eskiu code?

Yes:
- Compile with `-O2` or `-O3` (LLVM optimization levels)
- Use `-emit-llvm` to see generated IR and inspect it
- Profile with `perf` (Linux) or Instruments (macOS)
- See [PERFORMANCE.md](PERFORMANCE.md) for detailed optimization tips

### What LLVM version do I need?

LLVM 14 or later. Check with `llvm-config --version`.

### Can I see the generated code?

Yes! Use `--test-codegen` to see LLVM IR:

```bash
./eskiu compile program.esk --test-codegen
```

Or emit LLVM without compiling:

```bash
./eskiu compile program.esk -emit-llvm -o program.ll
cat program.ll
```

---

## Troubleshooting

### My program compiles but crashes at runtime

You hit undefined behavior. Common causes:
- Dereferencing null pointer
- Accessing out-of-bounds memory
- Using freed memory

Use `printf` to debug, or use a debugger (`lldb` on macOS, `gdb` on Linux).

See [DEBUGGING.md](DEBUGGING.md) for detailed debugging guide.

### I get "type mismatch" error

Eskiu doesn't auto-convert types. Be explicit:

```esk
let x: i64 = 100;
let y: i32 = (i32)x;  // Cast explicitly
```

### Linker error: "undefined reference"

You're missing a C function declaration:

```esk
// WRONG: undefined reference to 'printf'
fn main() -> i32 {
    printf("hello\n");
    return 0;
}

// RIGHT: extern declares the C function
extern fn printf(format: *i8, ...) -> i32;

fn main() -> i32 {
    printf("hello\n");
    return 0;
}
```

### "Expected ';' after variable declaration"

You forgot the semicolon or type annotation:

```esk
let x: i32 = 5  // ERROR: missing ;

let x = 5       // ERROR: missing type

let x: i32 = 5; // OK
```

---

## Development and Contributing

### How do I contribute?

See [CONTRIBUTING.md](CONTRIBUTING.md) for:
- Development workflow
- How to add features
- Code style
- Testing

### How do I report a bug?

File a GitHub issue with:
1. Minimal test case (5-20 lines)
2. Output of `--test-lexer`, `--test-parser`, `--test-typechecker`
3. Expected vs. actual behavior

### Can I use Eskiu in production?

Eskiu is v0.0.1-alpha. Use at your own risk. Breaking changes are likely. Wait for v1.0 (planned 2027) for stability guarantees.

---

## The QR Decoder Project

### What's the INE QR decoder?

Our production use case: a QR decoder that currently takes 3-5 seconds to process on constrained hardware. Eskiu's goal is to achieve sub-second latency in a single, unified language.

### Why does that matter?

It proves that Eskiu can compete with hand-optimized C/C++ on real, measurable problems. Language is validated by results, not promises.

### When will the decoder ship in Eskiu?

After Phase 5 is complete (Structs, Interfaces, Templates). Current ETA: Q4 2026.

---

## Miscellaneous

### Is Eskiu open source?

Yes, MIT license.

### Who maintains Eskiu?

Started by Eduardo Dorantes (@dorantes). Contributions welcome!

### What's the pronunciation?

Eh-skee-oo. (Spanish origin: "skillful" or "dexterous"—perfect for a systems language.)

### Can I use a different build system instead of CMake?

Not officially. CMake is the only supported build system. You could write a Makefile or use Bazel if you really want, but it's unsupported.

---

## Still Have Questions?

- Check [GETTING_STARTED.md](GETTING_STARTED.md) for a complete tutorial
- Read [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) for all syntax rules
- See [DEBUGGING.md](DEBUGGING.md) for troubleshooting
- File an issue on GitHub
