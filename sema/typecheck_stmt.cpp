#include "type_checker.h"
#include <functional>
#include <set>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with codegen; see template_utils.h.
#include "../template_utils.h"
#include "../ast/type_qual.h"

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
                if (thenExits && !elseExits) condNarrowings(ifs->condition.get(), false, keys);
                else if (elseExits && !thenExits) condNarrowings(ifs->condition.get(), true, keys);
                for (auto& k : applyNarrowings(keys)) guardNarrowed.push_back(k);
            }
        }
    }
    undoNarrowings(guardNarrowed);

    popScope();
}

void TypeChecker::visit(IfStmt* node) {
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        checkCondition(node, node->condition.get());
    }
    // Null-narrowing: `if (q != null)` proves `q` non-null in the then-branch (and
    // `if (q == null)` in the else-branch), so a `?*T` may be dereferenced there. An
    // assignment to `q` inside the branch ends the narrowing (see visit(BinaryExpr)).
    if (node->thenBranch) {
        std::vector<std::string> keys;
        condNarrowings(node->condition.get(), true, keys);
        auto inserted = applyNarrowings(keys);
        node->thenBranch->accept(this);
        undoNarrowings(inserted);
    }
    if (node->elseBranch) {
        std::vector<std::string> keys;
        condNarrowings(node->condition.get(), false, keys);
        auto inserted = applyNarrowings(keys);
        node->elseBranch->accept(this);
        undoNarrowings(inserted);
    }
}

void TypeChecker::visit(ForInStmt* node) {
    node->iterable->accept(this);
    std::string itType = getExpressionType(node->iterable.get());

    // Determine the element type the loop variable will bind.
    std::string elemType;
    ty::Type itT = ty::Type::parse(itType);
    if ((itT.kind == ty::Type::Kind::Array || itT.kind == ty::Type::Kind::Slice) && itT.elem) {
        // A fixed-size array (or slice): the element is one step in, so the rows of an
        // `int[2][3]` are `int[3]` (the leftmost bracket is the outer dimension).
        elemType = normalizeType(itT.elem->str());
        node->isArrayIter = true;
        node->arrayDim = itT.dim;
    } else {
        std::string s = ty::Type::parse(itType).nominalName();
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
    dropAssignedIn(node->body.get());   // later iterations see assignments in the body
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
    dropAssignedIn(node->body.get());
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
    dropAssignedIn(node->body.get());
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

    dropAssignedIn(node->condition.get());
    dropAssignedIn(node->step.get());
    dropAssignedIn(node->body.get());

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

    // Type check step
    if (node->step) {
        node->step->accept(this);
    }
    undoNarrowings(inserted);

    popScope();
}

void TypeChecker::visit(ReturnStmt* node) {
    if (node->value) {
        node->value->accept(this);
        // Returning the address of a local or parameter yields a dangling pointer
        // (its stack frame is gone on return). Flag the clear case `return &x` where
        // x is a local/param; `&(*ptr)` or `&ptrParam.field` point into caller memory
        // and are fine, so they are not flagged.
        if (auto* u = dynamic_cast<UnaryExpr*>(node->value.get()); u && u->op == "&") {
            if (auto* id = dynamic_cast<IdentExpr*>(u->operand.get())) {
                int defIdx = -1;
                for (int si = (int)scopes.size() - 1; si >= 0; --si)
                    if (scopes[si].count(id->name)) { defIdx = si; break; }
                if (defIdx >= 1)   // a function-scope local/param, not a global (index 0)
                    errorAt(node, "returning the address of local '" + id->name +
                                  "' (dangling pointer)");
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
        std::string fn;
        if (auto* c = dynamic_cast<CallExpr*>(node->expr.get())) {
            if (auto* id = dynamic_cast<IdentExpr*>(c->callee.get())) fn = id->name;
            else if (auto* m = dynamic_cast<MemberExpr*>(c->callee.get())) {
                // Method-call syntax `x.m()` resolves to `Type_m` (see visit(CallExpr)).
                std::string bt = ty::Type::parse(getExpressionType(m->base.get())).nominalName();
                if (mustUseFuncs.count(bt + "_" + m->member)) fn = bt + "_" + m->member;
            }
        } else if (auto* tc = dynamic_cast<TemplateCallExpr*>(node->expr.get())) {
            fn = tc->templateName;
        }
        if (!fn.empty() && mustUseFuncs.count(fn))
            errorAt(node->line > 0 ? static_cast<ASTNode*>(node) : node->expr.get(),
                    "result of '" + fn + "' must be used (it is marked must_use)");
    }
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

void TypeChecker::visit(AsmStmt* node) {
    for (auto& [constraint, expr] : node->inputs)
        if (expr) expr->accept(this);
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

void TypeChecker::visit(DeferStmt* node) {
    // Type-check the deferred body in its own scope.
    pushScope();
    if (node->body) node->body->accept(this);
    popScope();

    // A defer body runs during scope-exit cleanup, so it may not transfer control
    // out of itself: a `return`, or a `break`/`continue` not enclosed by a loop or
    // switch *within* the body, would jump to a target that is no longer valid.
    std::function<void(Stmt*, int)> check = [&](Stmt* s, int loopDepth) {
        if (!s) return;
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
            for (auto& it : b->items)
                if (auto* st = std::get_if<StmtPtr>(&it)) check(st->get(), loopDepth);
        } else if (auto* i = dynamic_cast<IfStmt*>(s)) {
            check(i->thenBranch.get(), loopDepth);
            check(i->elseBranch.get(), loopDepth);
        } else if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            check(w->body.get(), loopDepth + 1);
        } else if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) {
            check(dw->body.get(), loopDepth + 1);
        } else if (auto* f = dynamic_cast<ForStmt*>(s)) {
            check(f->body.get(), loopDepth + 1);
        } else if (auto* fi = dynamic_cast<ForInStmt*>(s)) {
            check(fi->body.get(), loopDepth + 1);
        } else if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
            for (auto& c : sw->cases) for (auto& st : c.stmts) check(st.get(), loopDepth + 1);
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
            if (!covered.count(m.first) && !(plain && valueArm.count(m.second)))   // an equal-valued arm covers it
                missing += (missing.empty() ? "" : ", ") + m.first;
        if (!missing.empty())
            errorAt(node, "non-exhaustive match on " + st + ": missing " + missing +
                          " (add those arms or a `_` default)");
    }
}

void TypeChecker::visit(SwitchStmt* node) {
    node->subject->accept(this);
    std::string subjType = getExpressionType(node->subject.get());
    if (subjType != "unknown" && !isIntType(normalizeType(subjType)))   // a classic enum counts as int
        errorAt(node,"switch subject must be integer type, got " + subjType);
    std::set<long long> seenCases;   // detect duplicate case values (else codegen
                                     // emits a switch the IR verifier rejects)
    bool seenDefault = false;
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
        ++switchDepth;
        for (auto& s : c.stmts) s->accept(this);
        --switchDepth;
    }
}

bool TypeChecker::isLvalueExpr(Expr* e) {
    if (auto* id = dynamic_cast<IdentExpr*>(e))
        return !lookupSymbol(id->name).empty() || !functionSignatures.count(id->name);
    if (auto* u = dynamic_cast<UnaryExpr*>(e)) return u->op == "*";
    if (dynamic_cast<MemberExpr*>(e)) return true;
    if (auto* ix = dynamic_cast<IndexExpr*>(e)) return !ix->highIndex && ix->opFunc.empty();
    return false;
}

bool TypeChecker::isConstIntExpr(Expr* e) {
    if (auto* l = dynamic_cast<LiteralExpr*>(e))
        return l->kind == LiteralExpr::Kind::INT || l->kind == LiteralExpr::Kind::CHAR ||
               l->kind == LiteralExpr::Kind::BOOL;
    if (auto* id = dynamic_cast<IdentExpr*>(e)) {
        std::string t = lookupSymbol(id->name);
        if (t.empty()) return enumConstants.count(id->name) > 0;
        return isConstSymbol(id->name) && isIntType(normalizeType(t));   // `const int K = 7` folds
    }
    if (auto* u = dynamic_cast<UnaryExpr*>(e))
        return (u->op == "-" || u->op == "~" || u->op == "!") && isConstIntExpr(u->operand.get());
    if (auto* b = dynamic_cast<BinaryExpr*>(e))
        return b->op != "=" && b->op != "&&" && b->op != "||" &&
               isConstIntExpr(b->left.get()) && isConstIntExpr(b->right.get());
    if (auto* c = dynamic_cast<CastExpr*>(e)) return isConstIntExpr(c->expr.get());
    if (dynamic_cast<SizeofExpr*>(e)) return true;
    return false;
}
