#pragma once

#include "../ast/ast.h"
#include "../lexer/lexer.h"
#include <map>
#include <set>
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
    void visit(IntrinsicDecl* node) override;
    void visit(InterfaceDecl* node) override;
    void visit(ContinueStmt* node) override;
    void visit(SwitchStmt* node) override;
    void visit(TemplateCallExpr* node) override;

    void visit(BlockStmt* node) override;
    void visit(IfStmt* node) override;
    void visit(WhileStmt* node) override;
    void visit(ForStmt* node) override;
    void visit(ForInStmt* node) override;
    void visit(ReturnStmt* node) override;
    void visit(BreakStmt* node) override;
    void visit(ExprStmt* node) override;

    void visit(BinaryExpr* node) override;
    void visit(UnaryExpr* node) override;
    void visit(QuestionExpr* node) override;
    void visit(CallExpr* node) override;
    void visit(IndexExpr* node) override;
    void visit(MemberExpr* node) override;
    void visit(CastExpr* node) override;
    void visit(LiteralExpr* node) override;
    void visit(IdentExpr* node) override;
    void visit(StructInitExpr* node) override;
    void visit(AllocWithExpr* node) override;
    void visit(LambdaExpr* node) override;
    void visit(AsmStmt* node) override;
    void visit(UnionDecl* node) override;
    void visit(SizeofExpr* node) override;
    void visit(ThreadCreateExpr* node) override;
    void visit(ThreadJoinStmt* node) override;
    void visit(ThrowStmt* node) override;
    void visit(TryStmt* node) override;
    void visit(EnumDecl* node) override;
    void visit(TypeAliasDecl* node) override;

    // -Wall: emit lint-style warnings (unused vars/params/functions, etc.)
    bool warnAll = false;

    // --- LSP / tooling interface (consumed by --hover-at / --definition-at) ---
    std::string sourceFile = "unknown";   // source file name (for error messages)
    std::string getTypeAtPosition(int line, int col) const;
    struct DefLocation { int line; int col; std::string file; };
    std::map<std::string, DefLocation> definitionLocations;
    // Use-site map: (line,col) → symbol name (populated from IdentExpr visits)
    std::map<std::pair<int,int>, std::string> useLocations;
    std::string getDefinitionAt(int line, int col) const;

private:
    // Symbol table: maps name -> type
    struct Symbol {
        std::string type;
        bool isDeclared;
        bool used = false;     // -Wall: referenced at least once
        int  line = 0, col = 0;
        bool isParam = false;
        bool isConst = false;  // declared with `const` — reassignment is an error
    };

    // True if `name` resolves to a symbol declared `const` (searches scopes).
    bool isConstSymbol(const std::string& name) const;
    // If assigning to `lhs` would mutate a `const` value in place (the binding
    // itself, or a field/element of a const aggregate), returns true and sets
    // `nameOut` to the constant's name. Stops at pointer dereferences: writing
    // *through* a const pointer mutates the pointee, not the binding.
    bool assignsToConst(Expr* lhs, std::string& nameOut);

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
    // Reverse map: mangled instance name -> (template name, concrete type args),
    // for inferring a type parameter from a composite argument like List<T>*.
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> templateInstanceArgs;
    // Structural unification of a parameter type pattern against a concrete type.
    void unifyTypeParam(std::string pattern, std::string concrete,
                        const std::set<std::string>& tps,
                        std::map<std::string, std::string>& subs);
    // Interface registry
    std::map<std::string, InterfaceDecl*> interfaceDecls;

    // Enum registry: member name -> integer value; and the set of enum type names
    std::map<std::string, long long> enumConstants;
    std::set<std::string> enumTypes;
    // Type aliases: alias name -> underlying type string
    std::map<std::string, std::string> typeAliases;

    // Function signatures: name -> (return type, parameter types)
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> functionSignatures;

    // -Wall function-usage tracking: top-level functions defined vs. referenced
    std::map<std::string, std::pair<int,int>> definedFns; // name -> (line,col)
    std::set<std::string> calledFns;

    // Current function context for return type checking
    std::string currentFunctionReturnType;

    // Error tracking
    std::vector<std::string> errors;
    bool hasErrors = false;

    // Helper methods
    void pushScope();
    void popScope();
    void defineSymbol(const std::string& name, const std::string& type);
    void defineSymbol(const std::string& name, const std::string& type,
                      int line, int col, bool isParam);
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

    // Error reporting
    void error(int line, int col, const std::string& message);
    void warning(int line, int col, const std::string& message);
    void warnAssignInCondition(Expr* cond);  // -Wall: `if (x = 0)`
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
    // Parallel to captureStack: the scope count at each lambda's entry. A name
    // is captured when it resolves to a scope index below this boundary (i.e. an
    // enclosing function's param/local), regardless of any same-named global.
    std::vector<int> captureBoundary;
    int lambdaScopeDepth = 0; // how many scopes the lambda itself pushed
};
