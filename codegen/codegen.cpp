#include "codegen.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_os_ostream.h"
#include <iostream>

CodeGen::CodeGen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("eskiu", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)) {}

CodeGen::~CodeGen() = default;

std::unique_ptr<llvm::Module> CodeGen::generateCode(std::shared_ptr<Program> program) {
    program->accept(this);

    // Verify the module
    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);
    if (llvm::verifyModule(*module, &errorStream)) {
        std::cerr << "LLVM verification failed:\n" << errorStr << std::endl;
        return nullptr;
    }

    // Return the module (ownership is transferred)
    return std::move(module);
}

void CodeGen::printIR() const {
    if (module) {
        std::cerr << "Module has " << module->getFunctionList().size() << " functions\n";
        llvm::raw_os_ostream out(std::cout);
        module->print(out, nullptr);
        out.flush();
    } else {
        std::cerr << "Module is null!\n";
    }
}

// ============================================================================
// Type System
// ============================================================================

llvm::Type* CodeGen::getTypeFromString(const std::string& typeStr) {
    if (typeStr == "int") {
        return llvm::Type::getInt32Ty(*context);
    }
    if (typeStr == "int8") {
        return llvm::Type::getInt8Ty(*context);
    }
    if (typeStr == "int16") {
        return llvm::Type::getInt16Ty(*context);
    }
    if (typeStr == "int32") {
        return llvm::Type::getInt32Ty(*context);
    }
    if (typeStr == "int64") {
        return llvm::Type::getInt64Ty(*context);
    }
    if (typeStr == "float") {
        return llvm::Type::getFloatTy(*context);
    }
    if (typeStr == "double") {
        return llvm::Type::getDoubleTy(*context);
    }
    if (typeStr == "bool") {
        return llvm::Type::getInt1Ty(*context);
    }
    if (typeStr == "void") {
        return llvm::Type::getVoidTy(*context);
    }
    if (typeStr == "string" || typeStr.find("*") != std::string::npos) {
        // string is char*, pointers are i8*
        return llvm::PointerType::get(*context, 0);
    }
    if (typeStr == "char") {
        return llvm::Type::getInt8Ty(*context);
    }

    // Default to i32
    std::cerr << "Warning: unknown type '" << typeStr << "', defaulting to i32" << std::endl;
    return llvm::Type::getInt32Ty(*context);
}

bool CodeGen::isPointerType(const std::string& typeStr) const {
    return typeStr.find("*") != std::string::npos || typeStr == "string";
}

bool CodeGen::isIntType(const std::string& typeStr) const {
    return typeStr.find("int") != std::string::npos || typeStr == "bool" || typeStr == "char";
}

bool CodeGen::isFloatType(const std::string& typeStr) const {
    return typeStr == "float" || typeStr == "double";
}

// ============================================================================
// Symbol Table Management
// ============================================================================

void CodeGen::pushScope() {
    scopeStack.push_back(symbolTable);
}

void CodeGen::popScope() {
    if (!scopeStack.empty()) {
        symbolTable = scopeStack.back();
        scopeStack.pop_back();
    }
}

llvm::Value* CodeGen::lookupSymbol(const std::string& name) {
    auto it = symbolTable.find(name);
    if (it != symbolTable.end()) {
        return it->second;
    }
    return nullptr;
}

void CodeGen::defineSymbol(const std::string& name, llvm::Value* value) {
    symbolTable[name] = value;
}

// ============================================================================
// Visitor Methods
// ============================================================================

void CodeGen::visit(Program* node) {
    for (auto& decl : node->declarations) {
        decl->accept(this);
    }
}

void CodeGen::visit(FunctionDecl* node) {
    // Get parameter types
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : node->params) {
        if (param.first == "...") {
            // Variadic parameters - skip for now
            continue;
        }
        paramTypes.push_back(getTypeFromString(param.first));
    }

    // Create function type
    llvm::Type* returnType = getTypeFromString(node->returnType);
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, false);

    // Create function
    llvm::Function* func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->name, module.get());

    // Set parameter names
    size_t paramIdx = 0;
    for (auto& arg : func->args()) {
        if (paramIdx < node->params.size() && node->params[paramIdx].first != "...") {
            arg.setName(node->params[paramIdx].second);
            paramIdx++;
        }
    }

    // Create entry block
    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entryBlock);

    // Save current function
    llvm::Function* prevFunc = currentFunction;
    currentFunction = func;

    // Push scope for function parameters
    pushScope();

    // Define parameters in symbol table
    paramIdx = 0;
    for (auto& arg : func->args()) {
        if (paramIdx < node->params.size() && node->params[paramIdx].first != "...") {
            defineSymbol(node->params[paramIdx].second, &arg);
            paramIdx++;
        }
    }

    // Generate function body
    if (node->body) {
        node->body->accept(this);
    }

    // If no explicit return, add default return
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (returnType->isVoidTy()) {
            builder->CreateRetVoid();
        } else if (returnType->isIntegerTy()) {
            builder->CreateRet(llvm::ConstantInt::get(returnType, 0));
        } else {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    // Pop scope
    popScope();
    currentFunction = prevFunc;
}

void CodeGen::visit(VarDecl* node) {
    // For now, just allocate on stack
    llvm::Type* type = getTypeFromString(node->type);
    llvm::AllocaInst* alloca = builder->CreateAlloca(type, nullptr, node->name);
    defineSymbol(node->name, alloca);

    // Initialize if there's an initializer
    if (node->initializer) {
        evaluateExpr(node->initializer);
        if (lastExprValue) {
            builder->CreateStore(lastExprValue, alloca);
        }
    }
}

void CodeGen::visit(StructDecl* node) {
    // TODO: Implement struct types
    std::cerr << "Warning: struct definitions not yet implemented" << std::endl;
}

void CodeGen::visit(ExternDecl* node) {
    // Get parameter types
    std::vector<llvm::Type*> paramTypes;
    bool hasVarargs = false;

    for (auto& param : node->params) {
        if (param.first == "...") {
            hasVarargs = true;
            break;
        }
        paramTypes.push_back(getTypeFromString(param.first));
    }

    // Create function type
    llvm::Type* returnType = getTypeFromString(node->returnType);
    llvm::FunctionType* funcType = llvm::FunctionType::get(returnType, paramTypes, hasVarargs);

    // Create external function declaration
    llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, node->name, module.get());
}

void CodeGen::visit(BlockStmt* node) {
    for (auto& stmt : node->statements) {
        stmt->accept(this);
    }
}

void CodeGen::visit(IfStmt* node) {
    // Evaluate condition
    evaluateExpr(node->condition);
    llvm::Value* cond = lastExprValue;

    if (!cond) {
        throw std::runtime_error("If condition evaluation failed");
    }

    // Convert to i1
    if (!cond->getType()->isIntegerTy(1)) {
        cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    }

    // Create blocks
    llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(*context, "then", currentFunction);
    llvm::BasicBlock* elseBlock = nullptr;
    llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(*context, "merge", currentFunction);

    if (node->elseBranch) {
        elseBlock = llvm::BasicBlock::Create(*context, "else", currentFunction);
        builder->CreateCondBr(cond, thenBlock, elseBlock);
    } else {
        builder->CreateCondBr(cond, thenBlock, mergeBlock);
    }

    // Then block
    builder->SetInsertPoint(thenBlock);
    node->thenBranch->accept(this);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(mergeBlock);
    }

    // Else block
    if (node->elseBranch) {
        builder->SetInsertPoint(elseBlock);
        node->elseBranch->accept(this);
        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateBr(mergeBlock);
        }
    }

    // Merge block
    builder->SetInsertPoint(mergeBlock);
}

void CodeGen::visit(WhileStmt* node) {
    llvm::BasicBlock* loopBlock = llvm::BasicBlock::Create(*context, "while", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "while_body", currentFunction);
    llvm::BasicBlock* exitBlock = llvm::BasicBlock::Create(*context, "while_exit", currentFunction);

    builder->CreateBr(loopBlock);

    // Loop condition
    builder->SetInsertPoint(loopBlock);
    evaluateExpr(node->condition);
    llvm::Value* cond = lastExprValue;
    if (!cond->getType()->isIntegerTy(1)) {
        cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    }
    builder->CreateCondBr(cond, bodyBlock, exitBlock);

    // Loop body
    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(loopBlock);
    }

    // Exit
    builder->SetInsertPoint(exitBlock);
}

void CodeGen::visit(ForStmt* node) {
    llvm::BasicBlock* loopBlock = llvm::BasicBlock::Create(*context, "for", currentFunction);
    llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(*context, "for_body", currentFunction);
    llvm::BasicBlock* stepBlock = llvm::BasicBlock::Create(*context, "for_step", currentFunction);
    llvm::BasicBlock* exitBlock = llvm::BasicBlock::Create(*context, "for_exit", currentFunction);

    // Init
    if (node->init) {
        node->init->accept(this);
    }
    builder->CreateBr(loopBlock);

    // Condition
    builder->SetInsertPoint(loopBlock);
    if (node->condition) {
        evaluateExpr(node->condition);
        llvm::Value* cond = lastExprValue;
        if (!cond->getType()->isIntegerTy(1)) {
            cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
        }
        builder->CreateCondBr(cond, bodyBlock, exitBlock);
    } else {
        builder->CreateBr(bodyBlock);
    }

    // Body
    builder->SetInsertPoint(bodyBlock);
    node->body->accept(this);
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(stepBlock);
    }

    // Step
    builder->SetInsertPoint(stepBlock);
    if (node->step) {
        evaluateExpr(node->step);
    }
    builder->CreateBr(loopBlock);

    // Exit
    builder->SetInsertPoint(exitBlock);
}

void CodeGen::visit(ReturnStmt* node) {
    if (node->value) {
        evaluateExpr(node->value);
        builder->CreateRet(lastExprValue);
    } else {
        builder->CreateRetVoid();
    }
}

void CodeGen::visit(BreakStmt* node) {
    // TODO: Implement break
    std::cerr << "Warning: break not yet implemented" << std::endl;
}

void CodeGen::visit(ExprStmt* node) {
    evaluateExpr(node->expr);
}

void CodeGen::visit(BinaryExpr* node) {
    evaluateExpr(node->left);
    llvm::Value* left = lastExprValue;

    evaluateExpr(node->right);
    llvm::Value* right = lastExprValue;

    if (!left || !right) {
        throw std::runtime_error("Binary expression operand evaluation failed");
    }

    if (node->op == "+") {
        lastExprValue = builder->CreateAdd(left, right);
    } else if (node->op == "-") {
        lastExprValue = builder->CreateSub(left, right);
    } else if (node->op == "*") {
        lastExprValue = builder->CreateMul(left, right);
    } else if (node->op == "/") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateSDiv(left, right);
        } else {
            lastExprValue = builder->CreateFDiv(left, right);
        }
    } else if (node->op == "%") {
        lastExprValue = builder->CreateSRem(left, right);
    } else if (node->op == "==") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateICmpEQ(left, right);
        } else {
            lastExprValue = builder->CreateFCmpOEQ(left, right);
        }
    } else if (node->op == "!=") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateICmpNE(left, right);
        } else {
            lastExprValue = builder->CreateFCmpONE(left, right);
        }
    } else if (node->op == "<") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateICmpSLT(left, right);
        } else {
            lastExprValue = builder->CreateFCmpOLT(left, right);
        }
    } else if (node->op == ">") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateICmpSGT(left, right);
        } else {
            lastExprValue = builder->CreateFCmpOGT(left, right);
        }
    } else if (node->op == "<=") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateICmpSLE(left, right);
        } else {
            lastExprValue = builder->CreateFCmpOLE(left, right);
        }
    } else if (node->op == ">=") {
        if (left->getType()->isIntegerTy()) {
            lastExprValue = builder->CreateICmpSGE(left, right);
        } else {
            lastExprValue = builder->CreateFCmpOGE(left, right);
        }
    } else if (node->op == "&&") {
        lastExprValue = builder->CreateLogicalAnd(left, right);
    } else if (node->op == "||") {
        lastExprValue = builder->CreateLogicalOr(left, right);
    } else if (node->op == "=") {
        // Assignment - left should be a pointer (from variable)
        builder->CreateStore(right, left);
        lastExprValue = right;
    } else {
        throw std::runtime_error("Unknown binary operator: " + node->op);
    }
}

void CodeGen::visit(UnaryExpr* node) {
    evaluateExpr(node->operand);
    llvm::Value* val = lastExprValue;

    if (!val) {
        throw std::runtime_error("Unary operand evaluation failed");
    }

    if (node->op == "-") {
        lastExprValue = builder->CreateNeg(val);
    } else if (node->op == "!") {
        lastExprValue = builder->CreateNot(val);
    } else if (node->op == "&") {
        // Address-of: return the pointer itself
        lastExprValue = val;
    } else if (node->op == "*") {
        // Dereference - assume i8 type for now (LLVM 22 has opaque pointers)
        lastExprValue = builder->CreateLoad(llvm::Type::getInt8Ty(*context), val);
    } else {
        throw std::runtime_error("Unknown unary operator: " + node->op);
    }
}

void CodeGen::visit(CallExpr* node) {
    // Evaluate callee
    evaluateExpr(node->callee);
    llvm::Value* calleeVal = lastExprValue;

    if (!calleeVal || !llvm::isa<llvm::Function>(calleeVal)) {
        throw std::runtime_error("Call target is not a function");
    }

    llvm::Function* func = llvm::cast<llvm::Function>(calleeVal);

    // Evaluate arguments
    std::vector<llvm::Value*> args;
    for (auto& arg : node->args) {
        evaluateExpr(arg);
        args.push_back(lastExprValue);
    }

    // Create call
    lastExprValue = builder->CreateCall(func, args);
}

void CodeGen::visit(IndexExpr* node) {
    // TODO: Array indexing
    std::cerr << "Warning: array indexing not yet implemented" << std::endl;
}

void CodeGen::visit(MemberExpr* node) {
    // TODO: Member access
    std::cerr << "Warning: member access not yet implemented" << std::endl;
}

void CodeGen::visit(CastExpr* node) {
    evaluateExpr(node->expr);
    llvm::Value* val = lastExprValue;

    llvm::Type* targetType = getTypeFromString(node->targetType);

    if (val->getType() == targetType) {
        lastExprValue = val;
    } else if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) {
        // Integer to integer
        unsigned srcWidth = llvm::cast<llvm::IntegerType>(val->getType())->getBitWidth();
        unsigned dstWidth = llvm::cast<llvm::IntegerType>(targetType)->getBitWidth();
        if (srcWidth < dstWidth) {
            lastExprValue = builder->CreateSExt(val, targetType);
        } else {
            lastExprValue = builder->CreateTrunc(val, targetType);
        }
    } else if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy()) {
        lastExprValue = builder->CreateSIToFP(val, targetType);
    } else if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy()) {
        lastExprValue = builder->CreateFPToSI(val, targetType);
    } else {
        throw std::runtime_error("Cannot cast between these types");
    }
}

void CodeGen::visit(LiteralExpr* node) {
    switch (node->kind) {
        case LiteralExpr::Kind::INT: {
            long long val = std::stoll(node->value);
            lastExprValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), val);
            break;
        }
        case LiteralExpr::Kind::FLOAT: {
            double val = std::stod(node->value);
            lastExprValue = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), val);
            break;
        }
        case LiteralExpr::Kind::STRING: {
            lastExprValue = builder->CreateGlobalString(node->value);
            break;
        }
        case LiteralExpr::Kind::BOOL: {
            bool val = node->value == "true";
            lastExprValue = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), val);
            break;
        }
        case LiteralExpr::Kind::NULL_VAL: {
            auto ptrType = llvm::PointerType::get(*context, 0);
            lastExprValue = llvm::ConstantPointerNull::get(ptrType);
            break;
        }
        case LiteralExpr::Kind::CHAR: {
            char val = node->value.empty() ? 0 : node->value[0];
            lastExprValue = llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), val);
            break;
        }
    }
}

void CodeGen::visit(IdentExpr* node) {
    // Look up variable
    llvm::Value* val = lookupSymbol(node->name);

    if (!val) {
        // Try to find as function
        val = module->getFunction(node->name);
    }

    if (!val) {
        throw std::runtime_error("Undefined variable or function: " + node->name);
    }

    // If it's a local variable (alloca), load it
    if (llvm::isa<llvm::AllocaInst>(val)) {
        lastExprValue = builder->CreateLoad(
            llvm::cast<llvm::AllocaInst>(val)->getAllocatedType(), val);
    } else {
        lastExprValue = val;
    }
}

void CodeGen::evaluateExpr(ExprPtr expr) {
    expr->accept(this);
}

bool CodeGen::emitObjectFile(const std::string& filename) {
    // TODO: Implement object file emission
    std::cerr << "Warning: object file emission not yet implemented" << std::endl;
    return false;
}
