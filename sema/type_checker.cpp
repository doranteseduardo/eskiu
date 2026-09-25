#include "type_checker.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <climits>
#include <set>
#include <functional>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with codegen; see template_utils.h.
#include "../template_utils.h"
#include "../ast/type_qual.h"

// ============================================================================

TypeChecker::TypeChecker() {
    pushScope();  // Global scope
}

bool TypeChecker::check(Program* program) {
    hasErrors = false;
    errors.clear();

    // First pass: register all struct declarations and function signatures
    std::set<std::string> definedFnBodies;   // names of functions WITH a body, for redefinition
    for (const auto& decl : program->declarations) {
        curFile = decl->sourceFile;
        if (auto enumDecl = dynamic_cast<EnumDecl*>(decl.get())) {
            enumTypes.insert(enumDecl->name);
            if (enumDecl->isADT() && !enumDecl->typeParams.empty()) {
                // Generic algebraic enum (Option<T>): a template; instances are
                // monomorphized on use (normalizeType).
                genericEnumDecls[enumDecl->name] = enumDecl;
                for (size_t i = 0; i < enumDecl->members.size(); ++i)
                    genericVariants[enumDecl->members[i].first] = {enumDecl->name, (int)i};
            } else if (enumDecl->isADT()) {
                // Algebraic enum: a tagged union, not int constants. Register each
                // variant by name -> (enum, tag) for construction + match.
                adtEnums.insert(enumDecl->name);
                enumDecls[enumDecl->name] = enumDecl;
                for (size_t i = 0; i < enumDecl->members.size(); ++i)
                    adtVariants[enumDecl->members[i].first] = {enumDecl->name, (int)i};
            } else {
                for (const auto& m : enumDecl->members) enumConstants[m.first] = m.second;
                plainEnumDecls[enumDecl->name] = enumDecl;   // for exhaustiveness-checked `match`
            }
            continue;
        }
        if (auto aliasDecl = dynamic_cast<TypeAliasDecl*>(decl.get())) {
            typeAliases[aliasDecl->name] = aliasDecl->aliased;
            continue;
        }
        if (auto ifaceDecl = dynamic_cast<InterfaceDecl*>(decl.get())) {
            interfaceDecls[ifaceDecl->name] = ifaceDecl;
            continue;
        }
        if (auto unionDecl = dynamic_cast<UnionDecl*>(decl.get())) {
            // A union is a struct to the type system (all fields at offset 0 in codegen).
            StructInfo info;
            info.name = unionDecl->name;
            for (const auto& f : unionDecl->fields) info.fields.push_back({f.type, f.name});
            structs[unionDecl->name] = info;
            continue;
        }
        if (auto funcDecl = dynamic_cast<FunctionDecl*>(decl.get())) {
            if (!funcDecl->typeParams.empty()) {
                funcTemplateDecls[funcDecl->name] = funcDecl;
                if (funcDecl->mustUse) mustUseFuncs.insert(funcDecl->name);
                // Don't register yet — template params are not real types,
                // the function is registered on instantiation.
                continue;
            }
        }
        if (auto structDecl = dynamic_cast<StructDecl*>(decl.get())) {
            if (!structDecl->typeParams.empty()) {
                templateDecls[structDecl->name] = structDecl;
                continue;
            }

            StructInfo info;
            info.name = structDecl->name;
            info.fields = structDecl->fields;
            structs[structDecl->name] = info;

            // Register methods as mangled functions: StructName_methodName(self, ...)
            for (const auto& method : structDecl->methods) {
                if (auto func = dynamic_cast<FunctionDecl*>(method.get())) {
                    std::string mangled = structDecl->name + "_" + func->name;
                    std::vector<std::string> paramTypes;
                    paramTypes.push_back("*" + structDecl->name); // implicit self
                    for (const auto& p : func->params) paramTypes.push_back(p.first);
                    defineFunction(mangled, func->returnType, paramTypes);
                }
            }
        } else if (auto funcDecl = dynamic_cast<FunctionDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : funcDecl->params) {
                paramTypes.push_back(param.first);  // first = type, second = name
            }
            // An async fn's call expression yields *Future<T>; the declared T is
            // the inner type the body returns (the transform wraps it). `async
            // void` uses a 1-byte unit (uint8) as the future's value type.
            std::string sigRet = funcDecl->returnType;
            if (funcDecl->isAsync)
                sigRet = "*Future<" + (funcDecl->returnType == "void" ? std::string("uint8")
                                                                      : funcDecl->returnType) + ">";
            // A second *definition* (body) of the same name is a redefinition; a
            // body-less forward declaration alongside one definition is fine.
            if (!funcDecl->operatorSym.empty()) {
                std::string disp = "operator " + (funcDecl->operatorSym == "u-" ? std::string("-") : funcDecl->operatorSym) + "(";
                for (size_t i = 0; i < funcDecl->params.size(); ++i)
                    disp += (i ? ", " : "") + funcDecl->params[i].first;
                fnDisplayNames[funcDecl->name] = disp + ")";
            }
            if (funcDecl->body) {
                if (definedFnBodies.count(funcDecl->name))
                    errorAt(funcDecl, "redefinition of function '" + fnDisplay(funcDecl->name) + "'");
                definedFnBodies.insert(funcDecl->name);
            }
            defineFunction(funcDecl->name, sigRet, paramTypes);
            functionParamEscaping[funcDecl->name] = funcDecl->paramEscaping;
            if (funcDecl->mustUse) mustUseFuncs.insert(funcDecl->name);
            // Operator overload: index it by op so `a op b` resolves by operand types
            // (with the usual numeric coercions), not by an exact mangled-name match.
            if (!funcDecl->operatorSym.empty())
                operatorOverloads[funcDecl->operatorSym].push_back({paramTypes, funcDecl->returnType, funcDecl->name});
        } else if (auto externDecl = dynamic_cast<ExternDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : externDecl->params) {
                paramTypes.push_back(param.first);  // first = type, second = name
            }
            defineFunction(externDecl->name, externDecl->returnType, paramTypes);
            functionParamEscaping[externDecl->name] = externDecl->paramEscaping;
            externFnNames.insert(externDecl->name);
        } else if (auto intrinDecl = dynamic_cast<IntrinsicDecl*>(decl.get())) {
            // Intrinsics carry an ordinary signature; only codegen treats them
            // specially (inline lowering instead of a call).
            std::vector<std::string> paramTypes;
            for (const auto& param : intrinDecl->params) {
                paramTypes.push_back(param.first);
            }
            defineFunction(intrinDecl->name, intrinDecl->returnType, paramTypes);
            functionParamEscaping[intrinDecl->name] = intrinDecl->paramEscaping;
        }
    }

    checkTopLevelNames(program);

    // A type alias that resolves back to itself (`type A = B; type B = A;`, `type A = *A;`)
    // names no type. Report it and drop it so type resolution terminates.
    {
        std::map<std::string, TypeAliasDecl*> aliasDecls;
        for (const auto& decl : program->declarations)
            if (auto* ad = dynamic_cast<TypeAliasDecl*>(decl.get())) aliasDecls[ad->name] = ad;
        auto baseName = [](const std::string& t) {
            ty::Type ty = ty::Type::parse(tyq::strip(t));
            while ((ty.kind == ty::Type::Kind::Array || ty.kind == ty::Type::Kind::Slice) && ty.elem) {
                ty::Type e = *ty.elem; ty = e;
            }
            return ty.nominalName();
        };
        std::set<std::string> cyclic;
        for (const auto& kv : aliasDecls) {
            std::set<std::string> seen{kv.first};
            std::string cur = kv.first;
            while (true) {
                auto it = typeAliases.find(cur);
                if (it == typeAliases.end()) break;
                std::string next = baseName(it->second);
                if (next == kv.first) { cyclic.insert(kv.first); break; }
                if (!seen.insert(next).second) break;   // a cycle not through kv.first
                cur = next;
            }
        }
        for (const auto& n : cyclic) {
            errorAt(aliasDecls[n], "type alias '" + n + "' refers to itself (a cyclic alias names no type)");
        }
        for (const auto& n : cyclic) typeAliases[n] = "unknown";   // resolves to the error sentinel
    }

    // An extern the program also defines is an Eskiu function, not a C one.
    for (const auto& n : definedFnBodies) externFnNames.erase(n);

    // Second pass: type check all declarations
    for (const auto& decl : program->declarations) {
        curFile = decl->sourceFile;
        decl->accept(this);
    }
    curFile.clear();

    checkPendingInstances();

    checkValueCycles(program);

    // -Wall: top-level functions defined but never referenced.
    if (warnAll) {
        for (const auto& [name, loc] : definedFns) {
            if (!calledFns.count(name))
                warning(loc.first, loc.second, "unused function '" + fnDisplay(name) + "'");
        }
    }

    // Report errors
    for (const auto& err : errors) {
        std::cerr << "error: " << err << "\n";
    }

    return !hasErrors;
}

// Top-level names share one namespace: a function, global variable, struct/union, enum,
// enum member, interface, or type alias may be declared once. Exceptions: `extern`
// declarations (repeated across modules), a body-less prototype plus its definition (with
// the same signature), a variable's `extern` declaration plus its definition, and a struct
// declared again with identical fields (a multi-file merge of the same declaration).
void TypeChecker::checkTopLevelNames(Program* program) {
    struct Entry { std::string kind; Decl* decl; };
    std::map<std::string, Entry> seen;
    auto sameFields = [](const std::vector<StructDecl::Field>& a, const std::vector<StructDecl::Field>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].type != b[i].type || a[i].name != b[i].name || a[i].bitWidth != b[i].bitWidth) return false;
        return true;
    };
    auto sigOf = [&](FunctionDecl* f) {
        std::string s = normalizeType(f->returnType) + "(";
        for (size_t i = 0; i < f->params.size(); ++i) s += (i ? ", " : "") + normalizeType(f->params[i].first);
        return s + ")";
    };
    auto declare = [&](const std::string& name, const std::string& kind, Decl* d) {
        auto it = seen.find(name);
        if (it == seen.end()) { seen[name] = {kind, d}; return; }
        Entry& prev = it->second;
        if (kind == "function" && prev.kind == "function") {
            auto* a = static_cast<FunctionDecl*>(prev.decl);
            auto* b = static_cast<FunctionDecl*>(d);
            if (!a->typeParams.empty() || !b->typeParams.empty()) {
                if (a->body && b->body) return;              // reported as a redefinition
            } else if (sigOf(a) != sigOf(b)) {
                errorAt(d, "conflicting declaration of function '" + fnDisplay(name) + "' (" +
                           sigOf(b) + " vs the earlier " + sigOf(a) + ")");
                return;
            }
            if (!prev.decl || (!a->body && b->body)) prev.decl = d;
            return;                                          // two bodies: reported as a redefinition
        }
        if (kind == "variable" && prev.kind == "variable") {
            auto* a = static_cast<VarDecl*>(prev.decl);
            auto* b = static_cast<VarDecl*>(d);
            if ((a->isExtern || b->isExtern) && normalizeType(a->type) == normalizeType(b->type)) return;
            errorAt(d, "redefinition of global variable '" + name + "'");
            return;
        }
        if (kind == "struct" && prev.kind == "struct") {
            auto* a = static_cast<StructDecl*>(prev.decl);
            auto* b = static_cast<StructDecl*>(d);
            if (sameFields(a->fields, b->fields) && a->methods.size() == b->methods.size() &&
                a->typeParams == b->typeParams) return;      // the same declaration, merged twice
            errorAt(d, "redefinition of struct '" + name + "' with different fields");
            return;
        }
        if (kind == prev.kind) {
            errorAt(d, "redefinition of " + kind + " '" + name + "'");
            return;
        }
        // Types and enum members live apart (a struct `G` beside a member `G` is fine);
        // otherwise one name has one meaning.
        auto isType = [](const std::string& k) {
            return k == "struct" || k == "union" || k == "enum" || k == "interface" || k == "type alias";
        };
        if ((isType(kind) && prev.kind == "enum member") || (isType(prev.kind) && kind == "enum member")) return;
        auto article = [](const std::string& k) { return std::string(k[0] == 'e' || k[0] == 'i' || k[0] == 'u' ? "an " : "a ") + k; };
        errorAt(d, "'" + name + "' is declared as both " + article(prev.kind) + " and " + article(kind));
    };
    for (const auto& decl : program->declarations) {
        Decl* d = decl.get();
        if (auto* f = dynamic_cast<FunctionDecl*>(d)) declare(f->name, "function", f);
        else if (auto* v = dynamic_cast<VarDecl*>(d)) declare(v->name, "variable", v);
        else if (auto* s = dynamic_cast<StructDecl*>(d)) {
            declare(s->name, "struct", s);
            // An inline method `S.m` is the function `S_m`; a free `S_m` would be a second one.
            for (const auto& m : s->methods)
                if (auto* mf = dynamic_cast<FunctionDecl*>(m.get())) {
                    std::string mangled = s->name + "_" + mf->name;
                    auto it = seen.find(mangled);
                    if (it != seen.end() && it->second.kind == "function")
                        errorAt(it->second.decl, "function '" + mangled + "' conflicts with method '" +
                                                 mf->name + "' of struct '" + s->name + "'");
                    else seen[mangled] = {"method", mf};
                }
        }
        else if (auto* u = dynamic_cast<UnionDecl*>(d)) declare(u->name, "union", u);
        else if (auto* e = dynamic_cast<EnumDecl*>(d)) {
            declare(e->name, "enum", e);
            for (const auto& m : e->members) declare(m.first, "enum member", e);
        }
        else if (auto* i = dynamic_cast<InterfaceDecl*>(d)) declare(i->name, "interface", i);
        else if (auto* a = dynamic_cast<TypeAliasDecl*>(d)) {
            auto it = seen.find(a->name);
            if (it != seen.end() && it->second.kind == "type alias" &&
                static_cast<TypeAliasDecl*>(it->second.decl)->aliased == a->aliased) continue;
            declare(a->name, "type alias", a);
        }
    }
}

// A struct (or union) that contains itself by value, directly or through other by-value
// fields, has no finite layout. Pointer and slice fields break the cycle; a fixed-size
// array of a struct does not.
void TypeChecker::checkValueCycles(Program* program) {
    std::map<std::string, Decl*> decls;
    for (const auto& decl : program->declarations)
        if (auto* sd = dynamic_cast<StructDecl*>(decl.get()); sd && sd->typeParams.empty()) decls[sd->name] = sd;
        else if (auto* ud = dynamic_cast<UnionDecl*>(decl.get())) decls[ud->name] = ud;
    auto fieldsOf = [&](Decl* d) -> const std::vector<StructDecl::Field>& {
        if (auto* sd = dynamic_cast<StructDecl*>(d)) return sd->fields;
        return static_cast<UnionDecl*>(d)->fields;
    };
    auto byValueStruct = [&](const std::string& ft) -> std::string {
        ty::Type t = ty::Type::parse(normalizeType(ft));
        while (t.kind == ty::Type::Kind::Array && t.elem) { ty::Type e = *t.elem; t = e; }
        if (t.kind != ty::Type::Kind::Struct) return "";
        std::string n = t.nominalName();
        return decls.count(n) ? n : "";
    };
    std::map<std::string, int> state;   // 0 = unvisited, 1 = on stack, 2 = done
    std::set<std::string> reported;
    std::function<void(const std::string&)> dfs = [&](const std::string& n) {
        state[n] = 1;
        for (const auto& f : fieldsOf(decls[n])) {
            std::string m = byValueStruct(f.type);
            if (m.empty()) continue;
            if (state[m] == 1) {
                if (reported.insert(m).second)
                    errorAt(decls[m], "struct '" + m + "' contains itself by value (through field '" +
                                      f.name + "' of '" + n + "'); use a pointer");
            } else if (state[m] == 0) dfs(m);
        }
        state[n] = 2;
    };
    for (const auto& kv : decls) if (state[kv.first] == 0) dfs(kv.first);
}

std::string TypeChecker::getExpressionType(Expr* expr) {
    auto it = expressionTypes.find(expr);
    if (it != expressionTypes.end()) {
        // A type built from source spellings inside a generic instance (a lambda's
        // `fn(T)->T`) still names the parameter: resolve it like any declared type.
        return inInstance ? resolveInstType(it->second) : it->second;
    }
    return "unknown";
}

std::string TypeChecker::resolveInstType(const std::string& t) const {
    if (!inInstance || instSubs.empty()) return t;
    for (const auto& kv : instSubs)
        if (t.find(kv.first) != std::string::npos) return substType(t, instSubs);
    return t;
}

void TypeChecker::queueInstance(FunctionDecl* fn, const std::vector<std::string>& typeParams,
                                const std::map<std::string, std::string>& subs,
                                const std::string& name, const std::string& suffix,
                                const std::string& mangled, const std::string& selfType,
                                const std::string& file) {
    if (!fn || !fn->body) return;
    std::string display = name + "<";
    for (size_t i = 0; i < typeParams.size(); ++i) {
        auto it = subs.find(typeParams[i]);
        std::string a = it == subs.end() ? typeParams[i] : it->second;
        for (size_t p; (p = a.find("struct:")) != std::string::npos; ) a.erase(p, 7);
        display += (i ? "," : "") + a;
    }
    display += ">" + suffix;
    if (!queuedInstances.insert(display).second) return;
    pendingInstances.push_back({fn, subs, display, mangled, selfType,
                                file.empty() ? diagFile() : file, instDepth + 1});
}

// Check each queued generic instance: the template body, with its type parameters
// bound to the instance's arguments. Checking one may queue more (nested generic
// calls, generic structs named in the body), so this runs until the queue drains.
void TypeChecker::checkPendingInstances() {
    static const int kMaxDepth = 64;
    for (size_t i = 0; i < pendingInstances.size(); ++i) {
        PendingInstance p = pendingInstances[i];
        if (p.depth > kMaxDepth) {
            curFile = p.file;
            errorAt(p.fn, "generic instantiation of '" + p.display + "' is nested more than " +
                          std::to_string(kMaxDepth) + " levels deep (infinitely recursive generic?)");
            curFile.clear();
            break;
        }
        std::vector<std::pair<std::string, std::string>> params;
        if (!p.selfType.empty()) params.push_back({p.selfType, "self"});
        for (const auto& pr : p.fn->params)
            params.push_back({pr.first == "..." ? pr.first : substType(pr.first, p.subs), pr.second});
        FunctionDecl inst(p.mangled, substType(p.fn->returnType, p.subs), params, p.fn->body);
        inst.line = p.fn->line; inst.col = p.fn->col;
        inst.paramEscaping = p.fn->paramEscaping;
        inst.paramPositions = p.fn->paramPositions;
        if (!p.selfType.empty()) {
            if (!inst.paramEscaping.empty()) inst.paramEscaping.insert(inst.paramEscaping.begin(), false);
            if (!inst.paramPositions.empty())
                inst.paramPositions.insert(inst.paramPositions.begin(), {p.fn->line, p.fn->col});
        }

        std::map<Expr*, std::string> instTypes;
        std::swap(expressionTypes, instTypes);
        auto savedNarrowed = narrowedNonNull;
        narrowedNonNull.clear();
        curFile = p.file;
        instSubs = p.subs;
        instContext = p.display;
        instDepth = p.depth;
        inInstance = true;
        inst.accept(this);
        inInstance = false;
        instDepth = 0;
        instContext.clear();
        instSubs.clear();
        curFile.clear();
        narrowedNonNull = savedNarrowed;
        std::swap(expressionTypes, instTypes);
    }
    pendingInstances.clear();
}

// Declaration visitors
void TypeChecker::visit(Program* node) {
    // Program node is handled by check() method
    // This is called if someone visits it directly
}

// Approximate the on-screen width of an expression's leading token, so hover can
// require the cursor to actually fall ON the expression rather than merely share
// its line. Identifiers and literals have a clear single-token footprint; for
// composite expressions we use width 1 (match only at their exact start column),
// which lets a more specific child identifier/literal win.
static int hoverSpanWidth(const Expr* e) {
    if (auto* id = dynamic_cast<const IdentExpr*>(e))
        return std::max(1, (int)id->name.size());
    if (auto* lit = dynamic_cast<const LiteralExpr*>(e)) {
        int n = (int)lit->value.size();
        if (lit->kind == LiteralExpr::Kind::STRING ||
            lit->kind == LiteralExpr::Kind::CHAR)
            n += 2;  // surrounding quotes, which the cursor sits within
        return std::max(1, n);
    }
    return 1;
}

std::string TypeChecker::getTypeAtPosition(int line, int col) const {
    // Return a type only when the cursor is within an expression's token span —
    // never for keywords, type annotations, operators, or whitespace. Among the
    // expressions that contain the cursor, prefer the narrowest (most specific).
    int         bestWidth = INT_MAX;
    std::string bestType;
    for (const auto& [node, type] : expressionTypes) {
        if (type == "unknown" || type.empty()) continue;
        if (node->line != line || node->col <= 0) continue;
        int width = hoverSpanWidth(node);
        if (col < node->col || col >= node->col + width) continue;
        if (width < bestWidth) { bestWidth = width; bestType = type; }
    }
    // Declared names (variables, parameters) at their declaration site.
    for (const auto& s : hoverSyms) {
        if (s.type.empty() || s.line != line || s.col <= 0) continue;
        int width = std::max(1, s.width);
        if (col < s.col || col >= s.col + width) continue;
        if (width < bestWidth) { bestWidth = width; bestType = s.type; }
    }
    return bestType;
}

std::string TypeChecker::getDefinitionAt(int line, int col) const {
    auto fmtLoc = [](const DefLocation& loc) {
        return loc.file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.col);
    };
    // 1. A use of a local/parameter whose name spans the cursor: the definition is
    //    the symbol scope resolution picked at that use.
    for (const auto& [pos, use] : useDefs) {
        if (pos.first == line && col >= pos.second && col < pos.second + use.width)
            return fmtLoc(use.def);
    }
    // 2. A use of a global (function, enum member, global variable) spanning it.
    for (const auto& [pos, name] : useLocations) {
        if (pos.first != line || col < pos.second || col >= pos.second + (int)name.size()) continue;
        auto defit = definitionLocations.find(name);
        if (defit != definitionLocations.end()) return fmtLoc(defit->second);
    }
    // 3. Cursor is directly on a declaration in this file.
    for (const auto& [name, loc] : definitionLocations) {
        if (loc.file == sourceFile && loc.line == line &&
            col >= loc.col && col < loc.col + (int)name.size())
            return fmtLoc(loc);
    }
    for (const auto& s : hoverSyms) {
        if (s.line == line && col >= s.col && col < s.col + std::max(1, s.width))
            return sourceFile + ":" + std::to_string(s.line) + ":" + std::to_string(s.col);
    }
    return "";
}

int TypeChecker::scopeOf(const std::string& name) const {
    auto it = scopeIndex.find(name);
    return it == scopeIndex.end() ? -1 : it->second.back();
}

const TypeChecker::Symbol* TypeChecker::findSymbol(const std::string& name) const {
    int si = scopeOf(name);
    return si < 0 ? nullptr : &scopes[si].find(name)->second;
}

// Scope management
void TypeChecker::pushScope() {
    scopes.push_back(std::map<std::string, Symbol>());
}

void TypeChecker::popScope() {
    if (scopes.empty()) return;
    // -Wall: warn about names declared in this scope that were never referenced.
    // Skip the global scope (size 1) — unused globals are often intentional.
    if (warnAll && scopes.size() > 1) {
        for (const auto& [name, sym] : scopes.back()) {
            if (sym.used || name == "self" || name == "_") continue;
            warning(sym.line, sym.col,
                    std::string(sym.isParam ? "unused parameter '" : "unused variable '")
                    + name + "'");
        }
    }
    for (const auto& entry : scopes.back()) {
        auto it = scopeIndex.find(entry.first);
        it->second.pop_back();
        if (it->second.empty()) scopeIndex.erase(it);
    }
    scopes.pop_back();
}

bool TypeChecker::isConstSymbol(const std::string& name) const {
    const Symbol* sym = findSymbol(name);
    return sym && sym->isConst;
}

bool TypeChecker::assignsToConst(Expr* lhs, std::string& nameOut) {
    Expr* cur = lhs;
    // Root identifier of an expression, for a readable diagnostic.
    auto rootName = [](Expr* e) -> std::string {
        while (e) {
            if (auto* id = dynamic_cast<IdentExpr*>(e)) return id->name;
            if (auto* u = dynamic_cast<UnaryExpr*>(e))  { e = u->operand.get(); continue; }
            if (auto* m = dynamic_cast<MemberExpr*>(e)) { e = m->base.get();    continue; }
            if (auto* ix = dynamic_cast<IndexExpr*>(e)) { e = ix->base.get();   continue; }
            return "";
        }
        return "";
    };
    while (cur) {
        if (auto* id = dynamic_cast<IdentExpr*>(cur)) {
            if (isConstSymbol(id->name)) { nameOut = id->name; return true; }
            return false;
        }
        // Dereference: `*p = x` writes the pointee — illegal if it is const.
        if (auto* u = dynamic_cast<UnaryExpr*>(cur)) {
            if (u->op == "*" && tyq::baseConst(getPointeeType(getExpressionType(u->operand.get())))) {
                nameOut = rootName(u->operand.get()); return true;
            }
            return false;
        }
        // Member/element of a *value* aggregate keeps the same const root and we
        // keep walking; through a pointer it writes the pointee, which is illegal
        // only if that pointee (the struct/element) is const.
        if (auto* m = dynamic_cast<MemberExpr*>(cur)) {
            std::string bt = getExpressionType(m->base.get());
            if (tyq::isPtr(bt)) {
                if (tyq::baseConst(getPointeeType(bt))) { nameOut = m->member; return true; }
                return false;
            }
            cur = m->base.get(); continue;
        }
        if (auto* ix = dynamic_cast<IndexExpr*>(cur)) {
            std::string bt = getExpressionType(ix->base.get());
            if (tyq::isPtr(bt)) {
                if (tyq::baseConst(getPointeeType(bt))) { nameOut = rootName(ix->base.get()); return true; }
                return false;
            }
            cur = ix->base.get(); continue;
        }
        return false;  // calls, etc. — not an in-place const mutation
    }
    return false;
}

void TypeChecker::defineSymbol(const std::string& name, const std::string& type) {
    defineSymbol(name, type, 0, 0, false);
}

void TypeChecker::defineSymbol(const std::string& name, const std::string& type,
                               int line, int col, bool isParam) {
    if (!scopes.empty()) {
        Symbol s;
        s.type = type; s.isDeclared = true;
        s.used = false; s.line = line; s.col = col; s.isParam = isParam;
        s.file = diagFile();
        auto ins = scopes.back().insert_or_assign(name, s);
        if (ins.second) scopeIndex[name].push_back((int)scopes.size() - 1);
    }
    // Record a hover span for the declared name (col points at the name token).
    // Parameters are excluded: the parser stamps them at the *function's*
    // position, not the parameter's, so a span there would be wrong. Parameter
    // uses inside the body still hover correctly via expression types.
    if (line > 0 && !isParam && inPrimaryFile()) {
        std::string disp = type;
        if (disp.rfind("struct:", 0) == 0) disp = disp.substr(7);  // display "Point", not "struct:Point"
        hoverSyms.push_back({line, col, (int)name.size(), disp});
    }
}

std::string TypeChecker::lookupSymbol(const std::string& name) {
    // The innermost scope that defines it
    int si = scopeOf(name);
    if (si < 0) return "";
    Symbol& sym = scopes[si].find(name)->second;
    sym.used = true;  // -Wall: mark referenced
    return sym.type;
}

void TypeChecker::defineFunction(const std::string& name, const std::string& returnType,
                                  const std::vector<std::string>& paramTypes) {
    functionSignatures[name] = {returnType, paramTypes};
}

// Error reporting
void TypeChecker::error(int line, int col, const std::string& message) {
    hasErrors = true;
    std::stringstream ss;
    ss << diagFile() << ":" << line << ":" << col << ": " << message;
    if (inInstance) ss << " (in instantiation of " << instContext << ")";
    errors.push_back(ss.str());
}

void TypeChecker::warning(int line, int col, const std::string& message) {
    if (inInstance) return;   // a generic body is linted once, not once per instance
    std::stringstream ss;
    ss << diagFile() << ":" << line << ":" << col << ": warning: " << message;
    std::cerr << ss.str() << "\n";
}

void TypeChecker::warnAssignInCondition(Expr* cond) {
    if (!warnAll) return;
    if (auto* b = dynamic_cast<BinaryExpr*>(cond); b && b->op == "=")
        warning(b->line, b->col,
                "assignment used as a condition — did you mean '=='?");
}

void TypeChecker::checkCondition(ASTNode* node, Expr* cond) {
    cond->accept(this);
    std::string condType = getExpressionType(cond);
    if (condType != "unknown" && condType != "bool" && !isNumericType(condType))
        errorAt(node, "condition must be boolean or numeric, got " + condType);
}
