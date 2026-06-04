#include "codegen.h"
#include "llvm/IR/InlineAsm.h"
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
    // Initialise only the two targets we ship: AArch64 and X86
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);
    std::string terr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, terr);
    if (target) {
        bool isCross = !targetTriple.empty() &&
            targetTriple != llvm::sys::getDefaultTargetTriple();
        auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
        llvm::TargetOptions opt;
        auto* tm = target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_);
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
    // Function pointer type: fn(T,...)->R — represented as opaque ptr
    if (typeStr.size() > 3 && typeStr.substr(0, 3) == "fn(")
        return llvm::PointerType::get(*context, 0);

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

    // Interface type → opaque pointer (interfaces passed by pointer to fat struct)
    if (ifaceFatPtrTypes.count(typeStr)) {
        return llvm::PointerType::get(*context, 0);
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

bool CodeGen::needsSret(llvm::Type* retType) const {
    if (!retType || retType->isVoidTy() || !retType->isSized()) return false;
    if (!retType->isAggregateType()) return false;
    // arm64: structs > 16 bytes use sret; x86-64: structs > 8 bytes that don't fit
    // in two registers.  Using 16 as a conservative threshold covers both.
    return module->getDataLayout().getTypeAllocSize(retType) > 16;
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
    if (!varTypeStack.empty())
        varTypeStack.back()[name] = type;
    else
        globalVarTypes[name] = type;   // top-level / global scope
}

std::string CodeGen::lookupVarType(const std::string& name) const {
    for (auto it = varTypeStack.rbegin(); it != varTypeStack.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second;
    }
    auto g = globalVarTypes.find(name);
    return g != globalVarTypes.end() ? g->second : "";
}

llvm::Constant* CodeGen::evaluateConstantExpr(ExprPtr expr) {
    // Fold unary minus on a numeric literal: -(N) → negative constant
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        if (unary->op == "-") {
            llvm::Constant* inner = evaluateConstantExpr(unary->operand);
            if (!inner) return nullptr;
            if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(inner))
                return llvm::ConstantInt::get(ci->getType(),
                    static_cast<uint64_t>(-(int64_t)ci->getZExtValue()), true);
            if (auto* cf = llvm::dyn_cast<llvm::ConstantFP>(inner))
                return llvm::ConstantFP::get(cf->getType(),
                    -cf->getValueAPF().convertToDouble());
        }
        return nullptr;
    }

    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    if (!lit) return nullptr;

    switch (lit->kind) {
        case LiteralExpr::Kind::INT: {
            long long v = std::stoll(lit->value, nullptr, 0);
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), v);
        }
        case LiteralExpr::Kind::FLOAT: {
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(*context),
                                          std::stod(lit->value));
        }
        case LiteralExpr::Kind::BOOL: {
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(*context),
                                           lit->value == "true" ? 1 : 0);
        }
        case LiteralExpr::Kind::CHAR: {
            char c = lit->value.empty() ? 0 : lit->value[0];
            return llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), c);
        }
        case LiteralExpr::Kind::STRING: {
            // Build a private string constant and return a pointer to it
            auto* arrType = llvm::ArrayType::get(llvm::Type::getInt8Ty(*context),
                                                   lit->value.size() + 1);
            std::vector<llvm::Constant*> chars;
            for (unsigned char c : lit->value)
                chars.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), c));
            chars.push_back(llvm::ConstantInt::get(llvm::Type::getInt8Ty(*context), 0));
            auto* strData = new llvm::GlobalVariable(
                *module, arrType, true,
                llvm::GlobalValue::PrivateLinkage,
                llvm::ConstantArray::get(arrType, chars), ".gstr");
            // Return pointer to first element (ptr in opaque-pointer IR)
            return strData;
        }
        case LiteralExpr::Kind::NULL_VAL:
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(*context, 0));
        default:
            return nullptr;
    }
}

std::string CodeGen::getExprEskiuType(ExprPtr expr) const {
    if (auto ident = dynamic_cast<IdentExpr*>(expr.get())) {
        return lookupVarType(ident->name);
    }
    if (auto member = dynamic_cast<MemberExpr*>(expr.get())) {
        std::string base = getExprEskiuType(member->base);
        if (base.size() > 7 && base.substr(0, 7) == "struct:") base = base.substr(7);
        if (!base.empty() && base.front() == '*') base = base.substr(1);
        while (!base.empty() && base.back()  == '*') base.pop_back();
        if (base.find('<') != std::string::npos) base = mangleTemplate(base);
        auto it = structFields.find(base);
        if (it != structFields.end()) {
            for (const auto& f : it->second) {
                if (f.name == member->member) return f.type;
            }
        }
    }
    if (auto unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        if (unary->op == "&") return "*" + getExprEskiuType(unary->operand);
        if (unary->op == "*") {
            std::string t = getExprEskiuType(unary->operand);
            return (!t.empty() && t.front() == '*') ? t.substr(1) : "";
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

    // Create function type — use sret for large struct returns
    llvm::Type* returnType = getTypeFromString(node->returnType);
    bool sret = needsSret(returnType);
    llvm::StructType* sretStructType = nullptr;
    if (sret) {
        sretStructType = llvm::cast<llvm::StructType>(returnType);
        funcSretTypes[node->name] = sretStructType;
        // Prepend hidden sret pointer as first parameter
        paramTypes.insert(paramTypes.begin(), llvm::PointerType::get(*context, 0));
    }
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        sret ? llvm::Type::getVoidTy(*context) : returnType,
        paramTypes, false);

    // Create function
    llvm::Function* func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, node->name, module.get());

    // Set parameter names (skip index 0 for sret functions — that's the hidden ret ptr)
    size_t paramIdx = 0;
    size_t argIdx   = 0;
    for (auto& arg : func->args()) {
        if (sret && argIdx == 0) {
            arg.setName("sret.ptr");
            argIdx++;
            continue;
        }
        if (paramIdx < node->params.size() && node->params[paramIdx].first != "...") {
            arg.setName(node->params[paramIdx].second);
            paramIdx++;
        }
        argIdx++;
    }

    // Create entry block
    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entryBlock);

    // Save current function + sret context
    llvm::Function* prevFunc       = currentFunction;
    llvm::Value*    prevSretParam  = currentSretParam;
    currentFunction = func;
    currentSretParam = sret ? &*func->arg_begin() : nullptr;

    // Push scope for function parameters
    pushScope();

    // Store Eskiu param types for interface boxing at call sites
    {
        std::vector<std::string> pts;
        for (const auto& p : node->params)
            if (p.first != "...") pts.push_back(p.first);
        funcEskiuParamTypes[node->name] = pts;
    }

    // Define parameters in symbol table + type map (skip sret hidden param at index 0)
    paramIdx = 0;
    argIdx   = 0;
    for (auto& arg : func->args()) {
        if (sret && argIdx == 0) { argIdx++; continue; }  // skip sret ptr
        if (paramIdx < node->params.size() && node->params[paramIdx].first != "...") {
            // Struct-by-value params need an alloca so MemberExpr GEP has a pointer
            llvm::Value* paramSlot = &arg;
            if (arg.getType()->isStructTy()) {
                auto* a = builder->CreateAlloca(arg.getType(), nullptr,
                                                node->params[paramIdx].second + ".byval");
                builder->CreateStore(&arg, a);
                paramSlot = a;
            }
            defineSymbol(node->params[paramIdx].second, paramSlot);
            std::string ptype = !typeParamOverride.empty()
                ? substType(node->params[paramIdx].first, typeParamOverride)
                : node->params[paramIdx].first;
            if (ptype.find('<') != std::string::npos) {
                std::string sfx;
                while (!ptype.empty() && ptype.back() == '*') { sfx += '*'; ptype.pop_back(); }
                ptype = mangleTemplate(ptype) + sfx;
            }
            defineVarType(node->params[paramIdx].second, ptype);
            paramIdx++;
        }
        argIdx++;
    }

    // Generate function body
    if (node->body) {
        node->body->accept(this);
    }

    // Default return if no explicit return emitted
    if (!builder->GetInsertBlock()->getTerminator()) {
        if (sret || returnType->isVoidTy()) {
            builder->CreateRetVoid();
        } else if (returnType->isIntegerTy()) {
            builder->CreateRet(llvm::ConstantInt::get(returnType, 0));
        } else {
            builder->CreateRet(llvm::Constant::getNullValue(returnType));
        }
    }

    // Restore context
    popScope();
    currentFunction  = prevFunc;
    currentSretParam = prevSretParam;
}

void CodeGen::visit(VarDecl* node) {
    llvm::Type* declType = getTypeFromString(node->type);

    // Global scope (no active function) → emit as llvm::GlobalVariable
    if (currentFunction == nullptr) {
        llvm::Constant* init = node->initializer
            ? evaluateConstantExpr(node->initializer)
            : nullptr;
        // Coerce initializer to match declared type
        if (init && init->getType() != declType) {
            if (init->getType()->isIntegerTy() && declType->isIntegerTy()) {
                // Use ConstantInt directly
                uint64_t v = llvm::cast<llvm::ConstantInt>(init)->getZExtValue();
                init = llvm::ConstantInt::get(declType, v);
            } else if (init->getType()->isFloatingPointTy() && declType->isFloatingPointTy()) {
                double v = llvm::cast<llvm::ConstantFP>(init)->getValueAPF().convertToDouble();
                init = llvm::ConstantFP::get(declType, v);
            }
        }
        if (!init) init = llvm::Constant::getNullValue(declType);

        auto* gv = new llvm::GlobalVariable(
            *module, declType, /*isConstant=*/false,
            llvm::GlobalValue::PrivateLinkage, init, node->name);

        defineSymbol(node->name, gv);
        defineVarType(node->name, node->type);
        return;
    }
    llvm::AllocaInst* alloca = builder->CreateAlloca(declType, nullptr, node->name);
    if (node->isVolatile) volatileVars.insert(node->name);
    defineSymbol(node->name, alloca);
    // Resolve type params and mangle template names for varTypeStack,
    // preserving pointer suffixes (e.g. "List<int>*" → "List_int*")
    std::string varType = !typeParamOverride.empty()
                          ? substType(node->type, typeParamOverride)
                          : node->type;
    if (varType.find('<') != std::string::npos) {
        // Strip trailing pointer stars, mangle the base, then re-append stars
        std::string suffix;
        while (!varType.empty() && varType.back() == '*') {
            suffix += '*'; varType.pop_back();
        }
        varType = mangleTemplate(varType) + suffix;
    }
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
                    if (src > dst)
                        val = builder->CreateTrunc(val, declType);
                    else if (src < dst)
                        // ZExt for i1 (bool comparisons) to avoid sign-extending 1 → -1
                        val = (src == 1) ? builder->CreateZExt(val, declType)
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
    llvm::BasicBlock* prevBreak    = breakTarget;
    llvm::BasicBlock* prevContinue = continueTarget;
    breakTarget    = exitBlock;
    continueTarget = loopBlock;
    node->body->accept(this);
    breakTarget    = prevBreak;
    continueTarget = prevContinue;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(loopBlock);

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
    llvm::BasicBlock* prevBreak    = breakTarget;
    llvm::BasicBlock* prevContinue = continueTarget;
    breakTarget    = exitBlock;
    continueTarget = stepBlock;   // continue jumps to the step
    node->body->accept(this);
    breakTarget    = prevBreak;
    continueTarget = prevContinue;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(stepBlock);

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
    // Coerce return value to the declared function return type
    auto coerceRetVal = [&](llvm::Value* v) -> llvm::Value* {
        if (!currentFunction) return v;
        llvm::Type* ft = currentFunction->getReturnType();
        if (v->getType() == ft) return v;
        if (v->getType()->isIntegerTy() && ft->isIntegerTy()) {
            unsigned vw = llvm::cast<llvm::IntegerType>(v->getType())->getBitWidth();
            unsigned fw = llvm::cast<llvm::IntegerType>(ft)->getBitWidth();
            return vw < fw ? builder->CreateSExt(v, ft) : builder->CreateTrunc(v, ft);
        }
        if (v->getType()->isIntegerTy() && ft->isFloatingPointTy())
            return builder->CreateSIToFP(v, ft);
        if (v->getType()->isFloatingPointTy() && ft->isIntegerTy())
            return builder->CreateFPToSI(v, ft);
        if (v->getType()->isFloatingPointTy() && ft->isFloatingPointTy())
            return builder->CreateFPCast(v, ft);  // double→float or float→double
        return v;
    };

    if (currentSretParam != nullptr) {
        // sret function: store result to hidden pointer, return void
        if (node->value) {
            llvm::Value* retValue = evaluateExpr(node->value);
            builder->CreateStore(retValue, currentSretParam);
        }
        builder->CreateRetVoid();
    } else if (node->value) {
        llvm::Value* retValue = coerceRetVal(evaluateExpr(node->value));
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
        // Coerce RHS to match the lvalue's expected element type
        llvm::Type* elemType = nullptr;
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(lhs))
            elemType = alloca->getAllocatedType();
        else if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(lhs))
            elemType = gep->getResultElementType();
        if (elemType && rhs->getType() != elemType) {
            if (rhs->getType()->isIntegerTy() && elemType->isIntegerTy()) {
                unsigned rw = llvm::cast<llvm::IntegerType>(rhs->getType())->getBitWidth();
                unsigned ew = llvm::cast<llvm::IntegerType>(elemType)->getBitWidth();
                rhs = rw < ew ? builder->CreateZExt(rhs, elemType)
                               : builder->CreateTrunc(rhs, elemType);
            } else if (rhs->getType()->isIntegerTy() && elemType->isFloatingPointTy()) {
                rhs = builder->CreateSIToFP(rhs, elemType);
            } else if (rhs->getType()->isFloatingPointTy() && elemType->isIntegerTy()) {
                rhs = builder->CreateFPToSI(rhs, elemType);
            } else if (rhs->getType()->isFloatingPointTy() && elemType->isFloatingPointTy()) {
                rhs = builder->CreateFPCast(rhs, elemType);  // e.g. double→float
            }
        }
        bool storeVol = false;
        if (auto* ident = llvm::dyn_cast<llvm::AllocaInst>(lhs)) {
            storeVol = volatileVars.count(ident->getName().str()) > 0;
        }
        auto* si = builder->CreateStore(rhs, lhs);
        si->setVolatile(storeVol);
        exprValueStack.push(rhs);
        return;
    }

    llvm::Value* left = evaluateExpr(node->left);
    llvm::Value* right = evaluateExpr(node->right);

    if (!left || !right) {
        throw std::runtime_error("Binary expression operand evaluation failed");
    }

    llvm::Value* result = nullptr;

    // Promote to common type: int→float, float→double
    auto promoteToFloat = [&]() {
        if (left->getType()->isFloatingPointTy() && right->getType()->isIntegerTy())
            right = builder->CreateSIToFP(right, left->getType());
        else if (right->getType()->isFloatingPointTy() && left->getType()->isIntegerTy())
            left = builder->CreateSIToFP(left, right->getType());
        // float × double: widen float → double
        else if (left->getType()->isFloatingPointTy() && right->getType()->isFloatingPointTy()
                 && left->getType() != right->getType()) {
            if (left->getType()->getPrimitiveSizeInBits() <
                right->getType()->getPrimitiveSizeInBits())
                left  = builder->CreateFPCast(left,  right->getType());
            else
                right = builder->CreateFPCast(right, left->getType());
        }
    };

    // Widen narrower integer to match wider for bitwise/shift ops
    auto widenForBitwise = [&]() {
        if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()
                && left->getType() != right->getType()) {
            unsigned lw = llvm::cast<llvm::IntegerType>(left->getType())->getBitWidth();
            unsigned rw = llvm::cast<llvm::IntegerType>(right->getType())->getBitWidth();
            if (lw < rw) left  = builder->CreateZExt(left,  right->getType());
            else          right = builder->CreateZExt(right, left->getType());
        }
    };

    // Widen narrower integer to match wider for arithmetic (e.g. i8 - i32)
    auto widenForArith = [&]() {
        if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()
                && left->getType() != right->getType()) {
            unsigned lw = llvm::cast<llvm::IntegerType>(left->getType())->getBitWidth();
            unsigned rw = llvm::cast<llvm::IntegerType>(right->getType())->getBitWidth();
            if (lw < rw) left  = builder->CreateZExt(left,  right->getType());
            else          right = builder->CreateZExt(right, left->getType());
        }
    };

    if (node->op == "+") {
        if (left->getType()->isPointerTy())
            result = builder->CreateGEP(llvm::Type::getInt8Ty(*context), left, right, "ptr.add");
        else {
            promoteToFloat();
            widenForArith();
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFAdd(left, right)
                : builder->CreateAdd(left, right);
        }
    } else if (node->op == "-") {
        if (left->getType()->isPointerTy() && right->getType()->isPointerTy()) {
            // ptr - ptr: byte-level difference → i64
            llvm::Value* l64 = builder->CreatePtrToInt(left,  llvm::Type::getInt64Ty(*context));
            llvm::Value* r64 = builder->CreatePtrToInt(right, llvm::Type::getInt64Ty(*context));
            result = builder->CreateSub(l64, r64, "ptrdiff");
        } else if (left->getType()->isPointerTy()) {
            llvm::Value* neg = builder->CreateNeg(right, "neg");
            result = builder->CreateGEP(llvm::Type::getInt8Ty(*context), left, neg, "ptr.sub");
        } else {
            promoteToFloat(); widenForArith();
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFSub(left, right)
                : builder->CreateSub(left, right);
        }
    } else if (node->op == "*") {
        promoteToFloat(); widenForArith();
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFMul(left, right)
            : builder->CreateMul(left, right);
    } else if (node->op == "/") {
        promoteToFloat(); widenForArith();
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFDiv(left, right)
            : builder->CreateSDiv(left, right);
    } else if (node->op == "%") {
        result = builder->CreateSRem(left, right);
    } else if (node->op == "==") {
        if (left->getType()->isFloatingPointTy())
            result = builder->CreateFCmpOEQ(left, right);
        else {
            // Widen narrower integer to match wider (e.g. i8 == i32)
            if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()
                    && left->getType() != right->getType()) {
                unsigned lw = llvm::cast<llvm::IntegerType>(left->getType())->getBitWidth();
                unsigned rw = llvm::cast<llvm::IntegerType>(right->getType())->getBitWidth();
                if (lw < rw) left  = builder->CreateZExt(left,  right->getType());
                else          right = builder->CreateZExt(right, left->getType());
            }
            result = builder->CreateICmpEQ(left, right);
        }
    } else if (node->op == "!=" || node->op == "<" || node->op == ">" ||
               node->op == "<=" || node->op == ">=") {
        // Widen narrower integer to match wider for all comparisons
        if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()
                && left->getType() != right->getType()) {
            unsigned lw = llvm::cast<llvm::IntegerType>(left->getType())->getBitWidth();
            unsigned rw = llvm::cast<llvm::IntegerType>(right->getType())->getBitWidth();
            if (lw < rw) left  = builder->CreateZExt(left,  right->getType());
            else          right = builder->CreateZExt(right, left->getType());
        }
        if (node->op == "!=") {
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFCmpONE(left, right)
                : builder->CreateICmpNE(left, right);
        } else if (node->op == "<") {
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFCmpOLT(left, right)
                : builder->CreateICmpSLT(left, right);
        } else if (node->op == ">") {
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFCmpOGT(left, right)
                : builder->CreateICmpSGT(left, right);
        } else if (node->op == "<=") {
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFCmpOLE(left, right)
                : builder->CreateICmpSLE(left, right);
        } else {
            result = left->getType()->isFloatingPointTy()
                ? builder->CreateFCmpOGE(left, right)
                : builder->CreateICmpSGE(left, right);
        }
    } else if (node->op == "&&") {
        result = builder->CreateLogicalAnd(left, right);
    } else if (node->op == "||") {
        result = builder->CreateLogicalOr(left, right);
    // Bitwise operators (widen narrower integer before operating)
    } else if (node->op == "&") {
        widenForBitwise(); result = builder->CreateAnd(left, right);
    } else if (node->op == "|") {
        widenForBitwise(); result = builder->CreateOr(left, right);
    } else if (node->op == "^") {
        widenForBitwise(); result = builder->CreateXor(left, right);
    } else if (node->op == "<<") {
        widenForBitwise(); result = builder->CreateShl(left, right);
    } else if (node->op == ">>") {
        widenForBitwise(); result = builder->CreateAShr(left, right);
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
        result = val->getType()->isFloatingPointTy()
            ? builder->CreateFNeg(val)
            : builder->CreateNeg(val);
    } else if (node->op == "~") {
        result = builder->CreateNot(val); // bitwise NOT
    } else if (node->op == "!") {
        // Logical NOT: convert to bool
        if (val->getType()->isIntegerTy(1))
            result = builder->CreateNot(val);
        else
            result = builder->CreateICmpEQ(val, llvm::ConstantInt::get(val->getType(), 0));
    } else if (node->op == "&") {
        // Address-of: return the lvalue (alloca/GEP pointer), not the loaded value
        result = evaluateLValue(node->operand);
    } else if (node->op == "*") {
        // Dereference: use Eskiu type info to load the correct element type
        std::string ptrEskiuType = getExprEskiuType(node->operand);
        llvm::Type* elemType = llvm::Type::getInt8Ty(*context); // fallback
        if (!ptrEskiuType.empty()) {
            std::string elemStr;
            if (ptrEskiuType.front() == '*')
                elemStr = ptrEskiuType.substr(1);
            else if (ptrEskiuType.back() == '*')
                elemStr = ptrEskiuType.substr(0, ptrEskiuType.size() - 1);
            if (!elemStr.empty() && elemStr != "void")
                elemType = getTypeFromString(elemStr);
        }
        result = builder->CreateLoad(elemType, val);
    } else {
        throw std::runtime_error("Unknown unary operator: " + node->op);
    }

    exprValueStack.push(result);
}

void CodeGen::visit(CallExpr* node) {
    // Unified method/interface call: callee is MemberExpr
    if (auto member = dynamic_cast<MemberExpr*>(node->callee.get())) {
        std::string baseType = getExprEskiuType(member->base);
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
        if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
        while (!baseType.empty() && baseType.back() == '*') baseType.pop_back();
        if (baseType.find('<') != std::string::npos) {
            auto [tn2, a2] = splitTemplateType(baseType);
            ensureTemplateInstantiated(mangleTemplate(baseType), tn2, a2);
            baseType = mangleTemplate(baseType);
        }

        // Interface vtable dispatch
        auto ifIt = ifaceMethodOrder.find(baseType);
        if (ifIt != ifaceMethodOrder.end()) {
            llvm::Value* fatPtr = evaluateLValue(member->base);
            llvm::StructType* fatType = ifaceFatPtrTypes[baseType];
            llvm::Value* dataGEP = builder->CreateStructGEP(fatType, fatPtr, 0);
            llvm::Value* dataPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), dataGEP);
            llvm::Value* vtGEP   = builder->CreateStructGEP(fatType, fatPtr, 1);
            llvm::Value* vtPtr   = builder->CreateLoad(llvm::PointerType::get(*context, 0), vtGEP);
            const auto& order = ifIt->second;
            size_t idx = 0;
            for (; idx < order.size(); ++idx) if (order[idx] == member->member) break;
            if (idx == order.size())
                throw std::runtime_error("Interface '" + baseType + "' has no method '" + member->member + "'");
            llvm::StructType* vtType = ifaceVtableTypes[baseType];
            llvm::Value* fnGEP = builder->CreateStructGEP(vtType, vtPtr, idx);
            llvm::Value* fnPtr = builder->CreateLoad(llvm::PointerType::get(*context, 0), fnGEP);
            std::vector<llvm::Value*> iargs = {dataPtr};
            for (auto& arg : node->args) iargs.push_back(evaluateExpr(arg));

            // Build the correct function type from stored method signature
            llvm::Type* retType = llvm::Type::getVoidTy(*context);
            auto& retTypes  = ifaceMethodReturnTypes[baseType];
            auto& paramLists = ifaceMethodParamEskiuTypes[baseType];
            if (idx < retTypes.size()) retType = getTypeFromString(retTypes[idx]);

            std::vector<llvm::Type*> paramLLVM = {llvm::PointerType::get(*context, 0)}; // self
            if (idx < paramLists.size())
                for (const auto& pt : paramLists[idx])
                    paramLLVM.push_back(getTypeFromString(pt));
            // Pad remaining args as ptr if count doesn't match (variadic safety)
            while (paramLLVM.size() < iargs.size())
                paramLLVM.push_back(llvm::PointerType::get(*context, 0));

            // Use sret if return type is a large aggregate
            bool iSret = needsSret(retType);
            llvm::Value* sretBuf = nullptr;
            if (iSret) {
                sretBuf = builder->CreateAlloca(retType, nullptr, "iface.sret");
                iargs.insert(iargs.begin() + 0, sretBuf); // sret after self? actually: sret first
                paramLLVM.insert(paramLLVM.begin(), llvm::PointerType::get(*context, 0));
                retType = llvm::Type::getVoidTy(*context);
            }

            auto* ftype = llvm::FunctionType::get(retType, paramLLVM, false);
            llvm::Value* call = builder->CreateCall(ftype, fnPtr, iargs);
            if (iSret)
                exprValueStack.push(builder->CreateLoad(
                    llvm::cast<llvm::StructType>(getTypeFromString(retTypes[idx])), sretBuf));
            else
                exprValueStack.push(call);
            return;
        }

        // Struct method call
        std::string mangled = baseType + "_" + member->member;
        llvm::Function* mfunc = module->getFunction(mangled);
        if (mfunc) {
            std::vector<llvm::Value*> margs = {evaluateLValue(member->base)};
            for (auto& arg : node->args) margs.push_back(evaluateExpr(arg));
            exprValueStack.push(builder->CreateCall(mfunc, margs));
            return;
        }
        throw std::runtime_error("Undefined method: " + baseType + "::" + member->member);
    }

    // Regular function call — auto-declare free/malloc if not yet visible
    if (auto ident = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if (ident->name == "free") {
            std::string freeSym = freestanding ? "esk_free" : "free";
            getOrDeclareFunc(freeSym, llvm::Type::getVoidTy(*context),
                             {llvm::PointerType::get(*context, 0)});
            // Redirect the call symbol if in freestanding mode
            if (freestanding) {
                auto* fn = module->getFunction("esk_free");
                std::vector<llvm::Value*> fargs = {evaluateExpr(node->args[0])};
                builder->CreateCall(fn, fargs);
                exprValueStack.push(llvm::Constant::getNullValue(
                    llvm::Type::getInt32Ty(*context)));
                return;
            }
        }
    }

    llvm::Value* calleeVal = evaluateExpr(node->callee);
    if (!calleeVal) throw std::runtime_error("Call target is null");

    // Indirect call through a function pointer (e.g. a lambda stored in a variable)
    if (!llvm::isa<llvm::Function>(calleeVal)) {
        std::string eskiuType = getExprEskiuType(node->callee);
        // Parse fn(T1,T2,...)->R
        if (eskiuType.size() > 3 && eskiuType.substr(0, 3) == "fn(") {
            // extract params and return type from "fn(T,...)->R"
            size_t rp = eskiuType.find(")->");
            std::string paramStr = eskiuType.substr(3, rp - 3);
            std::string retStr   = eskiuType.substr(rp + 3);
            std::vector<llvm::Type*> pts;
            // Split paramStr on ','
            if (!paramStr.empty()) {
                size_t pos = 0;
                while (pos < paramStr.size()) {
                    size_t comma = paramStr.find(',', pos);
                    if (comma == std::string::npos) comma = paramStr.size();
                    pts.push_back(getTypeFromString(paramStr.substr(pos, comma - pos)));
                    pos = comma + 1;
                }
            }
            llvm::Type* retTy = getTypeFromString(retStr);
            llvm::FunctionType* fty = llvm::FunctionType::get(retTy, pts, false);
            std::vector<llvm::Value*> iargs;
            for (auto& a : node->args) iargs.push_back(evaluateExpr(a));
            exprValueStack.push(builder->CreateCall(fty, calleeVal, iargs, "fn.call"));
            return;
        }
        throw std::runtime_error("Call target is not a function");
    }
    llvm::Function* func = llvm::cast<llvm::Function>(calleeVal);

    // Evaluate args, boxing structs as interfaces where the param type demands it
    std::vector<llvm::Value*> args;
    auto ptIt = funcEskiuParamTypes.find(func->getName().str());
    for (size_t i = 0; i < node->args.size(); ++i) {
        bool boxed = false;
        if (ptIt != funcEskiuParamTypes.end() && i < ptIt->second.size()) {
            const std::string& ep = ptIt->second[i];
            if (ifaceFatPtrTypes.count(ep)) {
                // Param expects an interface — evaluate arg as pointer and box it
                std::string argType = getExprEskiuType(node->args[i]);
                if (!argType.empty() && argType.front() == '*') argType = argType.substr(1);
                if (argType.size() > 7 && argType.substr(0, 7) == "struct:") argType = argType.substr(7);
                while (!argType.empty() && argType.back() == '*') argType.pop_back();
                llvm::Value* sPtr = evaluateExpr(node->args[i]); // &struct → ptr
                args.push_back(boxAsInterface(ep, argType, sPtr));
                boxed = true;
            }
        }
        if (!boxed) args.push_back(evaluateExpr(node->args[i]));
    }

    // Widen/truncate integer arguments to match function parameter types
    {
        auto fparams = func->getFunctionType()->params();
        for (size_t i = 0; i < args.size() && i < fparams.size(); ++i) {
            if (args[i]->getType()->isIntegerTy() && fparams[i]->isIntegerTy()
                    && args[i]->getType() != fparams[i]) {
                unsigned aw = llvm::cast<llvm::IntegerType>(args[i]->getType())->getBitWidth();
                unsigned pw = llvm::cast<llvm::IntegerType>(fparams[i])->getBitWidth();
                args[i] = aw < pw
                    ? builder->CreateSExt(args[i], fparams[i])
                    : builder->CreateTrunc(args[i], fparams[i]);
            }
        }
    }

    // sret: alloca for large struct return, pass as hidden arg 0, load result
    auto sretIt = funcSretTypes.find(func->getName().str());
    if (sretIt != funcSretTypes.end()) {
        llvm::Value* sretAlloca = builder->CreateAlloca(sretIt->second, nullptr, "sret.tmp");
        args.insert(args.begin(), sretAlloca);
        builder->CreateCall(func, args);
        exprValueStack.push(builder->CreateLoad(sretIt->second, sretAlloca));
    } else {
        exprValueStack.push(builder->CreateCall(func, args));
    }
}

void CodeGen::visit(IndexExpr* node) {
    llvm::Value* idx = evaluateExpr(node->index);
    std::string baseType = getExprEskiuType(node->base);

    // String indexing: string[i] → char element
    if (baseType == "string") {
        llvm::Value* ptr = evaluateExpr(node->base);
        llvm::Value* gep = builder->CreateGEP(llvm::Type::getInt8Ty(*context), ptr, idx);
        exprValueStack.push(builder->CreateLoad(llvm::Type::getInt8Ty(*context), gep));
        return;
    }

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
    while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();
    if (baseType.find('<') != std::string::npos) {
        auto [tn, args] = splitTemplateType(baseType);
        ensureTemplateInstantiated(mangleTemplate(baseType), tn, args);
        baseType = mangleTemplate(baseType);
    }

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
    } else if (val->getType()->isFloatingPointTy() && targetType->isFloatingPointTy()) {
        result = builder->CreateFPCast(val, targetType);
    } else if (val->getType()->isPointerTy() && targetType->isIntegerTy()) {
        result = builder->CreatePtrToInt(val, targetType);
    } else if (val->getType()->isIntegerTy() && targetType->isPointerTy()) {
        result = builder->CreateIntToPtr(val, targetType);
    } else if (val->getType()->isPointerTy() && targetType->isPointerTy()) {
        result = val; // opaque pointers: ptr == ptr, no bitcast needed
    } else {
        throw std::runtime_error("Cannot cast between these types");
    }

    exprValueStack.push(result);
}

void CodeGen::visit(LiteralExpr* node) {
    llvm::Value* result = nullptr;

    switch (node->kind) {
        case LiteralExpr::Kind::INT: {
            long long val = std::stoll(node->value, nullptr, 0); // base 0 = auto (dec/hex/oct)
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

    bool vol = volatileVars.count(node->name) > 0;
    if (llvm::isa<llvm::AllocaInst>(val)) {
        auto* inst = builder->CreateLoad(
            llvm::cast<llvm::AllocaInst>(val)->getAllocatedType(), val);
        inst->setVolatile(vol);
        result = inst;
    } else if (llvm::isa<llvm::GlobalVariable>(val)) {
        auto* gv = llvm::cast<llvm::GlobalVariable>(val);
        auto* inst = builder->CreateLoad(gv->getValueType(), gv);
        inst->setVolatile(vol);
        result = inst;
    } else {
        // Function argument or function pointer
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

    std::string allocSym = freestanding ? "esk_alloc" : "malloc";
    llvm::Function* mallocFn = getOrDeclareFunc(allocSym,
        llvm::PointerType::get(*context, 0), {llvm::Type::getInt64Ty(*context)});
    exprValueStack.push(builder->CreateCall(mallocFn, {total}, "alloc.ptr"));
}

void CodeGen::visit(LambdaExpr* node) {
    // Emit an anonymous LLVM function and push its pointer as the expression value.
    static int lambdaSeq = 0;
    std::string lambdaName = "__lambda" + std::to_string(lambdaSeq++);

    // Build LLVM param types
    std::vector<llvm::Type*> paramTypes;
    for (const auto& p : node->params)
        paramTypes.push_back(getTypeFromString(p.first));

    llvm::Type* retTy = getTypeFromString(node->returnType);
    llvm::FunctionType* fty = llvm::FunctionType::get(retTy, paramTypes, false);
    llvm::Function* func = llvm::Function::Create(
        fty, llvm::Function::InternalLinkage, lambdaName, module.get());

    size_t i = 0;
    for (auto& arg : func->args())
        arg.setName(node->params[i++].second);

    // Save builder state, compile body, restore
    llvm::Function* prevFunc      = currentFunction;
    llvm::Value*    prevSret      = currentSretParam;
    llvm::BasicBlock* prevInsert  = builder->GetInsertBlock();

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entry);
    currentFunction  = func;
    currentSretParam = nullptr;
    pushScope();

    i = 0;
    for (auto& arg : func->args()) {
        llvm::Value* slot = &arg;
        if (arg.getType()->isStructTy()) {
            auto* a = builder->CreateAlloca(arg.getType(), nullptr,
                                            node->params[i].second + ".byval");
            builder->CreateStore(&arg, a);
            slot = a;
        }
        defineSymbol(node->params[i].second, slot);
        defineVarType(node->params[i].second, node->params[i].first);
        i++;
    }

    if (node->body) node->body->accept(this);

    if (!builder->GetInsertBlock()->getTerminator()) {
        if (retTy->isVoidTy()) builder->CreateRetVoid();
        else builder->CreateRet(llvm::Constant::getNullValue(retTy));
    }

    popScope();
    currentFunction  = prevFunc;
    currentSretParam = prevSret;
    if (prevInsert) builder->SetInsertPoint(prevInsert);

    exprValueStack.push(func);
}

void CodeGen::visit(AsmStmt* node) {
    // Build LLVM inline asm from GCC-style extended asm syntax
    std::vector<llvm::Value*> argVals;
    std::string constraints;

    for (auto& [constraint, expr] : node->inputs) {
        argVals.push_back(evaluateExpr(expr));
        if (!constraints.empty()) constraints += ",";
        constraints += constraint;
    }
    for (const auto& clob : node->clobbers) {
        if (!constraints.empty()) constraints += ",";
        constraints += "~{" + clob + "}";
    }
    // sideeffect + alignstack are standard for kernel inline asm
    if (!constraints.empty()) constraints += ",~{dirflag},~{fpsr},~{flags}";

    std::vector<llvm::Type*> argTypes;
    for (auto* v : argVals) argTypes.push_back(v->getType());

    auto* fty = llvm::FunctionType::get(
        llvm::Type::getVoidTy(*context), argTypes, false);
    auto* iasm = llvm::InlineAsm::get(
        fty, node->asmString, constraints,
        /*hasSideEffects=*/true, /*isAlignStack=*/false,
        llvm::InlineAsm::AD_ATT);

    builder->CreateCall(iasm, argVals);
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
    // Build vtable struct type: %I_vtable = type { ptr, ptr, ... }
    std::vector<llvm::Type*> fnPtrs(node->methods.size(),
                                     llvm::PointerType::get(*context, 0));
    std::string vtName = node->name + "_vtable";
    llvm::StructType* vtType = llvm::StructType::create(*context, fnPtrs, vtName);
    ifaceVtableTypes[node->name] = vtType;

    // Method order + return/param type info for typed dispatch
    std::vector<std::string> order;
    std::vector<std::string> retTypes;
    std::vector<std::vector<std::string>> paramTypesList;
    for (const auto& m : node->methods) {
        order.push_back(m.name);
        retTypes.push_back(m.returnType);
        std::vector<std::string> pts;
        for (const auto& p : m.params) pts.push_back(p.first);
        paramTypesList.push_back(pts);
    }
    ifaceMethodOrder[node->name]           = order;
    ifaceMethodReturnTypes[node->name]      = retTypes;
    ifaceMethodParamEskiuTypes[node->name]  = paramTypesList;

    // Fat pointer type: %I_fat = type { ptr, ptr }
    llvm::StructType* fatPtr = llvm::StructType::create(*context,
        {llvm::PointerType::get(*context, 0), llvm::PointerType::get(*context, 0)},
        node->name + "_fat");
    ifaceFatPtrTypes[node->name] = fatPtr;
    // Interface values are always passed as ptr (pointer to fat struct)
    // getTypeFromString("I") → ptr  (handled in getTypeFromString below)
}

// Create a fat pointer {data_ptr, vtable_ptr} for struct S implementing interface I
llvm::Value* CodeGen::boxAsInterface(const std::string& ifaceName,
                                      const std::string& structName,
                                      llvm::Value* structPtr) {
    auto vtIt = ifaceVtableTypes.find(ifaceName);
    if (vtIt == ifaceVtableTypes.end())
        throw std::runtime_error("Unknown interface: " + ifaceName);

    const auto& methods = ifaceMethodOrder[ifaceName];
    llvm::StructType* vtType = vtIt->second;
    llvm::StructType* fatType = ifaceFatPtrTypes[ifaceName];

    // Build vtable constant: { &S_method1, &S_method2, ... }
    std::string vtGlobName = ifaceName + "_vtable_" + structName;
    llvm::GlobalVariable* vtGlob = module->getGlobalVariable(vtGlobName);
    if (!vtGlob) {
        std::vector<llvm::Constant*> entries;
        for (const auto& mname : methods) {
            std::string mangled = structName + "_" + mname;
            llvm::Function* fn = module->getFunction(mangled);
            if (!fn) throw std::runtime_error("Method not found: " + mangled);
            entries.push_back(fn);
        }
        llvm::Constant* vtInit = llvm::ConstantStruct::get(vtType, entries);
        vtGlob = new llvm::GlobalVariable(*module, vtType, true,
            llvm::GlobalValue::PrivateLinkage, vtInit, vtGlobName);
    }

    // Alloca for the fat pointer
    llvm::Value* fat = builder->CreateAlloca(fatType, nullptr, ifaceName + ".box");
    // fat[0] = data ptr
    llvm::Value* d = builder->CreateStructGEP(fatType, fat, 0);
    builder->CreateStore(structPtr, d);
    // fat[1] = vtable ptr
    llvm::Value* v = builder->CreateStructGEP(fatType, fat, 1);
    builder->CreateStore(vtGlob, v);
    return fat;  // pointer to fat pointer (alloca)
}

void CodeGen::visit(ContinueStmt* node) {
    if (!continueTarget)
        throw std::runtime_error("continue used outside of a loop");
    builder->CreateBr(continueTarget);
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
        llvm::BasicBlock*          savedBB         = builder->GetInsertBlock();
        llvm::BasicBlock::iterator savedPoint      = builder->GetInsertPoint();
        llvm::Function*            savedFunc       = currentFunction;
        llvm::Value*               savedSretParam  = currentSretParam;

        typeParamOverride = subs;
        auto inst = std::make_shared<FunctionDecl>(mangledName, fd->returnType, fd->params, fd->body);
        inst->accept(this);
        typeParamOverride.clear();

        // Restore caller's context
        currentFunction  = savedFunc;
        currentSretParam = savedSretParam;
        if (savedBB) builder->SetInsertPoint(savedBB, savedPoint);
    }

    llvm::Function* func = module->getFunction(mangledName);
    if (!func) throw std::runtime_error("Template instantiation failed: " + mangledName);

    std::vector<llvm::Value*> args;
    for (auto& arg : node->args) args.push_back(evaluateExpr(arg));

    auto sretIt = funcSretTypes.find(mangledName);
    if (sretIt != funcSretTypes.end()) {
        llvm::Value* sretAlloca = builder->CreateAlloca(sretIt->second, nullptr, "sret.tmp");
        args.insert(args.begin(), sretAlloca);
        builder->CreateCall(func, args);
        exprValueStack.push(builder->CreateLoad(sretIt->second, sretAlloca));
    } else {
        exprValueStack.push(builder->CreateCall(func, args));
    }
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
    while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();
    if (baseType.find('<') != std::string::npos) {
        auto [tn, args] = splitTemplateType(baseType);
        ensureTemplateInstantiated(mangleTemplate(baseType), tn, args);
        baseType = mangleTemplate(baseType);
    }
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
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();

    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, err);
    if (!target) {
        std::cerr << "error: " << err << std::endl;
        return false;
    }

    // Use native CPU for native compilation; generic CPU when cross-compiling
    bool isCross = !targetTriple.empty() &&
        targetTriple != llvm::sys::getDefaultTargetTriple();
    auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
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
