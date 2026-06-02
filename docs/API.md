# Eskiu Compiler Public API

This document describes the public C++ API for the Eskiu compiler, enabling you to embed the compiler in your own applications.

---

## Overview

The Eskiu compiler is structured as a pipeline with distinct phases:

```
Source Text → Lexer → Parser → TypeChecker (Phase 4) → CodeGen → LLVM Module
```

Each phase is encapsulated in its own class, allowing you to use them independently or together.

---

## Lexer API

**Header:** `lexer/lexer.h`

### TokenType Enum

All possible token types recognized by the lexer:

```cpp
enum class TokenType {
    // Keywords
    LET, INT, INT8, INT16, INT32, INT64,
    UINT, UINT8, UINT16, UINT32, UINT64,
    FLOAT, DOUBLE, BOOL, CHAR, STRING, VOID,
    STRUCT, INTERFACE, ENUM, FN,
    FOR, IN, WHILE, IF, ELSE, SWITCH, CASE, DEFAULT,
    BREAK, RETURN, IMPORT, EXTERN,
    ALLOC, FREE, NULL_KW, TRUE, FALSE,
    THREAD, SPAWN, MUTEX, TRY, CATCH, FINALLY, THROW,
    
    // Operators & Delimiters
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ, NE, LT, GT, LE, GE,
    AND, OR, NOT, AMPERSAND, PIPE, CARET, TILDE,
    LSHIFT, RSHIFT, ASSIGN,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACK, RBRACK,
    DOT, COMMA, SEMICOLON, COLON, ARROW, ELLIPSIS,
    
    // Literals
    INT_LIT, FLOAT_LIT, STRING_LIT, CHAR_LIT, IDENT,
    
    // Special
    EOF_TOKEN, UNKNOWN
};
```

### Token Struct

```cpp
struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;
};
```

Properties:
- `type` — The kind of token (keyword, operator, literal, etc.)
- `value` — The raw text of the token
- `line` — Line number (1-indexed)
- `column` — Column number (1-indexed)

### Lexer Class

```cpp
class Lexer {
public:
    explicit Lexer(const std::string& source);
    
    // Get next token from the stream
    Token nextToken();
    
    // Peek at the current character without consuming
    char peek() const;
    
    // Check if we've reached end of input
    bool isAtEnd() const;
};
```

**Example:**

```cpp
#include "lexer/lexer.h"

int main() {
    Lexer lexer("let x: int = 42;");
    
    Token token;
    while ((token = lexer.nextToken()).type != TokenType::EOF_TOKEN) {
        std::cout << "Token: " << static_cast<int>(token.type)
                  << " Value: " << token.value
                  << " (" << token.line << ":" << token.column << ")\n";
    }
}
```

---

## Parser API

**Header:** `parser/parser.h`

### Parser Class

```cpp
class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    
    // Parse entire program
    std::shared_ptr<Program> parse();
};
```

**Example:**

```cpp
#include "lexer/lexer.h"
#include "parser/parser.h"

int main() {
    Lexer lexer("int main() { return 42; }");
    
    std::vector<Token> tokens;
    Token tok;
    while ((tok = lexer.nextToken()).type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
    }
    tokens.push_back(tok);  // Add EOF
    
    Parser parser(tokens);
    auto program = parser.parse();
    
    if (!program) {
        std::cerr << "Parse error\n";
        return 1;
    }
}
```

---

## AST API

**Header:** `ast/ast.h`

### AST Node Hierarchy

All AST nodes inherit from `ASTNode`:

```cpp
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor* visitor) = 0;
};
```

### Declarations

```cpp
class Program : public ASTNode {
public:
    std::vector<DeclPtr> declarations;
};

class FunctionDecl : public ASTNode {
public:
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params;  // (name, type)
    StmtPtr body;
    bool isVararg = false;
};

class VarDecl : public ASTNode {
public:
    std::string name;
    std::string type;
    ExprPtr initializer;
};

class ExternDecl : public ASTNode {
public:
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params;
    bool isVararg = false;
};

class StructDecl : public ASTNode {
public:
    std::string name;
    std::vector<std::pair<std::string, std::string>> fields;  // (name, type)
};
```

### Statements

```cpp
class BlockStmt : public ASTNode {
public:
    std::vector<StmtPtr> statements;
};

class IfStmt : public ASTNode {
public:
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch;
};

class WhileStmt : public ASTNode {
public:
    ExprPtr condition;
    StmtPtr body;
};

class ForStmt : public ASTNode {
public:
    StmtPtr init;
    ExprPtr condition;
    ExprPtr increment;
    StmtPtr body;
};

class ReturnStmt : public ASTNode {
public:
    ExprPtr value;
};

class BreakStmt : public ASTNode {
    // No members
};

class ExprStmt : public ASTNode {
public:
    ExprPtr expression;
};
```

### Expressions

```cpp
class BinaryExpr : public ASTNode {
public:
    ExprPtr left;
    TokenType op;
    ExprPtr right;
};

class UnaryExpr : public ASTNode {
public:
    TokenType op;
    ExprPtr operand;
};

class CallExpr : public ASTNode {
public:
    ExprPtr function;
    std::vector<ExprPtr> arguments;
};

class IndexExpr : public ASTNode {
public:
    ExprPtr object;
    ExprPtr index;
};

class MemberExpr : public ASTNode {
public:
    ExprPtr object;
    std::string member;
};

class CastExpr : public ASTNode {
public:
    std::string targetType;
    ExprPtr value;
};

class LiteralExpr : public ASTNode {
public:
    TokenType kind;  // INT_LIT, FLOAT_LIT, STRING_LIT, CHAR_LIT, TRUE, FALSE, NULL_KW
    std::string value;
};

class IdentExpr : public ASTNode {
public:
    std::string name;
};
```

### Visitor Pattern

```cpp
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;
    
    // Declarations
    virtual void visit(FunctionDecl* node) = 0;
    virtual void visit(VarDecl* node) = 0;
    virtual void visit(StructDecl* node) = 0;
    virtual void visit(ExternDecl* node) = 0;
    
    // Statements
    virtual void visit(BlockStmt* node) = 0;
    virtual void visit(IfStmt* node) = 0;
    virtual void visit(WhileStmt* node) = 0;
    virtual void visit(ForStmt* node) = 0;
    virtual void visit(ReturnStmt* node) = 0;
    virtual void visit(BreakStmt* node) = 0;
    virtual void visit(ExprStmt* node) = 0;
    
    // Expressions
    virtual void visit(BinaryExpr* node) = 0;
    virtual void visit(UnaryExpr* node) = 0;
    virtual void visit(CallExpr* node) = 0;
    virtual void visit(IndexExpr* node) = 0;
    virtual void visit(MemberExpr* node) = 0;
    virtual void visit(CastExpr* node) = 0;
    virtual void visit(LiteralExpr* node) = 0;
    virtual void visit(IdentExpr* node) = 0;
};
```

**Example Custom Visitor:**

```cpp
class MyVisitor : public ASTVisitor {
public:
    void visit(FunctionDecl* node) override {
        std::cout << "Function: " << node->name << "\n";
        if (node->body) {
            node->body->accept(this);
        }
    }
    
    void visit(VarDecl* node) override {
        std::cout << "Variable: " << node->name << " : " << node->type << "\n";
    }
    
    // ... implement other visit methods ...
};

// Usage
MyVisitor visitor;
program->accept(&visitor);
```

---

## AST Printer API

**Header:** `ast/ast.h` (contains ASTPrinter)

### ASTPrinter Class

```cpp
class ASTPrinter : public ASTVisitor {
public:
    // Print AST with indentation to stdout
    static void print(ASTNode* node);
};
```

**Example:**

```cpp
ASTPrinter::print(program.get());  // Pretty-print the entire program
```

---

## Code Generator API

**Header:** `codegen/codegen.h`

### CodeGen Class

```cpp
class CodeGen : public ASTVisitor {
public:
    CodeGen();
    
    // Generate LLVM IR from AST
    std::unique_ptr<llvm::Module> generateCode(Program* program);
};
```

**Example:**

```cpp
#include "codegen/codegen.h"
#include <llvm/IR/Module.h>

CodeGen codegen;
auto module = codegen.generateCode(program.get());

if (!module) {
    std::cerr << "Code generation failed\n";
    return 1;
}

// Print LLVM IR to stdout
llvm::raw_os_ostream out(std::cout);
module->print(out, nullptr);
```

---

## Type Mappings

The compiler maps Eskiu types to LLVM types:

```cpp
// In codegen/codegen.cpp
static llvm::Type* getTypeFromString(llvm::LLVMContext* context, const std::string& type) {
    if (type == "int") return llvm::Type::getInt32Ty(*context);
    if (type == "int8") return llvm::Type::getInt8Ty(*context);
    if (type == "int16") return llvm::Type::getInt16Ty(*context);
    if (type == "int32") return llvm::Type::getInt32Ty(*context);
    if (type == "int64") return llvm::Type::getInt64Ty(*context);
    if (type == "float") return llvm::Type::getFloatTy(*context);
    if (type == "double") return llvm::Type::getDoubleTy(*context);
    if (type == "bool") return llvm::Type::getInt1Ty(*context);
    if (type == "char") return llvm::Type::getInt8Ty(*context);
    if (type == "string") return llvm::PointerType::get(*context, 0);
    if (type == "void") return llvm::Type::getVoidTy(*context);
    // ... handle pointer types ...
    return nullptr;
}
```

---

## Error Handling

Currently, errors are reported to stderr with the following format:

```
error: file.esk:line:col: message
```

In Phase 4 (Type Checker), a dedicated error reporting system will be implemented.

---

## Complete Example

```cpp
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "codegen/codegen.h"
#include <iostream>
#include <fstream>
#include <sstream>

std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file.esk>\n";
        return 1;
    }
    
    // Step 1: Tokenize
    Lexer lexer(readFile(argv[1]));
    std::vector<Token> tokens;
    Token tok;
    while ((tok = lexer.nextToken()).type != TokenType::EOF_TOKEN) {
        tokens.push_back(tok);
    }
    tokens.push_back(tok);  // Add EOF
    
    // Step 2: Parse
    Parser parser(tokens);
    auto program = parser.parse();
    if (!program) {
        std::cerr << "Parse failed\n";
        return 1;
    }
    
    // Step 3: Pretty-print AST
    ASTPrinter::print(program.get());
    
    // Step 4: Generate LLVM IR
    CodeGen codegen;
    auto module = codegen.generateCode(program.get());
    if (!module) {
        std::cerr << "Code generation failed\n";
        return 1;
    }
    
    // Step 5: Print LLVM IR
    llvm::raw_os_ostream out(std::cout);
    module->print(out, nullptr);
    
    return 0;
}
```

---

## Using the Compiler as a Library

To embed Eskiu in your C++ project:

1. **Link against the Eskiu libraries:**
   ```cmake
   find_package(Eskiu REQUIRED)
   target_link_libraries(your_project Eskiu::compiler)
   ```

2. **Include the necessary headers:**
   ```cpp
   #include <eskiu/lexer.h>
   #include <eskiu/parser.h>
   #include <eskiu/codegen.h>
   ```

3. **Use the API as shown above**

(Library packaging details to be added in Phase 5+)

---

## Known Limitations (v0.0.1)

- No type checking (Phase 4)
- No struct/interface support (Phase 5)
- No heap allocation (Phase 6)
- No standard library (Phase 7)
- No error recovery details in public API
- Module ownership transfers (use std::move)

See [PHASES.md](./PHASES.md) for the roadmap.

---

For questions about the API, open an issue with the `api` label.
