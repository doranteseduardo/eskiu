#include "type_checker.h"
#include <functional>
#include <algorithm>
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
    void run(FunctionDecl* fn, const std::string& selfType = "") {
        scopes.push_back({});
        if (!selfType.empty()) define("self", selfType);
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
        astwalk::forEachChildExprFlat(e, [&](ExprPtr& c) { walkExpr(c.get()); });
    }
};

// --- Definite-return analysis --------------------------------------------
// A non-void function must return (or throw, or provably diverge) on every
// path; falling off the end is an error, not an implicit zero return. The
// analysis is "can this statement complete normally?" (Java-style): a function
// whose body can complete normally is missing a return.

bool canCompleteNormally(Stmt* s);

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

// Does `s` contain a `break` (or, with `wantContinue`, a `continue`) that transfers
// to the loop or switch enclosing `s`, whose label is `label` ("" for none, always
// "" for a switch)? `inner` is set once we are inside a nested loop/switch, where
// an unlabeled jump binds to that construct instead; a labeled jump naming `label`
// reaches us from any depth.
bool jumpsTo(Stmt* s, const std::string& label, bool wantContinue, bool inner) {
    if (!s) return false;
    if (auto* b = dynamic_cast<BreakStmt*>(s)) {
        if (wantContinue) return false;
        return b->label.empty() ? !inner : (!label.empty() && b->label == label);
    }
    if (auto* c = dynamic_cast<ContinueStmt*>(s)) {
        if (!wantContinue) return false;
        return c->label.empty() ? !inner : (!label.empty() && c->label == label);
    }
    if (auto* b = dynamic_cast<BlockStmt*>(s)) {
        for (auto& it : b->items)
            if (std::holds_alternative<StmtPtr>(it) &&
                jumpsTo(std::get<StmtPtr>(it).get(), label, wantContinue, inner)) return true;
        return false;
    }
    if (auto* i = dynamic_cast<IfStmt*>(s))
        return jumpsTo(i->thenBranch.get(), label, wantContinue, inner) ||
               jumpsTo(i->elseBranch.get(), label, wantContinue, inner);
    if (auto* w = dynamic_cast<WhileStmt*>(s))   return jumpsTo(w->body.get(), label, wantContinue, true);
    if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) return jumpsTo(dw->body.get(), label, wantContinue, true);
    if (auto* f = dynamic_cast<ForStmt*>(s))     return jumpsTo(f->body.get(), label, wantContinue, true);
    if (auto* fi = dynamic_cast<ForInStmt*>(s))  return jumpsTo(fi->body.get(), label, wantContinue, true);
    if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
        // A switch captures an unlabeled `break`, but not a `continue`.
        bool in = wantContinue ? inner : true;
        for (auto& c : sw->cases)
            for (auto& st : c.stmts) if (jumpsTo(st.get(), label, wantContinue, in)) return true;
        return false;
    }
    if (auto* m = dynamic_cast<MatchStmt*>(s)) {   // match is not a jump target
        for (auto& arm : m->arms)
            if (jumpsTo(arm.body.get(), label, wantContinue, inner)) return true;
        return false;
    }
    if (auto* t = dynamic_cast<TryStmt*>(s)) {
        if (jumpsTo(t->body.get(), label, wantContinue, inner)) return true;
        for (auto& c : t->catches)
            if (jumpsTo(c.body.get(), label, wantContinue, inner)) return true;
        return jumpsTo(t->finally.get(), label, wantContinue, inner);
    }
    return false;   // a defer body may not jump out of itself (rejected separately)
}

// Can executing `s` fall through to the following statement? Conservative in the
// safe direction: when unsure it answers "yes", which at worst asks for a return.
bool canCompleteNormally(Stmt* s) {
    if (!s) return true;
    if (dynamic_cast<ReturnStmt*>(s) || dynamic_cast<ThrowStmt*>(s) ||
        dynamic_cast<BreakStmt*>(s) || dynamic_cast<ContinueStmt*>(s)) return false;
    if (auto* b = dynamic_cast<BlockStmt*>(s)) {
        for (auto& it : b->items)
            if (std::holds_alternative<StmtPtr>(it) &&
                !canCompleteNormally(std::get<StmtPtr>(it).get())) return false;
        return true;
    }
    if (auto* i = dynamic_cast<IfStmt*>(s))
        return !i->elseBranch || canCompleteNormally(i->thenBranch.get()) ||
               canCompleteNormally(i->elseBranch.get());
    if (auto* w = dynamic_cast<WhileStmt*>(s))
        return !condAlwaysTrue(w->condition.get()) || jumpsTo(w->body.get(), w->label, false, false);
    if (auto* f = dynamic_cast<ForStmt*>(s))
        return !condAlwaysTrue(f->condition.get()) || jumpsTo(f->body.get(), f->label, false, false);
    if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) {
        // The body runs at least once; the condition is reached when the body completes
        // or `continue`s, and the loop exits there unless the condition is always true.
        if (jumpsTo(dw->body.get(), dw->label, false, false)) return true;
        bool reachesCond = canCompleteNormally(dw->body.get()) ||
                           jumpsTo(dw->body.get(), dw->label, true, false);
        return reachesCond && !condAlwaysTrue(dw->condition.get());
    }
    // for-in iterates a possibly-empty collection.
    if (dynamic_cast<ForInStmt*>(s)) return true;
    if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
        // Cases fall through in order, so without a `break` out of it the switch
        // completes only if there is no default or the last case group completes.
        bool hasDefault = false;
        for (auto& c : sw->cases) if (!c.value) hasDefault = true;
        if (!hasDefault || sw->cases.empty()) return true;
        for (auto& c : sw->cases)
            for (auto& st : c.stmts) if (jumpsTo(st.get(), "", false, false)) return true;
        for (auto& st : sw->cases.back().stmts) if (!canCompleteNormally(st.get())) return false;
        return true;
    }
    // A `match` is verified exhaustive separately; it completes if any arm does.
    if (auto* m = dynamic_cast<MatchStmt*>(s)) {
        if (m->arms.empty()) return true;
        for (auto& arm : m->arms) if (canCompleteNormally(arm.body.get())) return true;
        return false;
    }
    if (auto* t = dynamic_cast<TryStmt*>(s)) {
        if (t->finally && !canCompleteNormally(t->finally.get())) return false;
        if (canCompleteNormally(t->body.get())) return true;
        for (auto& c : t->catches) if (canCompleteNormally(c.body.get())) return true;
        return false;
    }
    return true;
}

bool stmtAlwaysReturns(Stmt* s) { return !canCompleteNormally(s); }
} // namespace

bool stmtCanCompleteNormally(Stmt* s) { return canCompleteNormally(s); }

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

    // Parameter and return types must name known types.
    std::set<std::string> paramNames;
    for (const auto& param : node->params) {
        if (param.first == "...") continue;
        if (!param.second.empty() && !paramNames.insert(param.second).second)
            errorAt(node, "duplicate parameter '" + param.second + "' in function '" + fnDisplay(node->name) + "'");
        validateStructType(normalizeType(param.first), node);
        if (isVoidValueType(param.first))
            errorAt(node, "parameter '" + param.second + "' cannot have type 'void'");
    }
    validateStructType(normalizeType(node->returnType), node);

    // Record definition location
    if (!inInstance) definitionLocations[node->name] = {node->line, node->col, diagFile()};
    // -Wall: track top-level functions for unused-function reporting (skip main).
    if (node->name != "main" && !inInstance) definedFns[node->name] = {node->line, node->col};

    currentFunctionReturnType = node->returnType;   // inner T (async body returns T)
    bool prevInAsync = inAsyncFn;
    inAsyncFn = node->isAsync;
    bool prevAwaitSeen = awaitSeenInFn;
    awaitSeenInFn = false;
    pushScope();

    // Define parameters (preserving a pointee-const qualifier so writing through
    // a `const T*` parameter is caught; normalization otherwise strips const).
    for (size_t pi = 0; pi < node->params.size(); ++pi) {
        const auto& param = node->params[pi];
        std::string pt = normalizeType(param.first);
        if (pt == "int" && plainEnumDecls.count(param.first)) pt = param.first;   // keep enum name for `match`
        if (tyq::baseConst(param.first) && tyq::isPtr(param.first)) pt = "const " + pt;
        // The parameter's own position when the parser recorded it (else the function's).
        int pl = node->line, pc = node->col;
        if (pi < node->paramPositions.size()) { pl = node->paramPositions[pi].first; pc = node->paramPositions[pi].second; }
        defineSymbol(param.second, pt, pl, pc, /*isParam=*/true);
        if (!node->body) scopes.back()[param.second].used = true;   // a prototype's names are documentation
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
    if (node->body && !inInstance && !node->isAsync && node->returnType != "void" &&
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

    // Escape soundness is enforced on non-generic functions only (as before per-instance
    // checking existed): it counts passing a closure param down to another call as an
    // escape, which generic helpers such as sort<T> rely on.
    for (size_t i = 0; i < node->params.size() && !inInstance; ++i) {
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
        // An assignment (also nested: `x = y = 3`, `let y = (x = 5)`) writes its target
        // after evaluating the value; the target itself is not read.
        if (auto* b = dynamic_cast<BinaryExpr*>(e); b && b->op == "=") {
            scan(b->right.get());
            if (auto* id = dynamic_cast<IdentExpr*>(b->left.get())) uninit.erase(id->name);
            else scan(b->left.get());   // e.g. `arr[x] = …` reads x
            return;
        }
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            if (uninit.count(id->name)) {
                errorAt(id, "use of uninitialized variable '" + id->name + "'");
                uninit.erase(id->name);   // report once
            }
            return;
        }
        astwalk::forEachChildExprFlat(e, [&](ExprPtr& c){ scan(c.get()); });
    };
    for (auto& item : body->items) {
        if (std::holds_alternative<DeclPtr>(item)) {
            if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get())) {
                if (vd->initializer) { scan(vd->initializer.get()); uninit.erase(vd->name); }
                // A `static` local without an initializer is zero (static storage, C).
                else if (vd->isStatic) uninit.erase(vd->name);
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
        if (auto* es = dynamic_cast<ExprStmt*>(s)) { scan(es->expr.get()); continue; }
        if (auto* rs = dynamic_cast<ReturnStmt*>(s)) { if (rs->value) scan(rs->value.get()); continue; }
        break;   // control-flow or anything else: stop (stay conservative)
    }
}

// Is `e` a compile-time constant initializer codegen can fold? A literal, `sizeof`, an
// enum member, a top-level `const`, the address of a global, a non-capturing lambda, and
// unary/binary/ternary/cast combinations of those, or an array/struct literal built from
// constants. A call or a read of a non-const variable is not constant (as in C), since
// it would need code to run before `main`.
bool TypeChecker::isConstInit(const ExprPtr& e) const {
    if (!e) return true;
    if (dynamic_cast<LiteralExpr*>(e.get())) return true;
    if (dynamic_cast<SizeofExpr*>(e.get())) return true;
    if (auto* lam = dynamic_cast<LambdaExpr*>(e.get())) return lam->captures.empty();
    if (auto* id = dynamic_cast<IdentExpr*>(e.get())) {
        if (enumConstants.count(id->name)) return true;
        // Only a global `const` (scopes[0]) folds; a local const may be a runtime value.
        int si = scopeOf(id->name);
        return si == 0 && scopes[0].find(id->name)->second.isConst;
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get())) {
        if (u->op == "&") {
            // The address of a global variable is a link-time constant.
            auto* id = dynamic_cast<IdentExpr*>(u->operand.get());
            if (!id || functionSignatures.count(id->name)) return false;
            return scopeOf(id->name) == 0;
        }
        return (u->op == "-" || u->op == "~" || u->op == "!" || u->op == "+") && isConstInit(u->operand);
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(e.get())) {
        static const std::set<std::string> ops = {"+","-","*","/","%","&","|","^","<<",">>",
            "==","!=","<",">","<=",">=","&&","||"};
        // Down the left spine with a loop (a long `A + B + C ...` is as deep as it is long).
        for (;;) {
            if (!ops.count(b->op) || b->opFunc.size() || !isConstInit(b->right)) return false;
            auto* l = dynamic_cast<BinaryExpr*>(b->left.get());
            if (!l) return isConstInit(b->left);
            b = l;
        }
    }
    if (auto* t = dynamic_cast<TernaryExpr*>(e.get()))
        return isConstInit(t->condition) && isConstInit(t->thenExpr) && isConstInit(t->elseExpr);
    if (auto* c = dynamic_cast<CastExpr*>(e.get())) return isConstInit(c->expr);
    if (auto* a = dynamic_cast<ArrayLitExpr*>(e.get())) {
        for (auto& el : a->elements) if (!isConstInit(el)) return false;
        return true;
    }
    if (auto* s = dynamic_cast<StructInitExpr*>(e.get())) {
        for (auto& fi : s->fieldInits) if (!isConstInit(fi.second)) return false;
        return true;
    }
    return false;
}

// The type a bare integer-literal range bound takes: the first of int, int64, uint64
// that holds its value (C's rule for an unsuffixed literal, extended to uint64).
static std::string rangeLiteralType(const std::string& v) {
    bool neg = !v.empty() && v[0] == '-';
    unsigned long long mag = 0;
    try { mag = std::stoull(neg ? v.substr(1) : v, nullptr, 0); }
    catch (...) { return "int"; }
    if (mag <= (neg ? 2147483648ULL : 2147483647ULL)) return "int";
    if (neg || mag <= 9223372036854775807ULL) return "int64";
    return "uint64";
}

void TypeChecker::visit(VarDecl* node) {
    // `extern <type> <name>;` names a variable defined in another translation unit
    // (a C global). It lives at top level and carries no initializer.
    if (node->isExtern) {
        if (scopes.size() > 1)
            errorAt(node, "'extern' is only allowed on a top-level variable");
        if (node->initializer)
            errorAt(node, "an 'extern' variable cannot have an initializer");
    }

    // `static` is a local-only qualifier whose initializer must be a compile-time
    // constant (it runs once, at load time), as in C. Accept every form codegen's
    // constant folder can emit — a literal, a numeric unary/cast of one, or an
    // array/struct literal (or sizeof) built from those — not just a bare literal.
    if (node->isStatic) {
        if (scopes.size() <= 1)
            errorAt(node, "'static' is only allowed on a local variable");
        if (node->initializer && !isConstInit(node->initializer))
            errorAt(node, "a 'static' local's initializer must be a constant");
    }

    // Record definition location
    if (node->line > 0 && !inInstance)
        definitionLocations[node->name] = {node->line, node->col, diagFile()};
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
        // A `for (i in A..B)` bound decl takes its bound's integer type (promoted to at
        // least `int`); visit(ForStmt) then widens both decls to their common type.
        bool badRangeBound = false;
        if (node->rangeBound) {
            std::string bt = getExpressionType(node->initializer.get());
            if (auto* lit = dynamic_cast<LiteralExpr*>(node->initializer.get()))
                if (lit->kind == LiteralExpr::Kind::INT) bt = rangeLiteralType(lit->value);
            if (bt != "unknown") {
                std::string nt = tyq::strip(normalizeType(tyq::strip(bt)));
                std::string rt = ty::rangeVarType(nt, "int");
                if (rt.empty()) {
                    errorAt(node->initializer.get(), "range bound must be an integer, got '" + nt + "'");
                    badRangeBound = true;
                } else {
                    node->type = rt;
                }
            }
        }
        // A global's initializer is evaluated at compile time (there is no code before
        // `main` to run it), so it must be a constant expression, as in C.
        if (scopes.size() <= 1 && !node->isStatic && !isConstInit(node->initializer))
            errorAt(node, "initializer of global '" + node->name + "' is not a compile-time constant");
        // Array literal `= {..}`: the target must be a fixed-size array; check the
        // element count (fewer than the size zero-fill, C-style) and element types.
        if (auto* arr = dynamic_cast<ArrayLitExpr*>(node->initializer.get())) {
            // Recursively check a (possibly nested) array literal against a
            // (possibly multi-dimensional) array type: `int[2][3]` expects two rows,
            // each itself a `{...}` of up to three ints. A short list zero-fills.
            std::function<void(const std::string&, ArrayLitExpr*)> checkArr =
                [&](const std::string& arrT, ArrayLitExpr* a) {
                    ty::Type at = ty::Type::parse(arrT);
                    if (at.kind != ty::Type::Kind::Array) {
                        errorAt(node, "an array literal '{...}' can only initialize an array type, not '" +
                                      arrT + "'");
                        return;
                    }
                    std::string elemT = at.elem->str();
                    const std::string& dim = at.dim;
                    bool dimNum = !dim.empty() &&
                        std::all_of(dim.begin(), dim.end(), [](unsigned char c){ return std::isdigit(c); });
                    if (dimNum && a->elements.size() > (size_t)std::stoull(dim))
                        errorAt(node, "array literal has " + std::to_string(a->elements.size()) +
                                      " elements but '" + arrT + "' holds " + dim);
                    bool elemIsArray = (at.elem->kind == ty::Type::Kind::Array);
                    for (auto& el : a->elements) {
                        if (elemIsArray) {
                            if (auto* sub = dynamic_cast<ArrayLitExpr*>(el.get()))
                                checkArr(elemT, sub);
                            else
                                errorAt(node, "nested array initializer expected '{...}' for element type '" +
                                              elemT + "'");
                        } else {
                            std::string et = getExpressionType(el.get());
                            std::string e = assignabilityError(elemT, et, el.get());
                            if (!e.empty()) errorAt(node, "array element: " + e);
                        }
                    }
                };
            checkArr(node->type, arr);
        } else if (!badRangeBound) {
            std::string initType = getExpressionType(node->initializer.get());
            if (initType != "unknown") {
                if (dropsConstQual(node->type, initType))
                    errorAt(node, "cannot initialize '" + node->type + "' from '" + initType +
                                  "': conversion discards a const qualifier");
                else {
                    std::string e = assignabilityError(node->type, initType, node->initializer.get());
                    if (!e.empty()) errorAt(node, "initializing '" + node->name + "': " + e);
                }
            }
        }
    }
    // A const must be initialized — there is no later point to assign it. An
    // `extern const` is the exception: its definition (and value) lives elsewhere.
    if (node->isConst && !node->initializer && !node->isExtern) {
        errorAt(node, "const '" + node->name + "' must be initialized");
    }

    // Normalize the type (e.g., "Point" -> "struct:Point")
    std::string normalizedType = normalizeType(node->type);
    // A classic (payload-less) enum collapses to `int` in normalizeType, but keep its
    // nominal name on the variable so `match` can recover the variant set. It still behaves
    // as an int everywhere else (every other check runs the type back through normalizeType).
    if (normalizedType == "int" && plainEnumDecls.count(node->type))
        normalizedType = node->type;

    // Validate that struct types exist before use
    validateStructType(normalizedType, node);
    if (isVoidValueType(node->type))
        errorAt(node, "variable '" + node->name + "' cannot have type 'void'");

    // Preserve a pointee-const qualifier through normalization so the symbol
    // remembers it's read-only (const checks read it back; everything else strips).
    std::string storedType = normalizedType;
    if (tyq::baseConst(node->type) && tyq::isPtr(node->type))
        storedType = "const " + normalizedType;

    // Two locals of the same name in one scope (`let x; let x;`). Globals are checked
    // once for the whole program (checkTopLevelNames); an inner block may shadow.
    if (scopes.size() > 1 && scopes.back().count(node->name))
        errorAt(node, "redefinition of '" + node->name + "' in the same scope");
    defineSymbol(node->name, storedType, node->line, node->col, /*isParam=*/false);
    if (node->isConst && !scopes.empty()) {
        scopes.back()[node->name].isConst = true;
        scopes.back()[node->name].constInit = node->initializer.get();
    }
    if (node->isStatic && !scopes.empty()) scopes.back()[node->name].isStatic = true;
}

void TypeChecker::visit(StructDecl* node) {
    defineSymbol(node->name, "struct:" + node->name);
    {
        std::set<std::string> names;
        for (const auto& f : node->fields)
            if (!names.insert(f.name).second)
                errorAt(node, "duplicate field '" + f.name + "' in struct '" + node->name + "'");
        std::set<std::string> methods;
        for (const auto& m : node->methods)
            if (auto* mf = dynamic_cast<FunctionDecl*>(m.get())) {
                if (names.count(mf->name))
                    errorAt(mf, "method '" + mf->name + "' of struct '" + node->name + "' has the same name as a field");
                if (!methods.insert(mf->name).second)
                    errorAt(mf, "duplicate method '" + mf->name + "' in struct '" + node->name + "'");
            }
    }
    // Field types must name known types (a template's fields mention its type params and
    // are checked per instantiation through the instance's type arguments instead).
    if (node->typeParams.empty()) {
        for (const auto& f : node->fields) {
            validateStructType(normalizeType(f.type), node);
            if (isVoidValueType(f.type))
                errorAt(node, "field '" + f.name + "' of '" + node->name + "' cannot have type 'void'");
            checkBitfield(node, node->name, f);
        }
        for (const auto& method : node->methods)
            if (auto func = dynamic_cast<FunctionDecl*>(method.get())) {
                for (const auto& p : func->params) {
                    validateStructType(normalizeType(p.first), func);
                    if (isVoidValueType(p.first))
                        errorAt(func, "parameter '" + p.second + "' cannot have type 'void'");
                }
                validateStructType(normalizeType(func->returnType), func);
            }
    }
    // A generic struct's methods mention its type params: each body is checked per
    // struct instance (queued when `Box<int>` is instantiated). Lambda captures are
    // resolved here, type-independently, as for a generic function.
    if (!node->typeParams.empty()) {
        std::string selfT = "*" + node->name + "<";
        for (size_t i = 0; i < node->typeParams.size(); ++i) selfT += (i ? "," : "") + node->typeParams[i];
        selfT += ">";
        for (const auto& method : node->methods)
            if (auto* func = dynamic_cast<FunctionDecl*>(method.get()); func && func->body) {
                TemplateCapturePass p;
                p.run(func, selfT);
            }
        return;
    }
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
    // Extern functions are already registered in first pass; verify the signature's types.
    for (const auto& param : node->params) {
        if (param.first == "...") continue;
        validateStructType(normalizeType(param.first), node);
        if (isVoidValueType(param.first))
            errorAt(node, "parameter '" + param.second + "' cannot have type 'void'");
    }
    validateStructType(normalizeType(node->returnType), node);
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
    definitionLocations[node->name] = {node->line, node->col, diagFile()};
}

void TypeChecker::visit(TypeAliasDecl* node) {
    // The alias was registered in the first pass; validate the underlying type.
    validateStructType(normalizeType(node->aliased), node);
}

void TypeChecker::visit(InterfaceDecl* node) {
    // Interface registered in first pass; no body to type-check
}

void TypeChecker::visit(UnionDecl* node) {
    // Registered as a struct in the first pass (so field access works and a signature
    // declared before it may name it); here only its field types are validated.
    std::set<std::string> names;
    for (const auto& f : node->fields)
        if (!names.insert(f.name).second)
            errorAt(node, "duplicate field '" + f.name + "' in union '" + node->name + "'");
    for (const auto& f : node->fields) {
        validateStructType(normalizeType(f.type), node);
        if (isVoidValueType(f.type))
            errorAt(node, "field '" + f.name + "' of '" + node->name + "' cannot have type 'void'");
    }
}

void TypeChecker::checkBitfield(ASTNode* at, const std::string& owner, const StructDecl::Field& f) {
    if (f.bitWidth <= 0) return;
    std::string t = normalizeType(f.type);
    static const std::map<std::string, int> widths = {
        {"bool", 1}, {"char", 8}, {"int8", 8}, {"uint8", 8}, {"int16", 16}, {"uint16", 16},
        {"int", 32}, {"int32", 32}, {"uint", 32}, {"uint32", 32}, {"int64", 64}, {"uint64", 64}};
    auto w = widths.find(t);
    if (w == widths.end()) {
        errorAt(at, "bitfield '" + f.name + "' of '" + owner + "' must have an integer type, got '" + f.type + "'");
        return;
    }
    if (f.bitWidth > w->second)
        errorAt(at, "bitfield '" + f.name + "' of '" + owner + "' is " + std::to_string(f.bitWidth) +
                    " bits wide, more than its type '" + f.type + "' holds (" + std::to_string(w->second) + ")");
}
