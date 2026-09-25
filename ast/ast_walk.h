#pragma once
// ast_walk.h — the single source of truth for "which sub-expressions does each
// expression node have".
//
// Several passes need to recurse over the expressions inside an expression:
// the async transform renames frame variables (`rewrite`), tests for a contained
// `await` (`hasAwait`), and analyses captures (`TemplateCapturePass`). Each used
// to hand-roll its own dynamic_cast chain, so adding an expression node — or
// forgetting one — silently broke a pass (e.g. `rewrite` once skipped
// `StructInitExpr`, so a frame variable used in a struct literal after an await
// did not get renamed). Centralising the child enumeration here means a new
// expression node is handled everywhere by editing one list.
//
// `f` receives each child by mutable reference so callers may replace it (used by
// `rewrite`). Read-only callers simply ignore that.
//
// LambdaExpr is intentionally NOT descended into: a lambda has its own scope, so
// frame-variable renaming and await-search must stop at its boundary. Passes that
// need the body (capture analysis) handle LambdaExpr explicitly before delegating.

#include <functional>
#include <set>
#include <string>
#include "ast.h"

namespace astwalk {

inline void forEachChildExpr(Expr* e, const std::function<void(ExprPtr&)>& f) {
    if (!e) return;
    if (auto* b = dynamic_cast<BinaryExpr*>(e))        { f(b->left); f(b->right); }
    else if (auto* u = dynamic_cast<UnaryExpr*>(e))    { f(u->operand); }
    else if (auto* id = dynamic_cast<IncDecExpr*>(e))  { f(id->operand); }
    else if (auto* al = dynamic_cast<ArrayLitExpr*>(e)){ for (auto& el : al->elements) f(el); }
    else if (auto* m = dynamic_cast<MemberExpr*>(e))   { f(m->base); }
    else if (auto* ix = dynamic_cast<IndexExpr*>(e))   { f(ix->base); f(ix->index); if (ix->highIndex) f(ix->highIndex); }
    else if (auto* c = dynamic_cast<CastExpr*>(e))     { f(c->expr); }
    else if (auto* q = dynamic_cast<QuestionExpr*>(e)) { f(q->operand); }
    else if (auto* te = dynamic_cast<TernaryExpr*>(e)) { f(te->condition); f(te->thenExpr); f(te->elseExpr); }
    else if (auto* a = dynamic_cast<AwaitExpr*>(e))    { f(a->operand); }
    else if (auto* call = dynamic_cast<CallExpr*>(e))  { f(call->callee); for (auto& arg : call->args) f(arg); }
    else if (auto* tc = dynamic_cast<TemplateCallExpr*>(e)) { for (auto& arg : tc->args) f(arg); }
    else if (auto* si = dynamic_cast<StructInitExpr*>(e))   { for (auto& fi : si->fieldInits) f(fi.second); }
    else if (auto* aw = dynamic_cast<AllocWithExpr*>(e))    { f(aw->allocator); f(aw->count); }
    else if (auto* fc = dynamic_cast<FreeClosureExpr*>(e))  { f(fc->closure); }
    else if (auto* tcr = dynamic_cast<ThreadCreateExpr*>(e)) { f(tcr->worker); }
    // Leaves / scope boundaries (no child-expr to descend for these purposes):
    // LiteralExpr, IdentExpr, SizeofExpr, LambdaExpr.
}

// Every name spelled in a subtree: identifiers, declared locals/params (lambda bodies
// included), loop/catch/match binders, and member names. A pass that synthesizes a
// local (the async frame pointer, a range bound) picks a name outside this set, so it
// can never capture or shadow a user identifier.
inline void collectNames(Stmt* s, std::set<std::string>& out);
inline void collectNames(Expr* e, std::set<std::string>& out) {
    if (!e) return;
    if (auto* id = dynamic_cast<IdentExpr*>(e)) { out.insert(id->name); return; }
    if (auto* sz = dynamic_cast<SizeofExpr*>(e)) { out.insert(sz->typeName); return; }
    if (auto* m = dynamic_cast<MemberExpr*>(e)) out.insert(m->member);
    if (auto* lam = dynamic_cast<LambdaExpr*>(e)) {
        for (const auto& p : lam->params) out.insert(p.second);
        for (const auto& c : lam->captures) out.insert(c.first);
        collectNames(lam->body.get(), out);
        return;
    }
    forEachChildExpr(e, [&](ExprPtr& c) { collectNames(c.get(), out); });
}
inline void collectNames(Stmt* s, std::set<std::string>& out) {
    if (!s) return;
    auto E = [&](const ExprPtr& e) { collectNames(e.get(), out); };
    auto S = [&](const StmtPtr& st) { collectNames(st.get(), out); };
    if (auto* b = dynamic_cast<BlockStmt*>(s)) {
        for (auto& it : b->items) {
            if (std::holds_alternative<StmtPtr>(it)) { S(std::get<StmtPtr>(it)); continue; }
            if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get())) { out.insert(vd->name); E(vd->initializer); }
        }
    }
    else if (auto* i = dynamic_cast<IfStmt*>(s))        { E(i->condition); S(i->thenBranch); S(i->elseBranch); }
    else if (auto* f = dynamic_cast<ForStmt*>(s))       { S(f->init); E(f->condition); E(f->step); S(f->body); }
    else if (auto* fi = dynamic_cast<ForInStmt*>(s))    { out.insert(fi->varName); E(fi->iterable); S(fi->body); }
    else if (auto* w = dynamic_cast<WhileStmt*>(s))     { E(w->condition); S(w->body); }
    else if (auto* dw = dynamic_cast<DoWhileStmt*>(s))  { S(dw->body); E(dw->condition); }
    else if (auto* r = dynamic_cast<ReturnStmt*>(s))    { E(r->value); }
    else if (auto* sw = dynamic_cast<SwitchStmt*>(s))   { E(sw->subject); for (auto& c : sw->cases) { E(c.value); for (auto& st : c.stmts) S(st); } }
    else if (auto* m = dynamic_cast<MatchStmt*>(s))     { E(m->subject); for (auto& a : m->arms) { for (auto& bn : a.bindings) out.insert(bn); S(a.body); } }
    else if (auto* th = dynamic_cast<ThrowStmt*>(s))    { E(th->value); }
    else if (auto* t = dynamic_cast<TryStmt*>(s))       { S(t->body); for (auto& c : t->catches) { out.insert(c.name); S(c.body); } S(t->finally); }
    else if (auto* d = dynamic_cast<DeferStmt*>(s))     { S(d->body); }
    else if (auto* tj = dynamic_cast<ThreadJoinStmt*>(s)) { E(tj->tid); }
    else if (auto* a = dynamic_cast<AsmStmt*>(s))       { for (auto& in : a->inputs) E(in.second); }
    else if (auto* es = dynamic_cast<ExprStmt*>(s))     { E(es->expr); }
}

// `base` if it is not in `used`, else the first free `base_1`, `base_2`, ...; the
// chosen name is added to `used`.
inline std::string freshName(const std::string& base, std::set<std::string>& used) {
    std::string n = base;
    for (int k = 1; used.count(n); ++k) n = base + "_" + std::to_string(k);
    used.insert(n);
    return n;
}

}  // namespace astwalk
