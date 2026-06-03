#pragma once

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <stack>
#include "../ast/ast.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"

class CodeGen : public ASTVisitor {
public:
    CodeGen();
    ~CodeGen();

    // Generate code — fills internal module, returns raw pointer (null on failure)
    llvm::Module* generateCode(std::shared_ptr<Program> program);

    // Get the generated LLVM module (non-owning)
    llvm::Module* getModule() const { return module.get(); }

    // Print LLVM IR to stdout
    void printIR() const;

    // Emit native object file
    bool emitObjectFile(const std::string& filename);

private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    // Symbol table: maps variable/function names to LLVM Values
    std::map<std::string, llvm::Value*> symbolTable;
    std::vector<std::map<std::string, llvm::Value*>> scopeStack;

    // Current function being compiled
    llvm::Function* currentFunction = nullptr;

    // Concrete struct registry
    std::map<std::string, llvm::StructType*> structTypes;
    std::map<std::string, std::vector<StructDecl::Field>> structFields;

    // Template struct registry
    std::map<std::string, StructDecl*> templateDecls;

    // Interface registry: name → vtable type + method order
    std::map<std::string, llvm::StructType*> ifaceVtableTypes;
    std::map<std::string, std::vector<std::string>> ifaceMethodOrder;
    // Fat pointer type per interface: %I = type { ptr, ptr }
    std::map<std::string, llvm::StructType*> ifaceFatPtrTypes;

    // Eskiu param types per function — for interface boxing at call sites
    std::map<std::string, std::vector<std::string>> funcEskiuParamTypes;

    // Interface method return types — indexed by [ifaceName][methodIndex]
    std::map<std::string, std::vector<std::string>> ifaceMethodReturnTypes;
    // Interface method param Eskiu types (excluding self) — [ifaceName][methodIndex]
    std::map<std::string, std::vector<std::vector<std::string>>> ifaceMethodParamEskiuTypes;

    // Global-scope variable type tracking (complement to varTypeStack which is function-scoped)
    std::map<std::string, std::string> globalVarTypes;

    // Evaluate an expression as an LLVM Constant (for global variable initializers).
    // Returns nullptr for expressions that cannot be folded to a constant.
    llvm::Constant* evaluateConstantExpr(ExprPtr expr);

    // Helpers
    llvm::Value* boxAsInterface(const std::string& ifaceName,
                                const std::string& structName,
                                llvm::Value* structPtr);
    void ensureTemplateInstantiated(const std::string& mangledName,
                                    const std::string& templateName,
                                    const std::vector<std::string>& args);
    // Template function registry
    std::map<std::string, FunctionDecl*> funcTemplateDecls;
    // Active type param substitutions during template function instantiation
    std::map<std::string, std::string> typeParamOverride;

    // Variable type tracking for MemberExpr/IndexExpr resolution
    std::vector<std::map<std::string, std::string>> varTypeStack;
    void defineVarType(const std::string& name, const std::string& type);
    std::string lookupVarType(const std::string& name) const;

    // Break/continue targets for the innermost loop
    llvm::BasicBlock* breakTarget    = nullptr;
    llvm::BasicBlock* continueTarget = nullptr;

    // sret (structure return) support for large struct returns
    // Maps function name → actual return struct type (the LLVM function itself returns void)
    std::map<std::string, llvm::StructType*> funcSretTypes;
    // Active sret pointer for the current function (null if not sret)
    llvm::Value* currentSretParam = nullptr;

    // Returns true if retType is an aggregate that must use sret on this target
    bool needsSret(llvm::Type* retType) const;

    // Type system: map Eskiu types to LLVM types
    llvm::Type* getTypeFromString(const std::string& typeStr);
    bool isPointerType(const std::string& typeStr) const;
    bool isIntType(const std::string& typeStr) const;
    bool isFloatType(const std::string& typeStr) const;

    // Resolve the Eskiu type string of an expression (for struct/array access)
    std::string getExprEskiuType(ExprPtr expr) const;

    // Helper methods
    void pushScope();
    void popScope();
    llvm::Value* lookupSymbol(const std::string& name);
    void defineSymbol(const std::string& name, llvm::Value* value);

    // Visitor methods
    void visit(Program* node) override;
    void visit(FunctionDecl* node) override;
    void visit(VarDecl* node) override;
    void visit(StructDecl* node) override;
    void visit(ExternDecl* node) override;
    void visit(BlockStmt* node) override;
    void visit(IfStmt* node) override;
    void visit(ForStmt* node) override;
    void visit(WhileStmt* node) override;
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
    void visit(InterfaceDecl* node) override;
    void visit(ContinueStmt* node) override;
    void visit(SwitchStmt* node) override;
    void visit(StructInitExpr* node) override;
    void visit(AllocExpr* node) override;
    void visit(TemplateCallExpr* node) override;
    void visit(LambdaExpr* node) override;

    void emitStructInitInto(llvm::Value* dest, StructInitExpr* init);
    llvm::Function* getOrDeclareFunc(const std::string& name, llvm::Type* retType,
                                     std::vector<llvm::Type*> paramTypes, bool isVarArg = false);

    // Expression evaluation (returns LLVM Value)
    std::stack<llvm::Value*> exprValueStack;
    llvm::Value* evaluateExpr(ExprPtr expr);
    llvm::Value* evaluateLValue(ExprPtr expr);
};
