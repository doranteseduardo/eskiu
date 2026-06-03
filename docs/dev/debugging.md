# Debugging the Eskiu Compiler

How to diagnose issues in the lexer, parser, type checker, and code generator.

## Test Modes Reference

| Flag | What it does | What it outputs | When to use |
|---|---|---|---|
| `--test-lexer` | Tokenizes source, stops after lexing | One token per line with type, lexeme, line, and column | Diagnosing unrecognized tokens, bad keyword handling, or unexpected token splits |
| `--test-parser` | Tokenizes + parses, stops before type checking | Indented AST via the ASTVisitor/ASTPrinter pass | Diagnosing missing nodes, wrong nesting, or silently skipped declarations |
| `--test-typechecker` | Tokenizes + parses + type checks | Errors prefixed with file:line:col, or "Type checking succeeded!" | Diagnosing undefined variables, type mismatches, struct field errors |
| `--test-codegen` | Full pipeline except linking | LLVM IR text via `module->print()` | Diagnosing wrong IR types, missing terminators, LLVM verification failures |

All flags read from a `.esk` source file as the first positional argument:

```bash
./eskiuc program.esk --test-lexer
./eskiuc program.esk --test-parser
./eskiuc program.esk --test-typechecker
./eskiuc program.esk --test-codegen
```

## Error Format

**Compiler errors** (parser, type checker, main driver):

```
error: file.esk:line:col: message
```

**Type checker errors** — source location tracking is not yet wired into the type checker. All type errors currently report `0:0` as line and column:

```
error: file.esk:0:0: undefined variable 'x'
```

Search by message text rather than location when triaging type checker output.

**LLVM verification failures** — emitted after codegen if `llvm::verifyModule` finds an inconsistency:

```
LLVM verification failed: <message from LLVM>
```

The compiler returns `nullptr` from the codegen entry point and prints the LLVM diagnostic. The IR may still be partially visible above it.

**Runtime parse errors** — the parser throws `std::runtime_error` on unrecoverable syntax problems. The outer loop in `parseProgram()` catches these and attempts to skip to the next `;` before resuming. If the exception propagates to `main`, the process exits with a non-zero status and prints the `what()` string.

## Debugging a Lexer Issue

Run `--test-lexer` to inspect the full token stream before any parsing occurs:

```bash
./eskiuc program.esk --test-lexer
```

Each line of output shows the token type, lexeme, and source position. Look for tokens with type `UNKNOWN` (the character is not in the lexer's recognized set), a keyword that appears as `IDENT` instead of its keyword type (the keyword was not added to both `TokenType` in `lexer/lexer.h` and the `keywords` map in `lexer/lexer.cpp`), or incorrect line/column numbers (usually caused by a missing newline advance in the lexer loop).

The most common lexer mistake when adding a new keyword is updating the enum but forgetting to add the string-to-token mapping, or vice versa.

## Debugging a Parser Issue

Run `--test-parser` to see the AST the recursive-descent parser produced:

```bash
./eskiuc program.esk --test-parser
```

The `ASTPrinter` visitor walks the tree and prints each node with indentation proportional to depth. If the printer crashes or produces a truncated tree, the parser returned a malformed node.

**Silent backtracking** — the parser uses a `savePos`/`restorePos` pattern to speculatively parse alternatives. When a branch fails, it silently restores the token cursor. There is no logging for these backtracks by default. To find where a parse attempt is failing, add a temporary `std::cerr` in the relevant `parseFoo()` function at the point of failure.

**Silent declaration skipping** — `parseProgram()` wraps each top-level declaration attempt in a `try/catch`. If a declaration throws, the outer catch discards it and resyncs at the next `;`. A declaration that disappears from the AST without an error message was swallowed here.

## Debugging a Type Checker Issue

Run `--test-typechecker` to see all semantic errors:

```bash
./eskiuc program.esk --test-typechecker
```

All errors report `0:0` for location. Triage by message text, not by location.

**`expressionTypes` cache** — the type checker populates a `std::unordered_map<ASTNode*, std::string>` as it visits expressions. If `getExpressionType()` returns `"unknown"` for a node, the visitor for that node type was never called, or the expression was not added to the cache. Check whether the visitor method calls `expressionTypes[node] = resolvedType` before returning.

**Struct registration** — structs are registered in a first pass before the main visitor runs. If you get "undefined struct 'X'" for a struct that is declared in the file, verify that the first-pass loop is visiting `StructDecl` nodes and inserting into the struct registry.

## Debugging a Codegen Issue

Run `--test-codegen` to emit the LLVM IR and trigger module verification:

```bash
./eskiuc program.esk --test-codegen
```

`llvm::verifyModule` runs automatically after generation. If verification fails, the diagnostic is printed and the function returns `nullptr`. The IR is still printed to stdout before the error.

**`exprValueStack` invariant** — every `visit()` override that produces a value must push exactly one `llvm::Value*` onto the stack. A push-count mismatch causes the wrong value to be consumed by the parent node, or a null dereference.

**Type width mismatches** — `VarDecl` coerces the initializer value to the declared type width before storing. Assignment to struct fields does not yet apply this coercion. Storing an `i32` into an `i8` field will fail LLVM verification with a store type mismatch.

**Undefined variable in IR** — `lookupSymbol()` returns `nullptr` if the name is not found. Check that the variable was declared before use and that the scope stack is being pushed/popped correctly around block boundaries.

**Missing block terminator** — every basic block must end with a terminator (`ret`, `br`, etc.). A function body that falls off the end without a `return` will fail verification with "basic block does not have terminator".

## Common Errors and Fixes

| Error message | Likely cause | Fix |
|---|---|---|
| `Expected declaration` | Unexpected token at the top level of the file | Remove stray characters; only function, struct, extern, or variable declarations are valid at top level |
| `Expected ';'` | Missing semicolon at the end of a statement | Add `;`; the parser throws and the outer catch skips to the next `;`, so subsequent lines may also appear missing from the AST |
| `undefined variable 'x'` | Variable used before declaration, or referenced outside its scope | Declare the variable before use; check that it is not declared inside a block that has already exited |
| `undefined function 'f'` | Function called without a prior declaration or `extern` | Add an `extern` declaration at the top of the file, or move the function definition before its call site |
| `struct 'X' has no member 'y'` | Field name typo, or the expression has the wrong struct type | Check the spelling of the field in the `struct` declaration; run `--test-typechecker` to see what type the receiver expression resolved to |
| `LLVM verification failed` | Type width mismatch in a store or call, or a basic block missing its terminator | Run `--test-codegen` and read the LLVM diagnostic; common causes are storing wrong-width values into struct fields or missing `return` at end of function |

## Testing a New Feature

1. Write a `.esk` test file in `test/` or `examples/` that exercises only the new feature.
2. Run `--test-lexer` — confirm all tokens in the new syntax are recognized with the correct token types.
3. Run `--test-parser` — confirm the AST shape matches what the parser is supposed to produce; check nesting and node types.
4. Run `--test-typechecker` — confirm there are no false positives on valid code and that expected errors on invalid code are reported.
5. Run `--test-codegen` — confirm the emitted IR is well-typed, LLVM verification passes, and the IR structure matches the intended semantics.
