# Eskiu Lang

> Systems programming language with C-style syntax, compiled to LLVM. No GC, no borrow checker—just performance and honesty.

**Status:** v0.0.1-alpha · Phases 0–4 complete · [RELEASES.md](RELEASES.md)

---

## 5-Minute Start

```bash
# Build
mkdir build && cd build
cmake .. && make -j4

# Compile & run
./eskiu compile ../examples/hello.esk -o hello
./hello
```

See [QUICKSTART.md](QUICKSTART.md) for full guide.

---

## Why Eskiu?

- **Familiar:** C-style syntax. Read it immediately.
- **Fast:** Compiles to LLVM IR. Zero hidden costs.
- **Explicit:** Stack by default. You control memory.
- **Practical:** Go-style interfaces. No inheritance.

**Goal:** Port INE QR decoder from 3–5 seconds to <1 second. Real use case. Real constraints.

---

## Documentation

**New here?** → [QUICKSTART.md](QUICKSTART.md) (5 min)  
**Learn the language?** → [GETTING_STARTED.md](docs/GETTING_STARTED.md) (30 min)  
**Set it up?** → [BUILD.md](docs/BUILD.md) (macOS, Linux, Alpine)  
**Debugging?** → [DEBUGGING.md](docs/DEBUGGING.md) (test modes, errors)

**Full docs:** [INDEX.md](INDEX.md) — Navigation hub for all 18+ guides.

---

## Example

```esk
extern fn printf(format: *i8, ...) -> i32;

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    printf("5 + 3 = %d\n", add(5, 3));
    return 0;
}
```

---

## Architecture

```
Source → Lexer → Parser → Type Checker → Codegen → LLVM → Binary
         ✅      ✅       ✅             ✅        ✅
```

**How it works:** [ARCHITECTURE.md](docs/ARCHITECTURE.md)  
**Why these choices:** [DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md)

---

## Roadmap

| Phase | Status | Focus |
|-------|--------|-------|
| 0–4 | ✅ Complete | Lexer, Parser, Type Checker, Codegen |
| 5 | 🔄 Next | Structs, interfaces, templates |
| 6–7 | ⏳ Planned | Memory mgmt, stdlib |
| 8+ | ⏳ Future | Lambdas, exceptions, async |

Full: [PHASES.md](docs/PHASES.md)

---

## Contributing

File a bug: [GitHub Issues](https://github.com/yourusername/eskiu/issues)  
Want to add features? See [CONTRIBUTING.md](docs/CONTRIBUTING.md).

---

**v0.0.1-alpha · LLVM 22+ · C++17 · MIT License**
