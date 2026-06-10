#include "async_transform.h"
#include <stdexcept>
#include <set>
#include <memory>

// ── Small AST builders ───────────────────────────────────────────────────────
namespace {

ExprPtr ident(const std::string& n) { return std::make_shared<IdentExpr>(n); }
ExprPtr intlit(long long v) {
    return std::make_shared<LiteralExpr>(LiteralExpr::Kind::INT, std::to_string(v));
}
// fr.<field>
ExprPtr fr(const std::string& field) {
    return std::make_shared<MemberExpr>(ident("fr"), field);
}
ExprPtr binop(ExprPtr l, const std::string& op, ExprPtr r) {
    return std::make_shared<BinaryExpr>(std::move(l), op, std::move(r));
}
StmtPtr exprStmt(ExprPtr e)  { return std::make_shared<ExprStmt>(std::move(e)); }
StmtPtr assign(ExprPtr lhs, ExprPtr rhs) { return exprStmt(binop(std::move(lhs), "=", std::move(rhs))); }
StmtPtr ret(ExprPtr v) { return std::make_shared<ReturnStmt>(std::move(v)); }

bool isFuturePtr(const std::string& t) {
    std::string s = t;
    while (!s.empty() && s.front() == '*') s = s.substr(1);
    while (!s.empty() && s.back()  == '*') s.pop_back();
    return s.rfind("Future<", 0) == 0;
}

// Recursively rewrite references to frame variables (params + body locals) into
// `fr.<name>` member accesses, in place.
void rewrite(ExprPtr& e, const std::set<std::string>& vars) {
    if (!e) return;
    if (auto* id = dynamic_cast<IdentExpr*>(e.get())) {
        if (vars.count(id->name)) e = fr(id->name);
        return;
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(e.get())) { rewrite(b->left, vars); rewrite(b->right, vars); return; }
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get()))  { rewrite(u->operand, vars); return; }
    if (auto* m = dynamic_cast<MemberExpr*>(e.get())) { rewrite(m->base, vars); return; }
    if (auto* ix = dynamic_cast<IndexExpr*>(e.get())) { rewrite(ix->base, vars); rewrite(ix->index, vars); return; }
    if (auto* c = dynamic_cast<CastExpr*>(e.get()))   { rewrite(c->expr, vars); return; }
    if (auto* q = dynamic_cast<QuestionExpr*>(e.get())) { rewrite(q->operand, vars); return; }
    if (auto* a = dynamic_cast<AwaitExpr*>(e.get()))  { rewrite(a->operand, vars); return; }
    if (auto* call = dynamic_cast<CallExpr*>(e.get())) {
        rewrite(call->callee, vars);
        for (auto& arg : call->args) rewrite(arg, vars);
        return;
    }
    if (auto* tc = dynamic_cast<TemplateCallExpr*>(e.get())) {
        for (auto& arg : tc->args) rewrite(arg, vars);
        return;
    }
    // Literals and other leaf/unsupported nodes: nothing to rewrite.
}

// The closure  void() { fr.st = <state>; __<name>_resume(fr); }  used as a waker.
// Captures the frame pointer `fr` by value. Because this AST is synthesized after
// the type checker runs, we populate `captures` ourselves (sema would otherwise).
ExprPtr resumeWaker(const std::string& resumeName, int state, const std::string& framePtrTy) {
    std::vector<BlockItem> body;
    body.push_back(assign(fr("st"), intlit(state)));
    body.push_back(exprStmt(std::make_shared<CallExpr>(
        ident(resumeName), std::vector<ExprPtr>{ ident("fr") })));
    auto blk = std::make_shared<BlockStmt>(body);
    auto lam = std::make_shared<LambdaExpr>(
        std::vector<std::pair<std::string,std::string>>{}, "void", blk);
    lam->captures.push_back({"fr", framePtrTy});
    return lam;
}

} // namespace

void AsyncTransform::run(Program* program) {
    std::vector<DeclPtr> out;

    for (auto& decl : program->declarations) {
        auto* fn = dynamic_cast<FunctionDecl*>(decl.get());
        if (!fn || !fn->isAsync) { out.push_back(decl); continue; }

        const std::string name   = fn->name;
        const std::string frameT = "__" + name + "_frame";
        const std::string resumeN = "__" + name + "_resume";
        const std::string T      = fn->returnType;           // inner result type
        if (T == "void")
            throw std::runtime_error("async function '" + name +
                "': async void is not supported yet");

        // Flatten the body's top-level statements.
        auto* block = dynamic_cast<BlockStmt*>(fn->body.get());
        if (!block)
            throw std::runtime_error("async function '" + name + "': missing body");

        // Find the single await, bound in a `let x = await CALL(...)`.
        int awaitIdx = -1;
        VarDecl*  awaitVar = nullptr;
        AwaitExpr* awaitExpr = nullptr;
        for (size_t i = 0; i < block->items.size(); ++i) {
            if (!std::holds_alternative<DeclPtr>(block->items[i])) continue;
            auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(block->items[i]).get());
            if (vd && vd->initializer) {
                if (auto* aw = dynamic_cast<AwaitExpr*>(vd->initializer.get())) {
                    if (awaitIdx != -1)
                        throw std::runtime_error("async function '" + name +
                            "': only one await is supported in this version");
                    awaitIdx = (int)i; awaitVar = vd; awaitExpr = aw;
                }
            }
        }
        if (awaitIdx == -1)
            throw std::runtime_error("async function '" + name +
                "': expected exactly one `let x = await ...;`");

        const std::string Tp = awaitVar->type;               // awaited inner type T'
        const std::string awCallType = "*Future<" + Tp + ">";

        // ── Frame variables: params + every body-declared local ──────────────
        std::set<std::string> vars;
        std::vector<StructDecl::Field> fields;
        fields.push_back({"Future<" + T + ">", "ret"});
        fields.push_back({"int", "st"});
        fields.push_back({"FutureHdr*", "awaiting"});
        fields.push_back({awCallType, "__aw0"});
        for (const auto& p : fn->params) {
            vars.insert(p.second);
            fields.push_back({p.first, p.second});
        }
        for (auto& item : block->items) {
            if (!std::holds_alternative<DeclPtr>(item)) continue;
            if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get())) {
                vars.insert(vd->name);
                fields.push_back({vd->type, vd->name});
            }
        }

        // ── State 0: prefix statements + the await park ──────────────────────
        std::vector<BlockItem> s0;
        for (int i = 0; i < awaitIdx; ++i) {
            // (v1: prefix statements must be plain `let local = E;` or expr stmts)
            auto& item = block->items[i];
            if (std::holds_alternative<DeclPtr>(item)) {
                auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get());
                if (vd && vd->initializer) {
                    rewrite(vd->initializer, vars);
                    s0.push_back(assign(fr(vd->name), vd->initializer));
                }
            } else {
                auto stmt = std::get<StmtPtr>(item);
                if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) { rewrite(es->expr, vars); s0.push_back(stmt); }
            }
        }
        // fr.__aw0 = <await call>;
        ExprPtr callE = awaitExpr->operand;
        rewrite(callE, vars);
        s0.push_back(assign(fr("__aw0"), callE));
        // if (future_poll<T'>(fr.__aw0, waker) == 0) { fr.awaiting = (FutureHdr*)fr.__aw0; return; }
        ExprPtr pollCall = std::make_shared<TemplateCallExpr>(
            "future_poll", std::vector<std::string>{Tp},
            std::vector<ExprPtr>{ fr("__aw0"), resumeWaker(resumeN, 1, "*" + frameT) });
        std::vector<BlockItem> parkBody;
        parkBody.push_back(assign(fr("awaiting"),
            std::make_shared<CastExpr>("*FutureHdr", fr("__aw0"))));
        parkBody.push_back(ret(nullptr));
        StmtPtr parkIf = std::make_shared<IfStmt>(
            binop(pollCall, "==", intlit(0)), std::make_shared<BlockStmt>(parkBody));
        s0.push_back(parkIf);
        s0.push_back(assign(fr("st"), intlit(1)));           // ready: advance to state 1

        // ── State 1: extract value, suffix statements, complete ──────────────
        std::vector<BlockItem> s1;
        // fr.<x> = fr.__aw0.value;  free_future<T'>(fr.__aw0);
        s1.push_back(assign(fr(awaitVar->name),
            std::make_shared<MemberExpr>(fr("__aw0"), "value")));
        s1.push_back(exprStmt(std::make_shared<TemplateCallExpr>(
            "free_future", std::vector<std::string>{Tp}, std::vector<ExprPtr>{ fr("__aw0") })));
        for (size_t i = awaitIdx + 1; i < block->items.size(); ++i) {
            auto& item = block->items[i];
            if (std::holds_alternative<DeclPtr>(item)) {
                auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get());
                if (vd && vd->initializer) { rewrite(vd->initializer, vars); s1.push_back(assign(fr(vd->name), vd->initializer)); }
            } else {
                auto stmt = std::get<StmtPtr>(item);
                if (auto* rs = dynamic_cast<ReturnStmt*>(stmt.get())) {
                    // return E;  ==>  complete the future, then return.
                    ExprPtr v = rs->value;
                    rewrite(v, vars);
                    s1.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "value"), v));
                    // if (atomic_swap(&fr.ret.state, 2) == 1) { fr.ret.waker(); }
                    ExprPtr swapCall = std::make_shared<CallExpr>(ident("atomic_swap"),
                        std::vector<ExprPtr>{
                            std::make_shared<UnaryExpr>("&",
                                std::make_shared<MemberExpr>(fr("ret"), "state")),
                            intlit(2) });
                    std::vector<BlockItem> wk;
                    wk.push_back(exprStmt(std::make_shared<CallExpr>(
                        std::make_shared<MemberExpr>(fr("ret"), "waker"), std::vector<ExprPtr>{})));
                    s1.push_back(std::make_shared<IfStmt>(
                        binop(swapCall, "==", intlit(1)), std::make_shared<BlockStmt>(wk)));
                    s1.push_back(ret(nullptr));
                } else if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
                    rewrite(es->expr, vars); s1.push_back(stmt);
                }
            }
        }

        // ── Assemble resume:  if (fr.st == 0) { s0 }  if (fr.st == 1) { s1 } ──
        std::vector<BlockItem> resumeBody;
        resumeBody.push_back(std::make_shared<IfStmt>(
            binop(fr("st"), "==", intlit(0)), std::make_shared<BlockStmt>(s0)));
        resumeBody.push_back(std::make_shared<IfStmt>(
            binop(fr("st"), "==", intlit(1)), std::make_shared<BlockStmt>(s1)));
        auto resumeFn = std::make_shared<FunctionDecl>(
            resumeN, "void",
            std::vector<std::pair<std::string,std::string>>{ {"*" + frameT, "fr"} },
            std::make_shared<BlockStmt>(resumeBody));

        // ── Constructor:  *Future<T> name(params) { ... } ────────────────────
        std::vector<BlockItem> ctor;
        ctor.push_back(std::make_shared<VarDecl>("fr", "*" + frameT,
            std::make_shared<TemplateCallExpr>("alloc", std::vector<std::string>{frameT},
                std::vector<ExprPtr>{ intlit(1) })));
        ctor.push_back(assign(fr("st"), intlit(0)));
        ctor.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "state"), intlit(0)));
        for (const auto& p : fn->params)
            ctor.push_back(assign(fr(p.second), ident(p.second)));
        ctor.push_back(exprStmt(std::make_shared<CallExpr>(
            ident(resumeN), std::vector<ExprPtr>{ ident("fr") })));
        ctor.push_back(ret(std::make_shared<UnaryExpr>("&",
            std::make_shared<MemberExpr>(ident("fr"), "ret"))));
        auto ctorFn = std::make_shared<FunctionDecl>(
            name, "*Future<" + T + ">", fn->params, std::make_shared<BlockStmt>(ctor));

        // ── Emit frame struct + resume + constructor in place of the async fn ─
        out.push_back(std::make_shared<StructDecl>(frameT, fields));
        out.push_back(resumeFn);
        out.push_back(ctorFn);
        (void)isFuturePtr;
    }

    program->declarations = std::move(out);
}
