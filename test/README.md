# Eskiu Test Directory

Test files and utilities for verifying the Eskiu compiler. Testing is manual: there is no automated test runner. Each `.esk` file exercises specific language features, and you verify them by passing compiler flags.

## Test Files

- **test_hello.esk** — Simple hello world program using `int` types and variadic extern. Good smoke-test for a full compile run.
- **test_correct_syntax.esk** — Core syntax test covering extern declarations with pointer parameters (`i8*`), function definitions, variable declarations inside a block, and nested function calls. The canonical file to run through all compiler phases.
- **test_extern_only.esk** — Minimal single-line test for extern declaration parsing. Useful for isolating the lexer/parser on extern alone.
- **test_parser_only** — Standalone parser verification tool (binary or source, see below).

## Running Tests

Build the compiler first:

```bash
cmake -B build && cmake --build build
```

Then run a test file through any phase with the corresponding flag:

```bash
# Lexer only
./build/eskiuc test/test_correct_syntax.esk --test-lexer

# Parser only
./build/eskiuc test/test_correct_syntax.esk --test-parser

# Type checker
./build/eskiuc test/test_correct_syntax.esk --test-typechecker

# Code generation (emits LLVM IR)
./build/eskiuc test/test_correct_syntax.esk --test-codegen
```

Run all phases in sequence to confirm nothing regressed:

```bash
for phase in --test-lexer --test-parser --test-typechecker --test-codegen; do
    echo "=== $phase ===" && ./build/eskiuc test/test_correct_syntax.esk $phase
done
```

## Standalone Parser Tool

`test_parser_only` is a standalone binary that exercises the lexer + parser + AST printer without pulling in sema or codegen. Build it from source:

```bash
g++ -std=c++17 -I.. -o test_parser_only \
  test_parser_main.cpp \
  ../lexer/lexer.cpp \
  ../parser/parser.cpp \
  ../ast/ast.cpp \
  ../ast/ast_printer.cpp
```

Run it:

```bash
./test_parser_only test_correct_syntax.esk
```

## Syntax Reminder

Both declaration styles are valid in Eskiu:

```esk
// C-style (original)
int x = 5;

// Let-style (also supported)
let x: int = 5;
```

Both forms pass the type checker.

### Function declaration
```esk
int add(int a, int b) {
    return a + b;
}
```

### Extern declaration
```esk
extern i32 printf(i8* fmt);
```

### Struct with method
```esk
struct Point {
    int x;
    int y;
}

int Point.distance(Point other) {
    return 0;
}
```

### Template
```esk
fn identity<T>(T value) -> T {
    return value;
}
```

### Interface (structural typing)
```esk
interface Printable {
    fn print() -> void;
}
```

## Adding Tests for New Features

1. Create `test/test_<feature>.esk`.
2. Open with a comment block describing what is being tested and the expected outcome.
3. Use the simplest code that exercises the feature — do not combine multiple unrelated features in one file.
4. Run through all four phases (`--test-lexer` through `--test-codegen`) and note which phases are expected to pass.
5. Add an entry in the coverage table below.

## Recommended Test Flow for a New Feature

1. Write the grammar change (parser).
2. Add a minimal `.esk` file in `test/` that uses the new syntax.
3. Confirm `--test-parser` prints the correct AST node.
4. Add type-checker handling. Confirm `--test-typechecker` passes.
5. Add codegen. Confirm `--test-codegen` emits correct LLVM IR.
6. Optionally link and run the resulting binary end-to-end.
7. Update the coverage table below and commit both the `.esk` file and the README change together.

## Test Coverage Index

| File | Lexer | Parser | TypeChecker | Codegen | Notes |
|------|-------|--------|-------------|---------|-------|
| test_hello.esk | OK | OK | OK | OK | Uses `int` and variadic extern |
| test_correct_syntax.esk | OK | OK | OK | OK | Primary regression file |
| test_extern_only.esk | OK | OK | OK | OK | Single extern line |

Add a row here whenever you add a new test file.

## Known Gaps

- `switch/case` passes codegen but the type checker does not validate case expression types.
- Interface dispatch vtable uses `void` return type — typed return values not yet wired.

---

**Last Updated:** June 3, 2026
