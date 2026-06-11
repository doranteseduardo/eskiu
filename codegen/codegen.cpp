#include "codegen.h"
#include "../ast/type_qual.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/BoundsChecking.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/raw_os_ostream.h"
#include <iostream>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with the type checker; see template_utils.h.
#include "../template_utils.h"

// ============================================================================

CodeGen::CodeGen()
    : context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("eskiu", *context)),
      builder(std::make_unique<llvm::IRBuilder<>>(*context)) {}

CodeGen::~CodeGen() = default;

llvm::Module* CodeGen::generateCode(std::shared_ptr<Program> program) {
    // Set target triple + data layout early so sizeof queries work in alloc()
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
#ifdef ESKIU_HAS_X86
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    LLVMInitializeX86AsmParser();
#endif
    std::string tripleStr = targetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple() : targetTriple;
    llvm::Triple triple(tripleStr);
    module->setTargetTriple(triple);
    std::string terr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tripleStr, terr);
    if (target) {
        bool isCross = !targetTriple.empty() &&
            targetTriple != llvm::sys::getDefaultTargetTriple();
        auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
        llvm::TargetOptions opt;
        std::unique_ptr<llvm::TargetMachine> tm(
            target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_));
        module->setDataLayout(tm->createDataLayout());
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

bool CodeGen::resolveArrayDim(const std::string& dim, uint64_t& out) const {
    if (dim.empty()) return false;
    bool digits = true;
    for (char c : dim) if (!std::isdigit((unsigned char)c)) { digits = false; break; }
    if (digits) { out = std::stoull(dim); return true; }
    auto e = enumConstants.find(dim);
    if (e != enumConstants.end()) { out = (uint64_t)e->second; return true; }
    auto c = constInts.find(dim);
    if (c != constInts.end())     { out = (uint64_t)c->second; return true; }
    return false;
}

llvm::Type* CodeGen::getTypeFromString(const std::string& typeStr) {
    // const has no ABI/layout meaning — strip it before lowering.
    { std::string s = tyq::strip(typeStr); if (s != typeStr) return getTypeFromString(s); }

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

    // Resolve a type alias to its underlying type.
    if (auto it = typeAliases.find(typeStr); it != typeAliases.end())
        return getTypeFromString(it->second);
    // va_list — backing storage for variadic access. {ptr,ptr,ptr,i32,i32} is the
    // AArch64 layout (32 B, 8-aligned) and a superset of x86-64's (24 B), so one
    // type works for both; the va_start/va_arg machinery reads the target's slice.
    if (typeStr == "va_list") {
        auto it = structTypes.find("__va_list");
        if (it != structTypes.end()) return it->second;
        llvm::Type* p = llvm::PointerType::get(*context, 0);
        llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
        auto* st = llvm::StructType::create(*context, {p, p, p, i32, i32}, "__va_list");
        structTypes["__va_list"] = st;
        return st;
    }
    // A classic enum is an i32; an algebraic enum is its tagged-union struct.
    if (enumTypes.count(typeStr)) {
        auto st = structTypes.find(typeStr);
        if (st != structTypes.end()) return st->second;   // ADT enum
        return llvm::Type::getInt32Ty(*context);
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
    // Function pointer type: fn(T,...)->R — fat pointer {fn_ptr, env_ptr}
    if (typeStr.size() > 3 && typeStr.substr(0, 3) == "fn(")
        return llvm::StructType::get(*context, {
            llvm::PointerType::get(*context, 0),  // fn pointer
            llvm::PointerType::get(*context, 0),  // env pointer (null if no capture)
        });

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
        if (genericEnumDecls.count(tname)) {                 // generic ADT enum instance
            std::string mangled = ensureEnumInst(tname, args);
            return structTypes[mangled];
        }
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
            uint64_t n = 0;
            if (resolveArrayDim(sizeStr, n)) {
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

llvm::Constant* CodeGen::evaluateConstantExpr(const ExprPtr& expr) {
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

std::string CodeGen::expandAlias(const std::string& raw) const {
    // const is checked only by the type checker; codegen works on stripped types.
    std::string t = tyq::strip(raw);
    if (t.empty()) return t;
    if (t.front() == '*') return "*" + expandAlias(t.substr(1));
    if (t.back()  == '*') return expandAlias(t.substr(0, t.size() - 1)) + "*";
    auto it = typeAliases.find(t);
    if (it != typeAliases.end()) return expandAlias(it->second);
    return t;
}

void CodeGen::unifyTypeParam(std::string pattern, std::string concrete,
                             const std::set<std::string>& tps,
                             std::map<std::string, std::string>& subs) {
    auto stripStruct = [](std::string s) {
        return s.rfind("struct:", 0) == 0 ? s.substr(7) : s;
    };
    auto canon = [](std::string t) {           // move trailing '*' to leading
        int stars = 0;
        while (!t.empty() && t.back()  == '*') { t.pop_back();    stars++; }
        while (!t.empty() && t.front() == '*') { t = t.substr(1); stars++; }
        return std::string(stars, '*') + t;
    };
    pattern  = canon(stripStruct(pattern));
    concrete = canon(stripStruct(concrete));
    // strip matching leading '*' from both
    size_t pi = 0, ci = 0;
    while (pi < pattern.size() && pattern[pi] == '*' &&
           ci < concrete.size() && concrete[ci] == '*') { pi++; ci++; }
    pattern = pattern.substr(pi);
    concrete = stripStruct(concrete.substr(ci));
    if (pattern.empty() || concrete.empty()) return;

    if (tps.count(pattern)) {                   // bare type parameter
        if (!subs.count(pattern)) subs[pattern] = concrete;
        return;
    }
    size_t lt = pattern.find('<');              // Name<args> vs an instance
    if (lt == std::string::npos) return;
    auto [pbase, pargs] = splitTemplateType(pattern);
    std::string cbase; std::vector<std::string> cargs;
    if (concrete.find('<') != std::string::npos) {
        auto pr = splitTemplateType(concrete); cbase = pr.first; cargs = pr.second;
    } else {
        auto it = templateInstanceArgs.find(concrete);
        if (it != templateInstanceArgs.end()) { cbase = it->second.first; cargs = it->second.second; }
    }
    if (cbase != pbase) return;
    for (size_t i = 0; i < pargs.size() && i < cargs.size(); ++i)
        unifyTypeParam(pargs[i], cargs[i], tps, subs);
}

std::string CodeGen::getExprEskiuType(const ExprPtr& expr) const {
    if (auto ident = dynamic_cast<IdentExpr*>(expr.get())) {
        return expandAlias(lookupVarType(ident->name));
    }
    // A cast's static type IS the cast target — important so that pointer
    // arithmetic on e.g. (*uint8)structPtr uses a byte stride, not the struct's.
    if (auto cast = dynamic_cast<CastExpr*>(expr.get())) {
        return expandAlias(cast->targetType);
    }
    if (auto lit = dynamic_cast<LiteralExpr*>(expr.get())) {
        switch (lit->kind) {
            case LiteralExpr::Kind::INT:    return "int";
            case LiteralExpr::Kind::FLOAT:  return "double"; // float literals are double
            case LiteralExpr::Kind::STRING: return "string";
            case LiteralExpr::Kind::CHAR:   return "char";
            case LiteralExpr::Kind::BOOL:   return "bool";
            default: return "";
        }
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
                if (f.name == member->member) return tyq::strip(f.type);
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
    // A call's static type is its function's return type — needed so member
    // access on a temporary (e.g. make().x) can resolve the struct.
    if (auto call = dynamic_cast<CallExpr*>(expr.get())) {
        if (auto id = dynamic_cast<IdentExpr*>(call->callee.get())) {
            auto it = funcEskiuReturnType.find(id->name);
            if (it != funcEskiuReturnType.end()) return expandAlias(it->second);
        } else if (auto m = dynamic_cast<MemberExpr*>(call->callee.get())) {
            std::string bt = getExprEskiuType(m->base);
            if (bt.size() > 7 && bt.substr(0, 7) == "struct:") bt = bt.substr(7);
            if (!bt.empty() && bt.front() == '*') bt = bt.substr(1);
            while (!bt.empty() && bt.back() == '*') bt.pop_back();
            if (bt.find('<') != std::string::npos) bt = mangleTemplate(bt);
            auto it = funcEskiuReturnType.find(bt + "_" + m->member);
            if (it != funcEskiuReturnType.end()) return expandAlias(it->second);
        }
        return "";
    }
    if (auto tc = dynamic_cast<TemplateCallExpr*>(expr.get())) {
        auto td = funcTemplateDecls.find(tc->templateName);
        if (td != funcTemplateDecls.end()) {
            auto& tp = td->second->typeParams;
            std::map<std::string, std::string> subs;
            for (size_t i = 0; i < tp.size() && i < tc->typeArgs.size(); ++i)
                subs[tp[i]] = tc->typeArgs[i];
            return expandAlias(substType(td->second->returnType, subs));
        }
        return "";
    }
    if (auto si = dynamic_cast<StructInitExpr*>(expr.get())) {
        return si->structName;
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
    // Three-phase lowering so that any declaration may reference any other
    // regardless of source order (forward references, mutual recursion):
    //   1. type shells (structs/unions) + extern declarations
    //   2. function prototypes (free functions and struct methods)
    //   3. bodies / globals
    // Pre-pass: fold top-level `const` ints so they can be used as array sizes
    // in struct fields / globals declared anywhere (resolved during phase 1).
    for (auto& decl : node->declarations) {
        if (auto* v = dynamic_cast<VarDecl*>(decl.get())) {
            if (v->isConst && v->initializer) {
                if (auto* c = evaluateConstantExpr(v->initializer))
                    if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c))
                        constInts[v->name] = ci->getSExtValue();
            }
        }
    }
    for (auto& decl : node->declarations) {
        if (auto* s = dynamic_cast<StructDecl*>(decl.get())) {
            declareStructType(s); // registers template structs and creates concrete types
        } else if (dynamic_cast<UnionDecl*>(decl.get()) ||
                   dynamic_cast<InterfaceDecl*>(decl.get()) ||
                   dynamic_cast<EnumDecl*>(decl.get()) ||
                   dynamic_cast<TypeAliasDecl*>(decl.get()) ||
                   dynamic_cast<ExternDecl*>(decl.get()) ||
                   dynamic_cast<IntrinsicDecl*>(decl.get())) {
            decl->accept(this);
        }
    }
    for (auto& decl : node->declarations) {
        if (auto* f = dynamic_cast<FunctionDecl*>(decl.get())) {
            if (f->typeParams.empty())
                declareFunction(f->name, f->returnType, f->params);
        } else if (auto* s = dynamic_cast<StructDecl*>(decl.get())) {
            if (!s->typeParams.empty()) continue;
            for (auto& method : s->methods) {
                if (auto* mf = dynamic_cast<FunctionDecl*>(method.get())) {
                    std::vector<std::pair<std::string, std::string>> params;
                    params.push_back({"*" + s->name, "self"});
                    for (auto& p : mf->params) params.push_back(p);
                    declareFunction(s->name + "_" + mf->name, mf->returnType, params);
                }
            }
        }
    }
    for (auto& decl : node->declarations) {
        // Externs, unions, interfaces, enums, and aliases were handled in phase 1.
        if (dynamic_cast<ExternDecl*>(decl.get()) ||
            dynamic_cast<IntrinsicDecl*>(decl.get()) ||
            dynamic_cast<UnionDecl*>(decl.get()) ||
            dynamic_cast<InterfaceDecl*>(decl.get()) ||
            dynamic_cast<EnumDecl*>(decl.get()) ||
            dynamic_cast<TypeAliasDecl*>(decl.get()))
            continue;
        decl->accept(this);
    }
}

llvm::Function* CodeGen::declareFunction(
        const std::string& name, const std::string& returnTypeStr,
        const std::vector<std::pair<std::string, std::string>>& params) {
    // Get parameter types
    std::vector<llvm::Type*> paramTypes;
    for (auto& param : params) {
        if (param.first == "...") continue; // variadic — handled by extern decls
        paramTypes.push_back(getTypeFromString(param.first));
    }

    // Use sret for large struct returns
    llvm::Type* returnType = getTypeFromString(returnTypeStr);
    bool sret = needsSret(returnType);
    if (sret) {
        funcSretTypes[name] = llvm::cast<llvm::StructType>(returnType);
        // Prepend hidden sret pointer as first parameter
        paramTypes.insert(paramTypes.begin(), llvm::PointerType::get(*context, 0));
    }

    // Eskiu param types — for interface boxing at call sites
    {
        std::vector<std::string> pts;
        for (auto& p : params)
            if (p.first != "...") pts.push_back(p.first);
        funcEskiuParamTypes[name] = pts;
    }
    funcEskiuReturnType[name] = returnTypeStr;

    // Idempotent: reuse a prototype declared by the pre-pass.
    if (llvm::Function* existing = module->getFunction(name)) return existing;

    bool isVarArg = false;
    for (auto& p : params) if (p.first == "...") isVarArg = true;
    llvm::FunctionType* funcType = llvm::FunctionType::get(
        sret ? llvm::Type::getVoidTy(*context) : returnType, paramTypes, isVarArg);
    llvm::Function* func = llvm::Function::Create(
        funcType, llvm::Function::ExternalLinkage, name, module.get());

    // Set parameter names (skip index 0 for sret functions — that's the hidden ret ptr)
    size_t paramIdx = 0;
    size_t argIdx   = 0;
    for (auto& arg : func->args()) {
        if (sret && argIdx == 0) {
            arg.setName("sret.ptr");
            argIdx++;
            continue;
        }
        if (paramIdx < params.size() && params[paramIdx].first != "...") {
            arg.setName(params[paramIdx].second);
            paramIdx++;
        }
        argIdx++;
    }
    return func;
}

void CodeGen::visit(FunctionDecl* node) {
    if (!node->typeParams.empty()) {
        funcTemplateDecls[node->name] = node;
        return;
    }

    // Declare (or reuse) the prototype, then emit the body.
    llvm::Function* func = declareFunction(node->name, node->returnType, node->params);

    // A body-less declaration (forward declaration) only needs the prototype.
    if (!node->body) return;
    // Defensive: skip if a body was already emitted (e.g. forward decl + definition).
    if (!func->empty()) return;

    llvm::Type* returnType = getTypeFromString(node->returnType);
    bool sret = needsSret(returnType);

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
    // (Eskiu param types for interface boxing were registered by declareFunction.)

    // Define parameters in symbol table + type map (skip sret hidden param at index 0)
    size_t paramIdx = 0;
    size_t argIdx   = 0;
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
                // Instantiate the template instance's struct now, so member
                // access on this param (e.g. List<String>* self -> self.data)
                // finds its fields even if no earlier code referenced the type.
                auto [tn, targs] = splitTemplateType(ptype);
                ensureTemplateInstantiated(mangleTemplate(ptype), tn, targs);
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
    // Register `const` ints so a later (local) array dimension can use them.
    if (node->isConst && node->initializer && !constInts.count(node->name)) {
        if (auto* c = evaluateConstantExpr(node->initializer))
            if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c))
                constInts[node->name] = ci->getSExtValue();
    }

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
    templateInstanceArgs[mangled] = {tname, args};  // record for type-arg inference
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
    // #pragma pack(N>=2): same manual layout as concrete structs.
    if (tmpl->packAlign >= 2) {
        std::vector<llvm::Type*> phys;
        std::map<std::string, BitfieldSlot> slots;
        buildPackedLayout(fields, (unsigned)tmpl->packAlign, phys, slots);
        structTypes[mangled]  = llvm::StructType::create(*context, phys, mangled, /*isPacked=*/true);
        structFields[mangled] = fields;
        structLayout[mangled] = slots;
        return;
    }
    llvm::StructType* st = llvm::StructType::create(*context, fieldTypes, mangled, tmpl->isPacked);
    structTypes[mangled] = st;
    structFields[mangled] = fields;
}

bool CodeGen::buildPackedLayout(const std::vector<StructDecl::Field>& fields, unsigned packN,
                                std::vector<llvm::Type*>& phys,
                                std::map<std::string, BitfieldSlot>& slots) {
    const llvm::DataLayout& DL = module->getDataLayout();
    llvm::Type* i8 = llvm::Type::getInt8Ty(*context);
    uint64_t offset = 0, structAlign = 1;
    for (const auto& f : fields) {
        if (f.bitWidth > 0) return false;  // pack + bitfields: fall back to the bitfield path
        llvm::Type* ft = getTypeFromString(f.type);
        uint64_t align = std::min<uint64_t>(DL.getABITypeAlign(ft).value(), packN);
        if (align > structAlign) structAlign = align;
        uint64_t aligned = (offset + align - 1) / align * align;
        if (aligned > offset) { phys.push_back(llvm::ArrayType::get(i8, aligned - offset)); offset = aligned; }
        BitfieldSlot s;
        s.isBitfield = false;
        s.physIndex = (unsigned)phys.size();
        s.storageType = ft;
        slots[f.name] = s;
        phys.push_back(ft);
        offset += DL.getTypeAllocSize(ft).getFixedValue();
    }
    // Round the total size up to the struct's alignment (min(maxFieldAlign, N)),
    // so an array element stride matches the C `#pragma pack(N)` ABI.
    uint64_t total = (offset + structAlign - 1) / structAlign * structAlign;
    if (total > offset) phys.push_back(llvm::ArrayType::get(i8, total - offset));
    return true;
}

void CodeGen::declareStructType(StructDecl* node) {
    if (!node->typeParams.empty()) {
        templateDecls[node->name] = node;
        return;
    }
    if (structTypes.count(node->name)) return; // already created by the pre-pass

    bool hasBitfields = false;
    for (const auto& f : node->fields) if (f.bitWidth > 0) hasBitfields = true;

    if (!hasBitfields) {
        // #pragma pack(N>=2): manual layout (padding + physical-index remap).
        if (node->packAlign >= 2) {
            std::vector<llvm::Type*> phys;
            std::map<std::string, BitfieldSlot> slots;
            buildPackedLayout(node->fields, (unsigned)node->packAlign, phys, slots);
            structTypes[node->name]  = llvm::StructType::create(*context, phys, node->name, /*isPacked=*/true);
            structFields[node->name] = node->fields;
            structLayout[node->name] = slots;
            return;
        }
        std::vector<llvm::Type*> fieldTypes;
        for (const auto& field : node->fields)
            fieldTypes.push_back(getTypeFromString(field.type));
        structTypes[node->name] = llvm::StructType::create(*context, fieldTypes, node->name, node->isPacked);
        structFields[node->name] = node->fields;
        return;
    }

    // Packed layout: pack consecutive bitfields into storage words of their
    // declared type; non-bitfield fields close the current word and get their
    // own slot. Each field records its physical slot + bit position.
    std::vector<llvm::Type*> phys;
    std::map<std::string, BitfieldSlot> slots;
    int curPhys = -1; unsigned curUnitBits = 0, curOffset = 0;
    for (const auto& f : node->fields) {
        if (f.bitWidth > 0) {
            llvm::Type* sty = getTypeFromString(f.type);
            unsigned stBits = sty->getIntegerBitWidth();
            if (curPhys < 0 || curUnitBits != stBits ||
                curOffset + (unsigned)f.bitWidth > stBits) {
                phys.push_back(sty);
                curPhys = (int)phys.size() - 1;
                curUnitBits = stBits; curOffset = 0;
            }
            BitfieldSlot s;
            s.isBitfield = true; s.physIndex = (unsigned)curPhys;
            s.bitOffset = curOffset; s.bitWidth = (unsigned)f.bitWidth;
            s.storageType = sty; s.isSigned = (f.type.rfind("uint", 0) != 0);
            slots[f.name] = s;
            curOffset += (unsigned)f.bitWidth;
        } else {
            curPhys = -1; curUnitBits = 0; curOffset = 0;
            llvm::Type* ft = getTypeFromString(f.type);
            phys.push_back(ft);
            BitfieldSlot s;
            s.isBitfield = false; s.physIndex = (unsigned)phys.size() - 1;
            s.storageType = ft;
            slots[f.name] = s;
        }
    }
    structTypes[node->name]  = llvm::StructType::create(*context, phys, node->name, node->isPacked);
    structFields[node->name] = node->fields;
    structLayout[node->name] = slots;
}

void CodeGen::visit(StructDecl* node) {
    if (!node->typeParams.empty()) {
        templateDecls[node->name] = node;
        return;
    }

    declareStructType(node);

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

void CodeGen::visit(IntrinsicDecl* node) {
    // No declaration is emitted: a call to an intrinsic lowers to inline IR
    // (see the intrinsic dispatch at the top of visit(CallExpr)). We only record
    // the name so that callsites can be recognised regardless of source order.
    intrinsicNames.insert(node->name);
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

void CodeGen::visit(ForInStmt* node) {
    // Desugar `for (x in it)` into a counted for-loop. We lower to a real
    // ForStmt (not a while) so `continue` lands on the index increment.
    static int counter = 0;
    std::string idxName = "__forin_i_" + std::to_string(counter++);
    auto idx = [&]() -> ExprPtr { return std::make_shared<IdentExpr>(idxName); };
    auto intLit = [&](const std::string& v) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::INT, v);
    };

    std::string itType = getExprEskiuType(node->iterable);
    std::string elemType;
    ExprPtr lengthExpr, elemExpr;

    size_t lb = itType.rfind('[');
    if (lb != std::string::npos && !itType.empty() && itType.back() == ']') {
        // Fixed-size array T[N] — resolve N (literal, enum, or const int).
        elemType   = itType.substr(0, lb);
        uint64_t len = 0;
        resolveArrayDim(itType.substr(lb + 1, itType.size() - lb - 2), len);
        lengthExpr = intLit(std::to_string(len));
        elemExpr   = std::make_shared<IndexExpr>(node->iterable, idx());
    } else {
        // List-like struct: needs `data` (pointer) and `size` (int) fields.
        std::string s = itType;
        while (!s.empty() && s.front() == '*') s = s.substr(1);
        while (!s.empty() && s.back()  == '*') s.pop_back();
        auto it = structFields.find(s);
        std::string dataType;
        bool hasSize = false;
        if (it != structFields.end())
            for (const auto& f : it->second) {
                if (f.name == "data") dataType = f.type;
                if (f.name == "size") hasSize = true;
            }
        if (dataType.empty() || !hasSize)
            throw std::runtime_error("for-in over unsupported type: " + itType);
        while (!dataType.empty() && dataType.front() == '*') dataType = dataType.substr(1);
        while (!dataType.empty() && dataType.back()  == '*') dataType.pop_back();
        elemType   = dataType;
        lengthExpr = std::make_shared<MemberExpr>(node->iterable, "size");
        elemExpr   = std::make_shared<IndexExpr>(
            std::make_shared<MemberExpr>(node->iterable, "data"), idx());
    }

    auto idxDecl = std::make_shared<VarDecl>(idxName, "int", intLit("0"));
    StmtPtr init = std::make_shared<BlockStmt>(std::vector<BlockItem>{DeclPtr(idxDecl)});
    ExprPtr cond = std::make_shared<BinaryExpr>(idx(), "<", lengthExpr);
    ExprPtr step = std::make_shared<BinaryExpr>(idx(), "=",
                       std::make_shared<BinaryExpr>(idx(), "+", intLit("1")));

    auto elemDecl = std::make_shared<VarDecl>(node->varName, elemType, elemExpr);
    StmtPtr body = std::make_shared<BlockStmt>(std::vector<BlockItem>{
        DeclPtr(elemDecl), StmtPtr(node->body)});

    ForStmt loop(init, cond, step, body);
    visit(&loop);
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
        // Bitfield assignment is a read-modify-write, not a plain store.
        if (auto* mem = dynamic_cast<MemberExpr*>(node->left.get())) {
            auto lit = structLayout.find(structBaseTypeOf(mem->base));
            if (lit != structLayout.end()) {
                auto sit = lit->second.find(mem->member);
                if (sit != lit->second.end() && sit->second.isBitfield) {
                    llvm::Value* rhs = evaluateExpr(node->right);
                    storeBitfield(mem, rhs);
                    exprValueStack.push(rhs);
                    return;
                }
            }
        }
        llvm::Value* lhs = evaluateLValue(node->left);
        llvm::Value* rhs = evaluateExpr(node->right);
        // Coerce RHS to match the lvalue's expected element type.
        // Prefer the LHS's declared (static) scalar type: a union member lvalue
        // collapses to the union's base pointer (all fields at offset 0), so the
        // alloca/GEP type encodes the union storage, not the selected field — and
        // a double would be stored whole into a float field without truncation.
        llvm::Type* elemType = nullptr;
        std::string lhsEskiu = getExprEskiuType(node->left);
        if (!lhsEskiu.empty()) {
            llvm::Type* st = getTypeFromString(lhsEskiu);
            if (st && (st->isFloatingPointTy() || st->isIntegerTy()))
                elemType = st;
        }
        if (!elemType) {
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(lhs))
                elemType = alloca->getAllocatedType();
            else if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(lhs))
                elemType = gep->getResultElementType();
        }
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

    // Integer signedness of each operand, from its Eskiu type. Drives sign- vs
    // zero-extension when widening, and signed vs unsigned div/rem/shr/compare.
    auto isUnsignedEsk = [&](const ExprPtr& e) -> bool {
        std::string t = expandAlias(getExprEskiuType(e));
        return t == "uint" || t == "uint8" || t == "uint16" || t == "uint32" ||
               t == "uint64" || t == "char" || t == "bool";
    };
    bool lUns = isUnsignedEsk(node->left);
    bool rUns = isUnsignedEsk(node->right);
    bool opUnsigned = lUns || rUns;   // C-style: unsigned wins in a mixed op
    auto extTo = [&](llvm::Value* v, llvm::Type* ty, bool uns) {
        return uns ? builder->CreateZExt(v, ty) : builder->CreateSExt(v, ty);
    };

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

    // Widen the narrower integer to match the wider one, extending each operand
    // according to ITS OWN signedness (sign-extend signed, zero-extend unsigned).
    auto widenInts = [&]() {
        if (left->getType()->isIntegerTy() && right->getType()->isIntegerTy()
                && left->getType() != right->getType()) {
            unsigned lw = llvm::cast<llvm::IntegerType>(left->getType())->getBitWidth();
            unsigned rw = llvm::cast<llvm::IntegerType>(right->getType())->getBitWidth();
            if (lw < rw) left  = extTo(left,  right->getType(), lUns);
            else          right = extTo(right, left->getType(),  rUns);
        }
    };
    auto widenForBitwise = widenInts;
    auto widenForArith   = widenInts;

    // Resolve the element type for typed pointer arithmetic.
    // *int → i32, *uint8 → i8, *void/*char/unknown → i8 (byte arithmetic)
    auto ptrElemType = [&]() -> llvm::Type* {
        std::string eskTy = getExprEskiuType(node->left);
        if (eskTy.empty()) return llvm::Type::getInt8Ty(*context);
        // Strip leading *
        if (!eskTy.empty() && eskTy.front() == '*') eskTy = eskTy.substr(1);
        // Strip trailing *
        if (!eskTy.empty() && eskTy.back()  == '*') eskTy.pop_back();
        if (eskTy == "void" || eskTy == "char" || eskTy.empty())
            return llvm::Type::getInt8Ty(*context);
        return getTypeFromString(eskTy);
    };

    if (node->op == "+") {
        if (left->getType()->isPointerTy()) {
            llvm::Value* idx = builder->CreateSExtOrTrunc(
                right, llvm::Type::getInt64Ty(*context), "ptr.idx");
            result = builder->CreateGEP(ptrElemType(), left, idx, "ptr.add");
        } else {
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
            llvm::Value* neg = builder->CreateNeg(
                builder->CreateSExtOrTrunc(right, llvm::Type::getInt64Ty(*context)), "neg");
            result = builder->CreateGEP(ptrElemType(), left, neg, "ptr.sub");
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
            : (opUnsigned ? builder->CreateUDiv(left, right)
                          : builder->CreateSDiv(left, right));
    } else if (node->op == "%") {
        promoteToFloat(); widenForArith();
        result = left->getType()->isFloatingPointTy()
            ? builder->CreateFRem(left, right)
            : (opUnsigned ? builder->CreateURem(left, right)
                          : builder->CreateSRem(left, right));
    } else if (node->op == "==") {
        if (left->getType()->isFloatingPointTy())
            result = builder->CreateFCmpOEQ(left, right);
        else {
            widenInts();   // equality is bit-equal; widening just needs the right extend
            result = builder->CreateICmpEQ(left, right);
        }
    } else if (node->op == "!=" || node->op == "<" || node->op == ">" ||
               node->op == "<=" || node->op == ">=") {
        bool isFloat = left->getType()->isFloatingPointTy();
        if (!isFloat) widenInts();
        if (node->op == "!=") {
            result = isFloat ? builder->CreateFCmpONE(left, right)
                             : builder->CreateICmpNE(left, right);
        } else if (node->op == "<") {
            result = isFloat ? builder->CreateFCmpOLT(left, right)
                   : (opUnsigned ? builder->CreateICmpULT(left, right)
                                 : builder->CreateICmpSLT(left, right));
        } else if (node->op == ">") {
            result = isFloat ? builder->CreateFCmpOGT(left, right)
                   : (opUnsigned ? builder->CreateICmpUGT(left, right)
                                 : builder->CreateICmpSGT(left, right));
        } else if (node->op == "<=") {
            result = isFloat ? builder->CreateFCmpOLE(left, right)
                   : (opUnsigned ? builder->CreateICmpULE(left, right)
                                 : builder->CreateICmpSLE(left, right));
        } else {
            result = isFloat ? builder->CreateFCmpOGE(left, right)
                   : (opUnsigned ? builder->CreateICmpUGE(left, right)
                                 : builder->CreateICmpSGE(left, right));
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
        widenForBitwise();
        result = opUnsigned ? builder->CreateLShr(left, right)
                            : builder->CreateAShr(left, right);
    } else {
        throw std::runtime_error("Unknown binary operator: " + node->op);
    }

    exprValueStack.push(result);
}

void CodeGen::visit(QuestionExpr* node) {
    // `expr?` — if expr is an Err Result, return it from the enclosing function;
    // otherwise evaluate to the unwrapped success value.
    llvm::Value* resVal = evaluateExpr(node->operand);
    llvm::StructType* st = llvm::dyn_cast<llvm::StructType>(resVal->getType());
    if (!st || !st->hasName())
        throw std::runtime_error("`?` operator requires a named Result struct value");
    std::string opType = st->getName().str();

    auto fIt = structFields.find(opType);
    if (fIt == structFields.end())
        throw std::runtime_error("`?` operator on non-Result type: " + opType);

    unsigned okIdx = 0, valueIdx = 0;
    std::string valueFieldType;
    for (unsigned i = 0; i < fIt->second.size(); ++i) {
        if (fIt->second[i].name == "ok")    okIdx = i;
        if (fIt->second[i].name == "value") { valueIdx = i; valueFieldType = fIt->second[i].type; }
    }

    // Materialize the Result into a temp so we can read fields and return it whole.
    llvm::Value* tmp = builder->CreateAlloca(st, nullptr, "try.tmp");
    builder->CreateStore(resVal, tmp);

    llvm::Value* okPtr = builder->CreateStructGEP(st, tmp, okIdx);
    llvm::Type*  okTy  = st->getElementType(okIdx);
    llvm::Value* okVal = builder->CreateLoad(okTy, okPtr, "try.ok");
    llvm::Value* isErr = builder->CreateICmpEQ(okVal, llvm::ConstantInt::get(okTy, 0), "try.iserr");

    llvm::BasicBlock* errBB  = llvm::BasicBlock::Create(*context, "try.err",  currentFunction);
    llvm::BasicBlock* contBB = llvm::BasicBlock::Create(*context, "try.cont", currentFunction);
    builder->CreateCondBr(isErr, errBB, contBB);

    // Error path: propagate the Result unchanged out of the enclosing function.
    builder->SetInsertPoint(errBB);
    llvm::Value* whole = builder->CreateLoad(st, tmp, "try.whole");
    if (currentSretParam != nullptr) {
        builder->CreateStore(whole, currentSretParam);
        builder->CreateRetVoid();
    } else {
        builder->CreateRet(whole);
    }

    // Success path: unwrap and yield the value field.
    builder->SetInsertPoint(contBB);
    llvm::Value* valPtr = builder->CreateStructGEP(st, tmp, valueIdx);
    llvm::Type*  valTy  = getTypeFromString(valueFieldType);
    exprValueStack.push(builder->CreateLoad(valTy, valPtr, "try.value"));
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

// Lower a call to an `intrinsic`-declared function to inline IR. The registry of
// supported intrinsics lives here; their signatures are declared in stdlib (e.g.
// stdlib/atomic.esk) and the orderings/semantics are fixed (docs/dev/async-design.md §3).
llvm::Value* CodeGen::lowerIntrinsicCall(const std::string& fn, CallExpr* node) {
    // Atomics operate on a *int (i32) cell.
    if (fn == "atomic_load") {
        llvm::Value* cell = evaluateExpr(node->args[0]);
        auto* ld = builder->CreateLoad(llvm::Type::getInt32Ty(*context), cell, "atm.load");
        ld->setAtomic(llvm::AtomicOrdering::Acquire);
        ld->setAlignment(llvm::Align(4));
        return ld;
    }
    if (fn == "atomic_store") {
        llvm::Value* cell = evaluateExpr(node->args[0]);
        llvm::Value* v    = evaluateExpr(node->args[1]);
        auto* st = builder->CreateStore(v, cell);
        st->setAtomic(llvm::AtomicOrdering::Release);
        st->setAlignment(llvm::Align(4));
        return llvm::UndefValue::get(llvm::Type::getVoidTy(*context));
    }
    if (fn == "atomic_swap") {
        llvm::Value* cell = evaluateExpr(node->args[0]);
        llvm::Value* v    = evaluateExpr(node->args[1]);
        return builder->CreateAtomicRMW(
            llvm::AtomicRMWInst::Xchg, cell, v, llvm::MaybeAlign(4),
            llvm::AtomicOrdering::AcquireRelease);
    }
    if (fn == "atomic_cas") {
        llvm::Value* cell     = evaluateExpr(node->args[0]);
        llvm::Value* expected = evaluateExpr(node->args[1]);
        llvm::Value* desired  = evaluateExpr(node->args[2]);
        llvm::Value* cx = builder->CreateAtomicCmpXchg(
            cell, expected, desired, llvm::MaybeAlign(4),
            llvm::AtomicOrdering::AcquireRelease, llvm::AtomicOrdering::Acquire);
        return builder->CreateExtractValue(cx, 1, "atm.cas.ok"); // success bit (i1)
    }
    throw std::runtime_error("intrinsic '" + fn + "' is declared but has no codegen lowering");
}

void CodeGen::visit(CallExpr* node) {
    // Variadic access: va_start(ap) / va_end(ap) — `ap` is a local va_list, whose
    // alloca is the pointer the intrinsics need.
    if (auto* bid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if ((bid->name == "va_start" || bid->name == "va_end") && node->args.size() == 1) {
            llvm::Value* ap = evaluateLValue(node->args[0]);
            llvm::Function* fn = getOrDeclareFunc(
                bid->name == "va_start" ? "llvm.va_start.p0" : "llvm.va_end.p0",
                llvm::Type::getVoidTy(*context), {llvm::PointerType::get(*context, 0)}, false);
            exprValueStack.push(builder->CreateCall(fn, {ap}));
            return;
        }
    }
    // Algebraic variant construction: `Circle(2.0)`, `Some(x)`.
    if (auto* vid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if (!lookupSymbol(vid->name) && adtVariants.count(vid->name)) {
            exprValueStack.push(buildVariant(vid->name, node->args));
            return;
        }
        // Bare generic-variant with inference: infer each type param from the arg
        // whose payload slot IS that param (the type checker has verified all are
        // determined), then build the instance.
        if (!lookupSymbol(vid->name) && genericVariants.count(vid->name)) {
            auto& gi = genericVariants[vid->name];
            EnumDecl* ge = genericEnumDecls[gi.first];
            const auto& payload = ge->payloads[gi.second];
            std::map<std::string, std::string> subs;
            for (size_t i = 0; i < payload.size() && i < node->args.size(); ++i)
                for (const auto& tp : ge->typeParams)
                    if (payload[i] == tp && !subs.count(tp)) {
                        std::string at = getExprEskiuType(node->args[i]);
                        if (!typeParamOverride.empty()) at = substType(at, typeParamOverride);
                        subs[tp] = at;
                    }
            std::vector<std::string> targs;
            for (const auto& tp : ge->typeParams) targs.push_back(subs.count(tp) ? subs[tp] : "int");
            std::string mangled = ensureEnumInst(gi.first, targs);
            std::map<std::string, std::string> sub2;
            for (size_t i = 0; i < ge->typeParams.size() && i < targs.size(); ++i) sub2[ge->typeParams[i]] = targs[i];
            std::vector<llvm::Type*> fts;
            for (const auto& ft : payload) fts.push_back(getTypeFromString(substType(ft, sub2)));
            exprValueStack.push(buildEnumValue(structTypes[mangled], gi.second, fts, node->args));
            return;
        }
    }
    // Intrinsics: a call to an `intrinsic`-declared name lowers to inline IR
    // instead of a call. Gated on intrinsicNames (only populated when the
    // declaring module is imported), so a user function of the same name that
    // was *not* imported as an intrinsic is unaffected.
    if (auto* aid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        const std::string& fn = aid->name;
        if (intrinsicNames.count(fn) && !lookupSymbol(fn)) {
            exprValueStack.push(lowerIntrinsicCall(fn, node));
            return;
        }
    }

    // Template function called without explicit type arguments: infer each type
    // parameter by structurally unifying every parameter type against the concrete
    // argument type. Covers bare params (T max<T>(T a, T b) → max(3,5)) and
    // composite ones (T List_get<T>(List<T>* self, int i) → List_get(&nums, i)).
    if (auto* id = dynamic_cast<IdentExpr*>(node->callee.get())) {
        auto tIt = funcTemplateDecls.find(id->name);
        if (tIt != funcTemplateDecls.end()) {
            FunctionDecl* fd = tIt->second;
            std::set<std::string> tps(fd->typeParams.begin(), fd->typeParams.end());
            std::map<std::string, std::string> subs;
            for (size_t j = 0; j < fd->params.size() && j < node->args.size(); ++j)
                unifyTypeParam(fd->params[j].first, getExprEskiuType(node->args[j]), tps, subs);
            std::vector<std::string> typeArgs;
            for (const auto& tpName : fd->typeParams) {
                auto sIt = subs.find(tpName);
                if (sIt == subs.end()) break;
                typeArgs.push_back(sIt->second);
            }
            if (typeArgs.size() == fd->typeParams.size()) {
                TemplateCallExpr tc(id->name, typeArgs, node->args);
                visit(&tc);
                return;
            }
        }
    }

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
        // Not a method: if o.member is a fn-pointer field, fall through to the
        // general indirect-call path (evaluateExpr(callee) yields the fat ptr).
        std::string ft = getExprEskiuType(node->callee);
        if (!(ft.size() > 3 && ft.substr(0, 3) == "fn("))
            throw std::runtime_error("Undefined method: " + baseType + "::" + member->member);
    }

    // A bare name that resolves to a function (and is not shadowed by a local
    // fn-pointer variable) is a direct call — use the function itself, not the
    // decayed closure fat pointer that evaluating it as a value would produce.
    llvm::Value* calleeVal = nullptr;
    if (auto ident = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if (!lookupSymbol(ident->name))
            calleeVal = module->getFunction(ident->name);
    }
    if (!calleeVal) calleeVal = evaluateExpr(node->callee);
    if (!calleeVal) throw std::runtime_error("Call target is null");

    // Indirect call through a fat-pointer closure {fn_ptr, env_ptr}
    if (!llvm::isa<llvm::Function>(calleeVal)) {
        std::string eskiuType = getExprEskiuType(node->callee);
        if (eskiuType.size() > 3 && eskiuType.substr(0, 3) == "fn(") {
            // Extract params and return type from "fn(T,...)->R"
            size_t rp = eskiuType.find(")->");
            std::string paramStr = eskiuType.substr(3, rp - 3);
            std::string retStr   = eskiuType.substr(rp + 3);
            std::vector<llvm::Type*> pts;
            llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
            pts.push_back(ptrTy); // env* always first
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

            // Extract fn_ptr and env_ptr from the fat pointer struct
            llvm::StructType* fatTy = llvm::cast<llvm::StructType>(calleeVal->getType());
            llvm::Value* fnPtr  = builder->CreateExtractValue(calleeVal, {0}, "fn.ptr");
            llvm::Value* envPtr = builder->CreateExtractValue(calleeVal, {1}, "env.ptr");

            std::vector<llvm::Value*> iargs = {envPtr};
            for (auto& a : node->args) iargs.push_back(evaluateExpr(a));
            // A void-returning call must not be given a name (LLVM forbids it).
            exprValueStack.push(builder->CreateCall(
                fty, fnPtr, iargs, retTy->isVoidTy() ? "" : "fn.call"));
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

    // C default argument promotions for the variadic ("...") arguments: an
    // integer narrower than int widens to i32 (sign/zero per its signedness),
    // and a float widens to double. Without this, printf("%d", anInt8) reads a
    // full int from a byte-sized argument slot.
    if (func->getFunctionType()->isVarArg()) {
        auto argUnsigned = [&](size_t i) -> bool {
            if (i >= node->args.size()) return false;
            std::string t = expandAlias(getExprEskiuType(node->args[i]));
            return t == "uint" || t == "uint8" || t == "uint16" || t == "uint32" ||
                   t == "uint64" || t == "char" || t == "bool";
        };
        unsigned fixed = func->getFunctionType()->getNumParams();
        llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
        for (size_t i = fixed; i < args.size(); ++i) {
            llvm::Type* at = args[i]->getType();
            if (at->isIntegerTy() && at->getIntegerBitWidth() < 32) {
                // i1 (a bool / comparison result) is 0/1 — always zero-extend.
                bool uns = argUnsigned(i) || at->getIntegerBitWidth() == 1;
                args[i] = uns ? builder->CreateZExt(args[i], i32)
                              : builder->CreateSExt(args[i], i32);
            } else if (at->isFloatTy()) {
                args[i] = builder->CreateFPExt(args[i], llvm::Type::getDoubleTy(*context));
            }
        }
    }

    // sret: alloca for large struct return, pass as hidden arg 0, load result
    auto sretIt = funcSretTypes.find(func->getName().str());
    if (sretIt != funcSretTypes.end()) {
        llvm::Value* sretAlloca = builder->CreateAlloca(sretIt->second, nullptr, "sret.tmp");
        args.insert(args.begin(), sretAlloca);
        createMaybeInvoke(func->getFunctionType(), func, args);
        exprValueStack.push(builder->CreateLoad(sretIt->second, sretAlloca));
    } else {
        exprValueStack.push(
            createMaybeInvoke(func->getFunctionType(), func, args));
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

std::string CodeGen::structBaseTypeOf(const ExprPtr& base) {
    std::string baseType = getExprEskiuType(base);
    if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
    if (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
    while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();
    if (baseType.find('<') != std::string::npos) {
        auto [tn, args] = splitTemplateType(baseType);
        ensureTemplateInstantiated(mangleTemplate(baseType), tn, args);
        baseType = mangleTemplate(baseType);
    }
    return baseType;
}

void CodeGen::visit(MemberExpr* node) {
    std::string baseType = structBaseTypeOf(node->base);

    // A pointer-to-struct base is dereferenced via its value; a value-struct
    // base via its address (see the matching logic in evaluateLValue).
    std::string rawBaseTy = getExprEskiuType(node->base);
    bool baseIsPtr = (!rawBaseTy.empty() && (rawBaseTy.front() == '*' || rawBaseTy.back() == '*'));
    // A struct-valued temporary (call result, template call, struct literal) is
    // an rvalue with no address — materialize it into an alloca so we can GEP.
    Expr* b = node->base.get();
    bool baseIsTemp = !baseIsPtr &&
        (dynamic_cast<CallExpr*>(b) || dynamic_cast<TemplateCallExpr*>(b) ||
         dynamic_cast<StructInitExpr*>(b));
    auto baseAddr = [&]() -> llvm::Value* {
        if (baseIsPtr) return evaluateExpr(node->base);
        if (baseIsTemp) {
            llvm::Value* v = evaluateExpr(node->base);
            llvm::Value* tmp = builder->CreateAlloca(v->getType(), nullptr, "mem.tmp");
            builder->CreateStore(v, tmp);
            return tmp;
        }
        return evaluateLValue(node->base);
    };

    // Bitfield-layout struct: physical slot map (handles bitfields and the
    // non-bitfield fields whose physical index differs from the logical one).
    auto lit = structLayout.find(baseType);
    if (lit != structLayout.end()) {
        auto sit = lit->second.find(node->member);
        if (sit == lit->second.end())
            throw std::runtime_error("Struct '" + baseType + "' has no field '" + node->member + "'");
        const BitfieldSlot& slot = sit->second;
        llvm::Value* basePtr = baseAddr();
        llvm::Value* gep = builder->CreateStructGEP(structTypes[baseType], basePtr,
                                                    slot.physIndex, node->member);
        if (!slot.isBitfield) {
            exprValueStack.push(builder->CreateLoad(slot.storageType, gep, node->member));
            return;
        }
        llvm::Type* sty = slot.storageType;
        llvm::Value* word = builder->CreateLoad(sty, gep);
        llvm::Value* shifted = slot.bitOffset
            ? builder->CreateLShr(word, llvm::ConstantInt::get(sty, slot.bitOffset)) : word;
        uint64_t mask = (slot.bitWidth >= 64) ? ~0ULL : ((1ULL << slot.bitWidth) - 1);
        llvm::Value* masked = builder->CreateAnd(shifted, llvm::ConstantInt::get(sty, mask));
        if (slot.isSigned && slot.bitWidth < sty->getIntegerBitWidth()) {
            unsigned sh = sty->getIntegerBitWidth() - slot.bitWidth;
            masked = builder->CreateAShr(builder->CreateShl(masked, sh), sh);
        }
        exprValueStack.push(masked);
        return;
    }

    auto fit = structFields.find(baseType);
    if (fit == structFields.end())
        throw std::runtime_error("Unknown struct type in member access: '" + baseType + "'");

    const auto& fields = fit->second;
    bool isUnion = unionFields.count(baseType) > 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name == node->member) {
            llvm::Value* basePtr = baseAddr();
            llvm::Type*  fieldTy = getTypeFromString(fields[i].type);
            llvm::Value* ptr;
            if (isUnion) {
                // Union: all fields at offset 0 — base ptr is the field ptr
                ptr = basePtr;
            } else {
                ptr = builder->CreateStructGEP(structTypes[baseType], basePtr, i, node->member);
            }
            exprValueStack.push(builder->CreateLoad(fieldTy, ptr, node->member));
            return;
        }
    }
    throw std::runtime_error("Struct/union '" + baseType + "' has no field '" + node->member + "'");
}

void CodeGen::storeBitfieldInto(llvm::Value* wordPtr, const BitfieldSlot& slot,
                                llvm::Value* val) {
    llvm::Type* sty = slot.storageType;  // integer storage word
    if (val->getType() != sty) {
        if (val->getType()->isIntegerTy())
            val = val->getType()->getIntegerBitWidth() > sty->getIntegerBitWidth()
                ? builder->CreateTrunc(val, sty) : builder->CreateZExt(val, sty);
        else if (val->getType()->isFloatingPointTy())
            val = builder->CreateFPToSI(val, sty);
    }
    llvm::Value* word = builder->CreateLoad(sty, wordPtr);
    uint64_t mask = (slot.bitWidth >= 64) ? ~0ULL : ((1ULL << slot.bitWidth) - 1);
    llvm::Value* fieldMask = llvm::ConstantInt::get(sty, mask << slot.bitOffset);
    llvm::Value* cleared  = builder->CreateAnd(word, builder->CreateNot(fieldMask));
    llvm::Value* vMasked  = builder->CreateAnd(val, llvm::ConstantInt::get(sty, mask));
    llvm::Value* vShifted = slot.bitOffset
        ? builder->CreateShl(vMasked, llvm::ConstantInt::get(sty, slot.bitOffset)) : vMasked;
    builder->CreateStore(builder->CreateOr(cleared, vShifted), wordPtr);
}

void CodeGen::storeBitfield(MemberExpr* m, llvm::Value* val) {
    std::string baseType = structBaseTypeOf(m->base);
    const BitfieldSlot& slot = structLayout[baseType][m->member];
    llvm::Value* basePtr = evaluateLValue(m->base);
    llvm::Value* gep = builder->CreateStructGEP(structTypes[baseType], basePtr, slot.physIndex);
    storeBitfieldInto(gep, slot, val);
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
            // Widen per the SOURCE's signedness: an unsigned source (uint*/char/bool)
            // zero-extends — e.g. (int)(uint8)255 is 255, not -1.
            std::string st = expandAlias(getExprEskiuType(node->expr));
            bool uns = st == "uint" || st == "uint8" || st == "uint16" || st == "uint32" ||
                       st == "uint64" || st == "char" || st == "bool";
            result = uns ? builder->CreateZExt(val, targetType)
                         : builder->CreateSExt(val, targetType);
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
            // base 0 = auto (dec/hex/oct). Materialize as i64 when the value
            // doesn't fit in 32 bits, so large literals aren't truncated.
            unsigned long long uval;
            bool wide;
            try {
                long long sval = std::stoll(node->value, nullptr, 0);
                uval = (unsigned long long)sval;
                wide = (sval < -2147483648LL || sval > 4294967295LL);
            } catch (const std::out_of_range&) {
                uval = std::stoull(node->value, nullptr, 0); // e.g. large uint64 literal
                wide = true;
            }
            llvm::Type* ity = wide ? llvm::Type::getInt64Ty(*context)
                                   : llvm::Type::getInt32Ty(*context);
            result = llvm::ConstantInt::get(ity, uval, false);
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
    // Bare enum member, e.g. `Red` — an i32 constant.
    if (!lookupSymbol(node->name)) {
        auto ec = enumConstants.find(node->name);
        if (ec != enumConstants.end()) {
            exprValueStack.push(llvm::ConstantInt::get(
                llvm::Type::getInt32Ty(*context), ec->second, /*isSigned=*/true));
            return;
        }
        // Bare algebraic variant with no payload, e.g. `None`.
        if (adtVariants.count(node->name)) {
            exprValueStack.push(buildVariant(node->name, {}));
            return;
        }
    }

    // Look up variable
    llvm::Value* val = lookupSymbol(node->name);

    if (!val) {
        // A bare function name used as a value decays to a closure fat pointer.
        if (auto* fn = module->getFunction(node->name)) {
            exprValueStack.push(makeFunctionPointer(fn));
            return;
        }
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

llvm::Value* CodeGen::evaluateExpr(const ExprPtr& expr) {
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

void CodeGen::visit(AllocWithExpr* node) {
    // Lower to: (*T) <AllocType>_alloc(allocator, count * sizeof(T))
    // The allocator is a struct providing `*void alloc(... size)`.
    llvm::Value* allocPtr = evaluateExpr(node->allocator);

    std::string at = getExprEskiuType(node->allocator);
    while (!at.empty() && at.front() == '*') at = at.substr(1);
    while (!at.empty() && at.back()  == '*') at.pop_back();
    if (at.rfind("struct:", 0) == 0) at = at.substr(7);

    std::string fnName = at + "_alloc";
    llvm::Function* af = module->getFunction(fnName);
    if (!af)
        throw std::runtime_error("alloc_with: allocator type '" + at +
                                 "' has no alloc method (" + fnName + ")");

    llvm::Type* elemTy = getTypeFromString(node->elemType);
    uint64_t esz = module->getDataLayout().getTypeAllocSize(elemTy);
    llvm::Value* n64 = builder->CreateIntCast(evaluateExpr(node->count),
                            llvm::Type::getInt64Ty(*context), false);
    llvm::Value* total = builder->CreateMul(
        n64, llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), esz), "allocw.size");

    // Coerce the size to the alloc method's second parameter type.
    llvm::FunctionType* fty = af->getFunctionType();
    if (fty->getNumParams() >= 2 && fty->getParamType(1) != total->getType())
        total = builder->CreateIntCast(total, fty->getParamType(1), false);

    // Returns *void; the cast to *T is a no-op under opaque pointers.
    exprValueStack.push(builder->CreateCall(af, {allocPtr, total}, "allocw.ptr"));
}

llvm::Value* CodeGen::makeFunctionPointer(llvm::Function* target) {
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    std::string wname = "__fnptr_" + target->getName().str();
    llvm::Function* wrapper = module->getFunction(wname);
    if (!wrapper) {
        // Thunk: (env*, params...) -> ret  that ignores env and calls target.
        llvm::FunctionType* tfty = target->getFunctionType();
        std::vector<llvm::Type*> wparams;
        wparams.push_back(ptrTy);  // env (unused)
        for (llvm::Type* pt : tfty->params()) wparams.push_back(pt);
        llvm::FunctionType* wfty = llvm::FunctionType::get(
            tfty->getReturnType(), wparams, tfty->isVarArg());
        wrapper = llvm::Function::Create(wfty, llvm::Function::InternalLinkage,
                                         wname, module.get());

        llvm::BasicBlock* prev = builder->GetInsertBlock();
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", wrapper);
        builder->SetInsertPoint(entry);
        std::vector<llvm::Value*> callArgs;
        auto ai = wrapper->arg_begin(); ++ai;  // skip env
        for (; ai != wrapper->arg_end(); ++ai) callArgs.push_back(&*ai);
        llvm::Value* r = builder->CreateCall(target, callArgs);
        if (tfty->getReturnType()->isVoidTy()) builder->CreateRetVoid();
        else builder->CreateRet(r);
        if (prev) builder->SetInsertPoint(prev);
    }

    // Build fat pointer {wrapper, null} — same shape lambdas produce.
    llvm::StructType* fatTy = llvm::cast<llvm::StructType>(getTypeFromString("fn()->void"));
    llvm::Value* fatAlloca = builder->CreateAlloca(fatTy, nullptr, "fnptr.fat");
    builder->CreateStore(wrapper, builder->CreateStructGEP(fatTy, fatAlloca, 0));
    builder->CreateStore(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
                         builder->CreateStructGEP(fatTy, fatAlloca, 1));
    return builder->CreateLoad(fatTy, fatAlloca, "fnptr.fat.val");
}

void CodeGen::visit(LambdaExpr* node) {
    static int lambdaSeq = 0;
    std::string lambdaName = "__lambda" + std::to_string(lambdaSeq++);
    bool hasCaptures = !node->captures.empty();

    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);

    // ── Build env struct type (one field per captured variable) ──────────
    llvm::StructType* envTy = nullptr;
    llvm::Value*      envAlloca = nullptr;
    if (hasCaptures) {
        std::vector<llvm::Type*> envFields;
        for (const auto& [name, type] : node->captures)
            envFields.push_back(getTypeFromString(type));
        envTy = llvm::StructType::create(*context, envFields,
                                         lambdaName + ".env");
        if (node->escapes) {
            // Escaping closure (returned, stored, or passed to an `escaping`
            // parameter): heap-allocate the env so it outlives this function.
            // Freed via free_closure (the async transform emits it at the owner
            // boundary; otherwise the owner frees it explicitly).
            uint64_t envSize = module->getDataLayout().getTypeAllocSize(envTy);
            llvm::Function* mallocFn = getOrDeclareFunc(
                "malloc", ptrTy, {llvm::Type::getInt64Ty(*context)}, false);
            envAlloca = builder->CreateCall(mallocFn,
                {llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), envSize)},
                lambdaName + ".env.heap");
        } else {
            // Non-escaping closure: the env dies with this frame — stack-allocate
            // it. Zero cost, no leak. (The common map/filter/apply case.)
            envAlloca = builder->CreateAlloca(envTy, nullptr, lambdaName + ".env");
        }
        for (size_t ci = 0; ci < node->captures.size(); ++ci) {
            llvm::Value* capturedVal = nullptr;
            llvm::Value* sym = lookupSymbol(node->captures[ci].first);
            if (sym) {
                // Load the current value from the outer alloca/variable
                if (llvm::isa<llvm::AllocaInst>(sym)) {
                    auto* alloca = llvm::cast<llvm::AllocaInst>(sym);
                    capturedVal = builder->CreateLoad(
                        alloca->getAllocatedType(), sym, node->captures[ci].first);
                } else {
                    capturedVal = sym;
                }
            }
            if (capturedVal) {
                auto* gep = builder->CreateStructGEP(envTy, envAlloca, ci);
                builder->CreateStore(capturedVal, gep);
            }
        }
    }

    // ── Build lambda function: env* always first param ───────────────────
    std::vector<llvm::Type*> paramTypes = {ptrTy}; // env* (null if no captures)
    for (const auto& p : node->params)
        paramTypes.push_back(getTypeFromString(p.first));

    llvm::Type* retTy = getTypeFromString(node->returnType);
    llvm::FunctionType* fty = llvm::FunctionType::get(retTy, paramTypes, false);
    llvm::Function* func = llvm::Function::Create(
        fty, llvm::Function::InternalLinkage, lambdaName, module.get());

    auto argIt = func->arg_begin();
    argIt->setName("env");
    llvm::Argument* envArg = &*argIt++;
    size_t i = 0;
    for (; argIt != func->arg_end(); ++argIt, ++i)
        argIt->setName(node->params[i].second);

    // ── Compile lambda body ───────────────────────────────────────────────
    llvm::Function* prevFunc     = currentFunction;
    llvm::Value*    prevSret     = currentSretParam;
    llvm::BasicBlock* prevInsert = builder->GetInsertBlock();

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", func);
    builder->SetInsertPoint(entry);
    currentFunction  = func;
    currentSretParam = nullptr;
    pushScope();

    // Expose captured variables by loading from env
    if (hasCaptures) {
        for (size_t ci = 0; ci < node->captures.size(); ++ci) {
            const auto& [capName, capType] = node->captures[ci];
            llvm::Type* capLLVMTy = getTypeFromString(capType);
            auto* capAlloca = builder->CreateAlloca(capLLVMTy, nullptr, capName);
            auto* gep = builder->CreateStructGEP(envTy, envArg, ci, capName + ".gep");
            auto* val = builder->CreateLoad(capLLVMTy, gep, capName + ".val");
            builder->CreateStore(val, capAlloca);
            defineSymbol(capName, capAlloca);
            defineVarType(capName, capType);
        }
    }

    // Define parameters
    i = 0;
    argIt = func->arg_begin();
    ++argIt; // skip env
    for (; argIt != func->arg_end(); ++argIt, ++i) {
        llvm::Value* slot = &*argIt;
        if (argIt->getType()->isStructTy()) {
            auto* a = builder->CreateAlloca(argIt->getType(), nullptr,
                                            node->params[i].second + ".byval");
            builder->CreateStore(&*argIt, a);
            slot = a;
        }
        defineSymbol(node->params[i].second, slot);
        defineVarType(node->params[i].second, node->params[i].first);
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

    // ── Build fat pointer {fn_ptr, env_ptr} ──────────────────────────────
    llvm::StructType* fatTy = llvm::cast<llvm::StructType>(
        getTypeFromString("fn()->void")); // any fn type gives {ptr,ptr}
    llvm::Value* fatAlloca = builder->CreateAlloca(fatTy, nullptr, lambdaName + ".fat");
    auto* fnSlot  = builder->CreateStructGEP(fatTy, fatAlloca, 0);
    auto* envSlot = builder->CreateStructGEP(fatTy, fatAlloca, 1);
    builder->CreateStore(func, fnSlot);
    builder->CreateStore(
        hasCaptures ? (llvm::Value*)envAlloca
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
        envSlot);
    exprValueStack.push(builder->CreateLoad(fatTy, fatAlloca, lambdaName + ".fat.val"));
}

// ── Exception helpers (invoke/landingpad) ─────────────────────────────────

// Ensure personality function and _ZTIPv type_info are declared in the module.
static void ensureEHDecls(llvm::Module* mod, llvm::LLVMContext& ctx) {
    if (mod->getFunction("__gxx_personality_v0")) return;
    llvm::Type* i32  = llvm::Type::getInt32Ty(ctx);
    llvm::Type* ptrTy = llvm::PointerType::get(ctx, 0);
    // Personality function
    llvm::FunctionType* persType = llvm::FunctionType::get(i32, true);
    llvm::Function::Create(persType, llvm::Function::ExternalLinkage,
        "__gxx_personality_v0", mod);
    // _ZTIPv — void* type_info (from libc++)
    if (!mod->getNamedGlobal("_ZTIPv"))
        new llvm::GlobalVariable(*mod, ptrTy, true,
            llvm::GlobalValue::ExternalLinkage, nullptr, "_ZTIPv");
}

llvm::Value* CodeGen::createMaybeInvoke(
    llvm::FunctionType* fty, llvm::Value* callee,
    llvm::ArrayRef<llvm::Value*> args, const llvm::Twine& name) {

    if (!unwindTarget)
        return builder->CreateCall(fty, callee, args, name);

    // Create a "normal" continuation block
    llvm::Function* fn = builder->GetInsertBlock()->getParent();
    auto* contBB = llvm::BasicBlock::Create(*context, "invoke.cont", fn);
    auto* inv = builder->CreateInvoke(fty, callee, contBB, unwindTarget, args, name);
    builder->SetInsertPoint(contBB);
    return inv;
}

void CodeGen::visit(ThrowStmt* node) {
    ensureEHDecls(module.get(), *context);
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Type* i64   = llvm::Type::getInt64Ty(*context);

    // EskiuEx: { i64 value, ptr type_name } — 16 bytes
    llvm::Function* allocEx = getOrDeclareFunc("__cxa_allocate_exception",
        ptrTy, {i64});
    llvm::Value* exPtr = builder->CreateCall(allocEx,
        {llvm::ConstantInt::get(i64, 16)}, "ex.alloc");

    // Store value as i64
    llvm::Value* val = evaluateExpr(node->value);
    llvm::Value* ival;
    if (val->getType()->isPointerTy())
        ival = builder->CreatePtrToInt(val, i64);
    else
        ival = builder->CreateSExtOrTrunc(val, i64);
    builder->CreateStore(ival, exPtr);

    // Store type name at offset 8
    std::string thrownType = node->valueType.empty() ? "unknown" : node->valueType;
    auto* typeStr = builder->CreateGlobalString(thrownType, ".ex.tname");
    auto* typeSlot = builder->CreateConstGEP1_64(
        llvm::Type::getInt8Ty(*context), exPtr, 8, "ex.type.slot");
    auto* typeSlotPtr = builder->CreateBitCast(typeSlot, ptrTy, "ex.type.ptr");
    builder->CreateStore(typeStr, typeSlotPtr);

    // __cxa_throw(ex, _ZTIPv, null)
    // Must be an invoke when inside a try body so the local landingpad fires.
    llvm::Function* cxaThrow = getOrDeclareFunc("__cxa_throw",
        llvm::Type::getVoidTy(*context), {ptrTy, ptrTy, ptrTy});
    // Do NOT mark noreturn — it prevents invoke from propagating the exception
    llvm::Value* typeInfo = module->getNamedGlobal("_ZTIPv");
    llvm::Value* nullPtr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrTy));
    std::vector<llvm::Value*> throwArgs = {exPtr, typeInfo, nullPtr};

    if (unwindTarget) {
        // Inside a try — use invoke so the landingpad catches it
        llvm::Function* fn = builder->GetInsertBlock()->getParent();
        auto* unreachBB = llvm::BasicBlock::Create(*context, "throw.unreach", fn);
        builder->CreateInvoke(cxaThrow->getFunctionType(), cxaThrow,
            unreachBB, unwindTarget, throwArgs);
        builder->SetInsertPoint(unreachBB);
    } else {
        builder->CreateCall(cxaThrow, throwArgs);
    }
    builder->CreateUnreachable();
}

void CodeGen::visit(TryStmt* node) {
    ensureEHDecls(module.get(), *context);
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Type* i64   = llvm::Type::getInt64Ty(*context);
    llvm::Type* i32   = llvm::Type::getInt32Ty(*context);

    llvm::Function* fn = builder->GetInsertBlock()->getParent();

    // Set personality on the enclosing function if not already set
    if (!fn->hasPersonalityFn()) {
        auto* pers = module->getFunction("__gxx_personality_v0");
        fn->setPersonalityFn(pers);
    }

    llvm::BasicBlock* lpadBB    = llvm::BasicBlock::Create(*context, "try.lpad",    fn);
    llvm::BasicBlock* finallyBB = llvm::BasicBlock::Create(*context, "try.finally", fn);
    llvm::BasicBlock* doneBB    = llvm::BasicBlock::Create(*context, "try.done",    fn);

    // ── try body — all calls become invokes ───────────────────────────────
    llvm::BasicBlock* savedUnwind = unwindTarget;
    unwindTarget = lpadBB;
    if (node->body) node->body->accept(this);
    unwindTarget = savedUnwind;
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(finallyBB);

    // ── landingpad ────────────────────────────────────────────────────────
    builder->SetInsertPoint(lpadBB);
    auto* lp = builder->CreateLandingPad(
        llvm::StructType::get(*context, {ptrTy, i32}), 1, "lpad");
    // catch i8* null = catch-all
    lp->addClause(llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptrTy)));

    llvm::Value* exObjPtr = builder->CreateExtractValue(lp, {0}, "ex.ptr");

    // __cxa_begin_catch(ex) → pointer to our EskiuEx
    llvm::Function* beginCatch = getOrDeclareFunc("__cxa_begin_catch",
        ptrTy, {ptrTy});
    llvm::Value* exData = builder->CreateCall(beginCatch, {exObjPtr}, "ex.data");

    // Read the type name (offset 8)
    auto* typeSlot = builder->CreateConstGEP1_64(
        llvm::Type::getInt8Ty(*context), exData, 8, "ex.tslot");
    llvm::Value* exType = builder->CreateLoad(ptrTy,
        builder->CreateBitCast(typeSlot, ptrTy), "ex.type");

    // ── catch clauses ─────────────────────────────────────────────────────
    llvm::Function* endCatch  = getOrDeclareFunc("__cxa_end_catch",
        llvm::Type::getVoidTy(*context), {});
    llvm::Function* strcmpFn  = getOrDeclareFunc("strcmp", i32, {ptrTy, ptrTy});

    for (auto& c : node->catches) {
        auto* cTypeStr  = builder->CreateGlobalString(c.type, ".catch.t");
        llvm::Value* cmp   = builder->CreateCall(strcmpFn, {exType, cTypeStr}, "tcmp");
        llvm::Value* match = builder->CreateICmpEQ(cmp,
            llvm::ConstantInt::get(i32, 0), "tmatch");

        auto* handlerBB = llvm::BasicBlock::Create(*context, "catch." + c.type, fn);
        auto* nextBB    = llvm::BasicBlock::Create(*context, "catch.next",       fn);
        builder->CreateCondBr(match, handlerBB, nextBB);

        builder->SetInsertPoint(handlerBB);
        pushScope();

        // Load value (offset 0)
        llvm::Value* ival = builder->CreateLoad(i64, exData, "ex.ival");
        llvm::Type*  catchTy = getTypeFromString(c.type);
        llvm::Value* catchVal = catchTy->isPointerTy()
            ? builder->CreateIntToPtr(ival, catchTy)
            : builder->CreateTrunc(ival, catchTy);
        auto* catchAlloca = builder->CreateAlloca(catchTy, nullptr, c.name);
        builder->CreateStore(catchVal, catchAlloca);
        defineSymbol(c.name, catchAlloca);
        defineVarType(c.name, c.type);

        if (c.body) c.body->accept(this);
        popScope();

        if (!builder->GetInsertBlock()->getTerminator()) {
            builder->CreateCall(endCatch, {});
            builder->CreateBr(finallyBB);
        }

        builder->SetInsertPoint(nextBB);
    }

    // Unhandled: end catch + resume (rethrow)
    if (!builder->GetInsertBlock()->getTerminator()) {
        builder->CreateCall(endCatch, {});
        builder->CreateResume(lp);
    }

    // ── finally ───────────────────────────────────────────────────────────
    builder->SetInsertPoint(finallyBB);
    if (node->finally) node->finally->accept(this);
    if (!builder->GetInsertBlock()->getTerminator())
        builder->CreateBr(doneBB);

    builder->SetInsertPoint(doneBB);
}

// Create the tagged-union LLVM type for an ADT enum given each variant's payload
// field types: { i32 tag; [N x i64] payload }, N sized to the largest variant. The
// i64 array forces 8-byte alignment (enough for scalars/pointers/doubles).
static llvm::StructType* makeAdtStruct(llvm::LLVMContext& ctx, const llvm::DataLayout& DL,
                                       const std::string& name,
                                       const std::vector<std::vector<llvm::Type*>>& variantFields) {
    uint64_t maxBytes = 0;
    for (const auto& fields : variantFields) {
        uint64_t bytes = 0;
        for (llvm::Type* t : fields) {
            uint64_t al = DL.getABITypeAlign(t).value();
            bytes = ((bytes + al - 1) / al) * al + DL.getTypeAllocSize(t);
        }
        if (bytes > maxBytes) maxBytes = bytes;
    }
    uint64_t n = (maxBytes + 7) / 8; if (n == 0) n = 1;
    std::vector<llvm::Type*> f = {
        llvm::Type::getInt32Ty(ctx),
        llvm::ArrayType::get(llvm::Type::getInt64Ty(ctx), n)
    };
    return llvm::StructType::create(ctx, f, name);
}

void CodeGen::visit(EnumDecl* node) {
    enumTypes.insert(node->name);
    if (!node->isADT()) {
        for (const auto& m : node->members) enumConstants[m.first] = m.second;
        return;
    }
    if (!node->typeParams.empty()) {
        // Generic ADT enum: a template; instances are built on demand (ensureEnumInst).
        genericEnumDecls[node->name] = node;
        for (size_t v = 0; v < node->members.size(); ++v)
            genericVariants[node->members[v].first] = {node->name, (int)v};
        return;
    }
    adtEnums.insert(node->name);
    adtEnumDecls[node->name] = node;
    std::vector<std::vector<llvm::Type*>> vf;
    for (size_t v = 0; v < node->members.size(); ++v) {
        adtVariants[node->members[v].first] = {node->name, (int)v};
        std::vector<llvm::Type*> fields;
        for (const auto& ft : node->payloads[v]) fields.push_back(getTypeFromString(ft));
        vf.push_back(fields);
    }
    structTypes[node->name] = makeAdtStruct(*context, module->getDataLayout(), node->name, vf);
}

// Monomorphize a generic enum for `typeArgs` (build its struct + record the args).
std::string CodeGen::ensureEnumInst(const std::string& genericName,
                                    const std::vector<std::string>& typeArgs) {
    std::string inst = genericName + "<";
    for (size_t i = 0; i < typeArgs.size(); ++i) { if (i) inst += ","; inst += typeArgs[i]; }
    inst += ">";
    std::string mangled = mangleTemplate(inst);
    if (structTypes.count(mangled)) return mangled;
    EnumDecl* ge = genericEnumDecls[genericName];
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < ge->typeParams.size() && i < typeArgs.size(); ++i)
        subs[ge->typeParams[i]] = typeArgs[i];
    std::vector<std::vector<llvm::Type*>> vf;
    for (size_t v = 0; v < ge->members.size(); ++v) {
        std::vector<llvm::Type*> fields;
        for (const auto& ft : ge->payloads[v]) fields.push_back(getTypeFromString(substType(ft, subs)));
        vf.push_back(fields);
    }
    structTypes[mangled] = makeAdtStruct(*context, module->getDataLayout(), mangled, vf);
    enumInstanceArgs[mangled] = {genericName, typeArgs};
    return mangled;
}

// Core builder: alloca the enum struct, store the tag, write payload fields (viewed
// as the variant's struct, coerced to the field types), then load the value.
llvm::Value* CodeGen::buildEnumValue(llvm::StructType* et, int tag,
        const std::vector<llvm::Type*>& fieldTypes, const std::vector<ExprPtr>& args) {
    llvm::Value* tmp = builder->CreateAlloca(et, nullptr, "variant.tmp");
    builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*context), tag),
                         builder->CreateStructGEP(et, tmp, 0));
    if (!fieldTypes.empty()) {
        llvm::StructType* vt = llvm::StructType::get(*context, fieldTypes);
        llvm::Value* pay = builder->CreateStructGEP(et, tmp, 1);   // the [N x i64] area
        for (size_t i = 0; i < args.size() && i < fieldTypes.size(); ++i) {
            llvm::Value* fp = builder->CreateStructGEP(vt, pay, i);
            llvm::Value* val = evaluateExpr(args[i]);
            llvm::Type* ft = fieldTypes[i];
            if (val && val->getType() != ft) {       // coerce arg to the field type
                if (val->getType()->isIntegerTy() && ft->isIntegerTy())
                    val = val->getType()->getIntegerBitWidth() > ft->getIntegerBitWidth()
                          ? builder->CreateTrunc(val, ft) : builder->CreateSExt(val, ft);
                else if (val->getType()->isIntegerTy() && ft->isFloatingPointTy())
                    val = builder->CreateSIToFP(val, ft);
                else if (val->getType()->isFloatingPointTy() && ft->isIntegerTy())
                    val = builder->CreateFPToSI(val, ft);
                else if (val->getType()->isFloatingPointTy() && ft->isFloatingPointTy())
                    val = builder->CreateFPCast(val, ft);
            }
            builder->CreateStore(val, fp);
        }
    }
    return builder->CreateLoad(et, tmp, "variant.val");
}

llvm::Value* CodeGen::buildVariant(const std::string& variant, const std::vector<ExprPtr>& args) {
    auto& info = adtVariants[variant];
    EnumDecl* ed = adtEnumDecls[info.first];
    std::vector<llvm::Type*> fts;
    for (const auto& ft : ed->payloads[info.second]) fts.push_back(getTypeFromString(ft));
    return buildEnumValue(structTypes[info.first], info.second, fts, args);
}

void CodeGen::visit(TypeAliasDecl* node) {
    typeAliases[node->name] = node->aliased;
}

void CodeGen::visit(UnionDecl* node) {
    // Compute size = max(sizeof(field)) across all fields
    uint64_t maxSize = 0;
    for (const auto& f : node->fields) {
        llvm::Type* ft = getTypeFromString(f.type);
        uint64_t sz = module->getDataLayout().getTypeAllocSize(ft);
        if (sz > maxSize) maxSize = sz;
    }
    if (maxSize == 0) maxSize = 1;

    // Store as opaque byte array — same as struct in LLVM type registry
    llvm::Type* unionTy = llvm::ArrayType::get(
        llvm::Type::getInt8Ty(*context), maxSize);
    std::string mangledName = node->name;
    structTypes[mangledName] = llvm::cast<llvm::StructType>(
        llvm::StructType::get(*context, {unionTy}, /*isPacked=*/false));
    // Actually use a named struct wrapping the byte array for cleaner IR
    auto* namedTy = llvm::StructType::create(*context, {unionTy}, mangledName + ".union");
    structTypes[mangledName] = namedTy;

    // Register fields so MemberExpr can resolve them (all at offset 0, typed via cast)
    unionFields[mangledName] = node->fields;
    // Also register in structFields for MemberExpr type lookup
    std::vector<StructDecl::Field> sf;
    for (const auto& f : node->fields) sf.push_back({f.type, f.name});
    structFields[mangledName] = sf;
}

void CodeGen::visit(SizeofExpr* node) {
    llvm::Type* ty   = getTypeFromString(node->typeName);
    uint64_t    size = module->getDataLayout().getTypeAllocSize(ty);
    exprValueStack.push(
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(*context), size));
}

void CodeGen::visit(AwaitExpr* node) {
    // The async transform rewrites `async fn`/`await` into a state machine before
    // codegen; reaching here means the transform has not run on this code.
    (void)node;
    throw std::runtime_error("async/await codegen requires the state-machine "
                             "transform (not yet implemented)");
}

void CodeGen::visit(FreeClosureExpr* node) {
    // A closure is a fat pointer {fn_ptr, env_ptr}. Free its heap env (slot 1).
    // A non-capturing closure has a null env; free(null) is a safe no-op.
    llvm::Value* fat = evaluateExpr(node->closure);
    llvm::Value* env = builder->CreateExtractValue(fat, 1, "clos.env");
    llvm::Function* freeFn = getOrDeclareFunc(
        "free", llvm::Type::getVoidTy(*context),
        {llvm::PointerType::get(*context, 0)}, false);
    builder->CreateCall(freeFn, {env});
    exprValueStack.push(llvm::UndefValue::get(llvm::Type::getVoidTy(*context)));
}

void CodeGen::visit(ThreadCreateExpr* node) {
    // Evaluate the closure — a fat pointer {fn_ptr, env_ptr}
    llvm::Value* fatPtr = evaluateExpr(node->worker);

    // Extract fn_ptr and env_ptr
    llvm::Value* fnPtr  = builder->CreateExtractValue(fatPtr, {0}, "thr.fn");
    llvm::Value* envPtr = builder->CreateExtractValue(fatPtr, {1}, "thr.env");

    // pthread_t is typically *void; alloca space for the tid
    llvm::Type* ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Value* tidAlloca = builder->CreateAlloca(ptrTy, nullptr, "thr.tid");

    // pthread_create(pthread_t* tid, null, fn_ptr, env_ptr)
    llvm::Function* pthreadCreate = getOrDeclareFunc("pthread_create",
        llvm::Type::getInt32Ty(*context),
        {ptrTy, ptrTy, ptrTy, ptrTy});

    builder->CreateCall(pthreadCreate, {
        tidAlloca,
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy)),
        fnPtr,
        envPtr
    });

    // Return the thread handle (tid value)
    exprValueStack.push(builder->CreateLoad(ptrTy, tidAlloca, "thr.handle"));
}

void CodeGen::visit(ThreadJoinStmt* node) {
    llvm::Value* tid = evaluateExpr(node->tid);
    llvm::Type*  ptrTy = llvm::PointerType::get(*context, 0);
    llvm::Function* pthreadJoin = getOrDeclareFunc("pthread_join",
        llvm::Type::getInt32Ty(*context), {ptrTy, ptrTy});
    builder->CreateCall(pthreadJoin, {
        tid,
        llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptrTy))
    });
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

std::string CodeGen::resolveStructInitName(const std::string& name) {
    if (name.find('<') == std::string::npos) return name;
    // Resolve type args through the enclosing template's substitutions, so a
    // `Pair<A,B>{...}` literal inside a template body instantiates Pair<int,int>,
    // not a bogus Pair_A_B.
    std::string resolved = typeParamOverride.empty() ? name : substType(name, typeParamOverride);
    auto [tn, args] = splitTemplateType(resolved);
    std::string mangled = mangleTemplate(resolved);
    ensureTemplateInstantiated(mangled, tn, args);
    return mangled;
}

void CodeGen::emitStructInitInto(llvm::Value* dest, StructInitExpr* init) {
    std::string sname = resolveStructInitName(init->structName);
    auto fit = structFields.find(sname);
    if (fit == structFields.end()) return;
    const auto& fields = fit->second;
    llvm::StructType* st = structTypes[sname];

    bool named = !init->fieldInits.empty() && !init->fieldInits[0].first.empty();

    auto coerce = [&](llvm::Value* val, llvm::Type* fieldType) -> llvm::Value* {
        if (val && val->getType() != fieldType) {
            if (val->getType()->isIntegerTy() && fieldType->isIntegerTy()) {
                unsigned s = val->getType()->getIntegerBitWidth();
                unsigned d = fieldType->getIntegerBitWidth();
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
        return val;
    };

    auto storeField = [&](size_t idx, ExprPtr expr) {
        llvm::Value* val = evaluateExpr(expr);
        // Bitfield-layout struct: store via the physical slot.
        auto lit = structLayout.find(sname);
        if (lit != structLayout.end()) {
            const BitfieldSlot& slot = lit->second.at(fields[idx].name);
            llvm::Value* gep = builder->CreateStructGEP(st, dest, slot.physIndex);
            if (slot.isBitfield) { storeBitfieldInto(gep, slot, val); return; }
            if (val) builder->CreateStore(coerce(val, slot.storageType), gep);
            return;
        }
        llvm::Type* fieldType = getTypeFromString(fields[idx].type);
        val = coerce(val, fieldType);
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
    std::string sname = resolveStructInitName(node->structName);
    auto fit = structFields.find(sname);
    if (fit == structFields.end())
        throw std::runtime_error("Unknown struct: " + node->structName);
    llvm::StructType* st = structTypes[sname];
    // Temporary alloca — filled then loaded so caller can store it anywhere
    llvm::Value* tmp = builder->CreateAlloca(st, nullptr, sname + ".init");
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

void CodeGen::visit(MatchStmt* node) {
    llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
    // The type checker stamps node->enumName, except inside template-function
    // bodies (which it skips) — there we derive it from the subject's type, with
    // the active typeParamOverride applied, and ensure the instance is built.
    std::string enumName = node->enumName;
    if (enumName.empty() || !structTypes.count(enumName)) {
        std::string st = getExprEskiuType(node->subject);
        // getExprEskiuType's deref only strips a leading '*'; for a trailing-star
        // pointer (`Option<int>*`) it returns "" — fall back to the operand's type.
        if (st.empty())
            if (auto* u = dynamic_cast<UnaryExpr*>(node->subject.get()); u && u->op == "*")
                st = getExprEskiuType(u->operand);
        if (!typeParamOverride.empty()) st = substType(st, typeParamOverride);  // T -> int
        while (!st.empty() && st.front() == '*') st = st.substr(1);
        while (!st.empty() && st.back()  == '*') st.pop_back();
        if (st.rfind("struct:", 0) == 0) st = st.substr(7);
        if (st.find('<') != std::string::npos) {
            auto [b, a] = splitTemplateType(st);
            enumName = genericEnumDecls.count(b) ? ensureEnumInst(b, a) : mangleTemplate(st);
        } else if (!st.empty()) {
            enumName = st;
        }
    }
    // Resolve the enum decl + (for a generic instance) the type-arg substitutions,
    // so each variant's payload field types come out concrete.
    EnumDecl* ed = nullptr;
    std::map<std::string, std::string> subs;
    if (adtEnumDecls.count(enumName)) {
        ed = adtEnumDecls[enumName];                         // concrete ADT enum
    } else if (enumInstanceArgs.count(enumName)) {           // generic instance
        auto& inst = enumInstanceArgs[enumName];
        ed = genericEnumDecls[inst.first];
        for (size_t i = 0; i < ed->typeParams.size() && i < inst.second.size(); ++i)
            subs[ed->typeParams[i]] = inst.second[i];
    }
    auto variantIndex = [&](const std::string& v) -> int {
        for (size_t i = 0; i < ed->members.size(); ++i)
            if (ed->members[i].first == v) return (int)i;
        return -1;
    };
    // Concrete payload LLVM field types of a variant (after substitution).
    auto payloadTypes = [&](int idx) {
        std::vector<llvm::Type*> v;
        for (const auto& ft : ed->payloads[idx]) v.push_back(getTypeFromString(substType(ft, subs)));
        return v;
    };

    llvm::StructType* et = structTypes[enumName];

    // Materialize the subject so tag + payload can be read by pointer.
    llvm::Value* sv = evaluateExpr(node->subject);
    llvm::Value* sptr = builder->CreateAlloca(et, nullptr, "match.subj");
    builder->CreateStore(sv, sptr);
    llvm::Value* tag = builder->CreateLoad(i32, builder->CreateStructGEP(et, sptr, 0), "match.tag");

    llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(*context, "match.end", currentFunction);
    std::vector<llvm::BasicBlock*> armBlocks(node->arms.size());
    llvm::BasicBlock* defaultBlock = endBlock;
    for (size_t i = 0; i < node->arms.size(); ++i) {
        armBlocks[i] = llvm::BasicBlock::Create(*context, "match.arm", currentFunction);
        if (node->arms[i].variant.empty()) defaultBlock = armBlocks[i];
    }
    llvm::SwitchInst* sw = builder->CreateSwitch(tag, defaultBlock);
    for (size_t i = 0; i < node->arms.size(); ++i)
        if (!node->arms[i].variant.empty())
            sw->addCase(llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(i32, variantIndex(node->arms[i].variant))), armBlocks[i]);

    for (size_t i = 0; i < node->arms.size(); ++i) {
        auto& arm = node->arms[i];
        builder->SetInsertPoint(armBlocks[i]);
        pushScope();
        if (!arm.variant.empty() && !arm.bindings.empty()) {
            int vi = variantIndex(arm.variant);
            std::vector<llvm::Type*> vfields = payloadTypes(vi);
            llvm::StructType* vt = llvm::StructType::get(*context, vfields);
            llvm::Value* pay = builder->CreateStructGEP(et, sptr, 1);
            for (size_t b = 0; b < arm.bindings.size() && b < vfields.size(); ++b) {
                llvm::Value* fp = builder->CreateStructGEP(vt, pay, b);
                llvm::Value* val = builder->CreateLoad(vfields[b], fp, arm.bindings[b]);
                llvm::Value* slot = builder->CreateAlloca(vfields[b], nullptr, arm.bindings[b]);
                builder->CreateStore(val, slot);
                defineSymbol(arm.bindings[b], slot);
                defineVarType(arm.bindings[b], substType(ed->payloads[vi][b], subs));
            }
        }
        if (arm.body) arm.body->accept(this);
        popScope();
        if (!builder->GetInsertBlock()->getTerminator())
            builder->CreateBr(endBlock);
    }
    builder->SetInsertPoint(endBlock);
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
    // Variadic access: va_arg<T>(ap) -> next argument of type T.
    if (node->templateName == "va_arg" && node->args.size() == 1 && node->typeArgs.size() == 1) {
        llvm::Value* ap = evaluateLValue(node->args[0]);
        std::string t = typeParamOverride.empty() ? node->typeArgs[0]
                                                   : substType(node->typeArgs[0], typeParamOverride);
        exprValueStack.push(builder->CreateVAArg(ap, getTypeFromString(t), "va.arg"));
        return;
    }
    // Generic algebraic-variant construction: `Some<int>(5)`, `Left<A,B>(x)`. Type
    // args resolve through the enclosing template's substitutions (so `Left<A,B>`
    // inside select2<A,B> becomes Left<int,int> at instantiation).
    if (genericVariants.count(node->templateName)) {
        auto& gi = genericVariants[node->templateName];      // (genericName, tag)
        std::vector<std::string> args;
        for (const auto& t : node->typeArgs)
            args.push_back(typeParamOverride.empty() ? t : substType(t, typeParamOverride));
        std::string mangled = ensureEnumInst(gi.first, args);
        EnumDecl* ge = genericEnumDecls[gi.first];
        std::map<std::string, std::string> subs;
        for (size_t i = 0; i < ge->typeParams.size() && i < args.size(); ++i)
            subs[ge->typeParams[i]] = args[i];
        std::vector<llvm::Type*> fts;
        for (const auto& ft : ge->payloads[gi.second]) fts.push_back(getTypeFromString(substType(ft, subs)));
        exprValueStack.push(buildEnumValue(structTypes[mangled], gi.second, fts, node->args));
        return;
    }
    auto templ = funcTemplateDecls.find(node->templateName);
    if (templ == funcTemplateDecls.end())
        throw std::runtime_error("Unknown template function: " + node->templateName);

    FunctionDecl* fd = templ->second;
    auto& tp = fd->typeParams;
    // Resolve each explicit type argument through the enclosing template's active
    // substitutions. When this call appears inside another template body (e.g.
    // `mk<T>(n)` inside `esz<T>`), node->typeArgs holds the literal param name
    // "T"; without this it would instantiate `mk_T` (T unresolved → i32). We must
    // not mutate node->typeArgs — the same node is re-visited per instantiation.
    auto resolveArg = [&](const std::string& t) {
        return typeParamOverride.empty() ? t : substType(t, typeParamOverride);
    };
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < node->typeArgs.size(); ++i)
        subs[tp[i]] = resolveArg(node->typeArgs[i]);

    // Mangle the instantiated function name
    std::string mangledName = node->templateName;
    for (const auto& t : node->typeArgs) mangledName += "_" + mangleTemplate(resolveArg(t));

    // Instantiate if not already in module.
    // Save/restore the insert point — we may be inside another function's body.
    if (!module->getFunction(mangledName)) {
        llvm::BasicBlock*          savedBB         = builder->GetInsertBlock();
        llvm::BasicBlock::iterator savedPoint      = builder->GetInsertPoint();
        llvm::Function*            savedFunc       = currentFunction;
        llvm::Value*               savedSretParam  = currentSretParam;
        // Restore (not clear) the override: this call may be nested inside another
        // template body whose substitutions must survive the inner instantiation.
        auto                       savedOverride   = typeParamOverride;

        typeParamOverride = subs;
        auto inst = std::make_shared<FunctionDecl>(mangledName, fd->returnType, fd->params, fd->body);
        inst->accept(this);
        typeParamOverride = savedOverride;

        // Restore caller's context
        currentFunction  = savedFunc;
        currentSretParam = savedSretParam;
        if (savedBB) builder->SetInsertPoint(savedBB, savedPoint);
    }

    llvm::Function* func = module->getFunction(mangledName);
    if (!func) throw std::runtime_error("Template instantiation failed: " + mangledName);

    std::vector<llvm::Value*> args;
    for (auto& arg : node->args) args.push_back(evaluateExpr(arg));

    // Coerce arguments to the instantiated function's parameter types — e.g. a
    // `double` literal passed where T=float substituted the parameter to `float`.
    llvm::FunctionType* fty = func->getFunctionType();
    unsigned pbase = funcSretTypes.count(mangledName) ? 1u : 0u; // skip hidden sret ptr
    for (size_t i = 0; i < args.size(); ++i) {
        unsigned pidx = pbase + (unsigned)i;
        if (pidx >= fty->getNumParams()) break;
        llvm::Type* pt = fty->getParamType(pidx);
        llvm::Value* v = args[i];
        if (!v || v->getType() == pt) continue;
        if (v->getType()->isFloatingPointTy() && pt->isFloatingPointTy())
            args[i] = builder->CreateFPCast(v, pt);
        else if (v->getType()->isIntegerTy() && pt->isIntegerTy())
            args[i] = v->getType()->getIntegerBitWidth() > pt->getIntegerBitWidth()
                ? builder->CreateTrunc(v, pt) : builder->CreateSExt(v, pt);
        else if (v->getType()->isIntegerTy() && pt->isFloatingPointTy())
            args[i] = builder->CreateSIToFP(v, pt);
        else if (v->getType()->isFloatingPointTy() && pt->isIntegerTy())
            args[i] = builder->CreateFPToSI(v, pt);
    }

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

llvm::Value* CodeGen::evaluateLValue(const ExprPtr& expr) {
    if (auto ident = dynamic_cast<IdentExpr*>(expr.get())) {
        llvm::Value* val = lookupSymbol(ident->name);
        if (!val) throw std::runtime_error("Undefined variable: " + ident->name);
        return val;
    }

    // Dereference as lvalue: *ptr = val — load the pointer value, use as the store target
    if (auto unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        if (unary->op == "*") {
            // evaluateExpr gives us the pointer value; that IS the lvalue address
            return evaluateExpr(unary->operand);
        }
    }

    if (auto member = dynamic_cast<MemberExpr*>(expr.get())) {
        std::string baseType = getExprEskiuType(member->base);
        // A pointer-to-struct base must be dereferenced: the struct pointer is the
        // base's *value* (evaluateExpr loads a local pointer var or yields a param),
        // whereas a value-struct base uses its *address* (evaluateLValue).
        bool baseIsPtr = (!baseType.empty() && (baseType.front() == '*' || baseType.back() == '*'));
        auto baseAddr = [&]() -> llvm::Value* {
            return baseIsPtr ? evaluateExpr(member->base) : evaluateLValue(member->base);
        };
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
        // Union field access: all fields are at offset 0 — just return the base ptr
        // (the load/store will use the field's type via the caller)
        bool isUnion = unionFields.count(baseType) > 0;
        auto lit = structLayout.find(baseType);
        if (lit != structLayout.end()) {
            auto sit = lit->second.find(member->member);
            if (sit != lit->second.end()) {
                if (sit->second.isBitfield)
                    throw std::runtime_error("cannot take the address of bitfield '"
                                             + member->member + "'");
                llvm::Value* basePtr = baseAddr();
                return builder->CreateStructGEP(structTypes[baseType], basePtr,
                                                sit->second.physIndex);
            }
        }
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == member->member) {
                llvm::Value* basePtr = baseAddr();
                if (isUnion) return basePtr; // offset 0 for all union fields
                return builder->CreateStructGEP(structTypes[baseType], basePtr, i);
            }
        }
        throw std::runtime_error("Struct/union '" + baseType + "' has no field '" + member->member + "'");
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
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(tripleStr, err);
    if (!target) {
        std::cerr << "error: " << err << std::endl;
        return false;
    }

    // Use native CPU for native compilation; generic CPU when cross-compiling
    bool isCross = !targetTriple.empty() &&
        targetTriple != llvm::sys::getDefaultTargetTriple();
    auto cpu = isCross ? llvm::StringRef("generic") : llvm::sys::getHostCPUName();
    llvm::TargetOptions opt;
    std::unique_ptr<llvm::TargetMachine> tm(
        target->createTargetMachine(triple, cpu, "", opt, llvm::Reloc::PIC_));
    module->setDataLayout(tm->createDataLayout());

    // Sanitizer instrumentation (new pass manager), run over the whole module
    // before code generation. --asan instruments memory accesses (the asan
    // runtime is linked separately); --ubsan inserts trapping bounds checks.
    if (asan || ubsan) {
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;
        llvm::PassBuilder PB(tm.get());
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        // AddressSanitizer only instruments functions marked sanitize_address
        // (Clang stamps this per function); add it to every defined function.
        if (asan) {
            for (llvm::Function& F : *module)
                if (!F.isDeclaration()) F.addFnAttr(llvm::Attribute::SanitizeAddress);
        }

        llvm::ModulePassManager MPM;
        if (ubsan) {
            llvm::BoundsCheckingPass::Options opts;   // empty Runtime => trap on OOB
            llvm::FunctionPassManager FPM;
            FPM.addPass(llvm::BoundsCheckingPass(opts));
            MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
        }
        if (asan) {
            MPM.addPass(llvm::AddressSanitizerPass(llvm::AddressSanitizerOptions{}));
        }
        MPM.run(*module, MAM);
    }

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
    return true;
}
