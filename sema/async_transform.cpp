#include "async_transform.h"
#include <stdexcept>
#include <set>
#include <map>
#include <memory>
#include <functional>

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

// True if a statement contains an `await` anywhere (decides plain vs state-split).
bool stmtHasAwait(const StmtPtr& s) {
    if (!s) return false;
    if (auto* b = dynamic_cast<BlockStmt*>(s.get())) {
        for (auto& it : b->items) {
            if (std::holds_alternative<DeclPtr>(it)) {
                if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get()))
                    if (vd->initializer && hasAwait(vd->initializer)) return true;
            } else if (stmtHasAwait(std::get<StmtPtr>(it))) return true;
        }
        return false;
    }
    if (auto* i = dynamic_cast<IfStmt*>(s.get()))
        return hasAwait(i->condition) || stmtHasAwait(i->thenBranch) || stmtHasAwait(i->elseBranch);
    if (auto* w = dynamic_cast<WhileStmt*>(s.get()))
        return hasAwait(w->condition) || stmtHasAwait(w->body);
    if (auto* f = dynamic_cast<ForStmt*>(s.get()))
        return stmtHasAwait(f->init) || hasAwait(f->condition) || hasAwait(f->step) || stmtHasAwait(f->body);
    if (auto* fi = dynamic_cast<ForInStmt*>(s.get()))
        return hasAwait(fi->iterable) || stmtHasAwait(fi->body);
    if (auto* sw = dynamic_cast<SwitchStmt*>(s.get())) {
        if (hasAwait(sw->subject)) return true;
        for (auto& c : sw->cases) for (auto& st : c.stmts) if (stmtHasAwait(st)) return true;
        return false;
    }
    if (auto* rs = dynamic_cast<ReturnStmt*>(s.get())) return hasAwait(rs->value);
    if (auto* es = dynamic_cast<ExprStmt*>(s.get()))  return hasAwait(es->expr);
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

        // ── Desugar awaits not already bound in a `let`, recursing into control
        //    flow, so afterwards every await is the direct initializer of a let:
        //    `return await E;`   -> `let __awN = await E; return __awN;`
        //    `await E;`          -> `let __awN = await E;`            (discarded)
        //    `x = await E;`      -> `let __awN = await E; x = __awN;`
        int tmpN = 0;
        std::function<StmtPtr(const StmtPtr&)> desugarStmt;
        std::function<std::vector<BlockItem>(const std::vector<BlockItem>&)> desugarItems =
            [&](const std::vector<BlockItem>& its) {
                std::vector<BlockItem> out2;
                for (auto& it : its) {
                    if (std::holds_alternative<StmtPtr>(it)) {
                        auto stmt = std::get<StmtPtr>(it);
                        if (auto* rs = dynamic_cast<ReturnStmt*>(stmt.get())) {
                            if (auto* aw = dynamic_cast<AwaitExpr*>(rs->value.get())) {
                                std::string tn = "__aw_t" + std::to_string(tmpN++);
                                out2.push_back(DeclPtr(std::make_shared<VarDecl>(tn, aw->resolvedType, rs->value)));
                                out2.push_back(StmtPtr(std::make_shared<ReturnStmt>(ident(tn))));
                                continue;
                            }
                        } else if (auto* es = dynamic_cast<ExprStmt*>(stmt.get())) {
                            if (auto* aw = dynamic_cast<AwaitExpr*>(es->expr.get())) {
                                std::string tn = "__aw_t" + std::to_string(tmpN++);
                                out2.push_back(DeclPtr(std::make_shared<VarDecl>(tn, aw->resolvedType, es->expr)));
                                continue;          // discard
                            }
                            // x = await E;  ->  let __awN = await E; x = __awN;
                            if (auto* b = dynamic_cast<BinaryExpr*>(es->expr.get()))
                                if (b->op == "=")
                                    if (auto* aw = dynamic_cast<AwaitExpr*>(b->right.get())) {
                                        std::string tn = "__aw_t" + std::to_string(tmpN++);
                                        out2.push_back(DeclPtr(std::make_shared<VarDecl>(tn, aw->resolvedType, b->right)));
                                        out2.push_back(StmtPtr(std::make_shared<ExprStmt>(
                                            binop(b->left, "=", ident(tn)))));
                                        continue;
                                    }
                        }
                        out2.push_back(StmtPtr(desugarStmt(stmt)));
                    } else {
                        out2.push_back(it);        // a plain decl (incl. `let x = await E`)
                    }
                }
                return out2;
            };
        desugarStmt = [&](const StmtPtr& s) -> StmtPtr {
            if (auto* b = dynamic_cast<BlockStmt*>(s.get()))
                return std::make_shared<BlockStmt>(desugarItems(b->items));
            if (auto* i = dynamic_cast<IfStmt*>(s.get()))
                return std::make_shared<IfStmt>(i->condition,
                    i->thenBranch ? desugarStmt(i->thenBranch) : nullptr,
                    i->elseBranch ? desugarStmt(i->elseBranch) : nullptr);
            if (auto* w = dynamic_cast<WhileStmt*>(s.get()))
                return std::make_shared<WhileStmt>(w->condition, desugarStmt(w->body));
            if (auto* f = dynamic_cast<ForStmt*>(s.get()))
                return std::make_shared<ForStmt>(f->init, f->condition, f->step, desugarStmt(f->body));
            if (auto* sw = dynamic_cast<SwitchStmt*>(s.get())) {
                // Normalize awaits inside each case. Wrap the case body in a block so
                // its statements run through desugarItems (which let-binds awaits and
                // may introduce VarDecls); all locals are hoisted to frame fields
                // anyway, so the extra block does not change fall-through visibility.
                auto out = std::make_shared<SwitchStmt>(sw->subject, std::vector<SwitchStmt::Case>{});
                for (auto& c : sw->cases) {
                    SwitchStmt::Case nc; nc.value = c.value;
                    std::vector<BlockItem> bi;
                    for (auto& st : c.stmts) bi.push_back(BlockItem(st));
                    nc.stmts.push_back(std::make_shared<BlockStmt>(desugarItems(bi)));
                    out->cases.push_back(nc);
                }
                return out;
            }
            return s;
        };
        std::vector<BlockItem> items = desugarItems(block->items);

        // ── Collect awaits (source order, recursing into control flow) and all
        //    locals (hoisted to frame fields). Each await gets an __aw<i>. ─────
        struct AwaitSite { VarDecl* var; AwaitExpr* expr; };
        std::vector<AwaitSite> awaits;
        std::map<AwaitExpr*, int> awIdx;
        std::set<std::string> vars;
        std::vector<StructDecl::Field> fields;
        fields.push_back({"Future<" + Tret + ">", "ret"});
        fields.push_back({"int", "st"});
        fields.push_back({"FutureHdr*", "awaiting"});
        for (const auto& p : fn->params) { vars.insert(p.second); fields.push_back({p.first, p.second}); }

        std::function<void(const StmtPtr&)> scanS;
        std::function<void(const std::vector<BlockItem>&)> scanB =
            [&](const std::vector<BlockItem>& its) {
                for (auto& it : its) {
                    if (std::holds_alternative<DeclPtr>(it)) {
                        auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get());
                        if (!vd) continue;
                        if (!vars.count(vd->name)) { vars.insert(vd->name); fields.push_back({vd->type, vd->name}); }
                        if (vd->initializer)
                            if (auto* aw = dynamic_cast<AwaitExpr*>(vd->initializer.get())) {
                                awIdx[aw] = (int)awaits.size();
                                fields.push_back({"*Future<" + aw->resolvedType + ">", "__aw" + std::to_string(awaits.size())});
                                awaits.push_back({vd, aw});
                            }
                    } else scanS(std::get<StmtPtr>(it));
                }
            };
        scanS = [&](const StmtPtr& s) {
            if (!s) return;
            if (auto* b = dynamic_cast<BlockStmt*>(s.get())) scanB(b->items);
            else if (auto* i = dynamic_cast<IfStmt*>(s.get())) { scanS(i->thenBranch); scanS(i->elseBranch); }
            else if (auto* w = dynamic_cast<WhileStmt*>(s.get())) scanS(w->body);
            else if (auto* f = dynamic_cast<ForStmt*>(s.get())) { scanS(f->init); scanS(f->body); }
            else if (auto* fi = dynamic_cast<ForInStmt*>(s.get())) scanS(fi->body);
            else if (auto* sw = dynamic_cast<SwitchStmt*>(s.get()))
                for (auto& c : sw->cases) for (auto& st : c.stmts) scanS(st);
        };
        scanB(items);
        if (awaits.empty())
            throw std::runtime_error("async function '" + name + "': expected at least one `await`");

        // ── State graph ──────────────────────────────────────────────────────
        std::vector<std::vector<BlockItem>> states;
        auto newState = [&]() -> int { states.push_back({}); return (int)states.size() - 1; };
        auto goTo = [&](int s, int target) { states[s].push_back(assign(fr("st"), intlit(target))); };

        // break/continue inside an await-split loop can't stay literal — they would
        // break/continue the resume function's own `while(true)` dispatch loop. So
        // when a loop is lowered into states we push its exit/continue target states
        // here, and a `break`/`continue` becomes a transition to them.
        std::vector<int> brkTargets, contTargets;
        // Does `s` contain a `break`/`continue` that binds to an *enclosing* loop —
        // i.e. one not shadowed by a nested loop/switch? Such a statement can't be
        // emitted verbatim by rewritePlain inside a split loop; it must be lowered
        // structurally so the break/continue reach the transitions above. (Descends
        // if/block; stops at nested while/for/for-in/switch, which capture their own.)
        std::function<bool(const StmtPtr&)> loopEscapes = [&](const StmtPtr& s) -> bool {
            if (!s) return false;
            if (dynamic_cast<BreakStmt*>(s.get()) || dynamic_cast<ContinueStmt*>(s.get())) return true;
            if (auto* b = dynamic_cast<BlockStmt*>(s.get())) {
                for (auto& it : b->items)
                    if (std::holds_alternative<StmtPtr>(it) && loopEscapes(std::get<StmtPtr>(it))) return true;
                return false;
            }
            if (auto* i = dynamic_cast<IfStmt*>(s.get()))
                return loopEscapes(i->thenBranch) || loopEscapes(i->elseBranch);
            return false;
        };

        // Complete the future with `v` (already rewritten), then return.
        auto completeInto = [&](std::vector<BlockItem>& st, ExprPtr v) {
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
        };

        // Recursively rewrite a NO-await statement for inclusion in a state:
        // let -> fr.x = E; return -> completion; recurse into control-flow bodies.
        std::function<StmtPtr(const StmtPtr&)> rewritePlain = [&](const StmtPtr& s) -> StmtPtr {
            if (auto* b = dynamic_cast<BlockStmt*>(s.get())) {
                std::vector<BlockItem> out2;
                for (auto& it : b->items) {
                    if (std::holds_alternative<DeclPtr>(it)) {
                        auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get());
                        if (vd && vd->initializer) { rewrite(vd->initializer, vars); out2.push_back(assign(fr(vd->name), vd->initializer)); }
                    } else out2.push_back(rewritePlain(std::get<StmtPtr>(it)));
                }
                return std::make_shared<BlockStmt>(out2);
            }
            if (auto* rs = dynamic_cast<ReturnStmt*>(s.get())) {
                ExprPtr v = rs->value ? rs->value : intlit(0); rewrite(v, vars);
                std::vector<BlockItem> cb; completeInto(cb, v);
                return std::make_shared<BlockStmt>(cb);
            }
            if (auto* es = dynamic_cast<ExprStmt*>(s.get())) { rewrite(es->expr, vars); return s; }
            if (auto* i = dynamic_cast<IfStmt*>(s.get())) {
                rewrite(i->condition, vars);
                return std::make_shared<IfStmt>(i->condition, rewritePlain(i->thenBranch),
                    i->elseBranch ? rewritePlain(i->elseBranch) : nullptr);
            }
            if (auto* w = dynamic_cast<WhileStmt*>(s.get())) {
                rewrite(w->condition, vars);
                return std::make_shared<WhileStmt>(w->condition, rewritePlain(w->body));
            }
            if (auto* f = dynamic_cast<ForStmt*>(s.get())) {
                StmtPtr in2 = f->init ? rewritePlain(f->init) : nullptr;
                rewrite(f->condition, vars); rewrite(f->step, vars);
                return std::make_shared<ForStmt>(in2, f->condition, f->step, rewritePlain(f->body));
            }
            if (auto* fi = dynamic_cast<ForInStmt*>(s.get())) {
                rewrite(fi->iterable, vars);
                return std::make_shared<ForInStmt>(fi->varName, fi->iterable, rewritePlain(fi->body));
            }
            if (auto* sw = dynamic_cast<SwitchStmt*>(s.get())) {
                rewrite(sw->subject, vars);
                auto out2 = std::make_shared<SwitchStmt>(sw->subject, std::vector<SwitchStmt::Case>{});
                for (auto& c : sw->cases) {
                    SwitchStmt::Case nc; nc.value = c.value;
                    for (auto& st : c.stmts) nc.stmts.push_back(rewritePlain(st));
                    out2->cases.push_back(nc);
                }
                return out2;
            }
            return s;   // break/continue/etc.
        };

        // Lower a statement that CONTAINS an await into the state graph; lowerSeq
        // threads a list. Each returns the state where control continues.
        std::function<int(const std::vector<BlockItem>&, int)> lowerSeq;
        std::function<int(const StmtPtr&, int)> lowerStmt;
        auto lowerItem = [&](BlockItem& it, int cur) -> int {
            if (std::holds_alternative<DeclPtr>(it)) {
                auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get());
                if (!vd || !vd->initializer) return cur;
                if (auto* aw = dynamic_cast<AwaitExpr*>(vd->initializer.get())) {
                    int i = awIdx[aw]; std::string awf = "__aw" + std::to_string(i);
                    const std::string Tp = aw->resolvedType;   // the future's inner type T'
                    ExprPtr callE = aw->operand; rewrite(callE, vars);
                    states[cur].push_back(assign(fr(awf), callE));
                    int next = newState();
                    ExprPtr poll = std::make_shared<TemplateCallExpr>("future_poll",
                        std::vector<std::string>{Tp},
                        std::vector<ExprPtr>{ fr(awf), resumeWaker(resumeN, next, "*" + frameT) });
                    std::vector<BlockItem> pk;
                    pk.push_back(assign(fr("awaiting"), std::make_shared<CastExpr>("*FutureHdr", fr(awf))));
                    pk.push_back(ret(nullptr));
                    states[cur].push_back(std::make_shared<IfStmt>(binop(poll, "==", intlit(0)),
                        std::make_shared<BlockStmt>(pk)));
                    goTo(cur, next);
                    // extract into `next`
                    states[next].push_back(assign(fr("awaiting"), std::make_shared<CastExpr>("*FutureHdr", intlit(0))));
                    states[next].push_back(assign(fr(vd->name), std::make_shared<MemberExpr>(fr(awf), "value")));
                    states[next].push_back(exprStmt(std::make_shared<FreeClosureExpr>(
                        std::make_shared<MemberExpr>(fr(awf), "waker"))));
                    states[next].push_back(exprStmt(std::make_shared<TemplateCallExpr>("free_future",
                        std::vector<std::string>{Tp}, std::vector<ExprPtr>{ fr(awf) })));
                    return next;
                }
                if (hasAwait(vd->initializer))
                    throw std::runtime_error("async function '" + name + "': await must be the whole "
                        "initializer of a `let`, not part of a larger expression");
                rewrite(vd->initializer, vars);
                states[cur].push_back(assign(fr(vd->name), vd->initializer));
                return cur;
            }
            return lowerStmt(std::get<StmtPtr>(it), cur);
        };
        // lowerSeq/lowerStmt return -1 when control definitely terminates (a
        // `return` was emitted on every path) — so callers don't append a
        // terminator or a transition to an unreachable state.
        lowerSeq = [&](const std::vector<BlockItem>& its, int entry) -> int {
            int cur = entry;
            for (auto& it : its) {
                if (cur == -1) break;            // rest is unreachable
                BlockItem copy = it; cur = lowerItem(copy, cur);
            }
            return cur;
        };
        lowerStmt = [&](const StmtPtr& s, int cur) -> int {
            // return E  -> complete the future and terminate this path.
            if (auto* rs = dynamic_cast<ReturnStmt*>(s.get())) {
                if (hasAwait(rs->value))
                    throw std::runtime_error("async function '" + name + "': `return await ...` "
                        "must be bound first (`let r = await ...; return r;`)");
                ExprPtr v = rs->value ? rs->value : intlit(0); rewrite(v, vars);
                completeInto(states[cur], v);
                return -1;
            }
            // break/continue bind to the enclosing split loop -> state transition.
            if (dynamic_cast<BreakStmt*>(s.get())) {
                if (brkTargets.empty())
                    throw std::runtime_error("async function '" + name + "': `break` outside a loop");
                goTo(cur, brkTargets.back()); return -1;
            }
            if (dynamic_cast<ContinueStmt*>(s.get())) {
                if (contTargets.empty())
                    throw std::runtime_error("async function '" + name + "': `continue` outside a loop");
                goTo(cur, contTargets.back()); return -1;
            }
            // Emit verbatim only if there's no await AND no break/continue that would
            // escape into the resume loop; otherwise fall through to structural lowering.
            if (!stmtHasAwait(s) && !loopEscapes(s)) { states[cur].push_back(rewritePlain(s)); return cur; }
            if (auto* i = dynamic_cast<IfStmt*>(s.get())) {
                rewrite(i->condition, vars);
                int thenE = newState(), elseE = newState(), join = newState();
                std::vector<BlockItem> tb; tb.push_back(assign(fr("st"), intlit(thenE)));
                std::vector<BlockItem> eb; eb.push_back(assign(fr("st"), intlit(elseE)));
                states[cur].push_back(std::make_shared<IfStmt>(i->condition,
                    std::make_shared<BlockStmt>(tb), std::make_shared<BlockStmt>(eb)));
                int te = lowerStmt(i->thenBranch, thenE);
                int ee = i->elseBranch ? lowerStmt(i->elseBranch, elseE) : elseE;
                if (te != -1) goTo(te, join);
                if (ee != -1) goTo(ee, join);
                return (te == -1 && ee == -1) ? -1 : join;
            }
            if (auto* w = dynamic_cast<WhileStmt*>(s.get())) {
                rewrite(w->condition, vars);
                int header = newState(), bodyE = newState(), after = newState();
                goTo(cur, header);
                std::vector<BlockItem> tb; tb.push_back(assign(fr("st"), intlit(bodyE)));
                std::vector<BlockItem> eb; eb.push_back(assign(fr("st"), intlit(after)));
                states[header].push_back(std::make_shared<IfStmt>(w->condition,
                    std::make_shared<BlockStmt>(tb), std::make_shared<BlockStmt>(eb)));
                brkTargets.push_back(after);        // break -> after; continue -> re-test
                contTargets.push_back(header);
                int be = lowerStmt(w->body, bodyE);
                brkTargets.pop_back(); contTargets.pop_back();
                if (be != -1) goTo(be, header);    // back-edge
                return after;                       // the loop may not execute -> reachable
            }
            if (auto* f = dynamic_cast<ForStmt*>(s.get())) {
                // for (init; cond; step) body  — init/step run as plain code; the
                // loop is header(cond) -> body -> step -> back-edge -> header.
                if (f->init) states[cur].push_back(rewritePlain(f->init));
                // header(cond) -> body -> step -> back-edge -> header. The step gets
                // its own state so `continue` runs it before re-testing (C semantics).
                int header = newState(), bodyE = newState(), step = newState(), after = newState();
                goTo(cur, header);
                if (f->condition) {
                    rewrite(f->condition, vars);
                    std::vector<BlockItem> tb; tb.push_back(assign(fr("st"), intlit(bodyE)));
                    std::vector<BlockItem> eb; eb.push_back(assign(fr("st"), intlit(after)));
                    states[header].push_back(std::make_shared<IfStmt>(f->condition,
                        std::make_shared<BlockStmt>(tb), std::make_shared<BlockStmt>(eb)));
                } else {
                    goTo(header, bodyE);            // no condition -> infinite loop
                }
                if (f->step) { ExprPtr stp = f->step; rewrite(stp, vars); states[step].push_back(exprStmt(stp)); }
                goTo(step, header);                // step -> back-edge
                brkTargets.push_back(after);       // break -> after; continue -> step
                contTargets.push_back(step);
                int be = lowerStmt(f->body, bodyE);
                brkTargets.pop_back(); contTargets.pop_back();
                if (be != -1) goTo(be, step);      // body exit -> step
                return after;
            }
            if (auto* sw = dynamic_cast<SwitchStmt*>(s.get())) {
                // Dispatch on the subject in `cur`, then C-style fall-through between
                // per-case entry states; `break` (pushed below) exits to `join`.
                rewrite(sw->subject, vars);
                int n = (int)sw->cases.size();
                std::vector<int> entry(n);
                for (int k = 0; k < n; ++k) entry[k] = newState();
                int join = newState();
                int dflt = join;                              // no default -> skip to join
                for (int k = 0; k < n; ++k) if (!sw->cases[k].value) { dflt = entry[k]; break; }
                // if (subj==V0) st=entry0; else if (subj==V1) st=entry1; ... else st=dflt
                StmtPtr chain = assign(fr("st"), intlit(dflt));
                for (int k = n - 1; k >= 0; --k) {
                    if (!sw->cases[k].value) continue;        // default is the else
                    ExprPtr cv = sw->cases[k].value; rewrite(cv, vars);
                    std::vector<BlockItem> tb; tb.push_back(assign(fr("st"), intlit(entry[k])));
                    chain = std::make_shared<IfStmt>(binop(sw->subject, "==", cv),
                        std::make_shared<BlockStmt>(tb),
                        std::make_shared<BlockStmt>(std::vector<BlockItem>{ chain }));
                }
                states[cur].push_back(chain);
                brkTargets.push_back(join);                   // break -> switch end
                for (int k = 0; k < n; ++k) {
                    std::vector<BlockItem> body;
                    for (auto& st : sw->cases[k].stmts) body.push_back(BlockItem(st));
                    int e = lowerSeq(body, entry[k]);
                    if (e != -1) goTo(e, (k + 1 < n) ? entry[k + 1] : join);  // fall through
                }
                brkTargets.pop_back();
                return join;
            }
            if (auto* b = dynamic_cast<BlockStmt*>(s.get())) return lowerSeq(b->items, cur);
            throw std::runtime_error("async function '" + name + "': await inside this statement "
                "is not lowered yet (supported: if / while / C-style for / switch; not for-in)");
        };

        int entry = newState();                 // state 0
        int exit  = lowerSeq(items, entry);
        // Fall off the end of a reachable exit state: void completes with unit 0;
        // a non-void fn that falls off is a user error, but emit a bare return.
        if (exit != -1) {
            if (isVoid) completeInto(states[exit], intlit(0));
            else        states[exit].push_back(ret(nullptr));
        }

        // ── Resume:  while (true) { if(st==0){..} else if(st==1){..} ... else return; }
        StmtPtr chain = ret(nullptr);           // terminal: unknown state -> return
        for (int s = (int)states.size() - 1; s >= 0; --s)
            chain = std::make_shared<IfStmt>(binop(fr("st"), "==", intlit(s)),
                std::make_shared<BlockStmt>(states[s]), chain);
        std::vector<BlockItem> loopBody; loopBody.push_back(chain);
        std::vector<BlockItem> resumeBody;
        resumeBody.push_back(std::make_shared<WhileStmt>(
            std::make_shared<LiteralExpr>(LiteralExpr::Kind::BOOL, "true"),
            std::make_shared<BlockStmt>(loopBody)));
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
        ctor.push_back(assign(fr("awaiting"), std::make_shared<CastExpr>("*FutureHdr", intlit(0))));
        ctor.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "state"), intlit(0)));
        // ret is raw-allocated (not via future_new); init waker to a no-op so
        // free_future/future_drop can free_closure it safely.
        ctor.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "waker"),
            std::make_shared<LambdaExpr>(
                std::vector<std::pair<std::string,std::string>>{}, "void",
                std::make_shared<BlockStmt>(std::vector<BlockItem>{}))));
        // ret.on_drop: if cancelled while suspended, cascade-drop the awaited
        // future, then free the frame (== free &ret, the embedded first field).
        {
            std::vector<BlockItem> cascade;
            cascade.push_back(exprStmt(std::make_shared<CallExpr>(
                ident("future_drop"), std::vector<ExprPtr>{ fr("awaiting") })));
            std::vector<BlockItem> dropBody;
            dropBody.push_back(std::make_shared<IfStmt>(
                binop(fr("awaiting"), "!=", std::make_shared<CastExpr>("*FutureHdr", intlit(0))),
                std::make_shared<BlockStmt>(cascade)));
            // NOTE: do not free the frame here — future_drop frees it (== free &ret)
            // after this on_drop returns, and frees this closure's env too.
            auto dropLam = std::make_shared<LambdaExpr>(
                std::vector<std::pair<std::string,std::string>>{}, "void",
                std::make_shared<BlockStmt>(dropBody));
            dropLam->captures.push_back({"fr", "*" + frameT});
            ctor.push_back(assign(std::make_shared<MemberExpr>(fr("ret"), "on_drop"), dropLam));
        }
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
