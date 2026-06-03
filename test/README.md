# Eskiu Test Files

Test files and utilities for verifying the Eskiu compiler.

## Test Files

### Parser Tests

- **test_hello.esk** — Simple hello world program (copy of examples/hello.esk)
- **test_correct_syntax.esk** — Demonstrates correct C-style syntax
  - Extern declaration with pointer parameters
  - Function definitions
  - Variable declarations inside function body
  - Nested function calls
  
- **test_extern_only.esk** — Minimal test for extern declaration parsing

## Parser Test Tool

**test_parser_only** — Standalone parser verification tool

Build it locally:
```bash
g++ -std=c++17 -I.. -o test_parser_only \
  test_parser_main.cpp \
  ../lexer/lexer.cpp \
  ../parser/parser.cpp \
  ../ast/ast.cpp \
  ../ast/ast_printer.cpp
```

Usage:
```bash
./test_parser_only test_correct_syntax.esk
```

Expected output:
```
Program
  ExternDecl: printf → i32
  FunctionDecl: add → i32
    BlockStmt
      ...
  FunctionDecl: main → i32
    BlockStmt
      VarDecl: x = 5
      VarDecl: y = 10
      ...
```

## Test Categories

### Syntax Tests

Files that verify specific language features:

- **Extern declarations:** test_extern_only.esk
- **Functions:** test_correct_syntax.esk
- **Variables in blocks:** test_correct_syntax.esk
- **C-style pointers:** test_correct_syntax.esk

### Compiler Phases

Run tests with the main compiler:

```bash
# Test lexer
./build/eskiuc test/test_correct_syntax.esk --test-lexer

# Test parser
./build/eskiuc test/test_correct_syntax.esk --test-parser

# Test type checker
./build/eskiuc test/test_correct_syntax.esk --test-typechecker

# Test codegen
./build/eskiuc test/test_correct_syntax.esk --test-codegen
```

## Adding New Tests

When adding new test files:

1. Name them clearly: `test_<feature>.esk`
2. Include comments explaining what's being tested
3. Ensure they use **C-style syntax** (not Rust-style)
4. Add a description in this README

Example:
```esk
// Test: Variable declaration with initialization
i32 x = 42;
```

## Syntax Reminder: C-Style (Not Rust)

### ❌ Wrong (Rust-style)
```esk
fn main() -> i32 {
    let x: i32 = 5;
    return 0;
}
```

### ✅ Correct (C-style)
```esk
i32 main() {
    i32 x = 5;
    return 0;
}
```

## Known Issues

None currently. All test files pass parser validation.

## Maintenance

When you update the compiler:
- Re-run all tests to ensure parsing still works
- Add new tests for new language features
- Update this README with new test files

---

**Last Updated:** June 3, 2026
