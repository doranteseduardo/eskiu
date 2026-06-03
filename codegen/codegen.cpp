#include "codegen.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/raw_os_ostream.h"
#include <iostream>

// ============================================================================
// Template utilities (file-local)
// ============================================================================

static std::string mangleTemplate(const std::string& type) {
    std::string out;
    for (char c : type) {
        if (c == '<' || c == '>' || c == ',') out += '_';
        else if (c != ' ')                   out += c;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

static std::pair<std::string, std::vector<std::string>>
splitTemplateType(const std::string& type) {
    size_t lt = type.find('<');
    if (lt == std::string::npos) return {type, {}};
    std::string name = type.substr(0, lt);
    std::string inner = type.substr(lt + 1, type.size() - lt - 2);
    std::vector<std::string> args;
    int depth = 0; std::string cur;
    for (char c : inner) {
        if (c == '<') { depth++; cur += c; }
        else if (c == '>') { depth--; cur += c; }
        else if (c == ',' && depth == 0) { args.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) args.push_back(cur);
    return {name, args};
}

static std::string substType(const std::string& t,
                             const std::map<std::string, std::string>& subs) {
    auto it = subs.find(t);
    if (it != subs.end()) return it->second;
    if (!t.empty() && t.front() == '*') return "*" + substType(t.substr(1), subs);
    if (!t.empty() && t.back()  == '*') return substType(t.substr(0, t.size()-1), subs) + "*";
    size_t lb = t.rfind('[');
    if (lb != std::string::npos && t.back() == ']')
        return substType(t.substr(0, lb), subs) + t.substr(lb);
    // Template type: Name<T, E> → substitute type args
    size_t lt = t.find('<');
    if (lt != std::string::npos && t.back() == '>') {
        std::string name  = t.substr(0, lt);
        std::string inner = t.substr(lt + 1, t.size() - lt - 2);
        std::vector<std::string> args;
        int depth = 0; std::string cur;
        for (char c : inner) {
            if      (c == '<') { depth++; cur += c; }
            else if (c == '>') { depth--; cur += c; }
            else if (c == ',' && depth == 0) { args.push_back(substType(cur, subs)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) args.push_back(substType(cur, subs));
        std::string result = name + "<";
        for (size_t i = 0; i < args.size(); ++i) { if (i) result += ","; result += args[i]; }
        return result + ">";
    }
    return t;
}

// ============================================================================

CodeGen::CodeGen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("eskiu", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)) {}

CodeGen::~CodeGen() = default;

llvm::Module* CodeGen::generateCode(std::shared_ptr<Program> program) {
    // Set target triple + data layout early so sizeof queries work in alloc()
    llvm::InitializeNativeTarget();
    std::string tripleStr = llvm::sys::getDefaultTargetTriple();
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);
    std::string terr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, terr);
    if (target) {
        llvm::TargetOptions opt;
        auto* tm = target->createTargetMachine(triple, llvm::sys::getHostCPUName(),
                                                "", opt, llvm::Reloc::PIC_);
        module->setDataLayout(tm->createDataLayout());
        delete tm;
    }

    program->accept(this);

    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);
    if (llvm::verifyModule(*module, &errorStream)) {
        std::cerr << "LLVM verification failed:\n" << errorStr << std::endl;
        return nullptr;
    }

    return module.get();
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
    // Apply type parameter override during template function instantiation
    if (!typeParamOverride.empty()) {
        std::string resolved = substType(typeStr, typeParamOverride);
        if (resolved != typeStr) return getTypeFromString(resolved);
    }

    // Leading pointer: *T (spec style)
    if (!typeStr.empty() && typeStr.front() == '*') {
        return llvm::PointerType::get(*context, 0);
    }
    // Trailing pointer: T* (C style)
    if (!typeStr.empty() && typeStr.back() == '*') {
        return llvm::PointerType::get(*context, 0);
    }

    if (typeStr == "int"  || typeStr == "int32")  return llvm::Type::getInt32Ty(*context);
    if (typeStr == "int8")                         return llvm::Type::getInt8Ty(*context);
    if (typeStr == "int16")                        return llvm::Type::getInt16Ty(*context);
    if (typeStr == "int64")                        return llvm::Type::getInt64Ty(*context);
    if (typeStr == "uint" || typeStr == "uint32")  return llvm::Type::getInt32Ty(*context);
    if (typeStr == "uint8")                        return llvm::Type::getInt8Ty(*context);
    if (typeStr == "uint16")                       return llvm::Type::getInt16Ty(*context);
    if (typeStr == "uint64")                       return llvm::Type::getInt64Ty(*context);
    if (typeStr == "float")                        return llvm::Type::getFloatTy(*context);
    if (typeStr == "double")                       return llvm::Type::getDoubleTy(*context);
    if (typeStr == "bool")                         return llvm::Type::getInt1Ty(*context);
    if (typeStr == "void")                         return llvm::Type::getVoidTy(*context);
    if (typeStr == "char")                         return llvm::Type::getInt8Ty(*context);
    if (typeStr == "string")                       return llvm::PointerType::get(*context, 0);

    // Struct type with "struct:" prefix (from type checker normalization)
    if (typeStr.find("struct:") == 0) {
        std::string name = typeStr.substr(7);
        auto it = structTypes.find(name);
        if (it != structTypes.end()) return it->second;
        return llvm::PointerType::get(*context, 0); // forward ref placeholder
    }

    // Bare struct name (from parser, before normalization)
    {
        auto it = structTypes.find(typeStr);
        if (it != structTypes.end()) return it->second;
    }

    // Template instantiation: "Result<int,string>" → %Result_int_string
    if (typeStr.find('<') != std::string::npos) {
        auto [tname, args] = splitTemplateType(typeStr);
        std::string mangled = mangleTemplate(typeStr);
        ensureTemplateInstantiated(mangled, tname, args);
        auto it = structTypes.find(mangled);
        if (it != structTypes.end()) return it->second;
    }

    // Fixed-size array: T[N]  (e.g. "uint8[858]")
    {
        size_t lb = typeStr.rfind('[');
        if (lb != std::string::npos && typeStr.back() == ']') {
            std::string elemStr = typeStr.substr(0, lb);
            std::string sizeStr = typeStr.substr(lb + 1, typeStr.size() - lb - 2);
            llvm::Type* elem = getTypeFromString(elemStr);
            if (!sizeStr.empty()) {
                uint64_t n = std::stoull(sizeStr);
                return llvm::ArrayType::get(elem, n);
            }
            return llvm::PointerType::get(*context, 0); // unsized → pointer
        }
    }

    std::cerr << "Warning: unknown type '" << typeStr << "', defaulting to i32" << std::endl;
    return llvm::Type::getInt32Ty(*context);
}

bool CodeGen::isPointerType(const std::string& typeStr) const {
    if (typeStr.empty()) return false;
    return typeStr.front() == '*' || typeStr.back() == '*' || typeStr == "string";
}

bool CodeGen::isIntType(const std::string& typeStr) const {
    // Remove pointer suffix before checking
    std::string baseType = typeStr;
    if (!baseType.empty() && baseType.back() == '*') {
        baseType = baseType.substr(0, baseType.length() - 1);
    }
    return baseType.find("int") != std::string::npos || baseType == "bool" || baseType == "char";
}

bool CodeGen::isFloatType(const std::string& typeStr) const {
    // Remove pointer suffix before checking
    std::string baseType = typeStr;
    if (!baseType.empty() && baseType.back() == '*') {
        baseType = baseType.substr(0, baseType.length() - 1);
    }
    return baseType == "float" || baseType == "double";
}

// ============================================================================
// Symbol Table Management
// ============================================================================

void CodeGen::pushScope() {
    scopeStack.push_back(symbolTable);
    varTypeStack.push_back({});
}

void CodeGen::popScope() {
    if (!scopeStack.empty()) {
        symbolTable = scopeStack.back();
        scopeStack.pop_back();
    }
    if (!varTypeStack.empty()) {
        varTypeStack.pop_back();
    }
}

void CodeGen::defineVarType(const std::string& name, const std::string& type) {
    if (!varTypeStack.empty()) {
        varTypeStack.back()[name] = type;
    }
}

std::string CodeGen::lookupVarType(const std::string& name) const {
    for (auto it = varTypeStack.rbegin(); it != varTypeStack.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    return "";
}

std::string CodeGen::getExprEskiuType(ExprPtr expr) const {
    if (auto ident = dynamic_cast<IdentExpr*>(expr.get())) {
        return lookupVarType(ident->name);
    }
    if (auto member = dynamic_cast<MemberExpr*>(expr.get())) {
        std::string base = getExprEskiuType(member->base);
        if (base.size() > 7 && base.substr(0, 7) == "struct:") base = base.substr(7);
        if (!base.empty() && base.front() == '*') base = base.substr(1);
        auto it = structFields.find(base);
        if (it != structFields.end()) {
            for (const auto& f : it->second) {
                if (f.name == member->member) return f.type;
            }
        }
    }
    if (auto index = dynamic_cast<IndexExpr*>(expr.get())) {
        std::string base = getExprEskiuType(index->base);
        size_t lb = base.rfind('[');
        if (lb != std::string::npos) return base.substr(0, lb);
        if (!base.empty() && base.front() == '*') return base.substr(1);
        if (!base.empty() && base.back()  == '*') return base.substr(0, base.size() - 1);
    }
    return "";
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
    if (!node->typeParams.empty()) {
        funcTemplateDecls[node->name] = node;
        return;
    }

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

    // Define parameters in symbol table + type map
    paramIdx = 0;
    for (auto& arg : func->args()) {
        if (paramIdx < node->params.size() && node->params[paramIdx].first != "...") {
            defineSymbol(node->params[paramIdx].second, &arg);
            defineVarType(node->params[paramIdx].second, node->params[paramIdx].first);
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
    llvm::Type* declType = getTypeFromString(node->type);
    llvm::AllocaInst* alloca = builder->CreateAlloca(declType, nullptr, node->name);
    defineSymbol(node->name, alloca);
    // Resolve type params and mangle template names for varTypeStack
    std::string varType = !typeParamOverride.empty()
                          ? substType(node->type, typeParamOverride)
                          : node->type;
    if (varType.find('<') != std::string::npos) varType = mangleTemplate(varType);
    defineVarType(node->name, varType);

    if (node->initializer) {
        if (auto structInit = dynamic_cast<StructInitExpr*>(node->initializer.get())) {
            // Fill the alloca directly — no temporary needed
            emitStructInitInto(alloca, structInit);
        } else {
            llvm::Value* val = evaluateExpr(node->initializer);
            if (val && val->getType() != declType) {
                if (val->getType()->isIntegerTy() && declType->isIntegerTy()) {
                    unsigned src = llvm::cast<llvm::IntegerType>(val->getType())->getBitWidth();
                    unsigned dst = llvm::cast<llvm::IntegerType>(declType)->getBitWidth();
                    val = src > dst ? builder->CreateTrunc(val, declType)
                                    : builder->CreateSExt(val, declType);
                } else if (val->getType()->isIntegerTy() && declType->isFloatingPointTy()) {
                    val = builder->CreateSIToFP(val, declType);
                } else if (val->getType()->isFloatingPointTy() && declType->isIntegerTy()) {
                    val = builder->CreateFPToSI(val, declType);
                } else if (val->getType()->isFloatingPointTy() && declType->isFloatingPointTy()) {
                    val = builder->CreateFPCast(val, declType);
                }
            }
            if (val) builder->CreateStore(val, alloca);
        }
    }
}

void CodeGen::ensureTemplateInstantiated(const std::string& mangled,
                                          const std::string& tname,
                                          const std::vector<std::string>& args) {
    if (structTypes.count(mangled)) return;
    auto it = templateDecls.find(tname);
    if (it == templateDecls.end()) return;
    StructDecl* tmpl = it->second;

    auto& tp = tmpl->typeParams;
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < args.size(); ++i) subs[tp[i]] = args[i];

    std::vector<llvm::Type*> fieldTypes;
    std::vector<StructDecl::Field> fields;
    for (const auto& f : tmpl->fields) {
        std::string concrete = substType(f.type, subs);
        fieldTypes.push_back(getTypeFromString(concrete));
        fields.push_back({concrete, f.name});
    }
    llvm::StructType* st = llvm::StructType::create(*context, fieldTypes, mangled);
    structTypes[mangled] = st;
    structFields[mangled] = fields;
}

void CodeGen::visit(StructDecl* node) {
    if (!node->typeParams.empty()) {
        templateDecls[node->name] = node;
        return;
    }

    std::vector<llvm::Type*> fieldTypes;
    for (const auto& field : node->fields) {
        fieldTypes.push_back(getTypeFromString(field.type));
    }
    llvm::StructType* st = llvm::StructType::create(*context, fieldTypes, node->name);
    structTypes[node->name] = st;
    structFields[node->name] = node->fields;

    // Emit methods as mangled functions: StructName_methodName(self: *Struct, ...)
    for (const auto& method : node->methods) {
        if (auto func = dynamic_cast<FunctionDecl*>(method.get())) {
            std::vector<std::pair<std::string, std::string>> params;
            params.push_back({"*" + node->name, "self"});
            for (const auto& p : func->params) params.push_back(p);

            auto mangled = std::make_shared<FunctionDecl>(
                node->name + "_" + func->name,
                func->returnType, params, func->body);
            mangled->accept(this);
        }
    }
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
    // Generate code for items in the order they appear in the block
    for (auto& item : node->items) {
        if (std::holds_alternative<DeclPtr>(item)) {
            // Extract declaration and visit it
            auto decl = std::get<DeclPtr>(item);
            decl->accept(this);
        } else if (std::holds_alternative<StmtPtr>(item)) {
            // Extract statement and visit it
            auto stmt = std::get<StmtPtr>(item);
            stmt->accept(this);
        }
    }
}

void CodeGen::visit(IfStmt* node) {
    // Evaluate condition
    llvm::Value* cond = evaluateExpr(node->condition);

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

    builder->SetInsertPoint(loopBlock);
    llvm::Value* cond = evaluateExpr(node->condition);
    if (!cond->getType()->isIntegerTy(1)) {
        cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
    }
    builder->CreateCondBr(cond, bodyBlock, exitBlock);

    builder->SetInsertPoint(bodyBlock);
    llvm::BasicBlock* prevBreak = breakTarget;
    breakTarget = exitBlock;
    node->body->accept(this);
    breakTarget = prevBreak;
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateBr(loopBlock);
    }

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
        llvm::Value* cond = evaluateExpr(node->condition);
        if (!cond->getType()->isIntegerTy(1)) {
            cond = builder->CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0));
        }
        builder->CreateCondBr(cond, bodyBlock, exitBlock);
    } else {
        builder->CreateBr(bodyBlock);
    }

    // Body
    builder->SetInsertPoint(bodyBlock);
    llvm::BasicBlock* prevBreak = breakTarget;
    breakTarget = exitBlock;
    node->body->accept(this);
    breakTarget = prevBreak;
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
        llvm::Value* retValue = evaluateExpr(node->value);
        builder->CreateRet(retValue);
    } else {
        builder->CreateRetVoid();
    }
}

void CodeGen::visit(BreakStmt* node) {
    if (!breakTarget)
        throw std::runtime_error("break used outside of a loop");
    builder->CreateBr(breakTarget);
}

void CodeGen::visit(ExprStmt* node) {
    evaluateExpr(node->expr);
}

void CodeGen::visit(BinaryExpr* node) {
    // Assignment: evaluate left as lvalue (pointer), not rvalue
    if (node->op == "=") {
        llvm::Value* lhs = evaluateLValue(node->left);
        llvm::Value* rhs = evaluateExpr(node->right);
        builder->CreateStore(rhs, lhs);
        exprValueStack.push(rhs);
        return;
    }

    llvm::Value* left = evaluateExpr(node->left);
    llvm::Value* right = evaluateExpr(node->right);

    if (!left || !right) {
        throw std::runtime_error("Binary expression operand evaluation failed");
    }

    llvm::Value* result = nullptr;

    if (node->op == "+") {
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFAdd(left, right)
            : builder->CreateAdd(left, right);
    } else if (node->op == "-") {
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFSub(left, right)
            : builder->CreateSub(left, right);
    } else if (node->op == "*") {
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFMul(left, right)
            : builder->CreateMul(left, right);
    } else if (node->op == "/") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateSDiv(left, right);
        } else {
            result = builder->CreateFDiv(left, right);
        }
    } else if (node->op == "%") {
        result = builder->CreateSRem(left, right);
    } else if (node->op == "==") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateICmpEQ(left, right);
        } else {
            result = builder->CreateFCmpOEQ(left, right);
        }
    } else if (node->op == "!=") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateICmpNE(left, right);
        } else {
            result = builder->CreateFCmpONE(left, right);
        }
    } else if (node->op == "<") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateICmpSLT(left, right);
        } else {
            result = builder->CreateFCmpOLT(left, right);
        }
    } else if (node->op == ">") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateICmpSGT(left, right);
        } else {
            result = builder->CreateFCmpOGT(left, right);
        }
    } else if (node->op == "<=") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateICmpSLE(left, right);
        } else {
            result = builder->CreateFCmpOLE(left, right);
        }
    } else if (node->op == ">=") {
        if (left->getType()->isIntegerTy()) {
            result = builder->CreateICmpSGE(left, right);
        } else {
            result = builder->CreateFCmpOGE(left, right);
        }
    } else if (node->op == "&&") {
        result = builder->CreateLogicalAnd(left, right);
    } else if (node->op == "||") {
        result = builder->CreateLogicalOr(left, right);
    } else {
        throw std::runtime_error("Unknown binary operator: " + node->op);
    }

    exprValueStack.push(result);
}

void CodeGen::visit(UnaryExpr* node) {
    llvm::Value* val = evaluateExpr(node->operand);

    if (!val) {
        throw std::runtime_error("Unary operand evaluation failed");
    }

    llvm::Value* result = nullptr;

    if (node->op == "-") {
        result = builder->CreateNeg(val);
    } else if (node->op == "!") {
        result = builder->CreateNot(val);
    } else if (node->op == "&") {
        // Address-of: return the pointer itself
        result = val;
    } else if (node->op == "*") {
        // Dereference - assume i8 type for now (LLVM 22 has opaque pointers)
        result = builder->CreateLoad(llvm::Type::getInt8Ty(*context), val);
    } else {
        throw std::runtime_error("Unknown unary operator: " + node->op);
    }

    exprValueStack.push(result);
}

void CodeGen::visit(CallExpr* node) {
    // Method call: callee is MemberExpr (e.g. p.distance(q))
    if (auto member = dynamic_cast<MemberExpr*>(node->callee.get())) {
        std::string baseType = getExprEskiuType(member->base);
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
    if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);

        std::string mangled = baseType + "_" + member->member;
        llvm::Function* func = module->getFunction(mangled);
        if (func) {
            std::vector<llvm::Value*> args;
            args.push_back(evaluateLValue(member->base)); // implicit self pointer
            for (auto& arg : node->args) args.push_back(evaluateExpr(arg));
            exprValueStack.push(builder->CreateCall(func, args));
            return;
        }
        throw std::runtime_error("Undefined method: " + baseType + "::" + member->member);
    }

    // Regular function call — auto-declare free/malloc if not yet visible
    if (auto ident = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if (ident->name == "free") {
            getOrDeclareFunc("free", llvm::Type::getVoidTy(*context),
                             {llvm::PointerType::get(*context, 0)});
        }
    }

    llvm::Value* calleeVal = evaluateExpr(node->callee);
    if (!calleeVal || !llvm::isa<llvm::Function>(calleeVal)) {
        throw std::runtime_error("Call target is not a function");
    }
    llvm::Function* func = llvm::cast<llvm::Function>(calleeVal);

    std::vector<llvm::Value*> args;
    for (auto& arg : node->args) args.push_back(evaluateExpr(arg));

    exprValueStack.push(builder->CreateCall(func, args));
}

void CodeGen::visit(IndexExpr* node) {
    llvm::Value* idx = evaluateExpr(node->index);
    std::string baseType = getExprEskiuType(node->base);

    // Fixed-size array: T[N]
    size_t lb = baseType.rfind('[');
    if (lb != std::string::npos && baseType.back() == ']') {
        std::string elemStr = baseType.substr(0, lb);
        llvm::Type* arrType  = getTypeFromString(baseType);
        llvm::Type* elemType = getTypeFromString(elemStr);
        llvm::Value* basePtr = evaluateLValue(node->base);
        llvm::Value* zero    = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
        llvm::Value* gep     = builder->CreateGEP(arrType, basePtr, {zero, idx});
        exprValueStack.push(builder->CreateLoad(elemType, gep));
        return;
    }

    // Pointer: *T or T*
    if (isPointerType(baseType)) {
        std::string elemStr = (!baseType.empty() && baseType.front() == '*')
            ? baseType.substr(1)
            : baseType.substr(0, baseType.size() - 1);
        llvm::Type* elemType = getTypeFromString(elemStr);
        llvm::Value* ptr     = evaluateExpr(node->base);
        llvm::Value* gep     = builder->CreateGEP(elemType, ptr, idx);
        exprValueStack.push(builder->CreateLoad(elemType, gep));
        return;
    }

    throw std::runtime_error("Cannot index into type: " + baseType);
}

void CodeGen::visit(MemberExpr* node) {
    std::string baseType = getExprEskiuType(node->base);
    if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
    if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);

    auto fit = structFields.find(baseType);
    if (fit == structFields.end())
        throw std::runtime_error("Unknown struct type in member access: '" + baseType + "'");

    const auto& fields = fit->second;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == node->member) {
            llvm::Value* basePtr = evaluateLValue(node->base);
            llvm::StructType* st = structTypes[baseType];
            llvm::Value* gep     = builder->CreateStructGEP(st, basePtr, i, node->member);
            llvm::Value* loaded  = builder->CreateLoad(getTypeFromString(fields[i].type), gep);
            exprValueStack.push(loaded);
            return;
        }
    }
    throw std::runtime_error("Struct '" + baseType + "' has no field '" + node->member + "'");
}

void CodeGen::visit(CastExpr* node) {
    llvm::Value* val = evaluateExpr(node->expr);

    llvm::Type* targetType = getTypeFromString(node->targetType);

    llvm::Value* result = nullptr;

    if (val->getType() == targetType) {
        result = val;
    } else if (val->getType()->isIntegerTy() && targetType->isIntegerTy()) {
        // Integer to integer
        unsigned srcWidth = llvm::cast<llvm::IntegerType>(val->getType())->getBitWidth();
        unsigned dstWidth = llvm::cast<llvm::IntegerType>(targetType)->getBitWidth();
        if (srcWidth < dstWidth) {
            result = builder->CreateSExt(val, targetType);
        } else {
            result = builder->CreateTrunc(val, targetType);
        }
    } else if (val->getType()->isIntegerTy() && targetType->isFloatingPointTy()) {
        result = builder->CreateSIToFP(val, targetType);
    } else if (val->getType()->isFloatingPointTy() && targetType->isIntegerTy()) {
        result = builder->CreateFPToSI(val, targetType);
    } else {
        throw std::runtime_error("Cannot cast between these types");
    }

    exprValueStack.push(result);
}

void CodeGen::visit(LiteralExpr* node) {
    llvm::Value* result = nullptr;

    switch (node->kind) {
        case LiteralExpr::Kind::INT: {
            long long val = std::stoll(node->value);
            result = llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), val);
            break;
        }
        case LiteralExpr::Kind::FLOAT: {
            double val = std::stod(node->value);
            result = llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context), val);
            break;
        }
        case LiteralExpr::Kind::STRING: {
            result = builder->CreateGlobalString(node->value);
            break;
        }
        case LiteralExpr::Kind::BOOL: {
            bool val = node->value == "true";
            result = llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context), val);
            break;
        }
        case LiteralExpr::Kind::NULL_VAL: {
            auto ptrType = llvm::PointerType::get(*context, 0);
            result = llvm::ConstantPointerNull::get(ptrType);
            break;
        }
        case LiteralExpr::Kind::CHAR: {
            char val = node->value.empty() ? 0 : node->value[0];
            result = llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), val);
            break;
        }
    }

    exprValueStack.push(result);
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

    llvm::Value* result = nullptr;

    // If it's a local variable (alloca), load it
    if (llvm::isa<llvm::AllocaInst>(val)) {
        result = builder->CreateLoad(
            llvm::cast<llvm::AllocaInst>(val)->getAllocatedType(), val);
    } else {
        result = val;
    }

    exprValueStack.push(result);
}

llvm::Value* CodeGen::evaluateExpr(ExprPtr expr) {
    expr->accept(this);
    llvm::Value* result = exprValueStack.top();
    exprValueStack.pop();
    return result;
}

llvm::Function* CodeGen::getOrDeclareFunc(const std::string& name, llvm::Type* retType,
                                           std::vector<llvm::Type*> paramTypes, bool isVarArg) {
    llvm::Function* f = module->getFunction(name);
    if (!f) {
        auto* ft = llvm::FunctionType::get(retType, paramTypes, isVarArg);
        f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, module.get());
    }
    return f;
}

void CodeGen::visit(AllocExpr* node) {
    llvm::Type* elemType = getTypeFromString(node->elemType);
    llvm::Value* count   = evaluateExpr(node->count);

    // sizeof(T) from DataLayout
    uint64_t elemSize = module->getDataLayout().getTypeAllocSize(elemType);
    llvm::Value* size64 = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), elemSize);
    llvm::Value* n64    = builder->CreateIntCast(count, llvm::Type::getInt64Ty(*context), false);
    llvm::Value* total  = builder->CreateMul(n64, size64, "alloc.size");

    llvm::Function* mallocFn = getOrDeclareFunc("malloc",
        llvm::PointerType::get(*context, 0), {llvm::Type::getInt64Ty(*context)});
    exprValueStack.push(builder->CreateCall(mallocFn, {total}, "alloc.ptr"));
}

void CodeGen::emitStructInitInto(llvm::Value* dest, StructInitExpr* init) {
    auto fit = structFields.find(init->structName);
    if (fit == structFields.end()) return;
    const auto& fields = fit->second;
    llvm::StructType* st = structTypes[init->structName];

    bool named = !init->fieldInits.empty() && !init->fieldInits[0].first.empty();

    auto storeField = [&](size_t idx, ExprPtr expr) {
        llvm::Type* fieldType = getTypeFromString(fields[idx].type);
        llvm::Value* val = evaluateExpr(expr);
        if (val && val->getType() != fieldType) {
            if (val->getType()->isIntegerTy() && fieldType->isIntegerTy()) {
                unsigned s = llvm::cast<llvm::IntegerType>(val->getType())->getBitWidth();
                unsigned d = llvm::cast<llvm::IntegerType>(fieldType)->getBitWidth();
                val = s > d ? builder->CreateTrunc(val, fieldType)
                            : builder->CreateSExt(val, fieldType);
            } else if (val->getType()->isIntegerTy() && fieldType->isFloatingPointTy()) {
                val = builder->CreateSIToFP(val, fieldType);
            } else if (val->getType()->isFloatingPointTy() && fieldType->isIntegerTy()) {
                val = builder->CreateFPToSI(val, fieldType);
            } else if (val->getType()->isFloatingPointTy() && fieldType->isFloatingPointTy()) {
                val = builder->CreateFPCast(val, fieldType);
            }
        }
        llvm::Value* gep = builder->CreateStructGEP(st, dest, idx);
        if (val) builder->CreateStore(val, gep);
    };

    if (named) {
        for (const auto& [fname, expr] : init->fieldInits) {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].name == fname) { storeField(i, expr); break; }
            }
        }
    } else {
        for (size_t i = 0; i < init->fieldInits.size() && i < fields.size(); ++i) {
            storeField(i, init->fieldInits[i].second);
        }
    }
}

void CodeGen::visit(StructInitExpr* node) {
    auto fit = structFields.find(node->structName);
    if (fit == structFields.end())
        throw std::runtime_error("Unknown struct: " + node->structName);
    llvm::StructType* st = structTypes[node->structName];
    // Temporary alloca — filled then loaded so caller can store it anywhere
    llvm::Value* tmp = builder->CreateAlloca(st, nullptr, node->structName + ".init");
    emitStructInitInto(tmp, node);
    exprValueStack.push(builder->CreateLoad(st, tmp));
}

void CodeGen::visit(InterfaceDecl* node) {
    // Interface dispatch deferred — structural typing check at call sites
    // For now interfaces are parsed but generate no code
}

void CodeGen::visit(SwitchStmt* node) {
    // Pre-evaluate case values (must be ConstantInt) before creating the switch
    std::vector<llvm::ConstantInt*> caseVals;
    for (auto& c : node->cases) {
        if (!c.value) { caseVals.push_back(nullptr); continue; }
        llvm::Value* v = evaluateExpr(c.value);
        auto* ci = llvm::dyn_cast<llvm::ConstantInt>(v);
        if (!ci) throw std::runtime_error("switch case value must be a constant integer");
        caseVals.push_back(ci);
    }

    llvm::Value* subj = evaluateExpr(node->subject);
    if (!subj->getType()->isIntegerTy())
        throw std::runtime_error("switch subject must be integer");

    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "switch.end", currentFunction);

    std::vector<llvm::BasicBlock*> caseBlocks(node->cases.size());
    for (size_t i = 0; i < node->cases.size(); ++i) {
        std::string lbl = node->cases[i].value ? "case" + std::to_string(i) : "default";
        caseBlocks[i] = llvm::BasicBlock::Create(*context, lbl, currentFunction);
    }

    llvm::BasicBlock* defaultBlock = endBlock;
    for (size_t i = 0; i < node->cases.size(); ++i) {
        if (!node->cases[i].value) { defaultBlock = caseBlocks[i]; break; }
    }

    llvm::SwitchInst* sw = builder->CreateSwitch(subj, defaultBlock);
    for (size_t i = 0; i < node->cases.size(); ++i) {
        if (caseVals[i]) sw->addCase(caseVals[i], caseBlocks[i]);
    }

    llvm::BasicBlock* prevBreak = breakTarget;
    breakTarget = endBlock;

    for (size_t i = 0; i < node->cases.size(); ++i) {
        builder->SetInsertPoint(caseBlocks[i]);
        for (auto& stmt : node->cases[i].stmts) {
            stmt->accept(this);
            if (builder->GetInsertBlock()->getTerminator()) break;
        }
        if (!builder->GetInsertBlock()->getTerminator()) {
            llvm::BasicBlock* next = (i + 1 < caseBlocks.size()) ? caseBlocks[i+1] : endBlock;
            builder->CreateBr(next);
        }
    }

    breakTarget = prevBreak;
    builder->SetInsertPoint(endBlock);
}

void CodeGen::visit(TemplateCallExpr* node) {
    auto templ = funcTemplateDecls.find(node->templateName);
    if (templ == funcTemplateDecls.end())
        throw std::runtime_error("Unknown template function: " + node->templateName);

    FunctionDecl* fd = templ->second;
    auto& tp = fd->typeParams;
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < node->typeArgs.size(); ++i)
        subs[tp[i]] = node->typeArgs[i];

    // Mangle the instantiated function name
    std::string mangledName = node->templateName;
    for (const auto& t : node->typeArgs) mangledName += "_" + mangleTemplate(t);

    // Instantiate if not already in module.
    // Save/restore the insert point — we may be inside another function's body.
    if (!module->getFunction(mangledName)) {
        llvm::BasicBlock*          savedBB    = builder->GetInsertBlock();
        llvm::BasicBlock::iterator savedPoint = builder->GetInsertPoint();
        llvm::Function*            savedFunc  = currentFunction;

        typeParamOverride = subs;
        auto inst = std::make_shared<FunctionDecl>(mangledName, fd->returnType, fd->params, fd->body);
        inst->accept(this);
        typeParamOverride.clear();

        // Restore caller's context
        currentFunction = savedFunc;
        if (savedBB) builder->SetInsertPoint(savedBB, savedPoint);
    }

    llvm::Function* func = module->getFunction(mangledName);
    if (!func) throw std::runtime_error("Template instantiation failed: " + mangledName);

    std::vector<llvm::Value*> args;
    for (auto& arg : node->args) args.push_back(evaluateExpr(arg));
    exprValueStack.push(builder->CreateCall(func, args));
}

llvm::Value* CodeGen::evaluateLValue(ExprPtr expr) {
    if (auto ident = dynamic_cast<IdentExpr*>(expr.get())) {
        llvm::Value* val = lookupSymbol(ident->name);
        if (!val) throw std::runtime_error("Undefined variable: " + ident->name);
        return val;
    }

    if (auto member = dynamic_cast<MemberExpr*>(expr.get())) {
        std::string baseType = getExprEskiuType(member->base);
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
    if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
        auto fit = structFields.find(baseType);
        if (fit == structFields.end())
            throw std::runtime_error("Unknown struct type: " + baseType);
        const auto& fields = fit->second;
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == member->member) {
                llvm::Value* basePtr = evaluateLValue(member->base);
                return builder->CreateStructGEP(structTypes[baseType], basePtr, i);
            }
        }
        throw std::runtime_error("Struct '" + baseType + "' has no field '" + member->member + "'");
    }

    if (auto index = dynamic_cast<IndexExpr*>(expr.get())) {
        llvm::Value* idx      = evaluateExpr(index->index);
        std::string baseType  = getExprEskiuType(index->base);
        size_t lb = baseType.rfind('[');
        if (lb != std::string::npos && baseType.back() == ']') {
            llvm::Type* arrType = getTypeFromString(baseType);
            llvm::Value* base   = evaluateLValue(index->base);
            llvm::Value* zero   = llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), 0);
            return builder->CreateGEP(arrType, base, {zero, idx});
        }
        if (isPointerType(baseType)) {
            std::string elemStr = (baseType.front() == '*')
                ? baseType.substr(1) : baseType.substr(0, baseType.size() - 1);
            llvm::Value* ptr = evaluateExpr(index->base);
            return builder->CreateGEP(getTypeFromString(elemStr), ptr, idx);
        }
        throw std::runtime_error("Cannot take lvalue index of type: " + baseType);
    }

    throw std::runtime_error("Left-hand side of assignment is not an lvalue");
}

bool CodeGen::emitObjectFile(const std::string& filename) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string tripleStr = llvm::sys::getDefaultTargetTriple();
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        std::cerr << "error: " << err << std::endl;
        return false;
    }

    auto cpu = llvm::sys::getHostCPUName();
    llvm::TargetOptions opt;
    auto* tm = target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_);
    module->setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        std::cerr << "error: cannot open '" << filename << "': " << ec.message() << std::endl;
        return false;
    }

    llvm::legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        std::cerr << "error: target cannot emit object file" << std::endl;
        return false;
    }

    pm.run(*module);
    dest.flush();
    delete tm;
    return true;
}
