# Tests

Compiler regression tests. Each file exercises a specific feature or edge case.

| File | What it tests |
|------|---------------|
| `lambdas.esk` | Anonymous functions, `fn(T)->R` types, higher-order functions |
| `test_struct.esk` | Struct field access |
| `test_struct_error.esk` | Type checker rejects access to undefined field (expected error) |

Run all tests:

```bash
# Should compile and run successfully
./build/eskiuc tests/lambdas.esk && clang tests/lambdas.esk.o -o /tmp/t && /tmp/t

# Should emit a type error (not compile)
./build/eskiuc tests/test_struct_error.esk --test-typechecker
```
