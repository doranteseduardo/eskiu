# Eskiu Compiler Architecture

This document describes the internal architecture of the Eskiu compiler.

## High-Level Pipeline

```
┌──────────────┐
│ Source Code  │ (.esk files)
│   (.esk)     │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│   LEXER      │ lexer/lexer.cpp
│ Tokenization │ Breaks source into tokens
└──────┬───────┘
       │ Token stream
       ▼
┌──────────────┐
│   PARSER     │ parser/parser.cpp
│ Syntax Anal  │ Recursive descent parser
└──────┬───────┘
       │ Abstract Syntax Tree (AST)
       ▼
┌──────────────┐
│ TYPE CHECKER │ sema/type_checker.cpp (Phase 4)
│  Validation  │ Type inference & error reporting
└──────┬───────┘
       │ Validated AST
       ▼
┌──────────────┐
│  CODEGEN     │ codegen/codegen.cpp
│ LLVM IR Emit │ Visitor-based IR generation
└──────┬───────┘
       │ LLVM Module
       ▼
┌──────────────┐
│ LLVM Backend │
│  Optimize &  │ Passes, optimization, codegen
│  Compile     │
└──────┬───────┘
       │
       ▼
┌──────────────┐
│ Native Code  │ x86-64, ARM64, WASM, RISC-V
│  Executable  │
└──────────────┘
```

## Components

### 1. Lexer (`lexer/`)

**File:** `lexer/lexer.h`, `lexer/lexer.cpp`

**Purpose:** Converts source text into a stream of tokens.

**Key Classes:**
- `TokenType` — Enum of all token types (keywords, operators, literals, etc.)
- `Token` — Represents a single token (type, value, line, column)
- `Lexer` — Main lexer class

**Flow:**
```
Source string → next_token() → Token stream
```

**Responsibilities:**
- Recognize keywords (let, int, if, while, etc.)
- Identify operators (+, -, *, /, ==, etc.)
- Parse literals (integers, floats, strings, chars)
- Track line and column numbers for error reporting
- Handle comments (// and /* */)

**Example:**
```cpp
Lexer lexer("int x = 42;");
Token tok = lexer.next_token();
// tok.type == TokenType::INT
// tok.value == "int"
// tok.line == 1, tok.column == 1
```

### 2. Parser (`parser/`)

**File:** `parser/parser.h`, `parser/parser.cpp`

**Purpose:** Builds an Abstract Syntax Tree (AST) from a token stream.

**Key Classes:**
- `Parser` — Recursive descent parser
- AST nodes: `FunctionDecl`, `VarDecl`, `StructDecl`, `IfStmt`, etc. (see `ast/ast.h`)

**Strategy:** Recursive descent (hand-written, no yacc/bison)

**Key Methods:**
```cpp
// Entry point
std::shared_ptr<Program> parse();

// Declaration parsing
DeclPtr parseDeclaration();
DeclPtr parseFunctionDecl();
DeclPtr parseVarDecl();

// Statement parsing
StmtPtr parseStatement();
StmtPtr parseIfStatement();
StmtPtr parseForStatement();

// Expression parsing (precedence climbing)
ExprPtr parseExpression();
ExprPtr parseBinaryOp();
ExprPtr parseUnaryOp();
ExprPtr parsePrimary();
```

**Example AST:**
```
Program
  FunctionDecl: main
    Parameters: []
    Body:
      BlockStmt
        VarDecl: x (type: int)
          Initializer: LiteralExpr(42)
        ReturnStmt
          IdentExpr: x
```

### 3. AST (`ast/`)

**File:** `ast/ast.h`, `ast/ast.cpp`, `ast/ast_printer.cpp`

**Purpose:** Defines the Abstract Syntax Tree structure.

**Key Concepts:**
- **Visitor Pattern:** Each node has an `accept(ASTVisitor* v)` method
- **Hierarchy:** All nodes inherit from `ASTNode`
- **Categories:**
  - Declarations: `FunctionDecl`, `VarDecl`, `StructDecl`, `ExternDecl`
  - Statements: `BlockStmt`, `IfStmt`, `ForStmt`, `WhileStmt`, `ReturnStmt`
  - Expressions: `BinaryExpr`, `UnaryExpr`, `CallExpr`, `LiteralExpr`, etc.

**Visitor Example:**
```cpp
class MyVisitor : public ASTVisitor {
    void visit(FunctionDecl* node) override {
        // Process function declaration
        for (auto& param : node->params) {
            // Handle parameter
        }
        node->body->accept(this);  // Visit body
    }
};
```

### 4. Type Checker (`sema/`) — Phase 4

**Purpose:** Validates types before code generation.

**Responsibilities (to be implemented):**
- Type inference for expressions
- Function argument validation
- Return type checking
- Error reporting with file:line:col

### 5. Code Generator (`codegen/`)

**File:** `codegen/codegen.h`, `codegen/codegen.cpp`

**Purpose:** Generates LLVM IR from the validated AST.

**Key Classes:**
- `CodeGen` — Visitor-based IR generator

**Flow:**
```cpp
CodeGen codegen;
auto module = codegen.generateCode(program);
module->print(out, nullptr);  // Print LLVM IR
```

**Architecture:**
- Uses LLVM IRBuilder for IR construction
- Maintains symbol table for variable lookup
- Implements visitor methods for each AST node type
- Handles type conversions and casting

**Example Output:**
```llvm
define i32 @add(i32 %a, i32 %b) {
entry:
  %0 = add i32 %a, %b
  ret i32 %0
}
```

## Data Flow

### Example: `int main() { return 42; }`

**1. Lexer Output:**
```
INT, IDENT(main), LPAREN, RPAREN, LBRACE, RETURN, INT_LIT(42), SEMICOLON, RBRACE, EOF
```

**2. Parser Output (AST):**
```
Program {
  FunctionDecl {
    name: "main",
    returnType: "int",
    params: [],
    body: BlockStmt {
      ReturnStmt {
        value: LiteralExpr(kind: INT, value: "42")
      }
    }
  }
}
```

**3. Type Checker Output (validated AST):**
```
[same as parser output, but with type annotations]
```

**4. Code Generator Output (LLVM IR):**
```llvm
define i32 @main() {
entry:
  ret i32 42
}
```

**5. LLVM Backend Output:**
```asm
main:
    mov $42, %eax
    ret
```

## Symbol Table Management

The compiler maintains **scope-aware** symbol tables:

```cpp
// Global scope (functions, extern declarations)
std::map<std::string, llvm::Function*> globalSymbols;

// Local scope stack (variables in functions)
std::vector<std::map<std::string, llvm::Value*>> scopeStack;

// Methods
void pushScope();        // Enter new scope (function body, block)
void popScope();         // Exit scope
void defineSymbol(...);  // Register variable/function
llvm::Value* lookup(...); // Find variable/function
```

## Type System

Eskiu types map to LLVM types:

| Eskiu | LLVM |
|-------|------|
| `int` | `i32` |
| `int64` | `i64` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `i1` |
| `char` | `i8` |
| `string` | `i8*` (opaque pointer) |
| `*T` | Pointer to T |

**Type Checking (Phase 4):**
- Infer types of expressions
- Validate assignments
- Check function arguments
- Report type mismatches with file:line:col

## Error Handling

### Lexer Errors
```
error: file.esk:5:12: unexpected character '@'
```

### Parser Errors
```
error: file.esk:10:3: expected ';' after statement
```

### Type Checker Errors (Phase 4)
```
error: file.esk:15:8: type mismatch: expected int, got float
```

## Performance Considerations

1. **Minimal AST traversals** — Each phase traverses the AST once
2. **In-place transformations** — No tree copies, only references
3. **Streaming lexer** — Tokens are generated on-demand
4. **Single-pass parsing** — No backtracking (for now)

## Extension Points

**Adding a new statement type:**
1. Define `YourStmt` class in `ast/ast.h`
2. Implement `accept()` method
3. Add `void visit(YourStmt*)` to `ASTVisitor` base class
4. Implement visitor in `Lexer`, `Parser`, `CodeGen`, etc.

**Adding a new operator:**
1. Add `YOUR_OP` to `TokenType` enum
2. Recognize in `Lexer::next_token()`
3. Handle precedence in `Parser::parseBinaryOp()`
4. Emit IR in `CodeGen::visit(BinaryExpr*)`

## Testing Strategy

Use the three testing modes to validate each component:

```bash
# Test lexer in isolation
./eskiuc file.esk --test-lexer

# Test parser in isolation
./eskiuc file.esk --test-parser

# Test codegen in isolation
./eskiuc file.esk --test-codegen
```

---

For implementation details, see [PHASES.md](./PHASES.md).
