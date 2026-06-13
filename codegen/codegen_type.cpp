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

bool CodeGen::eskiuUnsigned(const std::string& t) const {
    std::string s = tyq::strip(expandAlias(t));
    return s == "uint" || s == "uint8" || s == "uint16" || s == "uint32" ||
           s == "uint64" || s == "char" || s == "bool";
}

llvm::AllocaInst* CodeGen::entryAlloca(llvm::Type* ty, llvm::Value* arrSize,
                                       const llvm::Twine& name) {
    llvm::BasicBlock* entry = &builder->GetInsertBlock()->getParent()->getEntryBlock();
    llvm::IRBuilder<> tmp(entry, entry->begin());
    return tmp.CreateAlloca(ty, arrSize, name);
}

llvm::Value* CodeGen::coerceInt(llvm::Value* val, llvm::Type* ty, bool unsignedSrc) {
    if (!val || val->getType() == ty) return val;
    if (!val->getType()->isIntegerTy() || !ty->isIntegerTy()) return val;
    unsigned sw = val->getType()->getIntegerBitWidth();
    unsigned dw = ty->getIntegerBitWidth();
    if (sw < dw) return unsignedSrc ? builder->CreateZExt(val, ty) : builder->CreateSExt(val, ty);
    if (sw > dw) return builder->CreateTrunc(val, ty);
    return val;
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
