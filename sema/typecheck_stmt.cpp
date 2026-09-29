#include "type_checker.h"
#include <climits>
#include <functional>
#include <set>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with codegen; see template_utils.h.
#include "../template_utils.h"
#include "../ast/type_qual.h"
#include "../ast/ast_walk.h"

// ============================================================================

// TypeChecker — statement visitors (blocks, control flow, match/switch,
// try/throw, threads).
// Part of the type_checker.cpp split; see type_checker.h.

// Statement visitors
void TypeChecker::visit(BlockStmt* node) {
    pushScope();

    // Type check items in order, maintaining exact parse order
    // Declarations can be interleaved with statements
    std::vector<std::string> guardNarrowed;   // narrowings from an early-exit guard, undone at block end
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
            // Early-exit guard: after `if (p == null) return ...;` the rest of the block
            // only runs when the condition was false (resp. true, when only the else exits).
            if (auto* ifs = dynamic_cast<IfStmt*>(stmt.get()); ifs && ifs->condition) {
                bool thenExits = !stmtCanCompleteNormally(ifs->thenBranch.get());
                bool elseExits = ifs->elseBranch && !stmtCanCompleteNormally(ifs->elseBranch.get());
                std::vector<std::string> keys;
                // A call on the path that falls through may have assigned a global.
                if (thenExits && !elseExits) {
                    condNarrowings(ifs->condition.get(), false, keys);
                    if (lastIfElseCalled) dropGlobalKeys(keys);
                } else if (elseExits && !thenExits) {
                    condNarrowings(ifs->condition.get(), true, keys);
                    if (lastIfThenCalled) dropGlobalKeys(keys);
                }
                for (auto& k : applyNarrowings(keys)) guardNarrowed.push_back(k);
                // The branch that falls through may have assigned the variable.
                if (thenExits && !elseExits) dropAssignedIn(ifs->elseBranch.get());
                else if (elseExits && !thenExits) dropAssignedIn(ifs->thenBranch.get());
            }
        }
    }
    undoNarrowings(guardNarrowed);

    popScope();
}

void TypeChecker::visit(IfStmt* node) {
    // An `else if` chain is walked with a loop; each link's else-narrowings stay in
    // force for the links after it and are undone, innermost first, at the end.
    std::vector<std::vector<std::string>> elseNarrowed;
    int thenStart = callEpoch, thenEnd = callEpoch;
    for (IfStmt* n = node; n;) {
        if (n->condition) {
            warnAssignInCondition(n->condition.get());
            checkCondition(n, n->condition.get());
        }
        // Null-narrowing: `if (q != null)` proves `q` non-null in the then-branch (and
        // `if (q == null)` in the else-branch), so a `?*T` may be dereferenced there. An
        // assignment to `q` inside the branch ends the narrowing (see visit(BinaryExpr)).
        if (n->thenBranch) {
            std::vector<std::string> keys;
            condNarrowings(n->condition.get(), true, keys);
            auto inserted = applyNarrowings(keys);
            if (n == node) thenStart = callEpoch;
            n->thenBranch->accept(this);
            if (n == node) thenEnd = callEpoch;
            undoNarrowings(inserted);
        }
        IfStmt* next = nullptr;
        if (n->elseBranch) {
            std::vector<std::string> keys;
            condNarrowings(n->condition.get(), false, keys);
            elseNarrowed.push_back(applyNarrowings(keys));
            next = dynamic_cast<IfStmt*>(n->elseBranch.get());
            if (!next) n->elseBranch->accept(this);
        }
        n = next;
    }
    for (size_t i = elseNarrowed.size(); i-- > 0;) undoNarrowings(elseNarrowed[i]);
    lastIfThenCalled = thenEnd != thenStart;
    lastIfElseCalled = callEpoch != thenEnd;
}

void TypeChecker::visit(ForInStmt* node) {
    node->iterable->accept(this);
    std::string itType = getExpressionType(node->iterable.get());

    // Determine the element type the loop variable will bind.
    std::string elemType;
    ty::Type itT = ty::Type::parse(dealiasOperand(itType));   // through an alias (`*p`, p: *A4)
    if ((itT.kind == ty::Type::Kind::Array || itT.kind == ty::Type::Kind::Slice) && itT.elem) {
        // A fixed-size array (or slice): the element is one step in, so the rows of an
        // `int[2][3]` are `int[3]` (the leftmost bracket is the outer dimension).
        elemType = normalizeType(itT.elem->str());
        if (!inInstance) { node->isArrayIter = true; node->arrayDim = itT.dim; }
    } else {
        ty::Type base = ty::Type::parse(normalizeType(itType));
        while (base.isPointer() && base.pointee) { ty::Type p = *base.pointee; base = p; }
        std::string s = base.isTemplate() ? mangleTemplate(base.str()) : base.nominalName();
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

    if (!inInstance) { node->resolvedElemType = elemType; node->resolvedIterType = itType; }
    dropAssignedIn(node->body.get()); markAddrTakenIn(node->body.get());   // later iterations see assignments in the body
    pushScope();
    if (elemType.empty()) {
        errorAt(node, "for-in expects a fixed-size array or a List-like value "
                      "(with `data` and `size` fields), got " + itType);
        defineSymbol(node->varName, "unknown");
    } else {
        defineSymbol(node->varName, elemType, node->line, node->col, /*isParam=*/false);
    }
    loopLabelStack.push_back(node->label);
    if (node->body) node->body->accept(this);
    loopLabelStack.pop_back();
    popScope();
}

void TypeChecker::visit(WhileStmt* node) {
    // A narrowing from outside the loop does not survive an assignment in the body
    // (the condition and later iterations would observe it).
    dropAssignedIn(node->body.get()); markAddrTakenIn(node->body.get());
    dropAssignedIn(node->condition.get());
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        checkCondition(node, node->condition.get());
    }
    loopLabelStack.push_back(node->label);
    if (node->body) {
        // `while (p != null)` re-tests p before every iteration, so the body sees it non-null.
        std::vector<std::string> keys;
        if (node->condition) condNarrowings(node->condition.get(), true, keys);
        auto inserted = applyNarrowings(keys);
        node->body->accept(this);
        undoNarrowings(inserted);
    }
    loopLabelStack.pop_back();
}

void TypeChecker::visit(DoWhileStmt* node) {
    dropAssignedIn(node->body.get()); markAddrTakenIn(node->body.get());
    dropAssignedIn(node->condition.get());
    loopLabelStack.push_back(node->label);
    if (node->body) node->body->accept(this);
    loopLabelStack.pop_back();
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        checkCondition(node, node->condition.get());
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
    // `for (i in A..B)`: the loop variable and the bound share the bounds' common type.
    if (auto* rb = dynamic_cast<BlockStmt*>(node->init.get())) {
        if (rb->items.size() == 2 && std::holds_alternative<DeclPtr>(rb->items[0]) &&
            std::holds_alternative<DeclPtr>(rb->items[1])) {
            auto* lo = dynamic_cast<VarDecl*>(std::get<DeclPtr>(rb->items[0]).get());
            auto* hi = dynamic_cast<VarDecl*>(std::get<DeclPtr>(rb->items[1]).get());
            if (lo && hi && lo->rangeBound && hi->rangeBound) {
                std::string ct = ty::rangeVarType(lo->type, hi->type);
                if (!ct.empty()) {
                    lo->type = hi->type = ct;
                    for (VarDecl* d : {lo, hi}) {
                        auto it = scopes.back().find(d->name);
                        if (it != scopes.back().end()) it->second.type = ct;
                    }
                }
            }
        }
    }

    dropAssignedIn(node->condition.get());
    dropAssignedIn(node->step.get());
    dropAssignedIn(node->body.get()); markAddrTakenIn(node->body.get());

    // Type check condition (for intentionally omits the assign-in-condition warning)
    if (node->condition) {
        checkCondition(node, node->condition.get());
    }

    // Type check body, then the step (which runs after it), both under the
    // condition's narrowing (`for (; p != null; p = p.next)`).
    std::vector<std::string> keys;
    if (node->condition) condNarrowings(node->condition.get(), true, keys);
    auto inserted = applyNarrowings(keys);
    loopLabelStack.push_back(node->label);
    if (node->body) {
        node->body->accept(this);
    }
    loopLabelStack.pop_back();

    // Type check step (its value is discarded, like an expression statement's)
    if (node->step) {
        node->step->accept(this);
        if (std::string fn = discardedMustUse(node->step.get()); !fn.empty())
            errorAt(node->step.get(), "result of '" + fnDisplay(fn) + "' must be used (it is marked must_use)");
    }
    undoNarrowings(inserted);

    popScope();
}

void TypeChecker::visit(ReturnStmt* node) {
    if (finallyDepth > 0) errorAt(node, "'return' is not allowed inside a finally block");
    if (node->value) {
        hintIfaceTarget(node->value.get(), currentFunctionReturnType);
        inferVariantTarget(node->value, currentFunctionReturnType);
        node->value->accept(this);
        // Returning the address of a local or parameter yields a dangling pointer
        // (its stack frame is gone on return). Flag the clear case `return &x` where
        // x is a local/param; `&(*ptr)` or `&ptrParam.field` point into caller memory
        // and are fine, so they are not flagged.
        // A field or element of a local value (`&p.a`, `&arr[1]`) and a slice of a local
        // array dangle the same way; a `static` local lives on.
        // The value may reach the return through a pointer cast or either `?:` arm.
        std::vector<Expr*> leaves{node->value.get()};
        while (!leaves.empty()) {
            Expr* v = leaves.back();
            leaves.pop_back();
            if (auto* c = dynamic_cast<CastExpr*>(v)) {
                std::string ct = normalizeType(c->targetType);
                if (!ct.empty() && ct[0] == '?') ct = ct.substr(1);
                if (isPointerType(ct)) { leaves.push_back(c->expr.get()); continue; }
            }
            if (auto* t = dynamic_cast<TernaryExpr*>(v)) {
                leaves.push_back(t->elseExpr.get());
                leaves.push_back(t->thenExpr.get());
                continue;
            }
            if (auto* u = dynamic_cast<UnaryExpr*>(v); u && u->op == "&") {
                std::string root = localStorageRoot(u->operand.get());
                if (!root.empty())
                    errorAt(node, "returning the address of local '" + root + "' (dangling pointer)");
            }
            if (auto* ix = dynamic_cast<IndexExpr*>(v);
                ix && ix->highIndex && ty::Type::parse(getExpressionType(ix->base.get())).kind == ty::Type::Kind::Array) {
                std::string root = localStorageRoot(ix->base.get());
                if (!root.empty())
                    errorAt(node, "returning a slice of local array '" + root + "' (dangling)");
            }
        }
        std::string valueType = getExpressionType(node->value.get());
        // A returned integer literal that fits the return type stays valid; other
        // narrowing needs an explicit cast (same rule as init / assignment).
        std::string e = assignabilityError(currentFunctionReturnType, valueType, node->value.get());
        if (!e.empty())
            errorAt(node, "return type mismatch: expected " + currentFunctionReturnType +
                          ", got " + valueType + " (" + e + ")");
    } else if (currentFunctionReturnType != "void") {
        errorAt(node,"return type mismatch: expected " + currentFunctionReturnType +
                    ", got void");
    }
}

// The local (or parameter) whose own storage `e` denotes: a variable, or a field / element
// of a value (not one reached through a pointer). "" for anything else, a global, or a
// `static` local.
std::string TypeChecker::localStorageRoot(Expr* e) {
    while (e) {
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            int si = scopeOf(id->name);
            if (si < 1 || scopes[si].find(id->name)->second.isStatic) return "";
            return id->name;
        }
        if (auto* m = dynamic_cast<MemberExpr*>(e)) {
            if (tyq::isPtr(getExpressionType(m->base.get()))) return "";
            e = m->base.get();
        } else if (auto* ix = dynamic_cast<IndexExpr*>(e); ix && !ix->highIndex) {
            if (ty::Type::parse(getExpressionType(ix->base.get())).kind != ty::Type::Kind::Array) return "";
            e = ix->base.get();
        } else {
            return "";
        }
    }
    return "";
}

void TypeChecker::visit(BreakStmt* node) {
    // A bare break needs an enclosing loop or switch of the SAME function (a lambda body
    // starts a fresh context); a labeled break must name an enclosing loop label.
    if (node->label.empty() && loopLabelStack.empty() && switchDepth == 0)
        errorAt(node, "'break' outside of a loop or switch");
    if (!node->label.empty()) {
        bool found = false;
        for (auto& l : loopLabelStack) if (l == node->label) { found = true; break; }
        if (!found) errorAt(node, "labeled 'break " + node->label + "' has no enclosing loop labeled '" + node->label + "'");
    }
}

void TypeChecker::visit(ExprStmt* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
    // A bare call to a `must_use` function discards its result — reject it.
    if (!mustUseFuncs.empty() && node->expr) {
        std::string fn = discardedMustUse(node->expr.get());
        if (!fn.empty())
            errorAt(node->line > 0 ? static_cast<ASTNode*>(node) : node->expr.get(),
                    "result of '" + fnDisplay(fn) + "' must be used (it is marked must_use)");
    }
}

// The `must_use` function whose result `e`, evaluated for its side effects only, would
// discard: a call (also through a method or an operator) or either arm of a `?:`. "" if none.
std::string TypeChecker::discardedMustUse(Expr* e) {
    std::string fn;
    if (auto* c = dynamic_cast<CallExpr*>(e)) {
        if (auto* id = dynamic_cast<IdentExpr*>(c->callee.get())) fn = id->name;
        else if (auto* m = dynamic_cast<MemberExpr*>(c->callee.get())) {
            // Method-call syntax `x.m()` resolves to `Type_m` (see visit(CallExpr)).
            std::string bt = ty::Type::parse(getExpressionType(m->base.get())).nominalName();
            if (mustUseFuncs.count(bt + "_" + m->member)) fn = bt + "_" + m->member;
            else if (auto ti = templateInstanceArgs.find(
                         ty::Type::parse(normalizeType(getExpressionType(m->base.get()))).nominalName());
                     ti != templateInstanceArgs.end())
                fn = ti->second.first + "_" + m->member;   // a generic `S_m<T>` (checkGenericMethodCall)
        }
    } else if (auto* tc = dynamic_cast<TemplateCallExpr*>(e)) {
        fn = tc->templateName;
    } else if (auto* t = dynamic_cast<TernaryExpr*>(e)) {
        fn = discardedMustUse(t->thenExpr.get());
        if (fn.empty()) fn = discardedMustUse(t->elseExpr.get());
    } else if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
        fn = b->opFunc;
    } else if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
        fn = u->opFunc;
    } else if (auto* ix = dynamic_cast<IndexExpr*>(e)) {
        fn = ix->opFunc;
    }
    return (!fn.empty() && mustUseFuncs.count(fn)) ? fn : "";
}

void TypeChecker::visit(ContinueStmt* node) {
    // Valid inside a loop of the same function. A labeled continue must name an
    // enclosing loop label.
    if (node->label.empty() && loopLabelStack.empty())
        errorAt(node, "'continue' outside of a loop");
    if (!node->label.empty()) {
        bool found = false;
        for (auto& l : loopLabelStack) if (l == node->label) { found = true; break; }
        if (!found) errorAt(node, "labeled 'continue " + node->label + "' has no enclosing loop labeled '" + node->label + "'");
    }
}

// Extended asm: an output (`"=r"(y)`, `"+r"(y)`, `"=m"(y)`) is written by the asm, so it
// must be a writable lvalue of a scalar type (an integer other than bool, a float or a
// pointer); an input's constraint cannot be an output's.
void TypeChecker::visit(AsmStmt* node) {
    for (auto& [constraint, expr] : node->outputs) {
        if (!expr) continue;
        expr->accept(this);
        if (constraint.empty() || (constraint[0] != '=' && constraint[0] != '+')) {
            errorAt(node, "asm output constraint '" + constraint + "' must start with '=' or '+'");
            continue;
        }
        if (isSliceLen(expr.get()) || !isLvalueExpr(expr.get())) {
            errorAt(node, "asm output operand must be an lvalue (a variable, field, element, or dereference)");
            continue;
        }
        std::string cname;
        if (assignsToConst(expr.get(), cname))
            errorAt(node, "asm output operand is read-only ('" + cname + "')");
        checkCapturedWrite(node, expr.get());
        if (auto* m = dynamic_cast<MemberExpr*>(expr.get())) {
            std::string bt = ty::Type::parse(normalizeType(tyq::strip(getExpressionType(m->base.get())))).nominalName();
            auto sit = structs.find(bt);
            if (sit != structs.end())
                for (const auto& f : sit->second.fields)
                    if (f.name == m->member && f.bitWidth > 0)
                        errorAt(node, "asm output operand cannot be a bitfield ('" + m->member + "')");
        }
        std::string t = getExpressionType(expr.get());
        std::string n = normalizeType(t);
        if (!n.empty() && n[0] == '?') n = n.substr(1);
        bool scalar = (isNumericType(n) && n != "bool") || n == "string" ||
                      (isPointerType(n) && n.back() != ']');
        if (t != "unknown" && !scalar)
            errorAt(node, "asm output operand must have an integer, floating-point or pointer type, got '" + t + "'");
        if (auto* id = dynamic_cast<IdentExpr*>(expr.get()))
            narrowedNonNull.erase(narrowKey(id->name));   // the asm may store null
    }
    for (auto& [constraint, expr] : node->inputs) {
        if (!constraint.empty() && (constraint[0] == '=' || constraint[0] == '+'))
            errorAt(node, "asm input constraint '" + constraint + "' cannot start with '=' or '+'");
        if (expr) expr->accept(this);
    }
}

void TypeChecker::visit(ThreadJoinStmt* node) {
    node->tid->accept(this);
    dropGlobalNarrowings();   // the joined thread may have assigned any global
    std::string t = getExpressionType(node->tid.get());
    std::string n = normalizeType(t);
    if (!n.empty() && n[0] == '?') n = n.substr(1);
    if (t != "unknown" && n != "*void")
        errorAt(node, "thread_join expects a thread handle ('*void' from thread_create), got '" + t + "'");
}

void TypeChecker::visit(ThrowStmt* node) {
    if (node->value) {
        node->value->accept(this);
        if (isVoidValueType(getExpressionType(node->value.get())))
            errorAt(node, "cannot throw a 'void' value");
        if (!inInstance) node->valueType = getExpressionType(node->value.get());
    }
}

void TypeChecker::visit(TryStmt* node) {
    if (node->body) node->body->accept(this);
    for (auto& c : node->catches) {
        pushScope();
        defineSymbol(c.name, c.type, c.line, c.col, false);
        if (c.body) c.body->accept(this);
        popScope();
    }
    // A `finally` runs on every exit, including an exception unwinding through it, so a
    // `return` there would silently discard the pending exit (the Java/C# footgun).
    if (node->finally) {
        ++finallyDepth;
        node->finally->accept(this);
        --finallyDepth;
    }
}

void TypeChecker::visit(DeferStmt* node) {
    // Type-check the deferred body in its own scope.
    pushScope();
    if (node->body) node->body->accept(this);
    popScope();

    // A defer body runs during scope-exit cleanup, so it may not transfer control
    // out of itself: a `return`, or a `break`/`continue` not enclosed by a loop or
    // switch *within* the body, would jump to a target that is no longer valid. A `?`
    // is an early return too (a lambda in the body is its own function).
    std::function<void(Expr*)> checkExpr = [&](Expr* e) {
        if (!e || dynamic_cast<LambdaExpr*>(e)) return;
        if (dynamic_cast<QuestionExpr*>(e))
            errorAt(e, "'?' is not allowed inside a defer body (it would return from the function)");
        astwalk::forEachChildExprFlat(e, [&](ExprPtr& c) { checkExpr(c.get()); });
    };
    std::function<void(Stmt*, int)> check = [&](Stmt* s, int loopDepth) {
        if (!s) return;
        if (auto* es = dynamic_cast<ExprStmt*>(s)) checkExpr(es->expr.get());
        else if (auto* ts = dynamic_cast<ThrowStmt*>(s)) checkExpr(ts->value.get());
        if (dynamic_cast<ReturnStmt*>(s)) {
            errorAt(s, "'return' is not allowed inside a defer body");
        } else if (dynamic_cast<BreakStmt*>(s) || dynamic_cast<ContinueStmt*>(s)) {
            // A bare break/continue is fine if a loop/switch *inside* the defer body encloses
            // it (loopDepth>0). A *labeled* one may target a loop outside the defer body, so it
            // is rejected regardless of depth.
            std::string lbl;
            if (auto* bs = dynamic_cast<BreakStmt*>(s)) lbl = bs->label;
            else if (auto* cs = dynamic_cast<ContinueStmt*>(s)) lbl = cs->label;
            if (loopDepth == 0 || !lbl.empty())
                errorAt(s, "'break'/'continue' inside a defer body may not escape it");
        } else if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            for (auto& it : b->items) {
                if (auto* st = std::get_if<StmtPtr>(&it)) check(st->get(), loopDepth);
                else if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get())) checkExpr(vd->initializer.get());
            }
        } else if (auto* i = dynamic_cast<IfStmt*>(s)) {
            checkExpr(i->condition.get());
            check(i->thenBranch.get(), loopDepth);
            check(i->elseBranch.get(), loopDepth);
        } else if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            checkExpr(w->condition.get());
            check(w->body.get(), loopDepth + 1);
        } else if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) {
            checkExpr(dw->condition.get());
            check(dw->body.get(), loopDepth + 1);
        } else if (auto* f = dynamic_cast<ForStmt*>(s)) {
            check(f->init.get(), loopDepth);
            checkExpr(f->condition.get()); checkExpr(f->step.get());
            check(f->body.get(), loopDepth + 1);
        } else if (auto* fi = dynamic_cast<ForInStmt*>(s)) {
            checkExpr(fi->iterable.get());
            check(fi->body.get(), loopDepth + 1);
        } else if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
            checkExpr(sw->subject.get());
            for (auto& c : sw->cases)
                for (auto& it : c.stmts) {
                    if (auto* st = std::get_if<StmtPtr>(&it)) check(st->get(), loopDepth + 1);
                    else if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get())) checkExpr(vd->initializer.get());
                }
        } else if (auto* m = dynamic_cast<MatchStmt*>(s)) {
            checkExpr(m->subject.get());
            for (auto& a : m->arms) check(a.body.get(), loopDepth);
        } else if (auto* t = dynamic_cast<TryStmt*>(s)) {
            check(t->body.get(), loopDepth);
            for (auto& c : t->catches) check(c.body.get(), loopDepth);
            check(t->finally.get(), loopDepth);
        } else if (auto* d = dynamic_cast<DeferStmt*>(s)) {
            check(d->body.get(), loopDepth);
        }
    };
    if (node->body) check(node->body.get(), 0);
}

void TypeChecker::visit(MatchStmt* node) {
    node->subject->accept(this);
    std::string rawSt = getExpressionType(node->subject.get());
    std::string st = normalizeType(rawSt);
    // An alias of a classic enum (`type C = Col`) names that enum's variant set.
    if (plainEnumDecls.count(tyq::strip(dealiasOperand(rawSt)))) rawSt = tyq::strip(dealiasOperand(rawSt));
    // A classic (payload-less) enum normalizes to `int`, so keep its declared name for a
    // `match`: that is the only place the variant set (for exhaustiveness) is recoverable.
    if (!enumDecls.count(st) && !templateInstanceArgs.count(st) && plainEnumDecls.count(rawSt))
        st = rawSt;
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
    } else if (plainEnumDecls.count(st)) {                   // classic int enum (no payloads)
        ed = plainEnumDecls[st];
    }
    if (!ed && st != "unknown")
        errorAt(node, "match subject must be an enum, got " + st);
    if (!inInstance) node->enumName = st;
    // Index of a variant within `ed` by name (-1 if absent).
    auto variantIndex = [&](const std::string& v) -> int {
        if (!ed) return -1;
        for (size_t i = 0; i < ed->members.size(); ++i)
            if (ed->members[i].first == v) return (int)i;
        return -1;
    };
    bool hasDefault = false;
    std::set<std::string> covered;
    // A classic enum may give two members the same value (legal, as in C); a `match` is a
    // switch on the value, so two arms for equal values would be one duplicate case.
    bool plain = ed && plainEnumDecls.count(st) && plainEnumDecls[st] == ed;
    std::map<long long, std::string> valueArm;
    auto memberValue = [&](const std::string& v, long long& out) {
        for (const auto& m : ed->members) if (m.first == v) { out = m.second; return true; }
        return false;
    };
    for (size_t ai = 0; ai < node->arms.size(); ++ai) {
        auto& arm = node->arms[ai];
        if (arm.variant.empty()) {
            if (hasDefault) errorAt(node, "duplicate match arm for variant '_'");
            hasDefault = true;
            if (ai + 1 < node->arms.size())   // arms after `_` can never match
                warning(node->line, node->col, "match arms after the `_` default are unreachable");
        }
        else if (!covered.insert(arm.variant).second)
            errorAt(node, "duplicate match arm for variant '" + arm.variant + "'");
        else if (long long val = 0; plain && memberValue(arm.variant, val)) {
            auto [it, fresh] = valueArm.insert({val, arm.variant});
            if (!fresh)
                errorAt(node, "duplicate match value: '" + arm.variant + "' has the same value (" +
                              std::to_string(val) + ") as '" + it->second + "'");
        }
        pushScope();
        if (!arm.variant.empty() && ed) {
            int vi = variantIndex(arm.variant);
            if (vi < 0) {
                errorAt(node, "'" + arm.variant + "' is not a variant of " + st);
            } else {
                static const std::vector<std::string> noPayload;   // classic enums carry none
                const auto& payload = (vi < (int)ed->payloads.size()) ? ed->payloads[vi] : noPayload;
                if (arm.bindings.size() != payload.size())
                    errorAt(node, "variant '" + arm.variant + "' binds " +
                        std::to_string(payload.size()) + " field(s), got " +
                        std::to_string(arm.bindings.size()));
                std::set<std::string> bound;
                for (const auto& b : arm.bindings)
                    if (b != "_" && !bound.insert(b).second)
                        errorAt(node, "duplicate binding '" + b + "' in match arm '" + arm.variant + "'");
                // The async lowering keeps a binding that lives across an await in a frame
                // field of this type (a generic body's nodes are shared: not stamped).
                if (!inInstance) arm.bindingTypes.clear();
                for (size_t i = 0; i < arm.bindings.size() && i < payload.size(); ++i) {
                    std::string bt = normalizeType(substType(payload[i], subs));
                    defineSymbol(arm.bindings[i], bt, node->line, node->col, /*isParam=*/false);
                    if (!inInstance) arm.bindingTypes.push_back(bt);
                }
            }
        }
        if (arm.body) arm.body->accept(this);
        popScope();
    }
    // Exhaustiveness: without a `_` default, every variant must be covered.
    if (ed && !hasDefault) {
        std::string missing;
        for (const auto& m : ed->members)
            if (!covered.count(m.first) && !(plain && valueArm.count(m.second)))   // an equal-valued arm covers it
                missing += (missing.empty() ? "" : ", ") + m.first;
        if (!missing.empty())
            errorAt(node, "non-exhaustive match on " + st + ": missing " + missing +
                          " (add those arms or a `_` default)");
    }
}

// Does the folded case value `v` lie in the range of integer type `t`? A 64-bit subject
// holds every folded value (it is folded in 64 bits).
static bool caseValueFits(const std::string& raw, long long v) {
    std::string t = tyq::strip(raw);
    if (t == "bool") return v >= 0 && v <= 1;
    if (t == "char" || t == "uint8") return v >= 0 && v <= 255;
    if (t == "int8") return v >= -128 && v <= 127;
    if (t == "uint16") return v >= 0 && v <= 65535;
    if (t == "int16") return v >= -32768 && v <= 32767;
    if (t == "uint" || t == "uint32") return v >= 0 && v <= 4294967295LL;
    if (t == "int" || t == "int32") return v >= INT_MIN && v <= INT_MAX;
    return true;
}

void TypeChecker::visit(SwitchStmt* node) {
    node->subject->accept(this);
    std::string subjType = getExpressionType(node->subject.get());
    if (subjType != "unknown" && !isIntType(normalizeType(subjType)))   // a classic enum counts as int
        errorAt(node,"switch subject must be integer type, got " + subjType);
    std::set<long long> seenCases;   // detect duplicate case values (else codegen
                                     // emits a switch the IR verifier rejects)
    bool seenDefault = false;
    pushScope();                       // the switch body is one scope (C): a case's
                                       // declaration is visible in the cases after it
    for (auto& c : node->cases) {
        if (!c.value) {
            if (seenDefault) errorAt(node, "multiple 'default' labels in one switch");
            seenDefault = true;
        }
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
            if (!isConstIntExpr(c.value.get()))
                errorAt(c.value.get(), "switch case value must be a constant integer expression");
            // Fold the label to its value so two spellings of one value (`7` and
            // `(4 * 2) - 1`) are caught as duplicates, as in C.
            long long cv = 0;
            bool haveCv = foldConstInt(c.value.get(), cv);
            // A label the subject's type cannot hold never matches (and, truncated to that
            // type, could collide with another label): reject it.
            if (haveCv && subjType != "unknown" && !caseValueFits(normalizeType(subjType), cv)) {
                errorAt(c.value.get(), "case value " + std::to_string(cv) +
                                       " is out of range for switch subject type '" + subjType + "'");
                haveCv = false;
            }
            if (haveCv) {
                if (seenCases.count(cv))
                    errorAt(c.value.get(), "duplicate case value in switch");
                else
                    seenCases.insert(cv);
            }
        }
        ++switchDepth;
        for (auto& it : c.stmts) {
            if (auto* st = std::get_if<StmtPtr>(&it)) (*st)->accept(this);
            else std::get<DeclPtr>(it)->accept(this);
        }
        --switchDepth;
    }
    popScope();
}

bool TypeChecker::isLvalueExpr(Expr* e) {
    if (auto* id = dynamic_cast<IdentExpr*>(e))
        return !lookupSymbol(id->name).empty() || !functionSignatures.count(id->name);
    if (auto* u = dynamic_cast<UnaryExpr*>(e)) return u->op == "*";
    // A field or an array element is storage only when its struct or array is: through a
    // pointer (or a slice), or itself stored (`mk().x` and `arr()[0]` are temporaries).
    auto storedBase = [&](Expr* base) {
        ty::Type bt = ty::Type::parse(normalizeType(dealiasOperand(getExpressionType(base))));
        if (bt.kind != ty::Type::Kind::Array && bt.kind != ty::Type::Kind::Struct &&
            bt.kind != ty::Type::Kind::Template && bt.kind != ty::Type::Kind::Named)
            return true;
        return isLvalueExpr(base);
    };
    if (auto* m = dynamic_cast<MemberExpr*>(e)) return !isSliceLen(e) && storedBase(m->base.get());
    if (auto* ix = dynamic_cast<IndexExpr*>(e))
        return !ix->highIndex && ix->opFunc.empty() && storedBase(ix->base.get());
    return false;
}

// `s.len` of a slice: a read-only view of the fat pointer's length.
bool TypeChecker::isSliceLen(Expr* e) {
    auto* m = dynamic_cast<MemberExpr*>(e);
    if (!m || m->member != "len") return false;
    std::string bt = getExpressionType(m->base.get());
    if (!bt.empty() && bt[0] == '?') bt = bt.substr(1);
    return ty::Type::parse(bt).kind == ty::Type::Kind::Slice;
}

// Value of an integer constant expression built from literals, enum members, unary
// and binary operators and casts. Returns false when the value is not known here
// (`sizeof`, a non-constant name, division by zero), so callers never guess.
bool TypeChecker::foldConstInt(Expr* e, long long& out) {
    if (auto* l = dynamic_cast<LiteralExpr*>(e)) {
        if (l->kind == LiteralExpr::Kind::INT) {
            try { out = (long long)std::stoull(l->value, nullptr, 0); return true; }
            catch (...) { return false; }
        }
        if (l->kind == LiteralExpr::Kind::CHAR) { out = l->value.empty() ? 0 : (unsigned char)l->value[0]; return true; }
        if (l->kind == LiteralExpr::Kind::BOOL) { out = l->value == "true" ? 1 : 0; return true; }
        return false;
    }
    if (auto* id = dynamic_cast<IdentExpr*>(e)) {
        if (const Symbol* sym = findSymbol(id->name)) {
            // A `const` integer folds through its initializer (bounded, so a
            // self-referential const can't recurse forever).
            if (!sym->isConst || !sym->constInit || !isIntType(normalizeType(sym->type))) return false;
            if (foldDepth > 64) return false;
            ++foldDepth;
            bool ok = foldConstInt(sym->constInit, out);
            --foldDepth;
            if (ok) out = truncConstInt(normalizeType(sym->type), out);
            return ok;
        }
        auto it = enumConstants.find(id->name);
        if (it == enumConstants.end()) return false;
        out = it->second;
        return true;
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
        long long v;
        if (!foldConstInt(u->operand.get(), v)) return false;
        if (u->op == "-") { out = (long long)(0ULL - (unsigned long long)v); return true; }
        if (u->op == "~") { out = ~v; return true; }
        if (u->op == "!") { out = v == 0; return true; }
        return false;
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
        // A left-leaning chain (`A + B + C ...`) is folded along its spine with a loop.
        std::vector<BinaryExpr*> spine{b};
        while (auto* l = dynamic_cast<BinaryExpr*>(spine.back()->left.get())) spine.push_back(l);
        long long x;
        if (!foldConstInt(spine.back()->left.get(), x)) return false;
        for (size_t i = spine.size(); i-- > 0;) {
            const std::string& op = spine[i]->op;
            // `&&` / `||` short-circuit: an unevaluated right operand need not fold.
            if (op == "&&" || op == "||") {
                if ((op == "&&") == (x == 0)) { x = op == "||"; continue; }
                long long y;
                if (!foldConstInt(spine[i]->right.get(), y)) return false;
                x = y != 0;
                continue;
            }
            long long y;
            if (!foldConstInt(spine[i]->right.get(), y)) return false;
            if (!foldConstBinaryOp(op, x, y, x)) return false;
        }
        out = x;
        return true;
    }
    if (auto* t = dynamic_cast<TernaryExpr*>(e)) {
        long long c;
        if (!foldConstInt(t->condition.get(), c)) return false;
        return foldConstInt(c != 0 ? t->thenExpr.get() : t->elseExpr.get(), out);
    }
    if (auto* c = dynamic_cast<CastExpr*>(e)) {
        std::string to = tyq::strip(normalizeType(c->targetType));
        bool isInt = true; long long i = 0; double d = 0;
        if (!foldConstNum(c->expr.get(), isInt, i, d)) return false;
        if (!isInt) {
            // A floating value converts toward zero; out of range it has no value (C).
            if (!isIntType(to) || (to != "bool" && !floatConstFitsInt(d, to))) return false;
            if (to == "bool") i = d != 0;
            else i = d >= 9223372036854775808.0 ? (long long)(unsigned long long)d : (long long)d;
        }
        out = truncConstInt(to, i);
        return true;
    }
    if (auto* z = dynamic_cast<SizeofExpr*>(e)) {
        // A type whose target layout is known here (scalars, pointers, arrays, structs
        // and unions of those) folds; the rest is left to codegen.
        if (inInstance || z->operand) return false;
        std::string zt = z->typeName;
        if (!isPrimitiveType(zt) && !structs.count(zt) && !typeAliases.count(zt) && !enumTypes.count(zt)) {
            std::string vt = lookupSymbol(zt);
            if (!vt.empty() && vt != "unknown" && vt != "struct:" + zt) zt = tyq::strip(vt);
        }
        out = constSizeof(zt);
        return out != 0;
    }
    return false;
}

bool TypeChecker::foldConstNum(Expr* e, bool& isInt, long long& i, double& d) {
    auto toFloat = [](const std::string& t, double v) { return t == "float" ? (double)(float)v : v; };
    if (auto* l = dynamic_cast<LiteralExpr*>(e); l && l->kind == LiteralExpr::Kind::FLOAT) {
        isInt = false; d = std::strtod(l->value.c_str(), nullptr);
        return true;
    }
    if (auto* id = dynamic_cast<IdentExpr*>(e)) {
        const Symbol* sym = findSymbol(id->name);
        std::string t = sym ? tyq::strip(normalizeType(sym->type)) : "";
        if (sym && sym->isConst && sym->constInit && (t == "float" || t == "double")) {
            if (foldDepth > 64) return false;
            ++foldDepth;
            bool ok = foldConstNum(sym->constInit, isInt, i, d);
            --foldDepth;
            if (!ok) return false;
            if (isInt) d = (double)i;
            isInt = false; d = toFloat(t, d);
            return true;
        }
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e); u && u->op == "-") {
        if (!foldConstNum(u->operand.get(), isInt, i, d)) return false;
        if (isInt) i = (long long)(0ULL - (unsigned long long)i); else d = -d;
        return true;
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
        std::vector<BinaryExpr*> spine{b};
        while (auto* l = dynamic_cast<BinaryExpr*>(spine.back()->left.get())) spine.push_back(l);
        if (!foldConstNum(spine.back()->left.get(), isInt, i, d)) return false;
        for (size_t k = spine.size(); k-- > 0;) {
            bool yInt = true; long long yi = 0; double yd = 0;
            const std::string& op = spine[k]->op;
            if (!foldConstNum(spine[k]->right.get(), yInt, yi, yd)) return false;
            if (isInt && yInt) {
                if (!foldConstBinaryOp(op, i, yi, i)) return false;
                continue;
            }
            double x = isInt ? (double)i : d, y = yInt ? (double)yi : yd;
            if (op == "+") d = x + y;
            else if (op == "-") d = x - y;
            else if (op == "*") d = x * y;
            else if (op == "/") d = x / y;
            else return false;
            isInt = false;
        }
        return true;
    }
    if (auto* c = dynamic_cast<CastExpr*>(e)) {
        std::string to = tyq::strip(normalizeType(c->targetType));
        if (to == "float" || to == "double") {
            if (!foldConstNum(c->expr.get(), isInt, i, d)) return false;
            if (isInt) d = (double)i;
            isInt = false; d = toFloat(to, d);
            return true;
        }
    }
    isInt = true;
    return foldConstInt(e, i);
}

// `v` converted to the integer type `raw` (C: truncate, then sign- or zero-extend);
// a non-integer or 64-bit type leaves it unchanged.
long long TypeChecker::truncConstInt(const std::string& raw, long long v) {
    std::string t = tyq::strip(raw);
    if (t == "bool") return v != 0;
    if (t == "char" || t == "uint8") return (long long)(uint8_t)v;
    if (t == "int8") return (long long)(int8_t)v;
    if (t == "uint16") return (long long)(uint16_t)v;
    if (t == "int16") return (long long)(int16_t)v;
    if (t == "uint" || t == "uint32") return (long long)(uint32_t)v;
    if (t == "int" || t == "int32") return (long long)(int32_t)v;
    return v;
}

// `x op y` over folded integer operands (two's-complement wrap); false when `op` does
// not fold or the operation is undefined.
bool TypeChecker::foldConstBinaryOp(const std::string& op, long long x, long long y, long long& out) {
    unsigned long long ux = (unsigned long long)x, uy = (unsigned long long)y;
    if (op == "+") out = (long long)(ux + uy);
    else if (op == "-") out = (long long)(ux - uy);
    else if (op == "*") out = (long long)(ux * uy);
    else if (op == "/" || op == "%") {
        if (y == 0 || (x == LLONG_MIN && y == -1)) return false;
        out = op == "/" ? x / y : x % y;
    }
    else if (op == "&") out = x & y;
    else if (op == "|") out = x | y;
    else if (op == "^") out = x ^ y;
    else if (op == "<<") { if (y < 0 || y > 63) return false; out = (long long)(ux << y); }
    else if (op == ">>") { if (y < 0 || y > 63) return false; out = x >> y; }
    else if (op == "==") out = x == y;
    else if (op == "!=") out = x != y;
    else if (op == "<") out = x < y;
    else if (op == ">") out = x > y;
    else if (op == "<=") out = x <= y;
    else if (op == ">=") out = x >= y;
    else return false;
    return true;
}

bool TypeChecker::isConstIntExpr(Expr* e) {
    if (auto* l = dynamic_cast<LiteralExpr*>(e))
        return l->kind == LiteralExpr::Kind::INT || l->kind == LiteralExpr::Kind::CHAR ||
               l->kind == LiteralExpr::Kind::BOOL;
    if (auto* id = dynamic_cast<IdentExpr*>(e)) {
        std::string t = lookupSymbol(id->name);
        if (t.empty()) return enumConstants.count(id->name) > 0;
        const Symbol* sym = findSymbol(id->name);   // `const int K = 7` folds (a const parameter does not)
        return sym && sym->isConst && sym->constInit && isIntType(normalizeType(t));
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e))
        return (u->op == "-" || u->op == "~" || u->op == "!") && isConstIntExpr(u->operand.get());
    if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
        // Down the left spine with a loop (a long `A + B + C ...` is as deep as it is long).
        for (;;) {
            if (b->op == "=" || b->op == "&&" || b->op == "||") return false;
            if (!isConstIntExpr(b->right.get())) return false;
            auto* l = dynamic_cast<BinaryExpr*>(b->left.get());
            if (!l) return isConstIntExpr(b->left.get());
            b = l;
        }
    }
    if (auto* c = dynamic_cast<CastExpr*>(e)) return isConstIntExpr(c->expr.get());
    if (dynamic_cast<SizeofExpr*>(e)) return true;
    return false;
}
