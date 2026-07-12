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
#include "ast.h"

namespace astwalk {

inline void forEachChildExpr(Expr* e, const std::function<void(ExprPtr&)>& f) {
    if (!e) return;
    if (auto* b = dynamic_cast<BinaryExpr*>(e))        { f(b->left); f(b->right); }
    else if (auto* u = dynamic_cast<UnaryExpr*>(e))    { f(u->operand); }
    else if (auto* id = dynamic_cast<IncDecExpr*>(e))  { f(id->operand); }
    else if (auto* al = dynamic_cast<ArrayLitExpr*>(e)){ for (auto& el : al->elements) f(el); }
    else if (auto* m = dynamic_cast<MemberExpr*>(e))   { f(m->base); }
    else if (auto* ix = dynamic_cast<IndexExpr*>(e))   { f(ix->base); f(ix->index); }
    else if (auto* c = dynamic_cast<CastExpr*>(e))     { f(c->expr); }
    else if (auto* q = dynamic_cast<QuestionExpr*>(e)) { f(q->operand); }
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

}  // namespace astwalk
