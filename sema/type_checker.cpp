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

TypeChecker::TypeChecker() {
    pushScope();  // Global scope
}

bool TypeChecker::check(Program* program) {
    hasErrors = false;
    errors.clear();

    // First pass: register all struct declarations and function signatures
    for (const auto& decl : program->declarations) {
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
        if (auto funcDecl = dynamic_cast<FunctionDecl*>(decl.get())) {
            if (!funcDecl->typeParams.empty()) {
                funcTemplateDecls[funcDecl->name] = funcDecl;
                // skip normal registration — will be registered on instantiation
                std::vector<std::string> paramTypes;
                for (const auto& p : funcDecl->params) paramTypes.push_back(p.first);
                // Don't register yet — template params are not real types
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
            defineFunction(funcDecl->name, sigRet, paramTypes);
            functionParamEscaping[funcDecl->name] = funcDecl->paramEscaping;
        } else if (auto externDecl = dynamic_cast<ExternDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : externDecl->params) {
                paramTypes.push_back(param.first);  // first = type, second = name
            }
            defineFunction(externDecl->name, externDecl->returnType, paramTypes);
            functionParamEscaping[externDecl->name] = externDecl->paramEscaping;
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

    // Second pass: type check all declarations
    for (const auto& decl : program->declarations) {
        decl->accept(this);
    }

    // -Wall: top-level functions defined but never referenced.
    if (warnAll) {
        for (const auto& [name, loc] : definedFns) {
            if (!calledFns.count(name))
                warning(loc.first, loc.second, "unused function '" + name + "'");
        }
    }

    // Report errors
    for (const auto& err : errors) {
        std::cerr << "error: " << err << "\n";
    }

    return !hasErrors;
}

std::string TypeChecker::getExpressionType(Expr* expr) {
    auto it = expressionTypes.find(expr);
    if (it != expressionTypes.end()) {
        return it->second;
    }
    return "unknown";
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
    // 1. Cursor is on a use-site: look up the symbol name from use location
    auto useit = useLocations.find({line, col});
    if (useit != useLocations.end()) {
        auto defit = definitionLocations.find(useit->second);
        if (defit != definitionLocations.end()) {
            const auto& loc = defit->second;
            return loc.file + ":" + std::to_string(loc.line) + ":" +
                   std::to_string(loc.col);
        }
    }
    // 2. Cursor may be slightly off — check nearby columns for a use on same line
    for (int dc = -8; dc <= 8; ++dc) {
        auto it2 = useLocations.find({line, col + dc});
        if (it2 != useLocations.end()) {
            auto defit = definitionLocations.find(it2->second);
            if (defit != definitionLocations.end()) {
                const auto& loc = defit->second;
                return loc.file + ":" + std::to_string(loc.line) + ":" +
                       std::to_string(loc.col);
            }
        }
    }
    // 3. Cursor is directly on the declaration itself
    for (const auto& [name, loc] : definitionLocations) {
        if (loc.line == line &&
            col >= loc.col && col < loc.col + (int)name.size())
            return loc.file + ":" + std::to_string(loc.line) + ":" +
                   std::to_string(loc.col);
    }
    return "";
}

namespace {
// Type-independent capture analysis for TEMPLATE function bodies.
//
// The normal capture detection (visit(IdentExpr)/visit(LambdaExpr)) runs as part
// of type-checking, which skips template bodies (their expressions mention the
// unresolved type parameter T). So a lambda inside a generic function would get
// an empty capture list and miscompile ("Referring to an argument in another
// function"). This pass fills that gap: a pure lexical scope + free-variable walk
// (no types resolved, no diagnostics) that records, for each lambda, the
// enclosing-scope names it references — with their SOURCE-form types (T intact).
// Codegen's getTypeFromString already substitutes typeParamOverride, so a capture
// typed `*Future<T>` becomes `*Future<int>` automatically per instantiation.
//
// Purely additive: it only writes LambdaExpr::captures, which were previously
// empty for template-body lambdas, so it cannot affect non-template code.
struct TemplateCapturePass {
    std::vector<std::map<std::string, std::string>> scopes;  // name -> source type
    struct Active { int boundary; std::map<std::string, std::string> caps; };
    std::vector<Active> lambdas;

    void define(const std::string& name, const std::string& srcType) {
        if (!scopes.empty()) scopes.back()[name] = srcType;
    }
    int defIndex(const std::string& name) {
        for (int i = (int)scopes.size() - 1; i >= 0; --i)
            if (scopes[i].count(name)) return i;
        return -1;   // not a tracked local -> a global or top-level fn (not captured)
    }
    void run(FunctionDecl* fn) {
        scopes.push_back({});
        for (auto& p : fn->params) define(p.second, p.first);  // params: (type, name)
        walkStmt(fn->body.get());
        scopes.pop_back();
    }
    void walkStmt(Stmt* s) {
        if (!s) return;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            scopes.push_back({});
            for (auto& it : b->items) {
                if (std::holds_alternative<DeclPtr>(it)) {
                    if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get())) {
                        if (vd->initializer) walkExpr(vd->initializer.get());
                        define(vd->name, vd->type);
                    }
                } else walkStmt(std::get<StmtPtr>(it).get());
            }
            scopes.pop_back(); return;
        }
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            walkExpr(i->condition.get()); walkStmt(i->thenBranch.get()); walkStmt(i->elseBranch.get()); return;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) { walkExpr(w->condition.get()); walkStmt(w->body.get()); return; }
        if (auto* f = dynamic_cast<ForStmt*>(s)) {
            scopes.push_back({});
            walkStmt(f->init.get()); walkExpr(f->condition.get()); walkExpr(f->step.get()); walkStmt(f->body.get());
            scopes.pop_back(); return;
        }
        if (auto* fi = dynamic_cast<ForInStmt*>(s)) {
            scopes.push_back({});
            walkExpr(fi->iterable.get()); define(fi->varName, "");
            walkStmt(fi->body.get());
            scopes.pop_back(); return;
        }
        if (auto* r = dynamic_cast<ReturnStmt*>(s)) { walkExpr(r->value.get()); return; }
        if (auto* es = dynamic_cast<ExprStmt*>(s)) { walkExpr(es->expr.get()); return; }
        if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
            walkExpr(sw->subject.get());
            for (auto& c : sw->cases) { walkExpr(c.value.get()); for (auto& st : c.stmts) walkStmt(st.get()); }
            return;
        }
        if (auto* th = dynamic_cast<ThrowStmt*>(s)) { walkExpr(th->value.get()); return; }
        if (auto* tr = dynamic_cast<TryStmt*>(s)) {
            walkStmt(tr->body.get());
            for (auto& cc : tr->catches) { scopes.push_back({}); define(cc.name, cc.type); walkStmt(cc.body.get()); scopes.pop_back(); }
            walkStmt(tr->finally.get()); return;
        }
        if (auto* tj = dynamic_cast<ThreadJoinStmt*>(s)) { walkExpr(tj->tid.get()); return; }
        if (auto* a = dynamic_cast<AsmStmt*>(s)) { for (auto& in : a->inputs) walkExpr(in.second.get()); return; }
        // BreakStmt / ContinueStmt: no children
    }
    void walkExpr(Expr* e) {
        if (!e) return;
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            int di = defIndex(id->name);
            if (di >= 0)
                for (auto& L : lambdas)
                    if (di < L.boundary) L.caps[id->name] = scopes[di][id->name];
            return;
        }
        if (auto* lam = dynamic_cast<LambdaExpr*>(e)) {
            lambdas.push_back({(int)scopes.size(), {}});
            scopes.push_back({});
            std::set<std::string> params;
            for (auto& p : lam->params) { define(p.second, p.first); params.insert(p.second); }
            walkStmt(lam->body.get());
            scopes.pop_back();
            Active fin = lambdas.back(); lambdas.pop_back();
            lam->captures.clear();
            for (auto& [n, t] : fin.caps)
                if (!params.count(n)) lam->captures.push_back({n, t});
            return;
        }
        // IdentExpr and LambdaExpr are handled above (capture recording / scope
        // boundary); every other expression just recurses into its children via
        // the shared enumeration, so this pass can never miss a node type.
        astwalk::forEachChildExpr(e, [&](ExprPtr& c) { walkExpr(c.get()); });
    }
};
} // namespace

void TypeChecker::visit(FunctionDecl* node) {
    if (!node->typeParams.empty()) {
        // Template body: type-checking is deferred to instantiation, but lambda
        // captures must be resolved now (codegen has no equivalent pass). See
        // TemplateCapturePass — purely additive, type-independent.
        if (node->body) { TemplateCapturePass p; p.run(node); }
        return;
    }

    // Record definition location
    definitionLocations[node->name] = {node->line, node->col, sourceFile};
    // -Wall: track top-level functions for unused-function reporting (skip main).
    if (node->name != "main") definedFns[node->name] = {node->line, node->col};

    currentFunctionReturnType = node->returnType;   // inner T (async body returns T)
    bool prevInAsync = inAsyncFn;
    inAsyncFn = node->isAsync;
    bool prevAwaitSeen = awaitSeenInFn;
    awaitSeenInFn = false;
    pushScope();

    // Define parameters (preserving a pointee-const qualifier so writing through
    // a `const T*` parameter is caught; normalization otherwise strips const).
    for (const auto& param : node->params) {
        std::string pt = normalizeType(param.first);
        if (tyq::baseConst(param.first) && tyq::isPtr(param.first)) pt = "const " + pt;
        defineSymbol(param.second, pt, node->line, node->col, /*isParam=*/true);
    }

    // Escape-soundness: a non-`escaping` closure parameter may only be *called*.
    // Any other use (returned, stored, passed as an argument, captured) lets the
    // closure outlive the call, which is unsound unless its env is heap-allocated
    // — so it must be marked `escaping`. Track such params and verify after the body.
    std::set<std::string> prevWatch = nonEscapingFnParams;
    std::set<std::string> prevEscaped = escapedFnParams;
    nonEscapingFnParams.clear();
    escapedFnParams.clear();
    for (size_t i = 0; i < node->params.size(); ++i) {
        const std::string& pty = node->params[i].first;
        bool isFn = pty.size() > 3 && pty.substr(0, 3) == "fn(";
        bool marked = i < node->paramEscaping.size() && node->paramEscaping[i];
        if (isFn && !marked) nonEscapingFnParams.insert(node->params[i].second);
    }

    // Type check body
    if (node->body) {
        node->body->accept(this);
    }

    // An `async fn` must contain at least one `await` — the state-machine
    // transform needs a suspend point. Report here, where the location is known.
    if (node->isAsync && !awaitSeenInFn) {
        errorAt(node, "async function '" + node->name + "' has no `await`; "
                      "remove `async` or add an `await`");
    }
    // A generic `async fn` is not supported: the coroutine frame is built once
    // from the body's source types, so a type parameter (`T`) would not be
    // substituted per instantiation. Reject it rather than miscompile.
    if (node->isAsync && !node->typeParams.empty()) {
        errorAt(node, "async function '" + node->name + "' cannot be generic; "
                      "write a concrete async function or await a generic helper from it");
    }

    for (size_t i = 0; i < node->params.size(); ++i) {
        if (escapedFnParams.count(node->params[i].second)) {
            errorAt(node, "closure parameter '" + node->params[i].second +
                "' escapes (used beyond a direct call); mark it `escaping`");
        }
    }
    nonEscapingFnParams = prevWatch;
    escapedFnParams = prevEscaped;

    popScope();
    currentFunctionReturnType = "";
    inAsyncFn = prevInAsync;
    awaitSeenInFn = prevAwaitSeen;
}

void TypeChecker::visit(VarDecl* node) {
    // Record definition location
    if (node->line > 0)
        definitionLocations[node->name] = {node->line, node->col, sourceFile};
    if (node->initializer) {
        node->initializer->accept(this);
        std::string initType = getExpressionType(node->initializer.get());
        if (initType != "unknown") {
            if (tyq::dropsConst(node->type, initType))
                errorAt(node, "cannot initialize '" + node->type + "' from '" + initType +
                              "' — conversion discards a const qualifier");
            else if (!isValidAssignment(node->type, initType))
                warning(0, 0, "implicit conversion from " + initType + " to " + node->type);
        }
    }
    // A const must be initialized — there is no later point to assign it.
    if (node->isConst && !node->initializer) {
        errorAt(node, "const '" + node->name + "' must be initialized");
    }

    // Normalize the type (e.g., "Point" -> "struct:Point")
    std::string normalizedType = normalizeType(node->type);

    // Validate that struct types exist before use
    validateStructType(normalizedType);

    // Preserve a pointee-const qualifier through normalization so the symbol
    // remembers it's read-only (const checks read it back; everything else strips).
    std::string storedType = normalizedType;
    if (tyq::baseConst(node->type) && tyq::isPtr(node->type))
        storedType = "const " + normalizedType;

    defineSymbol(node->name, storedType, node->line, node->col, /*isParam=*/false);
    if (node->isConst && !scopes.empty()) scopes.back()[node->name].isConst = true;
}

void TypeChecker::visit(StructDecl* node) {
    defineSymbol(node->name, "struct:" + node->name);
    // Type-check method bodies
    for (const auto& method : node->methods) {
        if (auto func = dynamic_cast<FunctionDecl*>(method.get())) {
            std::string savedReturn = currentFunctionReturnType;
            currentFunctionReturnType = func->returnType;
            pushScope();
            defineSymbol("self", "*" + node->name);
            for (const auto& p : func->params) defineSymbol(p.second, normalizeType(p.first));
            if (func->body) func->body->accept(this);
            popScope();
            currentFunctionReturnType = savedReturn;
        }
    }
}

void TypeChecker::visit(ExternDecl* node) {
    // Extern functions are already registered in first pass
    // Just verify they have valid signatures
}

void TypeChecker::visit(IntrinsicDecl* node) {
    // `intrinsic` is a compiler-provided mechanism, not a user extension point:
    // a name with no codegen lowering must be rejected here, not blow up later.
    if (!isSupportedIntrinsic(node->name)) {
        errorAt(node, "unknown intrinsic '" + node->name +
            "': the compiler provides no lowering for it. `intrinsic` cannot "
            "declare new operations — use `extern` for an external C symbol.");
    }
}

void TypeChecker::visit(EnumDecl* node) {
    // Members and the enum type were registered in the first pass.
    definitionLocations[node->name] = {node->line, node->col, sourceFile};
}

void TypeChecker::visit(TypeAliasDecl* node) {
    // The alias was registered in the first pass; validate the underlying type.
    validateStructType(normalizeType(node->aliased));
}

// Statement visitors
void TypeChecker::visit(BlockStmt* node) {
    pushScope();

    // Type check items in order, maintaining exact parse order
    // Declarations can be interleaved with statements
    for (const auto& item : node->items) {
        // Check if this item is a declaration or a statement
        if (std::holds_alternative<DeclPtr>(item)) {
            // It's a declaration
            const auto& decl = std::get<DeclPtr>(item);
            decl->accept(this);
        } else {
            // It's a statement
            const auto& stmt = std::get<StmtPtr>(item);
            stmt->accept(this);
        }
    }

    popScope();
}

void TypeChecker::visit(IfStmt* node) {
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            errorAt(node,"condition must be boolean or numeric, got " + condType);
        }
    }
    if (node->thenBranch) {
        node->thenBranch->accept(this);
    }
    if (node->elseBranch) {
        node->elseBranch->accept(this);
    }
}

void TypeChecker::visit(ForInStmt* node) {
    node->iterable->accept(this);
    std::string itType = getExpressionType(node->iterable.get());

    // Determine the element type the loop variable will bind.
    std::string elemType;
    auto lb = itType.find('[');
    if (lb != std::string::npos && !itType.empty() && itType.back() == ']') {
        elemType = normalizeType(itType.substr(0, lb));      // fixed-size array
        node->isArrayIter = true;
        node->arrayDim = itType.substr(lb + 1, itType.size() - lb - 2);
    } else {
        std::string s = itType;
        if (s.rfind("struct:", 0) == 0) s = s.substr(7);
        while (!s.empty() && s.front() == '*') s = s.substr(1);
        while (!s.empty() && s.back()  == '*') s.pop_back();
        auto it = structs.find(s);
        if (it != structs.end()) {                           // List-like struct
            bool hasSize = false; std::string dataType;
            for (const auto& f : it->second.fields) {
                if (f.name == "size") hasSize = true;
                if (f.name == "data") dataType = f.type;
            }
            if (hasSize && !dataType.empty()) {
                while (!dataType.empty() && dataType.front() == '*') dataType = dataType.substr(1);
                while (!dataType.empty() && dataType.back()  == '*') dataType.pop_back();
                elemType = normalizeType(dataType);
            }
        }
    }

    node->resolvedElemType = elemType;
    pushScope();
    if (elemType.empty()) {
        errorAt(node, "for-in expects a fixed-size array or a List-like value "
                      "(with `data` and `size` fields), got " + itType);
        defineSymbol(node->varName, "unknown");
    } else {
        defineSymbol(node->varName, elemType, node->line, node->col, /*isParam=*/false);
    }
    if (node->body) node->body->accept(this);
    popScope();
}

void TypeChecker::visit(WhileStmt* node) {
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            errorAt(node,"condition must be boolean or numeric, got " + condType);
        }
    }
    if (node->body) {
        node->body->accept(this);
    }
}

void TypeChecker::visit(ForStmt* node) {
    pushScope();

    // Type check init — if it's a BlockStmt wrapping a declaration (for-loop init
    // pattern from the parser), process items directly in the ForStmt scope so
    // the declared variable is accessible in the condition/step/body.
    if (node->init) {
        if (auto block = dynamic_cast<BlockStmt*>(node->init.get())) {
            for (const auto& item : block->items) {
                if (std::holds_alternative<DeclPtr>(item))
                    std::get<DeclPtr>(item)->accept(this);
                else
                    std::get<StmtPtr>(item)->accept(this);
            }
        } else {
            node->init->accept(this);
        }
    }

    // Type check condition
    if (node->condition) {
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            errorAt(node,"condition must be boolean or numeric, got " + condType);
        }
    }

    // Type check step
    if (node->step) {
        node->step->accept(this);
    }

    // Type check body
    if (node->body) {
        node->body->accept(this);
    }

    popScope();
}

void TypeChecker::visit(ReturnStmt* node) {
    if (node->value) {
        node->value->accept(this);
        std::string valueType = getExpressionType(node->value.get());
        if (valueType != "unknown" && !isValidAssignment(currentFunctionReturnType, valueType)) {
            errorAt(node,"return type mismatch: expected " + currentFunctionReturnType +
                        ", got " + valueType);
        }
    } else if (currentFunctionReturnType != "void") {
        errorAt(node,"return type mismatch: expected " + currentFunctionReturnType +
                    ", got void");
    }
}

void TypeChecker::visit(BreakStmt* node) {
    // Break statements are valid in loops (checked at parse time)
}

void TypeChecker::visit(ExprStmt* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
}

// Expression visitors
void TypeChecker::visit(BinaryExpr* node) {
    node->left->accept(this);
    node->right->accept(this);

    // Assigning to a `const` binding, a field/element of a const value, or
    // through a pointer-to-const (`const T*`) is an error. See assignsToConst.
    if (node->op == "=") {
        std::string cname;
        if (assignsToConst(node->left.get(), cname))
            errorAt(node, "cannot assign to read-only location '" + cname + "'");
        std::string lt = getExpressionType(node->left.get());
        std::string rt = getExpressionType(node->right.get());
        if (tyq::dropsConst(lt, rt))
            errorAt(node, "assignment discards a const qualifier ('" + rt + "' to '" + lt + "')");
    }

    std::string leftType = getExpressionType(node->left.get());
    std::string rightType = getExpressionType(node->right.get());

    if (leftType == "unknown" || rightType == "unknown") {
        expressionTypes[node] = "unknown";
        return;
    }

    // -Wextra: comparing a signed and an unsigned integer is a classic bug source
    // (the signed operand is converted to unsigned, so negatives become large).
    if (warnExtra && (node->op == "<" || node->op == ">" || node->op == "<=" ||
                      node->op == ">=" || node->op == "==" || node->op == "!=")) {
        auto isUns = [](const std::string& t) {
            return t == "uint" || t == "uint8" || t == "uint16" || t == "uint32" || t == "uint64";
        };
        auto isSgn = [](const std::string& t) {
            return t == "int" || t == "int8" || t == "int16" || t == "int32" || t == "int64";
        };
        std::string l = normalizeType(leftType), r = normalizeType(rightType);
        if ((isUns(l) && isSgn(r)) || (isSgn(l) && isUns(r)))
            warning(node->line, node->col, "comparison between signed and unsigned integers ('" +
                    leftType + "' and '" + rightType + "')");
    }

    std::string resultType = inferBinaryExprType(leftType, node->op, rightType);

    if (resultType == "error") {
        errorAt(node,"invalid operands for operator: " + leftType + " and " + rightType);
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = resultType;
    }
}

void TypeChecker::visit(QuestionExpr* node) {
    node->operand->accept(this);
    std::string opType = normalizeType(getExpressionType(node->operand.get()));
    std::string s = opType;
    if (s.rfind("struct:", 0) == 0) s = s.substr(7);

    auto it = structs.find(s);
    bool hasOk = false, hasValue = false;
    std::string valueType = "unknown";
    if (it != structs.end())
        for (const auto& f : it->second.fields) {
            if (f.name == "ok")    hasOk = true;
            if (f.name == "value") { hasValue = true; valueType = normalizeType(f.type); }
        }

    if (!hasOk || !hasValue) {
        errorAt(node, "`?` operator requires a Result-like value "
                      "(with `ok` and `value` fields), got " + opType);
        expressionTypes[node] = "unknown";
        return;
    }

    // The enclosing function must return the same Result type to propagate into.
    std::string ret = normalizeType(currentFunctionReturnType);
    if (ret.rfind("struct:", 0) == 0) ret = ret.substr(7);
    if (ret != s) {
        errorAt(node, "`?` can only be used in a function returning the same "
                      "Result type; this function returns '" +
                      currentFunctionReturnType + "'");
    }
    expressionTypes[node] = valueType;
}

void TypeChecker::visit(UnaryExpr* node) {
    node->operand->accept(this);
    std::string operandType = getExpressionType(node->operand.get());

    if (operandType == "unknown") {
        expressionTypes[node] = "unknown";
        return;
    }

    std::string resultType = inferUnaryExprType(node->op, operandType);

    if (resultType == "error") {
        errorAt(node,"invalid operand for unary operator: " + operandType);
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = resultType;
    }
}

void TypeChecker::visit(CallExpr* node) {
    // Variadic access builtins: va_start(ap) / va_end(ap) — void.
    if (auto* bid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        if ((bid->name == "va_start" || bid->name == "va_end") && lookupSymbol(bid->name).empty()) {
            for (auto& a : node->args) a->accept(this);
            expressionTypes[node] = "void";
            return;
        }
    }
    // Algebraic variant construction: `Circle(2.0)`, `Some(x)`. The callee names a
    // variant; the call yields the enum value, checking the payload field types.
    if (auto cid = dynamic_cast<IdentExpr*>(node->callee.get())) {
        auto vit = adtVariants.find(cid->name);
        if (vit != adtVariants.end() && lookupSymbol(cid->name).empty()) {
            const std::string& enumName = vit->second.first;
            const auto& payload = enumDecls[enumName]->payloads[vit->second.second];
            for (auto& a : node->args) a->accept(this);
            if (node->args.size() != payload.size())
                errorAt(node, "variant '" + cid->name + "' expects " +
                    std::to_string(payload.size()) + " argument(s), got " +
                    std::to_string(node->args.size()));
            else
                for (size_t i = 0; i < payload.size(); ++i) {
                    std::string at = getExpressionType(node->args[i].get());
                    if (at != "unknown" && !isValidAssignment(payload[i], at))
                        errorAt(node, "variant '" + cid->name + "' argument " +
                            std::to_string(i + 1) + " type mismatch");
                }
            expressionTypes[node] = enumName;
            return;
        }
        // Bare generic-variant construction with inference: `Some(5)`. Infer the
        // enum's type args by unifying the variant's payload against the arg types;
        // if they don't fully determine the type, require explicit args (turbofish).
        auto gvit = genericVariants.find(cid->name);
        if (gvit != genericVariants.end() && lookupSymbol(cid->name).empty()) {
            EnumDecl* ge = genericEnumDecls[gvit->second.first];
            const auto& payload = ge->payloads[gvit->second.second];
            for (auto& a : node->args) a->accept(this);
            std::set<std::string> tps(ge->typeParams.begin(), ge->typeParams.end());
            std::map<std::string, std::string> subs;
            for (size_t i = 0; i < payload.size() && i < node->args.size(); ++i) {
                std::string at = getExpressionType(node->args[i].get());
                if (at != "unknown" && !at.empty()) unifyTypeParam(payload[i], at, tps, subs);
            }
            bool allBound = true;
            for (const auto& tp : ge->typeParams) if (!subs.count(tp)) allBound = false;
            if (!allBound) {
                errorAt(node, "cannot infer type arguments for variant '" + cid->name +
                    "'; write them explicitly, e.g. " + cid->name + "<...>(...)");
                expressionTypes[node] = "unknown";
                return;
            }
            if (node->args.size() != payload.size())
                errorAt(node, "variant '" + cid->name + "' expects " +
                    std::to_string(payload.size()) + " argument(s), got " + std::to_string(node->args.size()));
            std::string inst = gvit->second.first + "<";
            for (size_t i = 0; i < ge->typeParams.size(); ++i) { if (i) inst += ","; inst += subs[ge->typeParams[i]]; }
            inst += ">";
            expressionTypes[node] = normalizeType(inst);
            return;
        }
    }
    // Method call: callee is MemberExpr (e.g. p.distance(q))
    if (auto member = dynamic_cast<MemberExpr*>(node->callee.get())) {
        member->base->accept(this);
        std::string baseType = getExpressionType(member->base.get());
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
        // Strip pointer decorators so *Rect and Rect both resolve to Rect_method
        while (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
        while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();

        std::string mangled = baseType + "_" + member->member;
        auto mit = functionSignatures.find(mangled);
        if (mit != functionSignatures.end()) {
            const auto& sig = mit->second;
            const auto& paramTypes = sig.second; // first param is "self"
            size_t selfSkip = 1;
            bool isVariadic = paramTypes.size() > selfSkip && paramTypes.back() == "...";
            size_t fixedCount = isVariadic ? paramTypes.size() - selfSkip - 1
                                           : paramTypes.size() - selfSkip;

            if (!isVariadic && node->args.size() != fixedCount) {
                errorAt(node,"method '" + member->member + "' expects " +
                            std::to_string(fixedCount) + " argument(s), got " +
                            std::to_string(node->args.size()));
            }
            for (size_t i = 0; i < node->args.size(); ++i) {
                node->args[i]->accept(this);
                if (i < fixedCount) {
                    std::string argType = getExpressionType(node->args[i].get());
                    size_t pi = i + selfSkip;
                    if (argType != "unknown" && !isValidAssignment(paramTypes[pi], argType)) {
                        errorAt(node,"argument " + std::to_string(i + 1) + " type mismatch");
                    }
                }
            }
            expressionTypes[node] = sig.first;
            return;
        }
        // Check if baseType is an interface — resolve via interface method list
        {
            auto ifaceIt = interfaceDecls.find(baseType);
            if (ifaceIt != interfaceDecls.end()) {
                for (const auto& sig : ifaceIt->second->methods) {
                    if (sig.name == member->member) {
                        for (auto& arg : node->args) arg->accept(this);
                        expressionTypes[node] = normalizeType(sig.returnType);
                        return;
                    }
                }
                errorAt(node, "interface '" + baseType + "' has no method '" + member->member + "'");
                expressionTypes[node] = "unknown";
                return;
            }
        }
        // Not a method — maybe a struct field holding a fn pointer: o.op(args).
        member->accept(this);
        std::string fieldTy = getExpressionType(member);
        if (fieldTy.size() > 3 && fieldTy.substr(0, 3) == "fn(") {
            for (auto& arg : node->args) arg->accept(this);
            size_t rp = fieldTy.find(")->");
            expressionTypes[node] = (rp != std::string::npos)
                ? normalizeType(fieldTy.substr(rp + 3)) : "unknown";
            return;
        }
        errorAt(node,"undefined method '" + member->member + "' on type '" + baseType + "'");
        expressionTypes[node] = "unknown";
        return;
    }

    // Regular function call
    std::string funcName;
    if (auto identExpr = dynamic_cast<IdentExpr*>(node->callee.get())) {
        funcName = identExpr->name;
        calledFns.insert(funcName);  // -Wall: mark referenced
        // Record use-site for go-to-definition
        useLocations[{identExpr->line, identExpr->col}] = funcName;
    } else {
        expressionTypes[node] = "unknown";
        return;
    }

    // Check if funcName is a variable holding a function pointer (fn(T,...)->R)
    {
        std::string varType = lookupSymbol(funcName);
        if (!varType.empty() && varType.size() > 3 && varType.substr(0, 3) == "fn(") {
            // The callee is a fn-pointer *variable*. Visit it so that, inside a
            // lambda, an outer-scope fn pointer used in callee position is
            // registered as a capture (visit(CallExpr) otherwise resolves the
            // name directly and never reaches visit(IdentExpr)).
            // calleeContext marks this occurrence as a *call* of the var, so a
            // watched closure param used here is not counted as escaping.
            { std::string prev = calleeContext; calleeContext = funcName;
              node->callee->accept(this); calleeContext = prev; }
            // Extract return type from fn(T,...)->R
            size_t rp = varType.find(")->");
            std::string retType = (rp != std::string::npos) ? varType.substr(rp + 3) : "unknown";
            for (auto& a : node->args) a->accept(this);
            expressionTypes[node] = retType;
            return;
        }
    }

    // Look up function signature
    auto it = functionSignatures.find(funcName);
    if (it == functionSignatures.end()) {
        // Template function called without explicit type arguments: infer each type
        // parameter from an argument whose parameter type is exactly that type param.
        auto tmplIt = funcTemplateDecls.find(funcName);
        if (tmplIt != funcTemplateDecls.end()) {
            FunctionDecl* fd = tmplIt->second;
            for (auto& a : node->args) a->accept(this);
            std::set<std::string> tps(fd->typeParams.begin(), fd->typeParams.end());
            std::map<std::string, std::string> subs;
            for (size_t j = 0; j < fd->params.size() && j < node->args.size(); ++j) {
                std::string at = getExpressionType(node->args[j].get());
                if (at != "unknown" && !at.empty())
                    unifyTypeParam(fd->params[j].first, at, tps, subs);
            }
            bool allBound = true;
            for (const auto& tpName : fd->typeParams)
                if (!subs.count(tpName)) { allBound = false; break; }
            if (allBound) {
                checkConstraints(node, fd->constraints, subs);
                expressionTypes[node] = normalizeType(substType(fd->returnType, subs));
                return;
            }
        }
        errorAt(node,"undefined function '" + funcName + "'");
        expressionTypes[node] = "unknown";
        return;
    }

    const auto& sig = it->second;
    const auto& expectedParamTypes = sig.second;

    bool isVariadic = !expectedParamTypes.empty() && expectedParamTypes.back() == "...";
    size_t fixedCount = isVariadic ? expectedParamTypes.size() - 1 : expectedParamTypes.size();

    // Check argument count
    if (isVariadic) {
        if (node->args.size() < fixedCount) {
            errorAt(node,"function '" + funcName + "' expects at least " +
                        std::to_string(fixedCount) + " arguments, got " +
                        std::to_string(node->args.size()));
            expressionTypes[node] = sig.first;
            return;
        }
    } else if (node->args.size() != fixedCount) {
        errorAt(node,"function '" + funcName + "' expects " +
                    std::to_string(fixedCount) + " arguments, got " +
                    std::to_string(node->args.size()));
        expressionTypes[node] = sig.first;
        return;
    }

    // Per-param escaping flags for this callee (empty if none declared).
    auto escIt = functionParamEscaping.find(funcName);
    const std::vector<bool>* escVec =
        (escIt != functionParamEscaping.end() && !escIt->second.empty())
            ? &escIt->second : nullptr;

    // Type check fixed arguments; visit (but do not type-check) variadic extras
    for (size_t i = 0; i < node->args.size(); ++i) {
        node->args[i]->accept(this);
        // Escape optimization: a lambda passed directly to a NON-escaping
        // parameter does not outlive the call (the callee may only call it —
        // enforced by the soundness check), so its env can stay on the stack.
        if (auto* lam = dynamic_cast<LambdaExpr*>(node->args[i].get())) {
            bool paramEscapes = escVec && i < escVec->size() && (*escVec)[i];
            if (!paramEscapes) lam->escapes = false;
        }
        if (i < fixedCount) {
            std::string argType = getExpressionType(node->args[i].get());
            if (argType != "unknown" && !isValidAssignment(expectedParamTypes[i], argType)) {
                errorAt(node,"argument " + std::to_string(i + 1) + " type mismatch: expected " +
                            expectedParamTypes[i] + ", got " + argType);
            }
        }
    }

    expressionTypes[node] = sig.first;
}

void TypeChecker::visit(IndexExpr* node) {
    node->base->accept(this);
    node->index->accept(this);

    std::string baseType = getExpressionType(node->base.get());
    std::string indexType = getExpressionType(node->index.get());

    if (indexType != "unknown" && !isIntType(indexType)) {
        errorAt(node,"array index must be integer, got " + indexType);
    }

    // string[i] → char
    if (baseType == "string") { expressionTypes[node] = "char"; return; }

    // Array type T[N]: element is T
    size_t lb = baseType.rfind('[');
    if (lb != std::string::npos && baseType.back() == ']') {
        expressionTypes[node] = baseType.substr(0, lb);
        return;
    }

    // Pointer: *T → T
    if (isPointerType(baseType)) {
        expressionTypes[node] = getPointeeType(baseType);
    } else {
        expressionTypes[node] = "unknown";
    }
}

void TypeChecker::visit(MemberExpr* node) {
    node->base->accept(this);

    std::string baseType = getExpressionType(node->base.get());

    // Auto-deref pointer-to-struct: *Point, struct:Point*, Point* → struct:Point
    if (hasPointerSuffix(baseType)) {
        baseType = extractBaseType(baseType);
    } else if (!baseType.empty() && baseType.front() == '*') {
        baseType = baseType.substr(1); // strip leading *
    }
    baseType = normalizeType(baseType); // "Point" → "struct:Point" if registered

    // Check if base is a struct type
    if (baseType.find("struct:") == 0) {
        // Extract struct name (remove "struct:" prefix)
        std::string structName = baseType.substr(7);  // strlen("struct:") = 7

        // Look up struct in registry
        auto it = structs.find(structName);
        if (it == structs.end()) {
            error(0, 0, "undefined struct '" + structName + "'");
            expressionTypes[node] = "unknown";
            return;
        }

        // Look for the member in struct's fields
        const auto& structInfo = it->second;
        for (const auto& field : structInfo.fields) {
            if (field.name == node->member) {
                // Found the member, return its type
                expressionTypes[node] = field.type;
                return;
            }
        }

        // Member not found in struct
        errorAt(node,"struct '" + structName + "' has no member '" + node->member + "'");
        expressionTypes[node] = "unknown";
    } else if (baseType == "unknown") {
        // Base type is unknown, can't validate member access
        expressionTypes[node] = "unknown";
    } else {
        // Base is not a struct
        errorAt(node,"cannot access member '" + node->member + "' on non-struct type '" + baseType + "'");
        expressionTypes[node] = "unknown";
    }
}

void TypeChecker::visit(CastExpr* node) {
    node->expr->accept(this);
    // Validate that struct types exist in casts
    std::string normalizedType = normalizeType(node->targetType);
    validateStructType(normalizedType);
    expressionTypes[node] = normalizedType;
}

void TypeChecker::visit(LiteralExpr* node) {
    switch (node->kind) {
        case LiteralExpr::Kind::INT:
            expressionTypes[node] = "int";
            break;
        case LiteralExpr::Kind::FLOAT:
            expressionTypes[node] = "float";
            break;
        case LiteralExpr::Kind::STRING:
            expressionTypes[node] = "string";
            break;
        case LiteralExpr::Kind::CHAR:
            expressionTypes[node] = "char";
            break;
        case LiteralExpr::Kind::BOOL:
            expressionTypes[node] = "bool";
            break;
        case LiteralExpr::Kind::NULL_VAL:
            expressionTypes[node] = "null";
            break;
        default:
            expressionTypes[node] = "unknown";
    }
}

void TypeChecker::visit(IdentExpr* node) {
    // -Wall: a function referenced as a value counts as used.
    if (functionSignatures.count(node->name)) calledFns.insert(node->name);

    // Escape soundness: a watched closure param referenced anywhere other than
    // as the immediate callee of a call escapes (see visit(FunctionDecl)).
    if (nonEscapingFnParams.count(node->name) && node->name != calleeContext)
        escapedFnParams.insert(node->name);

    std::string type = lookupSymbol(node->name);
    if (type.empty() && enumConstants.count(node->name)) {
        // Bare enum member, e.g. `Red` — an int constant.
        expressionTypes[node] = "int";
        useLocations[{node->line, node->col}] = node->name;
        return;
    }
    if (type.empty() && adtVariants.count(node->name)) {
        // Bare algebraic variant, e.g. `None` — constructs the enum value. Variants
        // with a payload must be called (`Some(x)`), handled in visit(CallExpr).
        auto& info = adtVariants[node->name];
        const auto& payload = enumDecls[info.first]->payloads[info.second];
        if (!payload.empty())
            errorAt(node, "variant '" + node->name + "' needs " +
                std::to_string(payload.size()) + " argument(s)");
        expressionTypes[node] = info.first;     // the enum value type
        return;
    }
    // A top-level function used as a value decays to a `fn(params)->ret` pointer.
    if (type.empty() && functionSignatures.count(node->name)) {
        const auto& sig = functionSignatures[node->name];   // (returnType, paramTypes)
        std::string t = "fn(";
        for (size_t i = 0; i < sig.second.size(); ++i) {
            if (i) t += ",";
            t += sig.second[i];
        }
        t += ")->" + sig.first;
        expressionTypes[node] = t;
        useLocations[{node->line, node->col}] = node->name;
        return;
    }
    if (type.empty()) {
        errorAt(node,"undefined variable '" + node->name + "'");
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = type;
    }
    // Record use-site so go-to-definition can map cursor → definition
    useLocations[{node->line, node->col}] = node->name;

    // Capture detection: inside a lambda, a name is captured when it resolves to
    // a variable in an enclosing scope (below the lambda's own scopes). We key off
    // the variable's defining scope index, NOT functionSignatures — a param or
    // local that shadows a same-named top-level function must still be captured.
    if (!captureStack.empty() && !type.empty()) {
        int boundary = captureBoundary.back();
        int defIdx = -1;
        for (int si = (int)scopes.size() - 1; si >= 0; --si) {
            if (scopes[si].count(node->name)) { defIdx = si; break; }
        }
        // Capture only enclosing-function scopes: index >= 1 (the global scope
        // at 0 is module-level and accessed directly, not captured by value)
        // and below the lambda's own boundary.
        if (defIdx >= 1 && defIdx < boundary) {
            captureStack.back()[node->name] = type;
        }
    }
}

void TypeChecker::visit(LambdaExpr* node) {
    // Build fn(T,U)->R type string
    std::string sig = "fn(";
    for (size_t i = 0; i < node->params.size(); ++i) {
        if (i > 0) sig += ",";
        sig += node->params[i].first;
    }
    sig += ")->" + node->returnType;

    // Push a capture collector before entering the lambda scope.
    // visit(IdentExpr) will add outer-scope vars to captureStack.back().
    captureStack.push_back({});
    captureBoundary.push_back((int)scopes.size());   // scopes below this are "outer"

    pushScope();
    std::string savedReturn = currentFunctionReturnType;
    currentFunctionReturnType = node->returnType;
    for (const auto& p : node->params)
        defineSymbol(p.second, normalizeType(p.first));
    // Mark param names so IdentExpr doesn't treat them as captures
    std::set<std::string> paramNames;
    for (const auto& p : node->params) paramNames.insert(p.second);

    // Store param set temporarily so IdentExpr can consult it
    // We use a simple approach: after defining params, record the scope depth
    if (node->body) node->body->accept(this);
    currentFunctionReturnType = savedReturn;
    popScope();

    // Harvest captures: only outer-scope vars, not params
    node->captures.clear();
    for (const auto& [name, type] : captureStack.back()) {
        if (!paramNames.count(name))
            node->captures.push_back({name, type});
    }
    captureStack.pop_back();
    captureBoundary.pop_back();

    expressionTypes[node] = sig;
}

void TypeChecker::visit(InterfaceDecl* node) {
    // Interface registered in first pass; no body to type-check
}

void TypeChecker::visit(ContinueStmt* node) {
    // Valid inside loops — no type checking needed
}

void TypeChecker::visit(AsmStmt* node) {
    for (auto& [constraint, expr] : node->inputs)
        if (expr) expr->accept(this);
}

void TypeChecker::visit(UnionDecl* node) {
    // Register the union as a struct in the type system so field access works.
    // All fields are registered; the codegen handles the shared-offset layout.
    StructInfo info;
    info.name = node->name;
    for (const auto& f : node->fields)
        info.fields.push_back({f.type, f.name});
    structs[node->name] = info;
}

void TypeChecker::visit(SizeofExpr* node) {
    expressionTypes[node] = "int64";
}

void TypeChecker::visit(AwaitExpr* node) {
    if (!inAsyncFn)
        errorAt(node, "await is only allowed inside an async function");
    awaitSeenInFn = true;
    node->operand->accept(this);
    std::string t = getExpressionType(node->operand.get());

    // Strip pointer decorators: the operand should be *Future<T> (or Future<T>*).
    std::string inner = t;
    while (!inner.empty() && inner.front() == '*') inner = inner.substr(1);
    while (!inner.empty() && inner.back()  == '*') inner.pop_back();

    // A `*Future<T>` reaches here in two spellings — source form (`Future<int>`,
    // from a plain function) or already-mangled (`struct:Future_int`, from a
    // template call whose return type went through normalizeType). Normalizing
    // once collapses both to `struct:Future_int` AND registers the reverse map, so
    // a single lookup recovers T regardless of how the future was produced.
    std::string base;
    std::vector<std::string> args;
    std::string norm = normalizeType(inner);
    if (norm.rfind("struct:", 0) == 0) {
        auto ti = templateInstanceArgs.find(norm.substr(7));  // "Future_int"
        if (ti != templateInstanceArgs.end()) { base = ti->second.first; args = ti->second.second; }
    }
    if (base == "Future") {
        std::string res = (args.size() == 1) ? normalizeType(args[0]) : "unknown";
        expressionTypes[node] = res;
        node->resolvedType = res;          // consumed by the async transform
    } else {
        if (t != "unknown")
            errorAt(node, "await expects a *Future<T>, got " + t);
        expressionTypes[node] = "unknown";
    }
}

void TypeChecker::visit(FreeClosureExpr* node) {
    node->closure->accept(this);
    std::string t = getExpressionType(node->closure.get());
    if (t != "unknown" && !(t.size() > 3 && t.substr(0, 3) == "fn("))
        errorAt(node, "free_closure expects a closure (fn(...)->R), got " + t);
    expressionTypes[node] = "void";
}

void TypeChecker::visit(ThreadCreateExpr* node) {
    node->worker->accept(this);
    expressionTypes[node] = "*void";
}

void TypeChecker::visit(ThreadJoinStmt* node) {
    node->tid->accept(this);
}

void TypeChecker::visit(ThrowStmt* node) {
    if (node->value) {
        node->value->accept(this);
        node->valueType = getExpressionType(node->value.get());
    }
}

void TypeChecker::visit(TryStmt* node) {
    if (node->body) node->body->accept(this);
    for (auto& c : node->catches) {
        pushScope();
        defineSymbol(c.name, c.type);
        if (c.body) c.body->accept(this);
        popScope();
    }
    if (node->finally) node->finally->accept(this);
}

void TypeChecker::visit(MatchStmt* node) {
    node->subject->accept(this);
    std::string st = normalizeType(getExpressionType(node->subject.get()));
    // Resolve the enum decl + (for a generic instance like Option_int) the type-
    // parameter substitutions, so payload types come out concrete.
    EnumDecl* ed = nullptr;
    std::map<std::string, std::string> subs;
    if (enumDecls.count(st)) {
        ed = enumDecls[st];                                  // concrete ADT enum
    } else if (templateInstanceArgs.count(st)) {             // generic instance
        auto& inst = templateInstanceArgs[st];
        auto git = genericEnumDecls.find(inst.first);
        if (git != genericEnumDecls.end()) {
            ed = git->second;
            for (size_t i = 0; i < ed->typeParams.size() && i < inst.second.size(); ++i)
                subs[ed->typeParams[i]] = inst.second[i];
        }
    }
    if (!ed && st != "unknown")
        errorAt(node, "match subject must be an algebraic enum, got " + st);
    node->enumName = st;
    // Index of a variant within `ed` by name (-1 if absent).
    auto variantIndex = [&](const std::string& v) -> int {
        if (!ed) return -1;
        for (size_t i = 0; i < ed->members.size(); ++i)
            if (ed->members[i].first == v) return (int)i;
        return -1;
    };
    bool hasDefault = false;
    std::set<std::string> covered;
    for (size_t ai = 0; ai < node->arms.size(); ++ai) {
        auto& arm = node->arms[ai];
        if (arm.variant.empty()) {
            hasDefault = true;
            if (ai + 1 < node->arms.size())   // arms after `_` can never match
                warning(node->line, node->col, "match arms after the `_` default are unreachable");
        }
        else if (!covered.insert(arm.variant).second)
            errorAt(node, "duplicate match arm for variant '" + arm.variant + "'");
        pushScope();
        if (!arm.variant.empty() && ed) {
            int vi = variantIndex(arm.variant);
            if (vi < 0) {
                errorAt(node, "'" + arm.variant + "' is not a variant of " + st);
            } else {
                const auto& payload = ed->payloads[vi];
                if (arm.bindings.size() != payload.size())
                    errorAt(node, "variant '" + arm.variant + "' binds " +
                        std::to_string(payload.size()) + " field(s), got " +
                        std::to_string(arm.bindings.size()));
                for (size_t i = 0; i < arm.bindings.size() && i < payload.size(); ++i)
                    defineSymbol(arm.bindings[i], normalizeType(substType(payload[i], subs)),
                                 node->line, node->col, /*isParam=*/false);
            }
        }
        if (arm.body) arm.body->accept(this);
        popScope();
    }
    // Exhaustiveness: without a `_` default, every variant must be covered.
    if (ed && !hasDefault) {
        std::string missing;
        for (const auto& m : ed->members)
            if (!covered.count(m.first)) missing += (missing.empty() ? "" : ", ") + m.first;
        if (!missing.empty())
            errorAt(node, "non-exhaustive match on " + st + ": missing " + missing +
                          " (add those arms or a `_` default)");
    }
}

void TypeChecker::visit(SwitchStmt* node) {
    node->subject->accept(this);
    std::string subjType = getExpressionType(node->subject.get());
    if (subjType != "unknown" && !isIntType(subjType))
        errorAt(node,"switch subject must be integer type, got " + subjType);
    std::set<long long> seenCases;   // detect duplicate case values (else codegen
                                     // emits a switch the IR verifier rejects)
    for (auto& c : node->cases) {
        if (c.value) {
            c.value->accept(this);
            std::string caseType = getExpressionType(c.value.get());
            if (caseType != "unknown" && subjType != "unknown") {
                if (!isValidAssignment(subjType, caseType) &&
                    !(isIntType(subjType) && isIntType(caseType))) {
                    errorAt(c.value.get(),
                        "case value type '" + caseType +
                        "' is incompatible with switch subject type '" + subjType + "'");
                }
            }
            // Constant-fold integer literals and enum constants to catch dupes.
            long long cv = 0;  bool haveCv = false;
            if (auto* lit = dynamic_cast<LiteralExpr*>(c.value.get())) {
                if (lit->kind == LiteralExpr::Kind::INT) {
                    try { cv = std::stoll(lit->value, nullptr, 0); haveCv = true; }
                    catch (...) {}
                }
            } else if (auto* id = dynamic_cast<IdentExpr*>(c.value.get())) {
                auto eit = enumConstants.find(id->name);
                if (eit != enumConstants.end()) { cv = eit->second; haveCv = true; }
            }
            if (haveCv) {
                if (seenCases.count(cv))
                    errorAt(c.value.get(), "duplicate case value in switch");
                else
                    seenCases.insert(cv);
            }
        }
        for (auto& s : c.stmts) s->accept(this);
    }
}

void TypeChecker::visit(TemplateCallExpr* node) {
    // Variadic access: va_arg<T>(ap) yields the next argument as T.
    if (node->templateName == "va_arg" && node->typeArgs.size() == 1) {
        for (auto& a : node->args) a->accept(this);
        expressionTypes[node] = normalizeType(node->typeArgs[0]);
        return;
    }
    // Generic algebraic-variant construction with explicit type args:
    // `Some<int>(5)`, `Left<int,string>(x)`, `None<int>()`.
    auto gv = genericVariants.find(node->templateName);
    if (gv != genericVariants.end()) {
        EnumDecl* ge = genericEnumDecls[gv->second.first];
        std::map<std::string, std::string> subs;
        for (size_t i = 0; i < ge->typeParams.size() && i < node->typeArgs.size(); ++i)
            subs[ge->typeParams[i]] = node->typeArgs[i];
        const auto& payload = ge->payloads[gv->second.second];
        for (auto& a : node->args) a->accept(this);
        if (node->args.size() != payload.size())
            errorAt(node, "variant '" + node->templateName + "' expects " +
                std::to_string(payload.size()) + " argument(s), got " + std::to_string(node->args.size()));
        else
            for (size_t i = 0; i < payload.size(); ++i) {
                std::string want = normalizeType(substType(payload[i], subs));
                std::string at = getExpressionType(node->args[i].get());
                if (at != "unknown" && !isValidAssignment(want, at))
                    errorAt(node, "variant '" + node->templateName + "' argument " +
                        std::to_string(i + 1) + " type mismatch");
            }
        // Build the instance type name (Option<int>) and normalize -> Option_int.
        std::string inst = gv->second.first + "<";
        for (size_t i = 0; i < node->typeArgs.size(); ++i) { if (i) inst += ","; inst += node->typeArgs[i]; }
        inst += ">";
        expressionTypes[node] = normalizeType(inst);
        return;
    }
    auto templ = funcTemplateDecls.find(node->templateName);
    if (templ == funcTemplateDecls.end()) {
        errorAt(node,"undefined template function '" + node->templateName + "'");
        expressionTypes[node] = "unknown";
        return;
    }
    FunctionDecl* fd = templ->second;
    auto& tp = fd->typeParams;
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < node->typeArgs.size(); ++i)
        subs[tp[i]] = node->typeArgs[i];

    checkConstraints(node, fd->constraints, subs);

    // Type-check arguments
    for (size_t i = 0; i < node->args.size() && i < fd->params.size(); ++i) {
        node->args[i]->accept(this);
        std::string expected = substType(fd->params[i].first, subs);
        std::string got      = getExpressionType(node->args[i].get());
        if (got != "unknown" && !isValidAssignment(expected, got))
            errorAt(node,"argument " + std::to_string(i+1) + ": expected " + expected + ", got " + got);
    }

    std::string retType = normalizeType(substType(fd->returnType, subs));
    expressionTypes[node] = retType;
}

void TypeChecker::visit(AllocWithExpr* node) {
    node->allocator->accept(this);
    node->count->accept(this);
    std::string countType = getExpressionType(node->count.get());
    if (countType != "unknown" && !isIntType(countType))
        errorAt(node,"alloc_with count must be integer, got " + countType);
    expressionTypes[node] = "*" + node->elemType;
}

void TypeChecker::visit(StructInitExpr* node) {
    // A template literal (Pair<int,float> { ... }) names an instantiation; run it
    // through normalizeType so the concrete struct gets registered, then resolve.
    std::string sname = node->structName;
    if (sname.find('<') != std::string::npos) {
        std::string norm = normalizeType(sname);
        if (norm.rfind("struct:", 0) == 0) sname = norm.substr(7);
    }
    auto it = structs.find(sname);
    if (it == structs.end()) {
        errorAt(node,"undefined struct '" + node->structName + "'");
        expressionTypes[node] = "unknown";
        return;
    }

    const auto& fields = it->second.fields;
    bool named = !node->fieldInits.empty() && !node->fieldInits[0].first.empty();

    for (size_t i = 0; i < node->fieldInits.size(); ++i) {
        const auto& [fname, expr] = node->fieldInits[i];
        expr->accept(this);

        std::string fieldType;
        if (named) {
            for (const auto& f : fields) {
                if (f.name == fname) { fieldType = f.type; break; }
            }
            if (fieldType.empty())
                errorAt(node,"struct '" + node->structName + "' has no field '" + fname + "'");
        } else if (i < fields.size()) {
            fieldType = fields[i].type;
        }

        if (!fieldType.empty()) {
            std::string valType = getExpressionType(expr.get());
            if (valType != "unknown" && !isValidAssignment(fieldType, valType))
                warning(0, 0, "field '" + (named ? fname : fields[i].name) +
                              "': implicit conversion from " + valType + " to " + fieldType);
        }
    }

    expressionTypes[node] = "struct:" + sname;
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
    scopes.pop_back();
}

bool TypeChecker::isConstSymbol(const std::string& name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second.isConst;
    }
    return false;
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
        scopes.back()[name] = s;
    }
    // Record a hover span for the declared name (col points at the name token).
    // Parameters are excluded: the parser stamps them at the *function's*
    // position, not the parameter's, so a span there would be wrong. Parameter
    // uses inside the body still hover correctly via expression types.
    if (line > 0 && !isParam) {
        std::string disp = type;
        if (disp.rfind("struct:", 0) == 0) disp = disp.substr(7);  // display "Point", not "struct:Point"
        hoverSyms.push_back({line, col, (int)name.size(), disp});
    }
}

std::string TypeChecker::lookupSymbol(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto sym = it->find(name);
        if (sym != it->end()) {
            sym->second.used = true;  // -Wall: mark referenced
            return sym->second.type;
        }
    }
    return "";
}

void TypeChecker::defineFunction(const std::string& name, const std::string& returnType,
                                  const std::vector<std::string>& paramTypes) {
    functionSignatures[name] = {returnType, paramTypes};
}

// Type inference
std::string TypeChecker::inferBinaryExprType(const std::string& leftType, const std::string& op,
                                             const std::string& rightType) {
    if (op == "=") {
        return isValidAssignment(leftType, rightType) ? leftType : "error";
    }
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        return "bool";
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
    // Strip a fixed-size array suffix (T[N]) — the element type is what matters
    // here; the dimension (a literal, enum, or const) is resolved in codegen.
    if (!baseType.empty() && baseType.back() == ']') {
        size_t lb = baseType.rfind('[');
        if (lb != std::string::npos) baseType = baseType.substr(0, lb);
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
        std::string mangled = structName + "_" + method.name;
        if (funcs.find(mangled) == funcs.end()) return false;
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
        // Normalize first (List<int> → struct:List_int), then strip the
        // struct:/pointer decoration to the bare name used in method mangling.
        std::string bare = normalizeType(concrete);
        if (bare.size() > 7 && bare.compare(0, 7, "struct:") == 0) bare = bare.substr(7);
        while (!bare.empty() && bare.front() == '*') bare = bare.substr(1);
        while (!bare.empty() && bare.back() == '*') bare.pop_back();
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
    if (isNumericType(lhs) && isNumericType(rhs)) return true;
    if (lhs == "null" || rhs == "null") return isPointerType(lhs) || isPointerType(rhs);
    if (isPointerType(lhs) && isPointerType(rhs)) return true;

    // Interface satisfaction: assigning a struct to an interface type
    auto ifaceIt = interfaceDecls.find(lhs);
    if (ifaceIt != interfaceDecls.end()) {
        std::string structName = rhs;
        // Strip leading * and struct: prefix to get bare struct name
        if (!structName.empty() && structName.front() == '*') structName = structName.substr(1);
        if (structName.size() > 7 && structName.substr(0, 7) == "struct:") structName = structName.substr(7);
        while (!structName.empty() && structName.back() == '*') structName.pop_back();
        if (structSatisfiesInterface(functionSignatures, structName, ifaceIt->second))
            return true;
    }

    return false;
}

bool TypeChecker::isNumericType(const std::string& type) {
    return isIntType(type) || isFloatType(type);
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

// Error reporting
void TypeChecker::error(int line, int col, const std::string& message) {
    hasErrors = true;
    std::stringstream ss;
    ss << sourceFile << ":" << line << ":" << col << ": " << message;
    errors.push_back(ss.str());
}

void TypeChecker::warning(int line, int col, const std::string& message) {
    std::stringstream ss;
    ss << sourceFile << ":" << line << ":" << col << ": warning: " << message;
    std::cerr << ss.str() << "\n";
}

void TypeChecker::warnAssignInCondition(Expr* cond) {
    if (!warnAll) return;
    if (auto* b = dynamic_cast<BinaryExpr*>(cond); b && b->op == "=")
        warning(b->line, b->col,
                "assignment used as a condition — did you mean '=='?");
}
