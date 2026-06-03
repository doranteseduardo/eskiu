# Eskiu Compiler — Architecture Reference

Internal reference for compiler contributors. Assumes familiarity with C++17 and LLVM IRBuilder.

---

## Pipeline Overview

```
Source (.esk)
    │
    ▼
┌─────────┐   Token stream (vector<Token>)
│  Lexer  │──────────────────────────────────►
└─────────┘                                  │
                                             ▼
                                       ┌──────────┐   shared_ptr<Program>
                                       │  Parser  │──────────────────────►
                                       └──────────┘                       │
                                                                          ▼
                                                                  ┌──────────────┐
                                                                  │ Type Checker │── errors → stderr
                                                                  └──────────────┘
                                                                          │ validated AST
                                                                          ▼
                                                                  ┌─────────┐   llvm::Module
                                                                  │ Codegen │──────────────► LLVM IR
                                                                  └─────────┘
```

| Stage | File(s) | Responsibility |
|---|---|---|
| Lexer | `lexer/lexer.cpp` | Converts raw source bytes into a flat `Token` stream with line/column positions |
| Parser | `parser/parser.cpp` | Recursive-descent; produces a `shared_ptr<Program>` AST from the token stream |
| Type Checker | `sema/type_checker.cpp` | Two-pass visitor; registers structs and function signatures, then validates types and scopes |
| Codegen | `codegen/codegen.cpp` | Visitor over the validated AST; emits LLVM IR via `IRBuilder<>` |

**How they compose.** `main.cpp` runs each stage in sequence. The lexer is invoked to exhaustion before parsing begins — the full token stream is materialized into `std::vector<Token>` and handed to `Parser`. The parser returns `shared_ptr<Program>`. The type checker and codegen each take a raw `Program*` or `shared_ptr<Program>` and walk the tree through the visitor interface. No stage modifies the AST; they only read it and produce output (errors or LLVM values).

---

## Visitor Pattern

### Why all passes implement ASTVisitor

Every analysis or code generation pass that must touch the entire tree implements `ASTVisitor`. This keeps traversal logic out of the AST nodes themselves and allows passes (type checker, codegen, printer) to be added without modifying `ast/ast.h`.

### How accept()/visit() dispatch works

Each concrete AST node overrides `accept(ASTVisitor* v)` and calls `v->visit(this)`. This is the classic double-dispatch: the node's static type selects `visit()` overload at compile time via the node's own `accept` implementation.

```cpp
// In ast.cpp — every node follows this pattern:
void BinaryExpr::accept(ASTVisitor* visitor) {
    visitor->visit(this);
}
```

The visitor declares a pure-virtual `visit()` overload for every concrete node type (20 overloads in `ASTVisitor`). Any new concrete node must add an overload to `ASTVisitor` and all existing implementors.

### How to add a new node (5-step checklist)

1. Declare the class in `ast/ast.h`, inheriting from `Expr`, `Stmt`, or `Decl`. Add the `accept` override declaration.
2. Implement `accept` in `ast/ast.cpp`: `visitor->visit(this);`.
3. Add `virtual void visit(MyNewNode* node) = 0;` to `ASTVisitor` in `ast/ast.h`.
4. Add a stub implementation to every `ASTVisitor` subclass: `ASTPrinter`, `TypeChecker`, `CodeGen`.
5. Wire the parse rule in `parser/parser.cpp` to construct and return the new node.

### Minimal visitor implementation

```cpp
class MyPass : public ASTVisitor {
public:
    void visit(Program* node) override {
        for (auto& decl : node->declarations) decl->accept(this);
    }
    void visit(FunctionDecl* node) override { /* ... */ }
    void visit(VarDecl* node) override      { /* ... */ }
    // ... one override per concrete node type
};
```

---

## Lexer (`lexer/`)

### What `Lexer::next_token()` returns

A single `Token` struct with four fields:

```cpp
struct Token {
    TokenType type;    // enum class value
    std::string value; // raw text ("42", "hello", "+", ...)
    int line;          // 1-based
    int column;        // 1-based
};
```

The caller drives the lexer by calling `next_token()` in a loop until `TokenType::EOF_TOKEN` is returned. There is no internal lookahead buffer; each call consumes exactly one token from the source.

### TokenType enum categories

| Category | Examples |
|---|---|
| Keywords | `LET`, `INT`, `FLOAT`, `STRUCT`, `EXTERN`, `FOR`, `WHILE`, `IF`, `RETURN`, `BREAK`, `INT8`…`UINT64` |
| Operators | `PLUS`, `MINUS`, `STAR`, `SLASH`, `EQ`, `EQEQ`, `NE`, `LT`, `LE`, `AND`, `OR`, `LSHIFT`, `RSHIFT` |
| Delimiters | `LBRACE`, `RBRACE`, `LPAREN`, `RPAREN`, `LBRACKET`, `RBRACKET`, `SEMICOLON`, `COMMA`, `DOT`, `COLON`, `ARROW`, `ELLIPSIS` |
| Literals | `INT_LIT`, `FLOAT_LIT`, `STRING_LIT`, `CHAR_LIT` |
| Identifiers | `IDENT` |
| Special | `EOF_TOKEN`, `UNKNOWN` |

### How keywords are resolved

`Lexer::keywords` is a `static std::unordered_map<std::string, TokenType>` initialized at program start with 44 entries. After `read_identifier()` accumulates an alphanumeric/underscore run, it does a single map lookup:

```cpp
auto it = keywords.find(ident);
if (it != keywords.end()) return Token(it->second, ident, ...);
return Token(TokenType::IDENT, ident, ...);
```

Any identifier not in the map becomes `IDENT`. This means user-defined type names are always `IDENT` at the lexer level; the parser and type checker give them meaning.

### Line/column tracking

`advance()` increments `column` on every character consumed. On `\n` it resets `column = 1` and increments `line`. Both are initialized to `1`. Each token captures the `line`/`column` at the point `next_token()` entered — specifically the value of `line`/`column` before any characters in that token were consumed.

---

## Parser (`parser/`)

### Recursive descent, no backtracking (except controlled savePos/catch)

The parser is purely recursive descent. There is no grammar table and no PEG memoization. The only lookahead used is `peek()` (current token) and `peek_ahead(1)` (one token ahead). Backtracking is used in exactly two places, both marked with `size_t savePos = current`:

- `parseDeclaration()` — distinguishes function vs variable by speculatively parsing the type and checking for `LPAREN` after the name, then resetting `current = savePos` and re-parsing as a function.
- `parseBlockStatement()` — tries `parseDeclaration()` in a `try/catch`; if it throws, resets `current` and falls through to `parseStatement()`.
- `parseUnary()` — attempts a cast expression `(TYPE) expr` speculatively; resets on failure.

### `parseType()`: handles leading `*T`, trailing `T*`, array `T[N]`

```
parseType()
  → consume any leading STAR tokens, count them
  → consume type keyword or IDENT
  → consume any trailing STAR tokens, append "*" to type string
  → prepend leading "*" for each leading star
  → consume "[" ... "]" sequences, append "[]" to type string
```

Array sizes inside `[N]` are skipped entirely (bracket-depth counter, no value captured). The resulting type string for `uint8[858]` is `"uint8[]"`. For `*Point` it is `"*Point"`. For `int**` it is `"int**"`.

### `parseDeclaration()` disambiguation: function vs variable by lookahead

At the top level, after ruling out `extern`, `struct`, and `let`:

```
savePos = current
type = parseType()            // consumes type tokens
if peek() == IDENT:
    name = advance()
    if peek() == LPAREN:      // function: int foo(
        current = savePos     // full reset
        return parseFunctionDecl()
    elif peek() == SEMICOLON or EQ:  // variable: int x; or int x =
        parse initializer if EQ
        return VarDecl(name, type, init)
```

`let` declarations follow a different path: `let name : type [= expr] ;`.

### `parseBlockStatement()`: `BlockItem = variant<DeclPtr, StmtPtr>`

Inside a block body, each item is attempted as a declaration first (same lookahead heuristic), and if that throws, retried as a statement. The result is a `vector<BlockItem>` where each element is a `std::variant<DeclPtr, StmtPtr>`. This allows declarations and statements to be freely interleaved — `int x = 5; x = x + 1; int y = x;` is valid.

### Error recovery: skip to next semicolon

`parseProgram()` wraps each `parseDeclaration()` call in a `try/catch`. On error it advances past tokens until it finds a `SEMICOLON`, then continues. This allows the parser to report multiple errors in a single run rather than aborting at the first problem.

---

## AST (`ast/`)

### Node hierarchy

```
ASTNode
├── Decl
│   ├── FunctionDecl   (name, returnType, params[(type,name)], body:StmtPtr)
│   ├── VarDecl        (name, type, initializer:ExprPtr?)
│   ├── StructDecl     (name, fields[{type,name}], methods[DeclPtr])
│   └── ExternDecl     (name, returnType, params[(type,name)])
├── Stmt
│   ├── BlockStmt      (items: vector<BlockItem>)
│   ├── IfStmt         (condition, thenBranch, elseBranch?)
│   ├── ForStmt        (init:StmtPtr?, condition:ExprPtr?, step:ExprPtr?, body)
│   ├── WhileStmt      (condition, body)
│   ├── ReturnStmt     (value:ExprPtr?)
│   ├── BreakStmt
│   └── ExprStmt       (expr)
└── Expr
    ├── BinaryExpr     (left, op:string, right)
    ├── UnaryExpr      (op:string, operand)
    ├── CallExpr       (callee:ExprPtr, args[ExprPtr])
    ├── IndexExpr      (base, index)
    ├── MemberExpr     (base, member:string)
    ├── CastExpr       (targetType:string, expr)
    ├── LiteralExpr    (kind:{INT,FLOAT,STRING,CHAR,BOOL,NULL_VAL}, value:string)
    └── IdentExpr      (name:string)

Program                (declarations: vector<DeclPtr>)   — root, also ASTNode
```

### `BlockItem = std::variant<DeclPtr, StmtPtr>` — why unified

C allows variables to be declared at any point in a block, not just at the top. Rather than forcing all declarations to precede statements (Pascal-style) or treating `VarDecl` as a statement subtype, `BlockStmt` uses `std::variant<DeclPtr, StmtPtr>`. Visitors iterate over items and dispatch with `std::holds_alternative` / `std::get`. This preserves the semantic distinction between declarations and statements throughout all passes.

### Smart pointer conventions

All AST nodes are heap-allocated and managed through `shared_ptr`. Type aliases:

```cpp
using ASTNodePtr = std::shared_ptr<ASTNode>;
using ExprPtr    = std::shared_ptr<Expr>;
using StmtPtr    = std::shared_ptr<Stmt>;
using DeclPtr    = std::shared_ptr<Decl>;
```

`shared_ptr` is used throughout rather than `unique_ptr` because AST nodes can be referenced from multiple places (e.g., `expressionTypes` cache in the type checker holds raw `Expr*` keys pointing into shared-owned subtrees).

### ASTPrinter for `--test-parser` output

`ASTPrinter` in `ast/ast_printer.h` implements `ASTVisitor` and prints a depth-indented tree to stdout. It is the output of `--test-parser`. Its `print(shared_ptr<Program>)` method calls `program->accept(this)` to start traversal.

---

## Type Checker (`sema/`)

### Two-pass strategy

`TypeChecker::check(Program*)` runs two sequential passes over `program->declarations`:

**Pass 1** — registration only, no type checking:
- `StructDecl` → insert into `structs` map (`map<string, StructInfo>`)
- `FunctionDecl` → insert into `functionSignatures` (`map<string, pair<string, vector<string>>>`)
- `ExternDecl` → same as `FunctionDecl`

**Pass 2** — full visitor traversal:
- Each declaration calls `decl->accept(this)` which dispatches to the appropriate `visit()` method.

This two-pass design ensures that forward references work: a function can call another function declared later in the file, and struct fields can reference structs declared in any order.

### Scope stack: `vector<map<string,Symbol>>`

```cpp
struct Symbol { std::string type; bool isDeclared; };
std::vector<std::map<std::string, Symbol>> scopes;
```

`pushScope()` appends an empty map. `popScope()` removes the last map. `defineSymbol(name, type)` inserts into `scopes.back()`. `lookupSymbol(name)` iterates `scopes.rbegin()` to `scopes.rend()` — innermost first. Returns `""` on miss.

Scope boundaries:
- Global scope: created in `TypeChecker()` constructor
- Function body: pushed/popped in `visit(FunctionDecl*)`
- Block: pushed/popped in `visit(BlockStmt*)`
- For loop: pushed/popped in `visit(ForStmt*)` (covers init variable)

### Struct registry: `map<string,StructInfo>` — validated before use

`StructInfo` holds the struct name and its `vector<StructDecl::Field>`. Before any variable or cast uses a struct type, `validateStructType()` is called on the normalized type. It strips pointer suffixes, checks for the `struct:` prefix, then verifies the struct name is in the registry.

`visit(MemberExpr*)` looks up the base expression's type, strips the `struct:` prefix to get the struct name, finds the `StructInfo`, and searches `fields` linearly for the member name. It sets `expressionTypes[node]` to the field's declared type on success.

### Type normalization: `"Point"` → `"struct:Point"`, `"*Point"` → `"struct:Point*"`

`normalizeType(type)` applies these rules in order:

1. If `type` ends with `*`: strip suffix, normalize base recursively, re-append `*` via `addPointerSuffix`.
2. If `type` already starts with `struct:` or `interface:`: return as-is.
3. If `type` is a known struct name in the registry: return `"struct:" + type`.
4. Otherwise return as-is (primitive).

The internal representation uses `struct:Name` as the canonical form. Pointer-to-struct is `"struct:Name*"` (trailing `*`). The type checker's `isPointerType` detects leading `*` (from unary `&` operator result or leading-`*` annotation): `type[0] == '*'`. The sema layer thus uses both conventions simultaneously — leading `*` for inference results, trailing `*` for normalized struct pointer types.

### Pointer style: leading `*T` internally (`type[0]=='*'`)

`isPointerType(type)` in the type checker returns `type[0] == '*'`. `getPointeeType` returns `type.substr(1)`. Unary `&` produces `"*" + operandType`. However, `hasPointerSuffix` / `extractBaseType` / `addPointerSuffix` operate on the trailing-`*` form for struct normalization. Contributors must be aware that both forms coexist in `expressionTypes`.

### Error format: `file.esk:line:col: message`

```cpp
void TypeChecker::error(int line, int col, const std::string& message) {
    std::stringstream ss;
    ss << "file.esk:" << line << ":" << col << ": " << message;
    errors.push_back(ss.str());
}
```

All calls currently pass `0, 0` for line and column — token positions are not threaded through the AST nodes. This is a known gap; see Phase 5 TODO below.

---

## Codegen (`codegen/`)

### IRBuilder usage pattern

`CodeGen` owns three LLVM objects:

```cpp
std::unique_ptr<llvm::LLVMContext> context;
std::unique_ptr<llvm::Module>      module;   // named "eskiu"
std::unique_ptr<llvm::IRBuilder<>> builder;
```

All IR emission goes through `builder`. The insert point is managed explicitly: `visit(FunctionDecl*)` creates a `BasicBlock` named `"entry"` and calls `builder->SetInsertPoint(entryBlock)`. Control-flow visitors (`IfStmt`, `WhileStmt`, `ForStmt`) create named basic blocks and call `SetInsertPoint` to switch between them.

After all declarations are visited, `generateCode()` calls `llvm::verifyModule`. If verification fails the method returns `nullptr`.

### Symbol table: `map<string, llvm::Value*>` + scope stack

```cpp
std::map<std::string, llvm::Value*> symbolTable;   // current flat view
std::vector<std::map<std::string, llvm::Value*>> scopeStack;  // saved outer scopes
```

`pushScope()` copies `symbolTable` onto `scopeStack`. `popScope()` restores `symbolTable = scopeStack.back(); scopeStack.pop_back()`. This means every scope entry/exit copies the full flat map — it is not a chained lookup structure. `lookupSymbol(name)` is a single `symbolTable.find(name)` call.

Functions are stored in `symbolTable` as `llvm::Function*` values. Parameters are stored as `llvm::Argument*` values (direct values, not allocas). Local variables are stored as `llvm::AllocaInst*`.

### `evaluateExpr()` vs `evaluateLValue()` — the lvalue/rvalue split

**`evaluateExpr(ExprPtr)`**: calls `expr->accept(this)`, which pushes an `llvm::Value*` onto `exprValueStack`. The method then pops and returns it. For `IdentExpr`, `visit(IdentExpr*)` loads from the `AllocaInst` if the symbol is a local variable (`llvm::isa<llvm::AllocaInst>(val)`), returning the loaded value. For function values it returns the `llvm::Function*` directly.

**`evaluateLValue(ExprPtr)`**: used only for the left-hand side of assignment (`op == "="`). Currently handles only `IdentExpr` — returns the `AllocaInst*` directly without loading. Any other expression node throws `std::runtime_error`.

This separation is necessary because `x = 5` must `store` into the alloca, not load from it.

### VarDecl type coercion: truncate/extend initializer to declared type

`visit(VarDecl*)` allocates an `AllocaInst` for the declared type, evaluates the initializer, then coerces:

```
src_type → dst_type    instruction emitted
int32    → int8        CreateTrunc
int8     → int32       CreateSExt
int      → float       CreateSIToFP
float    → int         CreateFPToSI
float    → double      CreateFPCast
```

If types already match, no coercion instruction is emitted.

### Pointer types: both `*T` (front) and `T*` (back) handled in `getTypeFromString`

```cpp
if (!typeStr.empty() && typeStr.front() == '*') return llvm::PointerType::get(*context, 0);
if (!typeStr.empty() && typeStr.back()  == '*') return llvm::PointerType::get(*context, 0);
```

All pointer types — regardless of syntax — map to the LLVM opaque pointer type (`ptr` in LLVM 15+, addrspace 0). No pointee type information is retained in the LLVM type system. The `string` type also maps to opaque pointer.

### Stubs / Phase 5 warnings

| Node | Current behavior |
|---|---|
| `StructDecl` | `std::cerr << "Warning: struct definitions not yet implemented"` |
| `MemberExpr` | `std::cerr << "Warning: member access not yet implemented"` |
| `IndexExpr`  | `std::cerr << "Warning: array indexing not yet implemented"` |
| `BreakStmt`  | `std::cerr << "Warning: break not yet implemented"` |
| `emitObjectFile` | Returns `false` with a warning |

### `emitObjectFile()`: not yet implemented

The method signature exists in `codegen.h` and the stub returns `false`. Object file emission will require `llvm::TargetMachine` setup (`InitializeAllTargets`, `getDefaultTargetTriple`, `createTargetMachine`) and `llvm::raw_fd_ostream` output.

---

## Symbol Table Design

### Global scope vs local scopes

On construction, `TypeChecker` calls `pushScope()` once — this is the global scope. `visit(FunctionDecl*)` calls `pushScope()` before entering the body and `popScope()` after. `visit(BlockStmt*)` does the same. `visit(ForStmt*)` adds a scope for the init variable.

Functions and externs are stored in `functionSignatures` (a separate map, not in the scope stack). Struct names receive a symbol of type `"struct:Name"` in the global scope via `visit(StructDecl*)`.

In `CodeGen`, there is no equivalent of the function signatures map — function `llvm::Value*` pointers are stored directly in `symbolTable`. `visit(IdentExpr*)` falls back to `module->getFunction(node->name)` when `lookupSymbol` returns null, which handles cross-function calls without requiring the function to be in the local symbol table.

### `pushScope`/`popScope` semantics

**TypeChecker**: `pushScope` appends an empty `map<string,Symbol>` to `scopes`. `popScope` removes the last. Symbol lookup scans from back to front.

**CodeGen**: `pushScope` snapshots the entire `symbolTable` map. `popScope` restores it. This means symbols defined in an inner scope are automatically invisible after `popScope` — but also that any symbols added to `symbolTable` while in an inner scope are discarded. There is no way for inner scope mutations to propagate outward (this is correct behavior for local variable lifetime).

### `lookupSymbol`: innermost-first search

**TypeChecker**:
```cpp
for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto sym = it->find(name);
    if (sym != it->end()) return sym->second.type;
}
return "";
```

**CodeGen**:
```cpp
auto it = symbolTable.find(name);  // flat map — scope isolation via snapshots
```

---

## Type Mappings

| Eskiu type | LLVM type | Notes |
|---|---|---|
| `int`, `int32` | `i32` | Default integer |
| `int8` | `i8` | |
| `int16` | `i16` | |
| `int64` | `i64` | |
| `uint`, `uint32` | `i32` | LLVM has no signedness in types; sign is in the instruction (`sdiv` vs `udiv`) |
| `uint8` | `i8` | |
| `uint16` | `i16` | |
| `uint64` | `i64` | |
| `float` | `float` (f32) | |
| `double` | `double` (f64) | |
| `bool` | `i1` | |
| `char` | `i8` | |
| `string` | `ptr` (opaque pointer, addrspace 0) | |
| `void` | `void` | Return type only |
| `*T` or `T*` | `ptr` (opaque pointer, addrspace 0) | All pointer types are opaque |
| `struct:Name` | `ptr` (opaque pointer, addrspace 0) | Temporary; Phase 5 will emit `llvm::StructType` |

Integer literals are emitted as `i32` by `visit(LiteralExpr*)`. Float literals are emitted as `double`. This means a literal `5` assigned to an `int8` variable will be truncated by the coercion logic in `visit(VarDecl*)`.

Unsigned arithmetic is not yet distinguished from signed at the codegen layer — all integer arithmetic uses signed instructions (`CreateAdd`, `CreateSDiv`, `CreateICmpSLT`). Phase 5 work includes plumbing signedness through the type representation to select `udiv`/`urem`/`ICmpULT` where appropriate.

---

## Known Gaps / Phase 5 TODO

### Struct codegen (`llvm::StructType`, GEP for field access)

`visit(StructDecl*)` emits only a warning. To implement:
1. Create `llvm::StructType::create(*context, name)` with a field list from `node->fields`.
2. Store the `llvm::StructType*` in a `map<string, llvm::StructType*>` inside `CodeGen`.
3. `getTypeFromString("struct:Name")` should look up and return the `llvm::StructType*` instead of an opaque pointer.
4. `visit(MemberExpr*)` must resolve the field index and emit `builder->CreateStructGEP(structType, ptr, fieldIdx)`.
5. `evaluateLValue(MemberExpr*)` must also be implemented for member assignment.

### Array type sizes (parsed but discarded)

`parseType()` appends `"[]"` for array syntax but discards the size expression. The size is needed for `llvm::ArrayType::get(elemType, size)`. This requires either: (a) storing the size in the type string (e.g., `"uint8[858]"`), or (b) adding an `ArrayType` AST node separate from the string representation.

### `alloc`/`free` plumbing

The lexer recognizes `ALLOC` and `FREE` tokens, but the parser has no rule for them yet. `alloc(T, N)` should emit `llvm::CallInst` to `malloc` (or a custom allocator) and cast the result to `T*`. `free(ptr)` should emit a call to `free`. This belongs in `visit(CallExpr*)` with special-casing on the callee name, or as dedicated AST nodes.

### Source location wiring through error reporting

`Token` carries `line` and `column`. AST nodes do not. All error calls in `type_checker.cpp` pass `0, 0`. To fix: add `int line; int column;` to `ASTNode` (or at minimum to `Expr` and `Decl`), populate them in the parser from the token at the start of each construct, and thread them through to `error(line, col, ...)`.

### `break` statement in codegen

`visit(BreakStmt*)` emits a warning and no IR. Implementing it requires tracking the "exit block" of the enclosing loop. The standard approach is a `std::stack<llvm::BasicBlock*> breakTargetStack` in `CodeGen`, pushed by `visit(WhileStmt*)` and `visit(ForStmt*)` before generating the body, popped after. `visit(BreakStmt*)` then emits `builder->CreateBr(breakTargetStack.top())`.
