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

    // Generate code from AST and return LLVM module
    std::unique_ptr<llvm::Module> generateCode(std::shared_ptr<Program> program);

    // Get the generated LLVM module
    llvm::Module* getModule() const { return module.get(); }

    // Print LLVM IR to stdout
    void printIR() const;

    // Emit object file
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

    // Type system: map Eskiu types to LLVM types
    llvm::Type* getTypeFromString(const std::string& typeStr);
    bool isPointerType(const std::string& typeStr) const;
    bool isIntType(const std::string& typeStr) const;
    bool isFloatType(const std::string& typeStr) const;

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

    // Expression evaluation (returns LLVM Value)
    std::stack<llvm::Value*> exprValueStack;
    llvm::Value* evaluateExpr(ExprPtr expr);
};
