#include "type_checker.h"
#include <functional>
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

// TypeChecker — declaration visitors (functions, structs, enums, unions,
// interfaces, type aliases) + the template capture pass.
// Part of the type_checker.cpp split; all methods are TypeChecker members
// declared in type_checker.h.

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
        if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) { walkStmt(dw->body.get()); walkExpr(dw->condition.get()); return; }
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

// --- Definite-return analysis --------------------------------------------
// A non-void function must return (or throw, or provably diverge) on every
// path; falling off the end is an error, not an implicit zero return.

bool stmtAlwaysReturns(Stmt* s);

// Is `cond` a literal that is always true? (`while(1)`, `while(true)`, or a
// `for(;;)` whose condition is null.)
bool condAlwaysTrue(Expr* cond) {
    if (!cond) return true;  // for(;;)
    auto* lit = dynamic_cast<LiteralExpr*>(cond);
    if (!lit) return false;
    if (lit->kind == LiteralExpr::Kind::BOOL) return lit->value == "true";
    if (lit->kind == LiteralExpr::Kind::INT)  return lit->value != "0";
    return false;
}

// Does `s` contain a `break` that would exit the *enclosing* loop, i.e. a
// break not swallowed by a nested loop or switch? Used to tell whether an
// otherwise-infinite loop can still fall through.
bool hasBreakAtThisLevel(Stmt* s) {
    if (!s) return false;
    if (dynamic_cast<BreakStmt*>(s)) return true;
    if (auto* b = dynamic_cast<BlockStmt*>(s)) {
        for (auto& it : b->items)
            if (std::holds_alternative<StmtPtr>(it) &&
                hasBreakAtThisLevel(std::get<StmtPtr>(it).get())) return true;
        return false;
    }
    if (auto* i = dynamic_cast<IfStmt*>(s))
        return hasBreakAtThisLevel(i->thenBranch.get()) ||
               hasBreakAtThisLevel(i->elseBranch.get());
    // Nested loops and switch capture their own `break` — do not descend.
    if (dynamic_cast<WhileStmt*>(s) || dynamic_cast<ForStmt*>(s) ||
        dynamic_cast<DoWhileStmt*>(s) ||
        dynamic_cast<ForInStmt*>(s) || dynamic_cast<SwitchStmt*>(s)) return false;
    if (auto* m = dynamic_cast<MatchStmt*>(s)) {   // match is not a loop
        for (auto& arm : m->arms)
            if (hasBreakAtThisLevel(arm.body.get())) return true;
        return false;
    }
    if (auto* t = dynamic_cast<TryStmt*>(s)) {
        if (hasBreakAtThisLevel(t->body.get())) return true;
        for (auto& c : t->catches)
            if (hasBreakAtThisLevel(c.body.get())) return true;
        return hasBreakAtThisLevel(t->finally.get());
    }
    return false;
}

// Does executing `s` guarantee control does not fall through to the following
// statement (it returns, throws, or provably diverges)? Conservative: any case
// it cannot prove returns false, which at worst asks for an explicit return.
bool stmtAlwaysReturns(Stmt* s) {
    if (!s) return false;
    if (dynamic_cast<ReturnStmt*>(s)) return true;
    if (dynamic_cast<ThrowStmt*>(s))  return true;
    if (auto* b = dynamic_cast<BlockStmt*>(s)) {
        for (auto& it : b->items)
            if (std::holds_alternative<StmtPtr>(it) &&
                stmtAlwaysReturns(std::get<StmtPtr>(it).get())) return true;
        return false;
    }
    if (auto* i = dynamic_cast<IfStmt*>(s))
        return i->elseBranch && stmtAlwaysReturns(i->thenBranch.get()) &&
               stmtAlwaysReturns(i->elseBranch.get());
    if (auto* w = dynamic_cast<WhileStmt*>(s))
        return condAlwaysTrue(w->condition.get()) && !hasBreakAtThisLevel(w->body.get());
    if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) {
        // The body runs at least once: if it always returns, so does the loop.
        if (stmtAlwaysReturns(dw->body.get())) return true;
        return condAlwaysTrue(dw->condition.get()) && !hasBreakAtThisLevel(dw->body.get());
    }
    if (auto* f = dynamic_cast<ForStmt*>(s))
        return condAlwaysTrue(f->condition.get()) && !hasBreakAtThisLevel(f->body.get());
    // for-in iterates a possibly-empty collection: never guarantees a return.
    if (dynamic_cast<ForInStmt*>(s)) return false;
    if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
        bool hasDefault = false;
        for (auto& c : sw->cases) {
            if (!c.value) hasDefault = true;
            bool caseReturns = false;
            for (auto& st : c.stmts) if (stmtAlwaysReturns(st.get())) { caseReturns = true; break; }
            if (!caseReturns) return false;
        }
        return hasDefault;
    }
    // A `match` is verified exhaustive earlier, so it always returns iff every
    // arm does.
    if (auto* m = dynamic_cast<MatchStmt*>(s)) {
        if (m->arms.empty()) return false;
        for (auto& arm : m->arms) if (!stmtAlwaysReturns(arm.body.get())) return false;
        return true;
    }
    if (auto* t = dynamic_cast<TryStmt*>(s)) {
        if (t->finally && stmtAlwaysReturns(t->finally.get())) return true;
        if (!stmtAlwaysReturns(t->body.get())) return false;
        for (auto& c : t->catches) if (!stmtAlwaysReturns(c.body.get())) return false;
        return true;
    }
    return false;
}
} // namespace

void TypeChecker::visit(FunctionDecl* node) {
    if (!node->typeParams.empty()) {
        // Template body: type-checking is deferred to instantiation, but lambda
        // captures must be resolved now (codegen has no equivalent pass). See
        // TemplateCapturePass — purely additive, type-independent.
        if (node->body) { TemplateCapturePass p; p.run(node); }
        // The definite-return check is structural (control-flow only), so it
        // applies to a template body too: a generic function declared to return
        // a value must return on every path regardless of the type argument.
        // (Instantiation reuses this body and never re-runs visit(FunctionDecl),
        // so this is the only place the template is checked.)
        if (node->body && !node->isAsync && node->returnType != "void" &&
            !stmtAlwaysReturns(node->body.get())) {
            errorAt(node, "missing return in non-void function '" + node->name +
                          "' (control can reach the end without returning a " +
                          node->returnType + ")");
        }
        return;
    }

    // `main` is the program entry point: its return value is the process exit code,
    // so it must return `int`. A `void main()` leaves the exit code as whatever garbage
    // is in the return register (undefined, and platform-dependent).
    if (node->name == "main" && normalizeType(node->returnType) != "int")
        errorAt(node, "'main' must return int (its return value is the process exit code); "
                      "got '" + node->returnType + "'");

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

    // Flag reads of uninitialized scalar locals (conservative straight-line scan).
    if (node->body && !node->isAsync)
        checkUninitPrefix(dynamic_cast<BlockStmt*>(node->body.get()));

    // A non-void function must return on every path; falling off the end is an
    // error (there is no implicit zero return). `void` may fall off; `async`
    // functions complete their future implicitly and are exempt.
    if (node->body && !node->isAsync && node->returnType != "void" &&
        !stmtAlwaysReturns(node->body.get())) {
        errorAt(node, "missing return in non-void function '" + node->name +
                      "' (control can reach the end without returning a " +
                      node->returnType + ")");
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

void TypeChecker::checkUninitPrefix(BlockStmt* body) {
    if (!body) return;
    std::set<std::string> uninit;   // scalar locals declared without init, not yet assigned
    std::function<void(Expr*)> scan = [&](Expr* e) {
        if (!e) return;
        // &x initializes x (its address may be written through) — not a read.
        if (auto* u = dynamic_cast<UnaryExpr*>(e); u && u->op == "&")
            if (auto* id = dynamic_cast<IdentExpr*>(u->operand.get())) { uninit.erase(id->name); return; }
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            if (uninit.count(id->name)) {
                errorAt(id, "use of uninitialized variable '" + id->name + "'");
                uninit.erase(id->name);   // report once
            }
            return;
        }
        astwalk::forEachChildExpr(e, [&](ExprPtr& c){ scan(c.get()); });
    };
    for (auto& item : body->items) {
        if (std::holds_alternative<DeclPtr>(item)) {
            if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get())) {
                if (vd->initializer) { scan(vd->initializer.get()); uninit.erase(vd->name); }
                else {
                    std::string t = normalizeType(vd->type);
                    // Only genuine scalars: an array (`T[N]`, incl. `*Node[3]`) ends in
                    // ']' and is excluded — element writes initialize it piecewise.
                    bool scalar = (!t.empty() && t.back() != ']') &&
                                  (isNumericType(t) || isPointerType(t) || t == "string" ||
                                   ty::Type::parse(t).isFn());
                    if (scalar) uninit.insert(vd->name);
                }
            }
            continue;
        }
        Stmt* s = std::get<StmtPtr>(item).get();
        if (auto* es = dynamic_cast<ExprStmt*>(s)) {
            if (auto* b = dynamic_cast<BinaryExpr*>(es->expr.get()); b && b->op == "=") {
                scan(b->right.get());
                if (auto* id = dynamic_cast<IdentExpr*>(b->left.get())) uninit.erase(id->name);
                else scan(b->left.get());   // e.g. `arr[x] = …` reads x
            } else scan(es->expr.get());
            continue;
        }
        if (auto* rs = dynamic_cast<ReturnStmt*>(s)) { if (rs->value) scan(rs->value.get()); continue; }
        break;   // control-flow or anything else: stop (stay conservative)
    }
}

void TypeChecker::visit(VarDecl* node) {
    // `static` is a local-only qualifier whose initializer must be a compile-time
    // constant (it runs once, at load time), as in C.
    if (node->isStatic) {
        if (scopes.size() <= 1)
            errorAt(node, "'static' is only allowed on a local variable");
        if (node->initializer && !dynamic_cast<LiteralExpr*>(node->initializer.get()))
            errorAt(node, "a 'static' local's initializer must be a constant");
    }

    // Record definition location
    if (node->line > 0)
        definitionLocations[node->name] = {node->line, node->col, sourceFile};
    if (node->initializer) {
        // Reconcile a lambda initializer's return type with a declared fn(...)->R
        // target BEFORE checking its body. A mismatched lambda header (e.g. an
        // `int(int x)` used as `fn(int)->float`) would otherwise emit a function
        // whose return type disagrees with the closure's call ABI — correct at -O0
        // by luck, a silent miscompile (0.0) under -O2. Setting the lambda's return
        // type to R lets its `return` coerce to R through the normal path.
        if (auto* lam = dynamic_cast<LambdaExpr*>(node->initializer.get())) {
            ty::Type dt = ty::Type::parse(node->type);
            if (dt.isFn() && dt.ret && dt.ret->str() != lam->returnType)
                lam->returnType = dt.ret->str();
        }
        node->initializer->accept(this);
        // Array literal `= {..}`: the target must be a fixed-size array; check the
        // element count (fewer than the size zero-fill, C-style) and element types.
        if (auto* arr = dynamic_cast<ArrayLitExpr*>(node->initializer.get())) {
            std::string t = node->type;
            size_t lb = t.rfind('[');
            if (lb == std::string::npos || t.back() != ']' || tyq::isPtr(t))
                errorAt(node, "an array literal '{...}' can only initialize an array type, not '" +
                              node->type + "'");
            else {
                std::string elemT = t.substr(0, lb);
                std::string dim = t.substr(lb + 1, t.size() - lb - 2);
                bool dimNum = !dim.empty() &&
                    std::all_of(dim.begin(), dim.end(), [](unsigned char c){ return std::isdigit(c); });
                if (dimNum && arr->elements.size() > (size_t)std::stoull(dim))
                    errorAt(node, "array literal has " + std::to_string(arr->elements.size()) +
                                  " elements but '" + node->type + "' holds " + dim);
                for (auto& el : arr->elements) {
                    std::string et = getExpressionType(el.get());
                    std::string e = assignabilityError(elemT, et, el.get());
                    if (!e.empty()) errorAt(node, "array element: " + e);
                }
            }
        } else {
            std::string initType = getExpressionType(node->initializer.get());
            if (initType != "unknown") {
                if (tyq::dropsConst(node->type, initType))
                    errorAt(node, "cannot initialize '" + node->type + "' from '" + initType +
                                  "': conversion discards a const qualifier");
                else {
                    std::string e = assignabilityError(node->type, initType, node->initializer.get());
                    if (!e.empty()) errorAt(node, "initializing '" + node->name + "': " + e);
                }
            }
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

void TypeChecker::visit(InterfaceDecl* node) {
    // Interface registered in first pass; no body to type-check
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
