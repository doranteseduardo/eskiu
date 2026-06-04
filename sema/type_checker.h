#pragma once

#include "../ast/ast.h"
#include "../lexer/lexer.h"
#include <map>
#include <vector>
#include <string>
#include <memory>

class TypeChecker : public ASTVisitor {
public:
    TypeChecker();

    // Main entry point
    bool check(Program* program);

    // Get inferred type of an expression
    std::string getExpressionType(Expr* expr);

    // Visitor methods
    void visit(Program* node) override;
    void visit(FunctionDecl* node) override;
    void visit(VarDecl* node) override;
    void visit(StructDecl* node) override;
    void visit(ExternDecl* node) override;
    void visit(InterfaceDecl* node) override;
    void visit(ContinueStmt* node) override;
    void visit(SwitchStmt* node) override;
    void visit(TemplateCallExpr* node) override;

    void visit(BlockStmt* node) override;
    void visit(IfStmt* node) override;
    void visit(WhileStmt* node) override;
    void visit(ForStmt* node) override;
    void visit(ReturnStmt* node) override;
    void visit(BreakStmt* node) override;
    void visit(ExprStmt* node) override;

    void visit(BinaryExpr* node) override;
    void visit(UnaryExpr* node) override;
    void visit(CallExpr* node) override;
    void visit(IndexExpr* node) override;
    void visit(MemberExpr* node) override;
    void visit(CastExpr* node) override;
    void visit(LiteralExpr* node) override;
    void visit(IdentExpr* node) override;
    void visit(StructInitExpr* node) override;
    void visit(AllocExpr* node) override;
    void visit(LambdaExpr* node) override;
    void visit(AsmStmt* node) override;
    void visit(ThreadCreateExpr* node) override;
    void visit(ThreadJoinStmt* node) override;
    void visit(ThrowStmt* node) override;
    void visit(TryStmt* node) override;

private:
    // Symbol table: maps name -> type
    struct Symbol {
        std::string type;
        bool isDeclared;
    };

    // Struct information: name -> fields
    struct StructInfo {
        std::string name;
        std::vector<StructDecl::Field> fields;
    };

    // Scope management
    std::vector<std::map<std::string, Symbol>> scopes;

    // Struct registry: name -> StructInfo  (concrete structs only)
    std::map<std::string, StructInfo> structs;

    // Template registry: template name -> StructDecl (not yet instantiated)
    std::map<std::string, StructDecl*> templateDecls;
    // Template function registry
    std::map<std::string, FunctionDecl*> funcTemplateDecls;
    // Interface registry
    std::map<std::string, InterfaceDecl*> interfaceDecls;

    // Function signatures: name -> (return type, parameter types)
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> functionSignatures;

    // Current function context for return type checking
    std::string currentFunctionReturnType;

    // Error tracking
    std::vector<std::string> errors;
    bool hasErrors = false;

    // Helper methods
    void pushScope();
    void popScope();
    void defineSymbol(const std::string& name, const std::string& type);
    std::string lookupSymbol(const std::string& name);
    void defineFunction(const std::string& name, const std::string& returnType,
                       const std::vector<std::string>& paramTypes);

    // Type inference
    std::string inferBinaryExprType(const std::string& leftType, const std::string& op,
                                    const std::string& rightType);
    std::string inferUnaryExprType(const std::string& op, const std::string& operandType);

    // Type validation
    void validateStructType(const std::string& type);

    // Type checking utilities
    bool isValidAssignment(const std::string& lhsType, const std::string& rhsType);
    bool isNumericType(const std::string& type);
    bool isIntType(const std::string& type);
    bool isFloatType(const std::string& type);
    bool isPrimitiveType(const std::string& type);
    bool isPointerType(const std::string& type);
    std::string getPointeeType(const std::string& pointerType);

    // Type promotion
    std::string promoteType(const std::string& type1, const std::string& type2);

    // Type normalization
    std::string normalizeType(const std::string& type);

    // Pointer type handling
    bool hasPointerSuffix(const std::string& type) const;
    std::string extractBaseType(const std::string& pointerType) const;
    std::string addPointerSuffix(const std::string& baseType) const;

public:
    // Source file name (for error messages)
    std::string sourceFile = "unknown";

    // Find the Eskiu type of the expression nearest to (line, col)
    std::string getTypeAtPosition(int line, int col) const;

    // Definition location: (line, col, file)
    struct DefLocation { int line; int col; std::string file; };
    std::map<std::string, DefLocation> definitionLocations;
    // Use-site map: (line,col) → symbol name (populated from IdentExpr visits)
    std::map<std::pair<int,int>, std::string> useLocations;
    std::string getDefinitionAt(int line, int col) const;

private:

    // Error reporting
    void error(int line, int col, const std::string& message);
    void warning(int line, int col, const std::string& message);
    // Convenience: report error at an AST node's position
    void errorAt(ASTNode* node, const std::string& message) {
        error(node->line, node->col, message);
    }

    // Cache for expression types
    std::map<Expr*, std::string> expressionTypes;

    // Capture detection: when non-empty, we are inside a lambda body.
    // Each entry is the set of (name, type) pairs captured so far.
    // IdentExpr visitor adds to the top entry when it finds an outer-scope var.
    std::vector<std::map<std::string, std::string>> captureStack;
    int lambdaScopeDepth = 0; // how many scopes the lambda itself pushed
};
