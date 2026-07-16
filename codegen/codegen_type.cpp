#include "codegen.h"
#include "../ast/type_qual.h"
#include <typeinfo>
#include <cstdlib>
#include <cstdio>
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

    // Resolve a type alias to its underlying type (bare-name aliases only; a
    // decorated spelling like *Alias is not an alias key — it lowers to a pointer).
    if (auto it = typeAliases.find(typeStr); it != typeAliases.end())
        return getTypeFromString(it->second);

    // Structural dispatch via the one grammar parser (`ty::Type::parse`) — the same
    // interpreter the type checker uses, so codegen no longer re-implements the
    // type-string grammar. Registry lookups + instantiation stay here.
    llvm::Type* i32 = llvm::Type::getInt32Ty(*context);
    ty::Type t = ty::Type::parse(typeStr);
    switch (t.kind) {
        case ty::Type::Kind::Pointer:
        case ty::Type::Kind::String:
            return llvm::PointerType::get(*context, 0);   // both lower to an opaque ptr
        case ty::Type::Kind::Void:  return llvm::Type::getVoidTy(*context);
        case ty::Type::Kind::Bool:  return llvm::Type::getInt1Ty(*context);
        case ty::Type::Kind::Char:  return llvm::Type::getInt8Ty(*context);
        case ty::Type::Kind::Float:
            return t.name == "float" ? llvm::Type::getFloatTy(*context)
                                     : llvm::Type::getDoubleTy(*context);
        case ty::Type::Kind::Int: {
            const std::string& s = t.name;
            if (s == "int8"  || s == "uint8")  return llvm::Type::getInt8Ty(*context);
            if (s == "int16" || s == "uint16") return llvm::Type::getInt16Ty(*context);
            if (s == "int64" || s == "uint64") return llvm::Type::getInt64Ty(*context);
            return i32;   // int/int32/uint/uint32
        }
        case ty::Type::Kind::VaList: {
            // {ptr,ptr,ptr,i32,i32} — AArch64 layout (superset of x86-64); one type
            // works for both; the va_start/va_arg machinery reads the target slice.
            auto it = structTypes.find("__va_list");
            if (it != structTypes.end()) return it->second;
            llvm::Type* p = llvm::PointerType::get(*context, 0);
            auto* st = llvm::StructType::create(*context, {p, p, p, i32, i32}, "__va_list");
            structTypes["__va_list"] = st;
            return st;
        }
        case ty::Type::Kind::Fn:
            return llvm::StructType::get(*context, {       // fat pointer {fn_ptr, env_ptr}
                llvm::PointerType::get(*context, 0),
                llvm::PointerType::get(*context, 0),
            });
        case ty::Type::Kind::Struct: {                     // "struct:Name" (normalized)
            auto it = structTypes.find(t.name);
            if (it != structTypes.end()) return it->second;
            return llvm::PointerType::get(*context, 0);     // forward-ref placeholder
        }
        case ty::Type::Kind::Interface:
            // QUIRK preserved: a decorated `interface:X` matched nothing in the old
            // code and fell through to i32. (Bare interface names lower to ptr via
            // the Named branch.) Kept identical; a deliberate fix is a later step.
            return i32;
        case ty::Type::Kind::Template: {                   // "Name<args>" — instantiate
            auto [tname, args] = splitTemplateType(typeStr);
            if (genericEnumDecls.count(tname)) {            // generic ADT enum instance
                std::string mangled = ensureEnumInst(tname, args);
                return structTypes[mangled];
            }
            std::string mangled = mangleTemplate(typeStr);
            ensureTemplateInstantiated(mangled, tname, args);
            auto it = structTypes.find(mangled);
            if (it != structTypes.end()) return it->second;
            break;   // fall through to the i32 fallback below
        }
        case ty::Type::Kind::Array: {                      // "T[N]"
            llvm::Type* elem = getTypeFromString(t.elem->str());
            uint64_t n = 0;
            if (resolveArrayDim(t.dim, n)) return llvm::ArrayType::get(elem, n);
            bool negLit = t.dim.size() > 1 && t.dim[0] == '-';
            for (size_t i = 1; negLit && i < t.dim.size(); ++i)
                if (!std::isdigit((unsigned char)t.dim[i])) negLit = false;
            if (negLit)
                throw std::runtime_error("array size must be a positive constant, got '"
                                         + t.dim + "'");
            return llvm::PointerType::get(*context, 0);     // unsized → pointer
        }
        case ty::Type::Kind::Slice: {                      // "T[]" → fat pointer { data, len }
            return llvm::StructType::get(*context, {
                llvm::PointerType::get(*context, 0),
                llvm::Type::getInt64Ty(*context)});
        }
        case ty::Type::Kind::Named: {                      // bare nominal: enum/struct/iface
            if (enumTypes.count(t.name)) {                 // classic enum → i32; ADT → struct
                auto st = structTypes.find(t.name);
                if (st != structTypes.end()) return st->second;
                return i32;
            }
            auto it = structTypes.find(t.name);            // bare struct name
            if (it != structTypes.end()) return it->second;
            if (ifaceFatPtrTypes.count(t.name))            // interface value → opaque ptr
                return llvm::PointerType::get(*context, 0);
            break;
        }
        default: break;                                    // Null/Unknown/Error
    }

    std::cerr << "Warning: unknown type '" << typeStr << "', defaulting to i32" << std::endl;
    return i32;
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

llvm::Value* CodeGen::intToFloat(llvm::Value* val, llvm::Type* ty, bool unsignedSrc) {
    // The single place integer->float conversion happens. An unsigned source
    // must use UIToFP, not SIToFP: a high-bit-set unsigned value (e.g. uint32
    // 4000000000) would otherwise convert to a negative float.
    return unsignedSrc ? builder->CreateUIToFP(val, ty)
                       : builder->CreateSIToFP(val, ty);
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
    // Single resolver: prefer the post-transform type checker's resolved type.
    if (resolvedExprTypes) {
        auto it = resolvedExprTypes->find(expr.get());
        if (it != resolvedExprTypes->end() && it->second != "unknown") {
            // [diagnostic] cross-check: the structural derivation must AGREE with
            // the resolver table (a disagreement is a latent two-evaluator bug, of
            // the class fixed in v0.2.4). Off unless ESKIU_RESOLVER_DEBUG is set.
            if (std::getenv("ESKIU_RESOLVER_DEBUG")) {
                std::string d = deriveExprEskiuType(expr);
                // Normalize away benign representational differences (alias spelling,
                // and the `struct:`/`interface:` tag the resolver carries but
                // derivation doesn't) so only semantically meaningful disagreements
                // — a real two-evaluator divergence — are reported.
                auto norm = [&](std::string s) {
                    s = expandAlias(s);
                    for (const char* tag : {"struct:", "interface:"}) {
                        size_t p;
                        while ((p = s.find(tag)) != std::string::npos)
                            s.erase(p, std::string(tag).size());
                    }
                    return s;
                };
                if (!d.empty() && d != "unknown" && norm(d) != norm(it->second))
                    fprintf(stderr, "[resolver-disagree] table=%s derive=%s %s\n",
                            it->second.c_str(), d.c_str(), typeid(*expr).name());
            }
            // Codegen treats a nullable `?*T` exactly like `*T` (same repr); the `?`
            // only governs sema deref-safety, so strip it here.
            const std::string& r = it->second;
            return (!r.empty() && r[0] == '?') ? r.substr(1) : r;
        }
        // A table miss is by design — the resolver doesn't annotate every expr, so
        // the structural derivation below legitimately carries the rest. (Only a
        // table/derivation *disagreement* above is a bug.)
    }
    std::string r = deriveExprEskiuType(expr);
    return (!r.empty() && r[0] == '?') ? r.substr(1) : r;   // `?*T` codegens as `*T`
}

std::string CodeGen::deriveExprEskiuType(const ExprPtr& expr) const {
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
        if (member->member == "len" && ty::Type::parse(base).kind == ty::Type::Kind::Slice)
            return "int64";                                   // slice length field
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
        ty::Type bt = ty::Type::parse(base);
        std::string elem;
        if (base == "string") elem = "char";
        else if (bt.kind == ty::Type::Kind::Array || bt.kind == ty::Type::Kind::Slice) elem = bt.elem->str();
        else if (!base.empty() && base.front() == '*') elem = base.substr(1);
        else if (!base.empty() && base.back()  == '*') elem = base.substr(0, base.size() - 1);
        if (!elem.empty()) return index->highIndex ? (elem + "[]") : elem;   // slice vs element
    }
    // A ternary's static type is the common type of its arms (the type checker's
    // resolver table usually supplies it; this is the structural fallback).
    if (auto tern = dynamic_cast<TernaryExpr*>(expr.get())) {
        std::string tt = getExprEskiuType(tern->thenExpr);
        std::string et = getExprEskiuType(tern->elseExpr);
        if (tt == et || et.empty() || et == "unknown") return tt;
        if (tt.empty() || tt == "unknown") return et;
        return tt;
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
