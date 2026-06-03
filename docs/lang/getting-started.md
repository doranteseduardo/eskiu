---
# Getting Started with Eskiu

A hands-on introduction to the Eskiu language. You will go from zero to writing and
inspecting real compiled programs in about 30 minutes.
---

## Installation

### Prerequisites

| Tool         | Minimum version | Notes                            |
| ------------ | --------------- | -------------------------------- |
| LLVM         | 17+             | Headers and libraries required   |
| CMake        | 3.20+           | Build system                     |
| C++ compiler | C++17           | GCC 7+, Clang 5+, or Apple Clang |

### macOS

```bash
brew install llvm cmake
export LLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm
```

Add the `export` to `~/.zshrc` to make it permanent. Then clone and build:

```bash
git clone https://github.com/doranteseduardo/eskiu.git
cd eskiu
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install -y cmake llvm-17-dev clang-17 build-essential
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Verify

```bash
./build/eskiuc --version
# Eskiu 0.0.1 (LLVM 17.x.x)
```

---

## Your First Program

The canonical `hello.esk` example with `add` and `printf`. Run `--test-codegen` to see the LLVM IR, which shows string literals emitted as globals, stack slots as `alloca`, and `add` compiling to a single `add i32` instruction.

---

## Variables and Types

Both C-style (`int x = 42;`) and `let`-style (`let x: int = 42;`) declarations are valid and equivalent. Full type table covers `bool` through `double`, all `intN`/`uintN` variants, and pointer types (`let ptr: *int = null;`). The parser accepts both `*T` and `T*`.

---

## Functions

Return type first, C convention. `extern` emits an LLVM `declare` for C interop. `...` in the parameter list marks variadic functions (required for `printf`).

---

## Control Flow

- `if`/`else` — condition must be parenthesized, `else` is optional
- `while` — standard condition loop
- `for` — loop variable must be declared before the header; init clause assigns to an existing variable
- `break` — parsed and type-checked; codegen wiring in progress (emits a warning but does not crash)

---

## Structs (Preview — Phase 5)

Fully parsed and type-checked. `--test-parser` shows the full AST including `MemberExpr` nodes. `--test-typechecker` catches invalid field names (`struct 'Point' has no member 'z'`). Codegen for member access is Phase 5.

---

## Using the Test Modes

| Flag                 | Phase          | Output                     | When to use                        |
| -------------------- | -------------- | -------------------------- | ---------------------------------- |
| `--test-lexer`       | Lexer          | Token stream with line/col | Debugging tokenization             |
| `--test-parser`      | Parser         | Indented AST               | Checking syntax structure          |
| `--test-typechecker` | Type checker   | Errors or success          | Validating types and struct fields |
| `--test-codegen`     | Code generator | LLVM IR                    | Seeing final output                |

`--test-lexer` on `int x = 5 + 3;` produces 7 tokens with exact line/column positions. `--test-typechecker` on a struct with a bad field access produces a named error before any IR is generated.

---

## What's Next

- Language reference: `docs/lang/spec.md`
- Contributing: `docs/dev/`
