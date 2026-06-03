# Debugging the Eskiu Compiler

How to diagnose issues in the lexer, parser, type checker, and code generator.

## Test Modes Reference

| Flag | What it does | What it outputs | When to use |
|---|---|---|---|
| `--test-lexer` | Tokenizes source, stops after lexing | One token per line with type, lexeme, line, and column | Diagnosing unrecognized tokens, bad keyword handling, or unexpected token splits |
| `--test-parser` | Tokenizes + parses, stops before type checking | Indented AST via the ASTVisitor/ASTPrinter pass | Diagnosing missing nodes, wrong nesting, or silently skipped declarations |
| `--test-typechecker` | Tokenizes + parses + type checks | Errors prefixed with `file:line:col`, or "Type checking succeeded!" | Diagnosing undefined variables, type mismatches, struct field errors |
| `--test-codegen` | Full pipeline except linking | LLVM IR text via `module->print()` | Diagnosing wrong IR types, missing terminators, LLVM verification failures |
...
