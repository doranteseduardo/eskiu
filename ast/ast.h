#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>

// Forward declarations
class ASTNode;
class Expr;
class Stmt;
class Decl;

// Smart pointers
using ASTNodePtr = std::shared_ptr<ASTNode>;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;
using DeclPtr = std::shared_ptr<Decl>;

// BlockItem: can be either a declaration or a statement (allows interleaving)
using BlockItem = std::variant<DeclPtr, StmtPtr>;

// ============================================================================
// Base Classes
// ============================================================================

class ASTNode {
public:
    int line = 0;
    int col  = 0;
    virtual ~ASTNode() = default;
    virtual void accept(class ASTVisitor* visitor) = 0;
};

// ============================================================================
// Declarations
// ============================================================================

class Decl : public ASTNode {
public:
    std::string name;
    explicit Decl(const std::string& name) : name(name) {}
    virtual ~Decl() = default;
};

class FunctionDecl : public Decl {
public:
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params; // (type, name)
    StmtPtr body;
    std::vector<std::string> typeParams; // non-empty → template function

    FunctionDecl(const std::string& name, const std::string& returnType,
                 const std::vector<std::pair<std::string, std::string>>& params,
                 StmtPtr body)
        : Decl(name), returnType(returnType), params(params), body(body) {}

    void accept(class ASTVisitor* visitor) override;
};

class VarDecl : public Decl {
public:
    std::string type;
    ExprPtr initializer;
    bool isVolatile = false;

    VarDecl(const std::string& name, const std::string& type, ExprPtr init = nullptr)
        : Decl(name), type(type), initializer(init) {}

    void accept(class ASTVisitor* visitor) override;
};

class InterfaceDecl : public Decl {
public:
    // Each method signature: (return_type, name, param_types)
    struct MethodSig {
        std::string returnType;
        std::string name;
        std::vector<std::pair<std::string, std::string>> params;
    };
    std::vector<MethodSig> methods;

    explicit InterfaceDecl(const std::string& name) : Decl(name) {}

    void accept(class ASTVisitor* visitor) override;
};

class StructDecl : public Decl {
public:
    struct Field {
        std::string type;
        std::string name;
    };
    std::vector<Field> fields;
    std::vector<DeclPtr> methods;
    std::vector<std::string> typeParams; // non-empty → this is a template

    StructDecl(const std::string& name, const std::vector<Field>& fields)
        : Decl(name), fields(fields) {}

    void accept(class ASTVisitor* visitor) override;
};

// union MyUnion { int i; float f; *uint8 b; }
// All fields share offset 0. Size = sizeof(largest field).
// Field access uses a cast to the field type.
class UnionDecl : public Decl {
public:
    std::vector<StructDecl::Field> fields;

    UnionDecl(const std::string& name, const std::vector<StructDecl::Field>& fields)
        : Decl(name), fields(fields) {}

    void accept(class ASTVisitor* visitor) override;
};

class ExternDecl : public Decl {
public:
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params;

    ExternDecl(const std::string& name, const std::string& returnType,
               const std::vector<std::pair<std::string, std::string>>& params)
        : Decl(name), returnType(returnType), params(params) {}

    void accept(class ASTVisitor* visitor) override;
};

// ============================================================================
// Statements
// ============================================================================

class Stmt : public ASTNode {
public:
    virtual ~Stmt() = default;
};

class BlockStmt : public Stmt {
public:
    std::vector<BlockItem> items;  // Unified list of declarations and statements

    explicit BlockStmt(const std::vector<BlockItem>& blk_items = {})
        : items(blk_items) {}

    void accept(class ASTVisitor* visitor) override;
};

class IfStmt : public Stmt {
public:
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch;

    IfStmt(ExprPtr cond, StmtPtr thenBr, StmtPtr elseBr = nullptr)
        : condition(cond), thenBranch(thenBr), elseBranch(elseBr) {}

    void accept(class ASTVisitor* visitor) override;
};

class ForStmt : public Stmt {
public:
    StmtPtr init;
    ExprPtr condition;
    ExprPtr step;
    StmtPtr body;

    ForStmt(StmtPtr init, ExprPtr cond, ExprPtr step, StmtPtr body)
        : init(init), condition(cond), step(step), body(body) {}

    void accept(class ASTVisitor* visitor) override;
};

class WhileStmt : public Stmt {
public:
    ExprPtr condition;
    StmtPtr body;

    WhileStmt(ExprPtr cond, StmtPtr body)
        : condition(cond), body(body) {}

    void accept(class ASTVisitor* visitor) override;
};

class ReturnStmt : public Stmt {
public:
    ExprPtr value;

    explicit ReturnStmt(ExprPtr val = nullptr) : value(val) {}

    void accept(class ASTVisitor* visitor) override;
};

class BreakStmt : public Stmt {
public:
    void accept(class ASTVisitor* visitor) override;
};

class ContinueStmt : public Stmt {
public:
    void accept(class ASTVisitor* visitor) override;
};

class SwitchStmt : public Stmt {
public:
    struct Case {
        ExprPtr value;               // nullptr = default
        std::vector<StmtPtr> stmts;
    };
    ExprPtr subject;
    std::vector<Case> cases;

    SwitchStmt(ExprPtr subj, std::vector<Case> cases)
        : subject(subj), cases(std::move(cases)) {}

    void accept(class ASTVisitor* visitor) override;
};

// throw expr;
class ThrowStmt : public Stmt {
public:
    ExprPtr     value;
    std::string valueType; // filled in by TypeChecker
    explicit ThrowStmt(ExprPtr v) : value(std::move(v)) {}
    void accept(class ASTVisitor* visitor) override;
};

// try { body } catch (Type name) { handler } finally { cleanup }
class TryStmt : public Stmt {
public:
    struct CatchClause {
        std::string type;
        std::string name;
        StmtPtr     body;
    };
    StmtPtr               body;
    std::vector<CatchClause> catches;
    StmtPtr               finally; // may be nullptr

    TryStmt(StmtPtr body, std::vector<CatchClause> catches, StmtPtr finally)
        : body(std::move(body)), catches(std::move(catches)),
          finally(std::move(finally)) {}

    void accept(class ASTVisitor* visitor) override;
};

// thread_join(*void tid)
class ThreadJoinStmt : public Stmt {
public:
    ExprPtr tid;
    explicit ThreadJoinStmt(ExprPtr t) : tid(std::move(t)) {}
    void accept(class ASTVisitor* visitor) override;
};

// Inline assembly: asm("cli") or asm("op" : : "r"(x) : "rax")
class AsmStmt : public Stmt {
public:
    std::string asmString;
    // Extended asm: each entry is (constraint, expression)
    std::vector<std::pair<std::string, ExprPtr>> inputs;
    std::vector<std::string> clobbers;

    AsmStmt(std::string asmStr,
            std::vector<std::pair<std::string, ExprPtr>> inputs = {},
            std::vector<std::string> clobbers = {})
        : asmString(std::move(asmStr)),
          inputs(std::move(inputs)),
          clobbers(std::move(clobbers)) {}

    void accept(class ASTVisitor* visitor) override;
};

class ExprStmt : public Stmt {
public:
    ExprPtr expr;

    explicit ExprStmt(ExprPtr e) : expr(e) {}

    void accept(class ASTVisitor* visitor) override;
};

// ============================================================================
// Expressions
// ============================================================================

class Expr : public ASTNode {
public:
    virtual ~Expr() = default;
};

class BinaryExpr : public Expr {
public:
    ExprPtr left;
    std::string op;
    ExprPtr right;

    BinaryExpr(ExprPtr left, const std::string& op, ExprPtr right)
        : left(left), op(op), right(right) {}

    void accept(class ASTVisitor* visitor) override;
};

class UnaryExpr : public Expr {
public:
    std::string op;
    ExprPtr operand;

    UnaryExpr(const std::string& op, ExprPtr operand)
        : op(op), operand(operand) {}

    void accept(class ASTVisitor* visitor) override;
};

class CallExpr : public Expr {
public:
    ExprPtr callee;
    std::vector<ExprPtr> args;

    CallExpr(ExprPtr callee, const std::vector<ExprPtr>& args = {})
        : callee(callee), args(args) {}

    void accept(class ASTVisitor* visitor) override;
};

class IndexExpr : public Expr {
public:
    ExprPtr base;
    ExprPtr index;

    IndexExpr(ExprPtr base, ExprPtr index)
        : base(base), index(index) {}

    void accept(class ASTVisitor* visitor) override;
};

class MemberExpr : public Expr {
public:
    ExprPtr base;
    std::string member;

    MemberExpr(ExprPtr base, const std::string& member)
        : base(base), member(member) {}

    void accept(class ASTVisitor* visitor) override;
};

class CastExpr : public Expr {
public:
    std::string targetType;
    ExprPtr expr;

    CastExpr(const std::string& type, ExprPtr expr)
        : targetType(type), expr(expr) {}

    void accept(class ASTVisitor* visitor) override;
};

class LiteralExpr : public Expr {
public:
    enum class Kind { INT, FLOAT, STRING, CHAR, BOOL, NULL_VAL };

    Kind kind;
    std::string value;

    LiteralExpr(Kind kind, const std::string& value)
        : kind(kind), value(value) {}

    void accept(class ASTVisitor* visitor) override;
};

class IdentExpr : public Expr {
public:
    std::string name;

    explicit IdentExpr(const std::string& name) : name(name) {}

    void accept(class ASTVisitor* visitor) override;
};

class AllocExpr : public Expr {
public:
    std::string elemType;  // T in alloc(T, N)
    ExprPtr count;         // N

    AllocExpr(const std::string& type, ExprPtr count)
        : elemType(type), count(count) {}

    void accept(class ASTVisitor* visitor) override;
};

class TemplateCallExpr : public Expr {
public:
    std::string templateName;
    std::vector<std::string> typeArgs;
    std::vector<ExprPtr> args;

    TemplateCallExpr(std::string name, std::vector<std::string> typeArgs, std::vector<ExprPtr> args)
        : templateName(std::move(name)), typeArgs(std::move(typeArgs)), args(std::move(args)) {}

    void accept(class ASTVisitor* visitor) override;
};

class StructInitExpr : public Expr {
public:
    std::string structName;
    // pair: (field_name, value) — field_name is empty string for positional init
    std::vector<std::pair<std::string, ExprPtr>> fieldInits;

    StructInitExpr(const std::string& name,
                   std::vector<std::pair<std::string, ExprPtr>> inits)
        : structName(name), fieldInits(std::move(inits)) {}

    void accept(class ASTVisitor* visitor) override;
};

// Lambda / anonymous function: fn(int x) -> int { return x * 2; }
class LambdaExpr : public Expr {
public:
    std::vector<std::pair<std::string, std::string>> params; // (type, name)
    std::string returnType;
    StmtPtr body;
    // Populated by TypeChecker: variables captured from enclosing scope
    std::vector<std::pair<std::string, std::string>> captures; // (name, type)

    LambdaExpr(std::vector<std::pair<std::string, std::string>> params,
               std::string returnType, StmtPtr body)
        : params(std::move(params)), returnType(std::move(returnType)),
          body(std::move(body)) {}

    void accept(class ASTVisitor* visitor) override;
};

// sizeof(T) -> int64
class SizeofExpr : public Expr {
public:
    std::string typeName;
    explicit SizeofExpr(std::string t) : typeName(std::move(t)) {}
    void accept(class ASTVisitor* visitor) override;
};

// thread_create(fn()->void) -> *void  (returns thread handle)
class ThreadCreateExpr : public Expr {
public:
    ExprPtr worker;
    explicit ThreadCreateExpr(ExprPtr w) : worker(std::move(w)) {}
    void accept(class ASTVisitor* visitor) override;
};

// ============================================================================
// Program (root node)
// ============================================================================

class Program : public ASTNode {
public:
    std::vector<DeclPtr> declarations;

    explicit Program(const std::vector<DeclPtr>& decls = {})
        : declarations(decls) {}

    void accept(class ASTVisitor* visitor) override;
};

// ============================================================================
// Visitor pattern for AST traversal
// ============================================================================

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Program* node) = 0;
    virtual void visit(FunctionDecl* node) = 0;
    virtual void visit(VarDecl* node) = 0;
    virtual void visit(StructDecl* node) = 0;
    virtual void visit(ExternDecl* node) = 0;
    virtual void visit(InterfaceDecl* node) = 0;
    virtual void visit(BlockStmt* node) = 0;
    virtual void visit(IfStmt* node) = 0;
    virtual void visit(ForStmt* node) = 0;
    virtual void visit(WhileStmt* node) = 0;
    virtual void visit(ReturnStmt* node) = 0;
    virtual void visit(BreakStmt* node) = 0;
    virtual void visit(ContinueStmt* node) = 0;
    virtual void visit(SwitchStmt* node) = 0;
    virtual void visit(ExprStmt* node) = 0;
    virtual void visit(BinaryExpr* node) = 0;
    virtual void visit(UnaryExpr* node) = 0;
    virtual void visit(CallExpr* node) = 0;
    virtual void visit(IndexExpr* node) = 0;
    virtual void visit(MemberExpr* node) = 0;
    virtual void visit(CastExpr* node) = 0;
    virtual void visit(LiteralExpr* node) = 0;
    virtual void visit(IdentExpr* node) = 0;
    virtual void visit(StructInitExpr* node) = 0;
    virtual void visit(AllocExpr* node) = 0;
    virtual void visit(TemplateCallExpr* node) = 0;
    virtual void visit(LambdaExpr* node) = 0;
    virtual void visit(AsmStmt* node) = 0;
    virtual void visit(UnionDecl* node) = 0;
    virtual void visit(SizeofExpr* node) = 0;
    virtual void visit(ThreadCreateExpr* node) = 0;
    virtual void visit(ThreadJoinStmt* node) = 0;
    virtual void visit(ThrowStmt* node) = 0;
    virtual void visit(TryStmt* node) = 0;
};
