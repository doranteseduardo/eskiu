# Debugging Guide

Learn how to understand compiler errors, use test modes effectively, and diagnose issues.

## Table of Contents

1. [Reading Error Messages](#reading-error-messages)
2. [Test Modes](#test-modes)
3. [Debugging Compilation](#debugging-compilation)
4. [Debugging Runtime](#debugging-runtime)
5. [Common Errors](#common-errors)

---

## Reading Error Messages

Eskiu error messages follow a standard format:

```
file.esk:line:column: error: message
    context line
    ^^^^^^^
```

### Example

**Code:**
```esk
fn main() -> i32 {
    let x: i32 = 42;
    return x + "string";  // ERROR: type mismatch
}
```

**Error output:**
```
example.esk:3:11: error: type mismatch: expected i32, got string in binary operator
    return x + "string";
           ^
```

**Reading it:**
- **File:** `example.esk`
- **Location:** Line 3, Column 11 (start of the expression)
- **Type:** `error` (fatal; compilation stops)
- **Message:** Describes what's wrong and what was expected
- **Context:** Shows the exact line with a caret (`^`) pointing to the problem

---

## Test Modes

Eskiu exposes four compiler stages as test modes. Use them to pinpoint where problems occur.

### 1. Lexer Test (`--test-lexer`)

**What it does:** Tokenizes your code, stops after lexing  
**Use when:** Debugging syntax or unrecognized tokens

```bash
./eskiu compile program.esk --test-lexer
```

**Example output:**
```
Tokenizing: program.esk
========================================================
Line 1, Col  1          EXTERN  'extern'
Line 1, Col  8              FN  'fn'
Line 1, Col 11           IDENT  'printf'
Line 1, Col 18          LPAREN  '('
Line 1, Col 19            MUL  '*'
Line 1, Col 20              I8  'i8'
Line 1, Col 23          COMMA  ','
Line 1, Col 25            ELLIPSIS  '...'
Line 1, Col 28          RPAREN  ')'
Line 1, Col 30          ARROW  '->'
Line 1, Col 32              I32  'i32'
Line 1, Col 35         SEMICOLON  ';'
========================================================
Total tokens: 13
```

**What to look for:**
- Are all tokens recognized? (No `UNKNOWN` or corrupted tokens?)
- Is the token type correct? (Is `+` really `PLUS`, not `UNKNOWN`?)
- Are line/column numbers sensible?

**Common issues:**
- **Wrong token type:** Typo in source or lexer bug
- **UNKNOWN token:** Unsupported character (lexer doesn't recognize it)
- **Missing tokens:** Whitespace issues or skipped characters

### 2. Parser Test (`--test-parser`)

**What it does:** Parses tokens into an Abstract Syntax Tree, stops before type checking  
**Use when:** Debugging syntax errors or incorrect parse structure

```bash
./eskiu compile program.esk --test-parser
```

**Example output:**
```
Program
  ExternDecl: printf -> i32
    Parameters:
      i8* format
      ... ...
  FunctionDecl: main -> i32
    Parameters:
    Body:
      BlockStmt
        Statements:
          ReturnStmt
            BinaryExpr: +
              Left:
                IdentExpr: x
              Right:
                LiteralExpr(INT): 5
```

**What to look for:**
- Is the structure correct? (Functions in right place?)
- Are parameters parsed correctly?
- Is the operator tree right? (Left/right operands make sense?)

**Common issues:**
- **Mismatched structure:** Parser recovered from error; check earlier errors
- **Missing nodes:** Some constructs not parsed (might be unimplemented)
- **Wrong nesting:** Control flow structure incorrect

### 3. Type Checker Test (`--test-typechecker`)

**What it does:** Validates types, scopes, and symbols; reports semantic errors  
**Use when:** Debugging type mismatches, undefined variables, scope issues

```bash
./eskiu compile program.esk --test-typechecker
```

**Output if successful:**
```
Type checking: program.esk
========================================================
========================================================
Type checking succeeded!
```

**Output if errors:**
```
Type checking: program.esk
========================================================
program.esk:3:11: error: type mismatch: expected i32, got string
program.esk:5:5: error: undefined variable 'y'
========================================================
Type checking failed with 2 errors.
```

**What to look for:**
- Are all variables defined before use?
- Do types match in assignments and operations?
- Are function calls valid?
- Are struct members valid?

**Common issues:**
- **"undefined variable X":** Variable not declared, or out of scope
- **"undefined function X":** Function not declared with `fn` or `extern`
- **"type mismatch":** You're assigning/using a value of the wrong type
- **"undefined struct 'X'":** Struct not declared before use
- **"no member 'X' in struct 'Y'":** Accessing invalid struct field

### 4. Code Generation Test (`--test-codegen`)

**What it does:** Generates LLVM IR; shows the low-level representation  
**Use when:** Debugging codegen bugs or understanding what the compiler emits

```bash
./eskiu compile program.esk --test-codegen
```

**Example output:**
```llvm
; ModuleID = 'eskiu'
source_filename = "eskiu"

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %0 = call i32 (ptr, ...) @printf(ptr @str.0)
  ret i32 0
}

@str.0 = private unnamed_addr constant [8 x i8] c"Hello\0a\00"
```

**What to look for:**
- Are function signatures correct?
- Are types translated correctly? (i32 → i32, struct → struct)
- Are calls to C functions declared?

**Common issues:**
- **Missing declarations:** C functions not declared with `extern`
- **Type mismatches in IR:** Codegen bug or type checker bug
- **Infinite recursion in generation:** Compiler infinite loop

---

## Debugging Compilation

### Workflow: Using Test Modes to Locate Issues

When you hit a compilation error, follow this sequence:

```bash
# 1. Check lexing
./eskiu compile program.esk --test-lexer

# 2. Check parsing
./eskiu compile program.esk --test-parser

# 3. Check type checking
./eskiu compile program.esk --test-typechecker

# 4. Check codegen (if types pass)
./eskiu compile program.esk --test-codegen

# 5. Try full compilation
./eskiu compile program.esk -o program
```

**Each test mode should succeed** if the stage before it passed. If `--test-parser` fails but `--test-lexer` passed, the bug is in the parser.

### Example: Debugging a Real Compilation Error

**Your program (`test.esk`):**
```esk
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    let x: i32 = add(5, "10");
    return 0;
}
```

**Try compilation:**
```bash
./eskiu compile test.esk -o test
```

**Error:**
```
test.esk:6:19: error: type mismatch: expected i32, got string
```

**Verify the error with type checker:**
```bash
./eskiu compile test.esk --test-typechecker
```

Same error. This tells you:
1. Lexer and parser work fine (not testing those)
2. Error is in type checking (function expects i32, got string)
3. Codegen never runs because type check failed

**Fix:** Change `"10"` to `10`

```esk
let x: i32 = add(5, 10);
```

**Verify fix:**
```bash
./eskiu compile test.esk --test-typechecker
# Type checking succeeded!

./eskiu compile test.esk -o test
# Compilation succeeded
```

---

## Debugging Runtime

### Segmentation Faults and Crashes

If your compiled program crashes at runtime, you hit undefined behavior. Common causes:

**1. Null Pointer Dereference**

```esk
let ptr: *i32 = null;
let x = *ptr;  // CRASH: dereferencing null
```

**Debug with:**
- Add `printf` statements before the crash to narrow down the location
- Use a debugger: `lldb ./program` (macOS) or `gdb ./program` (Linux)

**2. Buffer Overflow**

```esk
let arr: [10]i32 = {0};
arr[100] = 5;  // UNDEFINED: no bounds check
```

No fix from Eskiu; you must validate bounds manually.

**3. Use-After-Free**

```esk
let ptr: *i32 = ...;
free(ptr);
let x = *ptr;  // CRASH: using freed pointer
```

Track pointer lifetime; don't dereference after freeing.

### Using a Debugger

**macOS:**
```bash
lldb ./program
(lldb) run arg1 arg2
(lldb) bt           # Print backtrace where crash occurred
(lldb) p variable   # Print variable value
```

**Linux:**
```bash
gdb ./program
(gdb) run arg1 arg2
(gdb) bt
(gdb) p variable
```

---

## Common Errors

### Lexer Errors

#### "Unknown token 'X'"

Lexer doesn't recognize the character. Possible causes:
- Typo in source code
- Using unsupported character (e.g., `@` for non-`extern`)
- Unicode issues

**Fix:** Check spelling; use ASCII only for now.

### Parser Errors

#### "Expected 'X' after 'Y'"

Parser found something unexpected. Examples:
- Missing semicolon: `let x: i32 = 5` (should be `let x: i32 = 5;`)
- Missing type: `let x = 5` (should be `let x: i32 = 5`)
- Mismatched brackets: `fn foo() { return 0` (missing `}`)

**Fix:** Follow syntax rules in [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md)

### Type Checker Errors

#### "undefined variable 'X'"

Variable `X` not declared or out of scope.

```esk
fn main() -> i32 {
    printf("%d\n", x);  // ERROR: x not declared
    return 0;
}
```

**Fix:** Declare before use:
```esk
fn main() -> i32 {
    let x: i32 = 42;
    printf("%d\n", x);  // OK
    return 0;
}
```

#### "undefined function 'X'"

Function `X` not declared. Must use `fn` or `extern`.

```esk
fn main() -> i32 {
    return printf("hello");  // ERROR: printf not declared
}
```

**Fix:** Declare the function:
```esk
extern fn printf(format: *i8, ...) -> i32;

fn main() -> i32 {
    return printf("hello\n");
}
```

#### "type mismatch: expected T1, got T2"

You're assigning or using a value of the wrong type.

```esk
let x: i32 = 42.5;  // ERROR: assigning f64 to i32
```

**Fix:** Cast explicitly or change type:
```esk
let x: i32 = (i32)42.5;  // OK: explicit cast
let x: f64 = 42.5;       // OK: use f64
```

#### "struct 'X' has no member 'Y'"

Struct `X` doesn't have a field `Y`.

```esk
struct Point {
    x: i32
    y: i32
}

fn main() -> i32 {
    let p: Point = {0};
    return p.z;  // ERROR: Point has no 'z'
}
```

**Fix:** Use correct member name:
```esk
return p.x;  // OK
```

### Linker Errors

#### "undefined reference to 'printf'"

The linker can't find the C library. Usually means LLVM linking is misconfigured.

**Fix:** Ensure LLVM is properly installed and `llvm-config` works:
```bash
llvm-config --libs
# Should output something like: -lLLVM
```

#### "multiple definition of 'foo'"

You've defined the same function twice.

```esk
fn foo() -> i32 { return 1; }
fn foo() -> i32 { return 2; }  // ERROR: duplicate
```

**Fix:** Rename one or remove the duplicate.

---

## Tips and Tricks

### Minimize Test Cases

When reporting a bug, create the smallest program that reproduces it:

```esk
// BAD: 200 lines, hard to debug
// GOOD:
fn foo(x: i32) -> i32 {
    return x + 1;
}

fn main() -> i32 {
    return foo("not an int");  // ERROR: minimal repro
}
```

### Add Debug Output

Use `printf` to trace execution:

```esk
extern fn printf(format: *i8, ...) -> i32;

fn main() -> i32 {
    let x: i32 = 42;
    printf("before: x=%d\n", x);
    x = x + 1;
    printf("after: x=%d\n", x);
    return 0;
}
```

### Use Test Mode Output in Bug Reports

When filing a bug, include `--test-*` output:

```bash
./eskiu compile buggy.esk --test-typechecker 2>&1 | tee bug-report.txt
# File bug-report.txt on GitHub
```

---

## Getting Help

- Check [FAQ.md](FAQ.md) for common questions
- Read [GETTING_STARTED.md](GETTING_STARTED.md) for language basics
- File a bug on GitHub with:
  - Your `.esk` file (minimal example)
  - Output of `--test-lexer`, `--test-parser`, `--test-typechecker`
  - Expected vs. actual behavior
