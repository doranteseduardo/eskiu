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

// True if an expression contains an AwaitExpr anywhere (used to reject `await`
// in positions the v1 transform does not handle, e.g. inside a larger expression).
bool hasAwait(const ExprPtr& e) {
    if (!e) return false;
    if (dynamic_cast<AwaitExpr*>(e.get())) return true;
    if (auto* b = dynamic_cast<BinaryExpr*>(e.get())) return hasAwait(b->left) || hasAwait(b->right);
    if (auto* u = dynamic_cast<UnaryExpr*>(e.get()))  return hasAwait(u->operand);
    if (auto* m = dynamic_cast<MemberExpr*>(e.get())) return hasAwait(m->base);
    if (auto* ix = dynamic_cast<IndexExpr*>(e.get())) return hasAwait(ix->base) || hasAwait(ix->index);
    if (auto* c = dynamic_cast<CastExpr*>(e.get()))   return hasAwait(c->expr);
    if (auto* q = dynamic_cast<QuestionExpr*>(e.get())) return hasAwait(q->operand);
    if (auto* call = dynamic_cast<CallExpr*>(e.get())) {
        if (hasAwait(call->callee)) return true;
        for (auto& a : call->args) if (hasAwait(a)) return true;
        return false;
    }
    if (auto* tc = dynamic_cast<TemplateCallExpr*>(e.get())) {
        for (auto& a : tc->args) if (hasAwait(a)) return true;
        return false;
    }
    return false;
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
        const std::string T = fn->returnType;                 // declared return type
        const bool isVoid = (T == "void");
        // `async void` uses a 1-byte unit (uint8) as the Future's value type.
        const std::string Tret = isVoid ? "uint8" : T;

        auto* block = dynamic_cast<BlockStmt*>(fn->body.get());
        if (!block)
            throw std::runtime_error("async function '" + name + "': missing body");

        // ── Desugar awaits not already bound in a `let` ──────────────────────
        // `return await E;`  ->  `let __awret<N> = await E; return __awret<N>;`
        // `await E;`         ->  `let __awtmp<N> = await E;`  (result discarded)
        // After this, every await is the direct initializer of a `let`.
        std::vector<BlockItem> items;
        int tmpN = 0;
        for (auto& it : block->items) {
            if (std::holds_alternative<StmtPtr>(it)) {
                auto stmt = std::get<StmtPtr>(it);
                if (auto* rs = dynamic_cast<ReturnStmt*>(stmt.get())) {
                    if (auto* aw = dynamic_cast<AwaitExpr*>(rs->value.get())) {
                        std::string tn = "__awret" + std::to_string(tmpN++);
                        items.push_back(DeclPtr(std::make_shared<VarDecl>(tn, aw->resolvedType, rs->value)));
                        items.push_back(StmtPtr(std::make_shared<ReturnStmt>(ident(tn))));
                        continue;
                    }
                } else if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
                    if (auto* aw = dynamic_cast<AwaitExpr*>(es->expr.get())) {
                        std::string tn = "__awtmp" + std::to_string(tmpN++);
                        items.push_back(DeclPtr(std::make_shared<VarDecl>(tn, aw->resolvedType, es->expr)));
                        continue;
                    }
                }
            }
            items.push_back(it);
        }

        // Collect await sites in order: each is `let x = await CALL(...)`.
        struct AwaitSite { VarDecl* var; AwaitExpr* expr; size_t idx; };
        std::vector<AwaitSite> awaits;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!std::holds_alternative<DeclPtr>(items[i])) continue;
            auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(items[i]).get());
            if (vd && vd->initializer)
                if (auto* aw = dynamic_cast<AwaitExpr*>(vd->initializer.get()))
                    awaits.push_back({vd, aw, i});
        }
        if (awaits.empty())
            throw std::runtime_error("async function '" + name +
                "': expected at least one `await`");
        const int k = (int)awaits.size();   // number of awaits -> states 0..k

        // ── Frame variables: params + every body local + one __aw<i> per await ─
        std::set<std::string> vars;
        std::vector<StructDecl::Field> fields;
        fields.push_back({"Future<" + Tret + ">", "ret"});
        fields.push_back({"int", "st"});
        fields.push_back({"FutureHdr*", "awaiting"});
        for (int i = 0; i < k; ++i)
            fields.push_back({"*Future<" + awaits[i].var->type + ">", "__aw" + std::to_string(i)});
        for (const auto& p : fn->params) { vars.insert(p.second); fields.push_back({p.first, p.second}); }
        for (auto& item : items)
            if (std::holds_alternative<DeclPtr>(item))
                if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get())) {
                    vars.insert(vd->name); fields.push_back({vd->type, vd->name});
                }

        // ── Helpers that emit into a state's statement list ──────────────────
        // Park on await i; on suspend return, on the fast path advance to nextState.
        auto emitPark = [&](std::vector<BlockItem>& st, int i, int nextState) {
            const std::string awf = "__aw" + std::to_string(i);
            const std::string Tp  = awaits[i].var->type;
            ExprPtr callE = awaits[i].expr->operand; rewrite(callE, vars);
            st.push_back(assign(fr(awf), callE));
            ExprPtr poll = std::make_shared<TemplateCallExpr>("future_poll",
                std::vector<std::string>{Tp},
                std::vector<ExprPtr>{ fr(awf), resumeWaker(resumeN, nextState, "*" + frameT) });
            std::vector<BlockItem> parkBody;
            parkBody.push_back(assign(fr("awaiting"), std::make_shared<CastExpr>("*FutureHdr", fr(awf))));
            parkBody.push_back(ret(nullptr));
            st.push_back(std::make_shared<IfStmt>(binop(poll, "==", intlit(0)),
                std::make_shared<BlockStmt>(parkBody)));
            st.push_back(assign(fr("st"), intlit(nextState)));
        };
        // Extract await i's value into its frame var, then free the awaited future.
        auto emitExtract = [&](std::vector<BlockItem>& st, int i) {
            const std::string awf = "__aw" + std::to_string(i);
            const std::string Tp  = awaits[i].var->type;
            st.push_back(assign(fr(awaits[i].var->name), std::make_shared<MemberExpr>(fr(awf), "value")));
            st.push_back(exprStmt(std::make_shared<TemplateCallExpr>("free_future",
                std::vector<std::string>{Tp}, std::vector<ExprPtr>{ fr(awf) })));
        };
        // A plain (non-await) statement: rewrite refs; let->assign; return->complete.
        auto emitPlain = [&](std::vector<BlockItem>& st, BlockItem& item) {
            if (std::holds_alternative<DeclPtr>(item)) {
                auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(item).get());
                if (vd && vd->initializer) {
                    if (hasAwait(vd->initializer))
                        throw std::runtime_error("async function '" + name + "': await must be "
                            "the whole initializer (`let x = await ...;`), not part of an expression");
                    rewrite(vd->initializer, vars);
                    st.push_back(assign(fr(vd->name), vd->initializer));
                }
                return;
            }
            auto stmt = std::get<StmtPtr>(item);
            if (auto* rs = dynamic_cast<ReturnStmt*>(stmt.get())) {
                if (hasAwait(rs->value))
                    throw std::runtime_error("async function '" + name + "': `return await ...` "
                        "is not supported yet — bind it first (`let r = await ...; return r;`)");
                // void `return;` completes the future with the 1-byte unit (0).
                ExprPtr v = rs->value ? rs->value : intlit(0);
                rewrite(v, vars);
                st.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "value"), v));
                ExprPtr swap = std::make_shared<CallExpr>(ident("atomic_swap"),
                    std::vector<ExprPtr>{ std::make_shared<UnaryExpr>("&",
                        std::make_shared<MemberExpr>(fr("ret"), "state")), intlit(2) });
                std::vector<BlockItem> wk;
                wk.push_back(exprStmt(std::make_shared<CallExpr>(
                    std::make_shared<MemberExpr>(fr("ret"), "waker"), std::vector<ExprPtr>{})));
                st.push_back(std::make_shared<IfStmt>(binop(swap, "==", intlit(1)),
                    std::make_shared<BlockStmt>(wk)));
                st.push_back(ret(nullptr));
            } else if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
                if (hasAwait(es->expr))
                    throw std::runtime_error("async function '" + name +
                        "': await must be bound in a `let` (v1)");
                rewrite(es->expr, vars); st.push_back(stmt);
            } else {
                throw std::runtime_error("async function '" + name + "': v1 supports linear "
                    "bodies only (let/expr/return); control flow around await is not lowered yet");
            }
        };

        // ── Build states 0..k by walking the body, splitting at each await ────
        std::vector<std::vector<BlockItem>> states(k + 1);
        int cur = 0, ai = 0;
        for (size_t idx = 0; idx < items.size(); ++idx) {
            if (ai < k && idx == awaits[ai].idx) {
                emitPark(states[cur], ai, cur + 1);      // park on await ai in current state
                ++cur;
                emitExtract(states[cur], ai);            // next state begins by extracting it
                ++ai;
            } else {
                emitPlain(states[cur], items[idx]);
            }
        }

        // An `async void` body may fall off the end with no `return`; complete
        // the future (unit 0) in the final state so the awaiter is resumed.
        bool endsInReturn = !items.empty() &&
            std::holds_alternative<StmtPtr>(items.back()) &&
            dynamic_cast<ReturnStmt*>(std::get<StmtPtr>(items.back()).get());
        if (isVoid && !endsInReturn) {
            auto& st = states[k];
            st.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "value"), intlit(0)));
            ExprPtr swap = std::make_shared<CallExpr>(ident("atomic_swap"),
                std::vector<ExprPtr>{ std::make_shared<UnaryExpr>("&",
                    std::make_shared<MemberExpr>(fr("ret"), "state")), intlit(2) });
            std::vector<BlockItem> wk;
            wk.push_back(exprStmt(std::make_shared<CallExpr>(
                std::make_shared<MemberExpr>(fr("ret"), "waker"), std::vector<ExprPtr>{})));
            st.push_back(std::make_shared<IfStmt>(binop(swap, "==", intlit(1)),
                std::make_shared<BlockStmt>(wk)));
        }

        // ── Assemble resume:  if (fr.st==0){..} if(fr.st==1){..} ... ─────────
        std::vector<BlockItem> resumeBody;
        for (int s = 0; s <= k; ++s)
            resumeBody.push_back(std::make_shared<IfStmt>(
                binop(fr("st"), "==", intlit(s)), std::make_shared<BlockStmt>(states[s])));
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
            name, "*Future<" + Tret + ">", fn->params, std::make_shared<BlockStmt>(ctor));

        // ── Emit frame struct + resume + constructor in place of the async fn ─
        out.push_back(std::make_shared<StructDecl>(frameT, fields));
        out.push_back(resumeFn);
        out.push_back(ctorFn);
        (void)isFuturePtr;
    }

    program->declarations = std::move(out);
}
