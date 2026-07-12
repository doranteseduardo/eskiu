#include "type_checker.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <climits>
#include <set>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with codegen; see template_utils.h.
#include "../template_utils.h"
#include "../intrinsics.h"
#include "../ast/ast_walk.h"
#include "../ast/type_qual.h"

// ============================================================================

// TypeChecker — the type system: inference, normalization, validation,
// promotion, interface-satisfaction + constraint checking.
// Part of the type_checker.cpp split; see type_checker.h.

// Type inference
std::string TypeChecker::inferBinaryExprType(const std::string& leftType, const std::string& op,
                                             const std::string& rightType) {
    if (op == "=") {
        return isValidAssignment(leftType, rightType) ? leftType : "error";
    }
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        // Operands must be mutually comparable: both numeric, both pointer-like
        // (including `null`), or the same non-aggregate type. Rejecting the rest
        // keeps codegen from emitting an `icmp`/`fcmp` on mismatched types (an
        // assertion / miscompile) or a meaningless comparison of aggregates.
        std::string l = normalizeType(leftType), r = normalizeType(rightType);
        if (l == "error" || r == "error" || l == "unknown" || r == "unknown")
            return "bool";                       // do not cascade a prior error
        auto ptrish = [&](const std::string& t) { return isPointerType(t) || t == "null"; };
        auto isAgg  = [&](const std::string& t) {
            return t.rfind("struct:", 0) == 0 || t.rfind("interface:", 0) == 0 ||
                   adtEnums.count(t) > 0;
        };
        bool ok = (isNumericType(l) && isNumericType(r)) ||
                  (ptrish(l) && ptrish(r)) ||
                  (l == r && !isAgg(l));
        return ok ? "bool" : "error";
    }
    if (op == "&&" || op == "||") {
        return "bool";
    }
    // Bitwise and shift operators work on integers
    if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        if (isIntType(leftType) && isIntType(rightType)) return promoteType(leftType, rightType);
        return "error";
    }
    // Pointer arithmetic: ptr + int / ptr - int → ptr; ptr - ptr → int64
    if (op == "-" && isPointerType(leftType) && isPointerType(rightType)) return "int64";
    if ((op == "+" || op == "-") && isPointerType(leftType) && isIntType(rightType)) {
        return leftType;
    }
    if (!isNumericType(leftType) || !isNumericType(rightType)) {
        return "error";
    }
    return promoteType(leftType, rightType);
}

std::string TypeChecker::inferUnaryExprType(const std::string& op, const std::string& operandType) {
    if (op == "!") {
        return "bool";
    }
    if (op == "-" || op == "+") {
        if (isNumericType(operandType)) {
            return operandType;
        }
        return "error";
    }
    if (op == "~") {
        if (isIntType(operandType)) return operandType;
        return "error";
    }
    if (op == "&") {
        return "*" + operandType;
    }
    if (op == "*") {
        if (isPointerType(operandType)) {
            return getPointeeType(operandType);
        }
        return "error";
    }
    return "error";
}

// Type validation
void TypeChecker::validateStructType(const std::string& type) {
    // Function pointer types are always valid
    if (type.size() > 3 && type.substr(0, 3) == "fn(") return;
    std::string baseType = type;
    // Strip fixed-size array suffixes (T[N], T[N][M], ...) — the element type is what
    // matters here; each dimension (a literal, enum, or const) is resolved in codegen.
    while (!baseType.empty() && baseType.back() == ']') {
        ty::Type t = ty::Type::parse(baseType);
        if (t.kind != ty::Type::Kind::Array) break;
        baseType = t.elem->str();
    }
    // Strip ALL pointer decorators (*T, T*, **T, etc.)
    bool stripped = true;
    while (stripped && !baseType.empty()) {
        stripped = false;
        if (hasPointerSuffix(baseType)) { baseType = extractBaseType(baseType); stripped = true; }
        else if (baseType.front() == '*') { baseType = baseType.substr(1); stripped = true; }
    }

    // Check if it's an explicit struct type (struct: prefix)
    if (baseType.find("struct:") == 0) {
        // Extract struct name (remove "struct:" prefix)
        std::string structName = baseType.substr(7);  // strlen("struct:") = 7

        // Look up struct in registry
        if (structs.find(structName) == structs.end()) {
            error(0, 0, "undefined struct '" + structName + "'");
        }
    } else if (!isPrimitiveType(baseType) && baseType != "va_list") {
        // Valid if it's a known struct, type alias, or enum type — anything else
        // is an undefined type. (Aliases/enums are resolved later in codegen.)
        if (structs.find(baseType) == structs.end() &&
            typeAliases.find(baseType) == typeAliases.end() &&
            enumTypes.find(baseType) == enumTypes.end() &&
            adtEnums.find(baseType) == adtEnums.end()) {     // incl. generic enum instances
            error(0, 0, "undefined struct '" + baseType + "'");
        }
    }
}

// Type checking utilities
// Check if a struct satisfies an interface (structural typing)
static bool structSatisfiesInterface(
        const std::map<std::string, std::pair<std::string, std::vector<std::string>>>& funcs,
        const std::string& structName,
        InterfaceDecl* iface) {
    for (const auto& method : iface->methods) {
        // A struct satisfies via a mangled method `Type_method`.
        std::string mangled = structName + "_" + method.name;
        if (funcs.find(mangled) != funcs.end()) continue;
        // Free-function fallback (lets PRIMITIVES satisfy a constraint): a
        // top-level fn named `method` whose first parameter is the constrained
        // type acts as the receiver-taking implementation — `t.method(...)`
        // lowers to `method(t, ...)`. So `int cmp(int,int)` satisfies `Ord` for int.
        // Gated to scalar primitives to match codegen's dispatch (a struct must
        // satisfy via a real method) — keeps sema and codegen in lockstep.
        static const std::set<std::string> kScalarPrims = {
            "int","int8","int16","int32","int64","uint","uint8","uint16","uint32",
            "uint64","char","bool","float","double"};
        auto fit = funcs.find(method.name);
        if (kScalarPrims.count(structName) && fit != funcs.end() && !fit->second.second.empty()) {
            std::string p0 = ty::Type::parse(fit->second.second[0]).nominalName();
            if (p0 == structName) continue;
        }
        return false;
    }
    return true;
}

// Bounded generics: verify each constrained type parameter's concrete argument
// satisfies its interface constraint(s). `subs` maps type-param name → concrete
// type. Reuses the structural-satisfaction check (the concrete type must define
// every method the interface requires).
void TypeChecker::checkConstraints(ASTNode* node,
        const std::map<std::string, std::vector<std::string>>& constraints,
        const std::map<std::string, std::string>& subs) {
    for (const auto& kv : constraints) {
        auto sit = subs.find(kv.first);
        if (sit == subs.end()) continue;
        std::string concrete = sit->second;
        // Normalize first (List<int> → struct:List_int), then take the bare
        // nominal name used in method mangling. The strip used to be hand-rolled
        // here (and got the struct: ordering wrong once) — now it's the structured
        // Type's nominalName(), which can't be gotten wrong.
        std::string bare = ty::Type::parse(normalizeType(concrete)).nominalName();
        for (const auto& ic : kv.second) {
            auto iit = interfaceDecls.find(ic);
            if (iit == interfaceDecls.end()) {
                if (node) errorAt(node, "unknown constraint interface '" + ic + "'");
                else error(0, 0, "unknown constraint interface '" + ic + "'");
                continue;
            }
            if (!structSatisfiesInterface(functionSignatures, bare, iit->second)) {
                std::string msg = "type '" + concrete + "' does not satisfy constraint '" +
                                  ic + "' (required by a bounded type parameter)";
                if (node) errorAt(node, msg); else error(0, 0, msg);
            }
        }
    }
}

bool TypeChecker::isValidAssignment(const std::string& lhsType, const std::string& rhsType) {
    // const-correctness: reject a conversion that would silently drop a pointee
    // const (`const int*` → `int*`). Adding const (`int*` → `const int*`) is fine.
    if (tyq::dropsConst(lhsType, rhsType)) return false;

    // Normalize both sides so "Point" == "struct:Point"
    std::string lhs = normalizeType(lhsType);
    std::string rhs = normalizeType(rhsType);

    if (lhs == rhs) return true;
    // Numeric: widening and same-width (incl. signedness changes) are fine; a
    // narrowing conversion (float->int, or wider->narrower) loses information and
    // must be an explicit cast. A literal small enough for the target is handled at
    // the call site (it stays valid), so this type-level rule can be strict.
    if (isNumericType(lhs) && isNumericType(rhs)) return !isNarrowingNumeric(lhs, rhs);
    if (lhs == "null" || rhs == "null") return isPointerType(lhs) || isPointerType(rhs);

    // Function/closure types carry a fixed call ABI (which registers hold the
    // parameters and the return value), so there is no implicit adapter between two
    // different fn signatures. Require an exact match — reinterpreting e.g. an
    // int-returning fn as float-returning is a silent miscompile (wrong even at -O0,
    // 0.0 under -O2). Checked before the generic pointer rule below, which a fn type
    // would otherwise satisfy. (A null fn pointer is handled by the `null` case above.)
    {
        ty::Type lt = ty::Type::parse(lhs), rt = ty::Type::parse(rhs);
        if (lt.isFn() || rt.isFn())
            return lt.isFn() && rt.isFn() && lt.str() == rt.str();
    }

    if (isPointerType(lhs) && isPointerType(rhs)) return true;

    // Interface satisfaction: assigning a struct to an interface type
    auto ifaceIt = interfaceDecls.find(lhs);
    if (ifaceIt != interfaceDecls.end()) {
        std::string structName = ty::Type::parse(rhs).nominalName();
        if (structSatisfiesInterface(functionSignatures, structName, ifaceIt->second))
            return true;
    }

    return false;
}

bool TypeChecker::isNumericType(const std::string& type) {
    return isIntType(type) || isFloatType(type);
}

bool TypeChecker::isNarrowingNumeric(const std::string& lhsType, const std::string& rhsType) {
    // C-aligned: integer-width narrowing (int64 -> int, int -> uint8) and float-width
    // narrowing (double -> float) are implicit, as in C. The one conversion Eskiu
    // rejects is float/double -> integer, which silently drops the fractional part
    // (`int x = 3.9` giving 3) -- the case C itself flags under -Wall. An out-of-range
    // integer *literal* is caught separately at the assignment sites.
    return isFloatType(rhsType) && isIntType(lhsType);
}

std::string TypeChecker::assignabilityError(const std::string& targetType,
                                            const std::string& srcType, Expr* srcExpr) {
    if (srcType == "unknown" || targetType.empty() || targetType == "unknown") return "";
    std::string nt = normalizeType(targetType), ns = normalizeType(srcType);
    // An integer literal that provably does not fit the target is rejected even though
    // integer-width narrowing is otherwise implicit (its value is statically known).
    if (isIntType(nt))
        if (auto* l = dynamic_cast<LiteralExpr*>(srcExpr);
            l && l->kind == LiteralExpr::Kind::INT && !intLiteralFits(nt, srcExpr))
            return "integer literal " + l->value + " is out of range for '" + targetType + "'";
    if (isValidAssignment(targetType, srcType)) return "";
    ty::Type lt = ty::Type::parse(nt);
    ty::Type rt = ty::Type::parse(ns);
    if (lt.isFn() || rt.isFn())
        return "incompatible function type '" + srcType + "' for '" + targetType + "'";
    // The only remaining numeric mismatch is float/double -> integer: it drops the
    // fractional part, so require an explicit cast (integer/float-width narrowing is
    // allowed by isValidAssignment above, C-style).
    if (isNumericType(nt) && isNumericType(ns))
        return "cannot assign a floating-point value ('" + srcType + "') to integer type '" +
               targetType + "' without an explicit cast (it drops the fraction)";
    if (tyq::dropsConst(targetType, srcType)) return "";   // reported by the caller's const check
    return "cannot convert '" + srcType + "' to '" + targetType + "'";
}

bool TypeChecker::intLiteralFits(const std::string& targetType, Expr* e) {
    auto* lit = dynamic_cast<LiteralExpr*>(e);
    if (!lit || lit->kind != LiteralExpr::Kind::INT) return false;
    std::string t = tyq::strip(targetType);
    bool neg = !lit->value.empty() && lit->value[0] == '-';
    unsigned long long mag = 0;
    try { mag = std::stoull(neg ? lit->value.substr(1) : lit->value, nullptr, 0); }
    catch (...) { return false; }
    if (t == "int64" || t == "uint64") return true;      // holds any literal we parse
    // Unsigned targets take no negative literal.
    if (t=="bool")   return !neg && mag <= 1ULL;
    if (t=="uint8"||t=="char") return !neg && mag <= 255ULL;
    if (t=="uint16") return !neg && mag <= 65535ULL;
    if (t=="uint"||t=="uint32") return !neg && mag <= 4294967295ULL;
    if (t=="int8")   return neg ? mag <= 128ULL       : mag <= 127ULL;
    if (t=="int16")  return neg ? mag <= 32768ULL     : mag <= 32767ULL;
    if (t=="int"||t=="int32") return neg ? mag <= 2147483648ULL : mag <= 2147483647ULL;
    return false;
}

bool TypeChecker::isIntType(const std::string& rawType) {
    std::string type = tyq::strip(rawType);
    return type == "int"   || type == "int8"  || type == "int16"  ||
           type == "int32" || type == "int64" ||
           type == "uint"  || type == "uint8" || type == "uint16" ||
           type == "uint32"|| type == "uint64"||
           type == "char"  || type == "bool";
}

bool TypeChecker::isFloatType(const std::string& rawType) {
    std::string type = tyq::strip(rawType);
    return type == "float" || type == "double";
}

bool TypeChecker::isPrimitiveType(const std::string& rawType) {
    std::string type = tyq::strip(rawType);
    if (type.size() > 3 && type.substr(0, 3) == "fn(") return true;
    return isNumericType(type) || type == "void" || type == "string";
}

bool TypeChecker::isPointerType(const std::string& rawType) {
    std::string type = tyq::strip(rawType);
    return !type.empty() && (type[0] == '*' || type.back() == '*' || type == "string");
}

std::string TypeChecker::getPointeeType(const std::string& pointerType) {
    // Accept both pointer spellings: *T (canonical) and T* (trailing-star).
    // The pointee's own const is preserved (a `const int*` derefs to `const int`).
    if (pointerType.size() >= 6 && pointerType.compare(pointerType.size() - 6, 6, "*const") == 0)
        return pointerType.substr(0, pointerType.size() - 6);
    if (!pointerType.empty() && pointerType.back() == '*')
        return pointerType.substr(0, pointerType.size() - 1);
    // leading-star spelling: const sits before the star(s), e.g. "const *int"
    if (!pointerType.empty() && pointerType.front() == '*')
        return pointerType.substr(1);
    if (tyq::baseConst(pointerType)) {
        std::string inner = pointerType.substr(6);
        if (!inner.empty() && inner.front() == '*') return "const " + inner.substr(1);
    }
    return "";
}

// Type normalization
void TypeChecker::unifyTypeParam(std::string pattern, std::string concrete,
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
    if (pattern.find('<') == std::string::npos) return;
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

std::string TypeChecker::normalizeType(const std::string& rawType) {
    // const has no bearing on identity/layout — strip it so the rest of the
    // type machinery is const-agnostic. (const survives only in stored declared
    // types, read back by the const-correctness checks.)
    std::string type = tyq::strip(rawType);
    if (hasPointerSuffix(type)) {
        return addPointerSuffix(normalizeType(extractBaseType(type)));
    }
    // Resolve a type alias to its underlying type.
    if (auto it = typeAliases.find(type); it != typeAliases.end())
        return normalizeType(it->second);
    // A classic enum is an integer; an algebraic enum is its own value type.
    if (adtEnums.count(type)) return type;
    if (enumTypes.count(type)) return "int";
    if (type.find("struct:") == 0 || type.find("interface:") == 0) {
        return type;
    }
    // Generic algebraic enum instance: Option<int> → the value type "Option_int".
    if (type.find('<') != std::string::npos) {
        auto [gname, gargs] = splitTemplateType(type);
        if (genericEnumDecls.count(gname)) {
            std::string mangled = mangleTemplate(type);
            templateInstanceArgs[mangled] = {gname, gargs};   // resolved back for match/construction
            adtEnums.insert(mangled);                          // a distinct value type
            return mangled;
        }
    }
    // Template instantiation: Result<int,string> → struct:Result_int_string
    if (type.find('<') != std::string::npos) {
        auto [tname, args] = splitTemplateType(type);
        auto templ = templateDecls.find(tname);
        if (templ != templateDecls.end()) {
            std::string mangled = mangleTemplate(type);
            templateInstanceArgs[mangled] = {tname, args};  // for type-arg inference
            // Instantiate if not already done
            if (structs.find(mangled) == structs.end()) {
                auto& tp = templ->second->typeParams;
                std::map<std::string, std::string> subs;
                for (size_t i = 0; i < tp.size() && i < args.size(); ++i)
                    subs[tp[i]] = args[i];
                StructInfo info;
                info.name = mangled;
                for (const auto& f : templ->second->fields)
                    info.fields.push_back({substType(f.type, subs), f.name});
                structs[mangled] = info;
                // Bounded generics on a struct template (`Map<K: Hashable, V>`):
                // verify the type args satisfy their constraints, once per instance.
                checkConstraints(nullptr, templ->second->constraints, subs);
            }
            return "struct:" + mangled;
        }
        return type; // unknown template — will error elsewhere
    }
    if (structs.find(type) != structs.end()) {
        return "struct:" + type;
    }
    return type;
}

// Type promotion
std::string TypeChecker::promoteType(const std::string& raw1, const std::string& raw2) {
    std::string type1 = tyq::strip(raw1), type2 = tyq::strip(raw2);
    if (type1 == type2) return type1;
    if (type1 == "double"  || type2 == "double")  return "double";
    if (type1 == "float"   || type2 == "float")   return "float";
    if (type1 == "int64"   || type2 == "int64")   return "int64";
    if (type1 == "uint64"  || type2 == "uint64")  return "uint64";
    if (type1 == "int32"   || type2 == "int32")   return "int32";
    if (type1 == "uint32"  || type2 == "uint32")  return "uint32";
    if (type1 == "int16"   || type2 == "int16")   return "int16";
    if (type1 == "uint16"  || type2 == "uint16")  return "uint16";
    return type1;
}

// Pointer type utilities
bool TypeChecker::hasPointerSuffix(const std::string& type) const {
    return !type.empty() && type.back() == '*';
}

std::string TypeChecker::extractBaseType(const std::string& pointerType) const {
    if (hasPointerSuffix(pointerType)) {
        // Remove the trailing '*'
        return pointerType.substr(0, pointerType.length() - 1);
    }
    return pointerType;
}

std::string TypeChecker::addPointerSuffix(const std::string& baseType) const {
    return baseType + "*";
}
