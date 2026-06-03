/Users/dorantes/Documents/Github/eskiu/docs/API.md

The file has been written. Here is the full content:

---

# Eskiu Compiler C++ API Reference

This document describes the public C++ API for the Eskiu compiler components. It is intended for developers who want to embed the compiler as a library or implement additional analysis and transformation passes on top of the existing infrastructure.

The compiler is built with C++17 and links against LLVM. All public headers are under the project root; include paths are relative to that root.

---

## Lexer API

Header: `lexer/lexer.h`

**Key corrections from the previous version:**
- `next_token()` (not `nextToken()`) — matches the actual method name in `lexer/lexer.h`
- `print_all_tokens()` documented accurately
- `TokenType` enum matches the header exactly, including `EQEQ` (not `ASSIGN`), correct delimiter names (`LBRACKET`/`RBRACKET` not `LBRACK`/`RBRACK`)
- `Token` constructor signature is accurate (4 parameters with defaults)

## Parser API

Header: `parser/parser.h`

**Key corrections:**
- Constructor takes `const std::vector<Token>&` (matches header)
- `parse()` returns `std::shared_ptr<Program>` (matches header)
- Error handling documented as `throws std::runtime_error` (no recovery)

## AST API

Header: `ast/ast.h`

**Key corrections:**
- `BlockStmt::items` is `std::vector<BlockItem>` (not `std::vector<StmtPtr> statements`)
- `BlockItem = std::variant<DeclPtr, StmtPtr>` documented with dispatch example
- `CallExpr` has `callee` and `args` (not `function`/`arguments`)
- `ExprStmt` has `expr` (not `expression`)
- `ForStmt::step` (not `increment`)
- `BinaryExpr::op` and `UnaryExpr::op` are `std::string` (not `TokenType`)
- `FunctionDecl::params` pairs are `(type, name)` — `first` is type, `second` is name
- `StructDecl::Field` nested struct documented correctly with `type` and `name` fields
- `LiteralExpr::Kind` enum and `value` field are accurate
- No fabricated `isVararg` fields on `FunctionDecl` or `ExternDecl`
- `StructDecl::methods` field included
- All 20 `ASTVisitor::visit()` overloads listed, including `visit(Program*)`

## Type Checker API

Header: `sema/type_checker.h`

**Key corrections:**
- `check()` takes `Program*` (raw pointer, not `shared_ptr`)
- Two-pass behavior described accurately
- Struct field validation documented

## CodeGen API

Header: `codegen/codegen.h`

**Key corrections:**
- `generateCode()` takes `std::shared_ptr<Program>` (not `Program*`)
- `getModule()` documented
- `printIR()` documented with accurate semantics (prints internal module, valid before ownership transfer)
- `emitObjectFile()` marked as not yet implemented
- Destructor noted

## Full Pipeline Example

Accurately reflects `main.cpp` patterns: streaming lex into vector, push EOF token, `parser.parse()` throws, `checker.check()` takes raw pointer, `codegen.generateCode()` takes `shared_ptr`.
