---
name: Bug Report
about: Report a compiler or language bug
title: "[BUG] Brief description"
labels: bug
assignees: ''

---

## Description
Clear description of the bug. What did you expect to happen? What happened instead?

## Minimal Test Case
Smallest Eskiu program that reproduces the issue:

```eskiu
extern int printf(string fmt, ...);

int main() {
    // Your code here
    return 0;
}
```

## Steps to Reproduce
1. Save the test case to `test.esk`
2. Run: `./build/eskiuc test.esk -o test.o && clang test.o -o test && ./test`
3. What happens? (error message, crash, wrong output)

## Expected Behavior
What should happen instead?

## Compiler Output
Run with test modes and paste the output:

```bash
./build/eskiuc test.esk --test-lexer
# Output:
```

```bash
./build/eskiuc test.esk --test-parser
# Output:
```

```bash
./build/eskiuc test.esk --test-typechecker
# Output:
```

```bash
./build/eskiuc test.esk --test-codegen
# Output:
```

## System Information
- **OS:** macOS / Linux
- **LLVM version:** (run: `llvm-config --version`)
- **Compiler version:** (run: `./build/eskiuc --version`)
- **Build type:** Debug / Release

## Related Issues
Link to any related issues or PRs

---

**Note:** The more details and output you provide, the faster we can fix it!
