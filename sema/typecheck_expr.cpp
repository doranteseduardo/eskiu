#include "type_checker.h"
#include <algorithm>
#include <cstdint>
#include <set>

// Template type-name utilities (mangleTemplate / splitTemplateType / substType)
// are shared with codegen; see template_utils.h.
#include "../template_utils.h"
#include "../ast/type_qual.h"
#include "../ast/ast_walk.h"

// ============================================================================

// TypeChecker — expression visitors (operators, calls, member/index access,
// literals, lambdas, await, template calls, struct init).
// Part of the type_checker.cpp split; see type_checker.h.

// Expression visitors
// `lhs = rhs` (also the desugared compound `x op= y`): the target must be a writable
// lvalue, the value assignable to it. Kept out of visit(BinaryExpr) so that visitor's
// frame stays small (it recurses once per operator of a long expression chain).
[[gnu::noinline]] void TypeChecker::checkAssignment(BinaryExpr* node) {
    // Assigning to a `const` binding, a field/element of a const value, or
    // through a pointer-to-const (`const T*`) is an error. See assignsToConst.
    if (!isLvalueExpr(node->left.get()))
        errorAt(node, "cannot assign to this expression: it is not a variable, field, element, or dereference");
    std::string cname;
    if (assignsToConst(node->left.get(), cname))
        errorAt(node, "cannot assign to read-only location '" + cname + "'");
    std::string lt = getExpressionType(node->left.get());
    // The target of `p = ...` keeps its declared type even while `p` is narrowed,
    // and the assignment ends the narrowing (unless it stores an address `&x`).
    if (auto* id = dynamic_cast<IdentExpr*>(node->left.get())) {
        std::string declared = lookupSymbol(id->name);
        if (!declared.empty() && declared[0] == '?') {
            lt = declared;
            // `p = &x` stores an address, and `p = p + k` / `p += k` steps a non-null
            // pointer; any other value may be null.
            auto* u = dynamic_cast<UnaryExpr*>(node->right.get());
            auto* ar = dynamic_cast<BinaryExpr*>(node->right.get());
            auto* self = ar ? dynamic_cast<IdentExpr*>(ar->left.get()) : nullptr;
            bool keeps = (u && u->op == "&") ||
                         (ar && (ar->op == "+" || ar->op == "-") && self && self->name == id->name);
            if (!keeps) narrowedNonNull.erase(narrowKey(id->name));
        }
    }
    std::string rt = getExpressionType(node->right.get());
    if (dropsConstQual(lt, rt))
        errorAt(node, "assignment discards a const qualifier ('" + rt + "' to '" + lt + "')");
    else if (lt != "unknown" && rt != "unknown") {
        // Type compatibility incl. narrowing (a literal that fits the target
        // stays valid). Handled here so `=` gets the same rules as init/return.
        std::string e = assignabilityError(lt, rt, node->right.get());
        if (!e.empty()) errorAt(node, "assignment: " + e);
        // A compound assignment `x op= lit` (desugared to `x = x op lit`, sharing the
        // target node) stores into x's type, so its literal operand must fit it, as
        // for `x = lit`.
        if (auto* cb = dynamic_cast<BinaryExpr*>(node->right.get());
            cb && cb->left.get() == node->left.get() && cb->op != "<<" && cb->op != ">>" &&
            isIntType(normalizeType(lt)))
            if (auto* lit = dynamic_cast<LiteralExpr*>(cb->right.get());
                lit && lit->kind == LiteralExpr::Kind::INT && !intLiteralFits(normalizeType(lt), lit))
                errorAt(node, "compound assignment: integer literal " + lit->value +
                              " is out of range for '" + lt + "'");
    }
    expressionTypes[node] = lt;
}

void TypeChecker::visit(BinaryExpr* node) {
    node->left->accept(this);
    // Short-circuit narrowing: in `p != null && *p`, the right operand only runs when
    // the left is true (for `||`, when it is false), so it sees `p` as non-null.
    if (node->op == "&&" || node->op == "||") {
        std::vector<std::string> keys;
        condNarrowings(node->left.get(), node->op == "&&", keys);
        auto inserted = applyNarrowings(keys);
        node->right->accept(this);
        undoNarrowings(inserted);
    } else {
        node->right->accept(this);
    }

    if (node->op == "=") { checkAssignment(node); return; }

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

    // Division or remainder by a literal zero is a guaranteed runtime trap; reject
    // it at compile time (the value is statically known).
    if ((node->op == "/" || node->op == "%")) {
        if (auto* l = dynamic_cast<LiteralExpr*>(node->right.get());
            l && l->kind == LiteralExpr::Kind::INT) {
            bool zero = true;
            for (char c : l->value) if (c != '0' && c != '-' && c != '+') { zero = false; break; }
            if (zero) errorAt(node, std::string(node->op == "/" ? "division" : "remainder") +
                                    " by zero");
        }
    }

    std::string resultType = inferBinaryExprType(leftType, node->op, rightType);

    if (resultType == "error") {
        // Operator overloading: `a op b` on non-built-in operands resolves to a user
        // `operator op(L, R)` declared for these operand types (with numeric coercion).
        // Not found → the operands really are invalid.
        std::string ret, opFn = resolveOperator(node->op, {leftType, rightType}, ret);
        if (!opFn.empty()) {
            if (!inInstance) node->opFunc = opFn;
            calledFns.insert(opFn);        // -Wall: an operator use references it
            expressionTypes[node] = ret;   // the operator's declared return type
        } else {
            errorAt(node,"invalid operands for operator: " + leftType + " and " + rightType);
            expressionTypes[node] = "unknown";
        }
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

// An integer literal that does not fit `int` has a wider type (C: the first of int,
// long, unsigned long that holds it), so `c ? x : 10000000000` is 64-bit, not an int.
static std::string literalArmType(Expr* e, const std::string& t) {
    auto* lit = dynamic_cast<LiteralExpr*>(e);
    if (!lit || lit->kind != LiteralExpr::Kind::INT) return t;
    try {
        long long v = std::stoll(lit->value, nullptr, 0);
        if (v >= INT32_MIN && v <= INT32_MAX) return t;
        return "int64";
    } catch (...) {
        // Above INT64_MAX: only an unsigned 64-bit type holds it.
        return "uint64";
    }
}

void TypeChecker::visit(TernaryExpr* node) {
    node->condition->accept(this);
    {
        std::vector<std::string> keys;
        condNarrowings(node->condition.get(), true, keys);
        auto inserted = applyNarrowings(keys);
        node->thenExpr->accept(this);
        undoNarrowings(inserted);
        keys.clear();
        condNarrowings(node->condition.get(), false, keys);
        inserted = applyNarrowings(keys);
        node->elseExpr->accept(this);
        undoNarrowings(inserted);
    }

    std::string ct = getExpressionType(node->condition.get());
    if (ct != "unknown" && !isNumericType(ct) && !isPointerType(ct) &&
        normalizeType(ct) != "bool")
        errorAt(node, "ternary condition must be a bool, integer, or pointer, got " + ct);

    std::string tt = literalArmType(node->thenExpr.get(), getExpressionType(node->thenExpr.get()));
    std::string et = literalArmType(node->elseExpr.get(), getExpressionType(node->elseExpr.get()));

    // The result type is the arms' common type: identical types pass through, two
    // numerics promote to the wider (C-style), and otherwise the arms must be mutually
    // assignable (else it is a type error).
    std::string result = tt;
    if (tt == "unknown")       result = et;
    else if (et == "unknown")  result = tt;
    else if (tt == et)         result = tt;
    // A `null` arm takes the other arm's pointer type (`c ? null : &x`).
    else if (tt == "null" && (isPointerType(et) || et[0] == '?')) result = et;
    else if (et == "null" && (isPointerType(tt) || tt[0] == '?')) result = tt;
    else if (isNumericType(tt) && isNumericType(et)) result = promoteType(tt, et);
    else if (assignabilityError(tt, et, node->elseExpr.get()).empty()) result = tt;
    else if (assignabilityError(et, tt, node->thenExpr.get()).empty()) result = et;
    else {
        errorAt(node, "ternary branches have incompatible types '" + tt + "' and '" + et + "'");
        result = tt;
    }
    expressionTypes[node] = result;
}

std::string TypeChecker::narrowKey(const std::string& name) const {
    int si = scopeOf(name);
    return si < 0 ? "" : name + "@" + std::to_string(si);
}

void TypeChecker::condNarrowings(Expr* cond, bool whenTrue, std::vector<std::string>& keys) {
    auto nullableIdent = [&](Expr* e) -> std::string {
        auto* id = dynamic_cast<IdentExpr*>(e);
        if (!id) return "";
        std::string t = lookupSymbol(id->name);
        return (!t.empty() && t[0] == '?') ? narrowKey(id->name) : "";
    };
    if (auto* u = dynamic_cast<UnaryExpr*>(cond); u && u->op == "!") {
        condNarrowings(u->operand.get(), !whenTrue, keys);
        return;
    }
    if (auto* b = dynamic_cast<BinaryExpr*>(cond)) {
        if ((b->op == "&&" && whenTrue) || (b->op == "||" && !whenTrue)) {
            condNarrowings(b->left.get(), whenTrue, keys);
            condNarrowings(b->right.get(), whenTrue, keys);
            return;
        }
        if (b->op == "!=" || b->op == "==") {
            auto isNull = [](Expr* e) {
                auto* l = dynamic_cast<LiteralExpr*>(e);
                return l && l->kind == LiteralExpr::Kind::NULL_VAL;
            };
            std::string k;
            if (isNull(b->right.get())) k = nullableIdent(b->left.get());
            else if (isNull(b->left.get())) k = nullableIdent(b->right.get());
            if (!k.empty() && (b->op == "!=") == whenTrue) keys.push_back(k);
        }
        return;
    }
    if (whenTrue) {                                    // `if (p)`: a pointer tested for non-null
        std::string k = nullableIdent(cond);
        if (!k.empty()) keys.push_back(k);
    }
}

std::vector<std::string> TypeChecker::applyNarrowings(const std::vector<std::string>& keys) {
    std::vector<std::string> inserted;
    for (const auto& k : keys)
        if (narrowedNonNull.insert(k).second) inserted.push_back(k);
    return inserted;
}

void TypeChecker::undoNarrowings(const std::vector<std::string>& inserted) {
    for (const auto& k : inserted) narrowedNonNull.erase(k);
}

void TypeChecker::dropAssignedIn(Expr* e) {
    if (!e || narrowedNonNull.empty()) return;
    auto dropName = [&](const std::string& n) {
        for (auto it = narrowedNonNull.begin(); it != narrowedNonNull.end();)
            if (it->compare(0, n.size() + 1, n + "@") == 0) it = narrowedNonNull.erase(it); else ++it;
    };
    if (auto* b = dynamic_cast<BinaryExpr*>(e); b && b->op == "=")
        if (auto* id = dynamic_cast<IdentExpr*>(b->left.get())) dropName(id->name);
    if (auto* u = dynamic_cast<UnaryExpr*>(e); u && u->op == "&")
        if (auto* id = dynamic_cast<IdentExpr*>(u->operand.get())) dropName(id->name);
    if (auto* lam = dynamic_cast<LambdaExpr*>(e)) { dropAssignedIn(lam->body.get()); return; }
    astwalk::forEachChildExpr(e, [&](ExprPtr& c) { dropAssignedIn(c.get()); });
}

void TypeChecker::dropAssignedIn(Stmt* s) {
    if (!s || narrowedNonNull.empty()) return;
    if (auto* b = dynamic_cast<BlockStmt*>(s)) {
        for (auto& it : b->items) {
            if (auto* st = std::get_if<StmtPtr>(&it)) dropAssignedIn(st->get());
            else if (auto* vd = dynamic_cast<VarDecl*>(std::get<DeclPtr>(it).get())) dropAssignedIn(vd->initializer.get());
        }
    } else if (auto* es = dynamic_cast<ExprStmt*>(s)) dropAssignedIn(es->expr.get());
    else if (auto* r = dynamic_cast<ReturnStmt*>(s)) dropAssignedIn(r->value.get());
    else if (auto* i = dynamic_cast<IfStmt*>(s)) {
        dropAssignedIn(i->condition.get()); dropAssignedIn(i->thenBranch.get()); dropAssignedIn(i->elseBranch.get());
    } else if (auto* w = dynamic_cast<WhileStmt*>(s)) { dropAssignedIn(w->condition.get()); dropAssignedIn(w->body.get()); }
    else if (auto* dw = dynamic_cast<DoWhileStmt*>(s)) { dropAssignedIn(dw->condition.get()); dropAssignedIn(dw->body.get()); }
    else if (auto* f = dynamic_cast<ForStmt*>(s)) {
        dropAssignedIn(f->init.get()); dropAssignedIn(f->condition.get());
        dropAssignedIn(f->step.get()); dropAssignedIn(f->body.get());
    } else if (auto* fi = dynamic_cast<ForInStmt*>(s)) { dropAssignedIn(fi->iterable.get()); dropAssignedIn(fi->body.get()); }
    else if (auto* sw = dynamic_cast<SwitchStmt*>(s)) {
        dropAssignedIn(sw->subject.get());
        for (auto& c : sw->cases) for (auto& st : c.stmts) dropAssignedIn(st.get());
    } else if (auto* m = dynamic_cast<MatchStmt*>(s)) {
        dropAssignedIn(m->subject.get());
        for (auto& arm : m->arms) dropAssignedIn(arm.body.get());
    } else if (auto* t = dynamic_cast<TryStmt*>(s)) {
        dropAssignedIn(t->body.get());
        for (auto& c : t->catches) dropAssignedIn(c.body.get());
        dropAssignedIn(t->finally.get());
    } else if (auto* d = dynamic_cast<DeferStmt*>(s)) dropAssignedIn(d->body.get());
    else if (auto* th = dynamic_cast<ThrowStmt*>(s)) dropAssignedIn(th->value.get());
}

void TypeChecker::checkNullableDeref(Expr* operand, const char* how) {
    std::string t = getExpressionType(operand);
    if (t.empty() || t[0] != '?') return;                 // not (or no longer) a nullable pointer
    errorAt(operand, std::string("cannot ") + how + " a possibly-null pointer of type '" + t +
                     "'; check it first (e.g. `if (x != null)`)");
}

void TypeChecker::visit(UnaryExpr* node) {
    node->operand->accept(this);
    std::string operandType = getExpressionType(node->operand.get());
    if (node->op == "&" && !isLvalueExpr(node->operand.get()))
        errorAt(node, "cannot take the address of this expression: it is not a variable, field, element, or dereference");
    if (node->op == "&")
        if (auto* m = dynamic_cast<MemberExpr*>(node->operand.get())) {
            std::string bt = ty::Type::parse(normalizeType(tyq::strip(getExpressionType(m->base.get())))).nominalName();
            auto sit = structs.find(bt);
            if (sit != structs.end())
                for (const auto& f : sit->second.fields)
                    if (f.name == m->member && f.bitWidth > 0)
                        errorAt(node, "cannot take the address of bitfield '" + m->member + "'");
        }
    // `&p` of a narrowed `?*T` is a `*?*T` (a write through it may store null), so the
    // narrowing ends here and the address carries the declared nullable type.
    if (node->op == "&")
        if (auto* id = dynamic_cast<IdentExpr*>(node->operand.get())) {
            std::string declared = lookupSymbol(id->name);
            if (!declared.empty() && declared[0] == '?') {
                operandType = declared;
                narrowedNonNull.erase(narrowKey(id->name));
            }
        }

    if (node->op == "*") checkNullableDeref(node->operand.get(), "dereference");

    if (operandType == "unknown") {
        expressionTypes[node] = "unknown";
        return;
    }

    std::string resultType = inferUnaryExprType(node->op, operandType);

    // Address-of a const location yields a pointer *to const*, so writing through
    // it (or assigning it to a mutable pointer) is caught by the const checks.
    // Without this, `const int N; *int p = &N; *p = 99;` would silently mutate N.
    if (node->op == "&" && resultType != "error") {
        std::string dummy;
        if (assignsToConst(node->operand.get(), dummy))
            resultType = "const " + resultType;
    }

    if (resultType == "error") {
        // Unary operator overloading: `-v`/`!v`/`~v` on a non-built-in operand resolves to a
        // user `operator -/!/~(V)`. Deref `*` and address-of `&` are structural, not overloadable.
        std::string lookupOp = (node->op == "-") ? "u-" : node->op;
        std::string ret, opFn;
        if (node->op == "-" || node->op == "!" || node->op == "~")
            opFn = resolveOperator(lookupOp, {operandType}, ret);
        if (!opFn.empty()) {
            if (!inInstance) node->opFunc = opFn;
            calledFns.insert(opFn);
            expressionTypes[node] = ret;
        } else {
            errorAt(node,"invalid operand for unary operator: " + operandType);
            expressionTypes[node] = "unknown";
        }
    } else {
        expressionTypes[node] = resultType;
    }
}

void TypeChecker::visit(IncDecExpr* node) {
    node->operand->accept(this);
    Expr* op = node->operand.get();
    bool isLval = dynamic_cast<IdentExpr*>(op) || dynamic_cast<MemberExpr*>(op) ||
                  dynamic_cast<IndexExpr*>(op) ||
                  (dynamic_cast<UnaryExpr*>(op) && static_cast<UnaryExpr*>(op)->op == "*");
    if (!isLval)
        errorAt(node, "'++'/'--' requires a modifiable variable");
    std::string cname;
    if (assignsToConst(op, cname))
        errorAt(node, "cannot modify read-only location '" + cname + "'");
    std::string t = getExpressionType(op);
    if (t != "unknown" && !isIntType(t) && !isPointerType(t))
        errorAt(node, "'++'/'--' requires an integer or pointer, got '" + t + "'");
    expressionTypes[node] = t;
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
        // bare nominal: *Rect and Rect both resolve to Rect_method
        std::string baseType = ty::Type::parse(getExpressionType(member->base.get())).nominalName();

        std::string mangled = baseType + "_" + member->member;
        auto mit = functionSignatures.find(mangled);
        if (mit != functionSignatures.end()) {
            const auto& sig = mit->second;
            const auto& paramTypes = sig.second; // first param is "self"
            calledFns.insert(mangled);           // -Wall: `x.m()` references `Type_m`
            if (auto gm = genericMethodInsts.find(mangled); gm != genericMethodInsts.end())
                queueInstance(gm->second.fn, gm->second.owner->typeParams, gm->second.subs,
                              gm->second.owner->name, "." + member->member, mangled,
                              "*" + baseType, gm->second.owner->sourceFile);
            // A method whose `self` is a plain (mutable) pointer may write through it, so
            // it cannot be called on a read-only receiver (a const value, or through a
            // pointer to const) unless it declares `const T* self`.
            {
                std::string rt = getExpressionType(member->base.get());
                std::string cname;
                bool roRecv = tyq::isPtr(rt) ? tyq::baseConst(tyq::pointee(rt))
                                             : assignsToConst(member->base.get(), cname);
                if (roRecv && !paramTypes.empty() && !tyq::baseConst(tyq::pointee(paramTypes[0])))
                    errorAt(node, "cannot call method '" + member->member + "' on a read-only value: its '" +
                                  paramTypes[0] + " self' may modify it (declare it 'const " + baseType +
                                  "* self' if it does not)");
            }
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
                        std::vector<std::string> pts;
                        for (const auto& p : sig.params) pts.push_back(p.first);
                        checkCallArgs(node, "method '" + member->member + "'", pts);
                        expressionTypes[node] = normalizeType(sig.returnType);
                        return;
                    }
                }
                errorAt(node, "interface '" + baseType + "' has no method '" + member->member + "'");
                expressionTypes[node] = "unknown";
                return;
            }
        }
        // Inside a generic instance, a constrained call `t.m(x)` on a primitive receiver is
        // satisfied by a free function `m(T, ...)` (codegen lowers it to `m(t, x)`).
        static const std::set<std::string> kScalarPrims = {
            "int","int8","int16","int32","int64","uint","uint8","uint16","uint32",
            "uint64","char","bool","float","double"};
        if (inInstance && kScalarPrims.count(baseType)) {
            auto fit = functionSignatures.find(member->member);
            if (fit != functionSignatures.end() && !fit->second.second.empty()) {
                calledFns.insert(member->member);
                const auto& pts = fit->second.second;
                std::string recvT = getExpressionType(member->base.get());
                std::string e = assignabilityError(pts[0], recvT, member->base.get());
                if (!e.empty())
                    errorAt(node, "receiver of '" + member->member + "' type mismatch: expected " +
                                  pts[0] + ", got " + recvT + " (" + e + ")");
                for (auto& arg : node->args) arg->accept(this);
                checkCallArgs(node, "function '" + member->member + "'",
                              std::vector<std::string>(pts.begin() + 1, pts.end()));
                expressionTypes[node] = fit->second.first;
                return;
            }
        }
        // Not a method — maybe a struct field holding a fn pointer: o.op(args).
        member->accept(this);
        std::string fieldTy = getExpressionType(member);
        if (fieldTy.size() > 3 && fieldTy.substr(0, 3) == "fn(") {
            for (auto& arg : node->args) arg->accept(this);
            expressionTypes[node] = checkFnValueCall(node, "'" + member->member + "'", fieldTy);
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
        if (inPrimaryFile()) useLocations[{identExpr->line, identExpr->col}] = funcName;
    } else {
        // Any other callee expression (`mk()(1)`, `(*pf)(x)`): call its fn-typed value.
        node->callee->accept(this);
        for (auto& a : node->args) a->accept(this);
        std::string ct = getExpressionType(node->callee.get());
        expressionTypes[node] = ty::Type::parse(normalizeType(ct)).isFn()
            ? checkFnValueCall(node, "the called function value", ct) : "unknown";
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
            for (auto& a : node->args) a->accept(this);
            expressionTypes[node] = checkFnValueCall(node, "'" + funcName + "'", varType);
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
            std::string unbound;
            for (const auto& tpName : fd->typeParams)
                if (!subs.count(tpName)) unbound += (unbound.empty() ? "" : ", ") + tpName;
            if (unbound.empty()) {
                size_t errsBefore = errors.size();
                checkConstraints(node, fd->constraints, subs);
                // The inferred instantiation's parameter types must accept every argument.
                std::vector<std::string> pts;
                for (const auto& p : fd->params) pts.push_back(substType(p.first, subs));
                checkCallArgs(node, "function '" + funcName + "'", pts);
                if (errors.size() == errsBefore) {
                    std::string mangled = funcName;
                    for (const auto& tpn : fd->typeParams) mangled += "_" + mangleTemplate(subs[tpn]);
                    queueInstance(fd, fd->typeParams, subs, funcName, "", mangled, "", fd->sourceFile);
                }
                expressionTypes[node] = normalizeType(substType(fd->returnType, subs));
                return;
            }
            errorAt(node, "cannot infer type argument(s) " + unbound + " of generic function '" +
                          funcName + "' from the call; write them explicitly, e.g. " + funcName + "<...>(...)");
            expressionTypes[node] = "unknown";
            return;
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
            if (!paramEscapes && !inInstance) lam->escapes = false;
        }
        if (i < fixedCount) {
            std::string argType = getExpressionType(node->args[i].get());
            // A C function's fn-typed parameter is a C function pointer: C can only call
            // a named top-level function (or get null), never a closure's environment.
            if (externFnNames.count(funcName)
                    && ty::Type::parse(normalizeType(expectedParamTypes[i])).isFn()) {
                auto* id = dynamic_cast<IdentExpr*>(node->args[i].get());
                auto* lit = dynamic_cast<LiteralExpr*>(node->args[i].get());
                if (lit && lit->kind == LiteralExpr::Kind::NULL_VAL) continue;
                if (!(id && lookupSymbol(id->name).empty() && functionSignatures.count(id->name))) {
                    errorAt(node, "argument " + std::to_string(i + 1) + " of extern '" + funcName +
                                  "' is a C function pointer: pass a top-level function by name, not a closure value");
                    continue;
                }
            }
            std::string e = assignabilityError(expectedParamTypes[i], argType, node->args[i].get());
            if (!e.empty())
                errorAt(node,"argument " + std::to_string(i + 1) + " type mismatch: expected " +
                            expectedParamTypes[i] + ", got " + argType + " (" + e + ")");
        }
    }

    expressionTypes[node] = sig.first;
}

void TypeChecker::checkCallArgs(CallExpr* node, const std::string& what,
                                const std::vector<std::string>& paramTypes) {
    bool variadic = !paramTypes.empty() && paramTypes.back() == "...";
    size_t fixed = variadic ? paramTypes.size() - 1 : paramTypes.size();
    if (variadic ? node->args.size() < fixed : node->args.size() != fixed) {
        errorAt(node, what + " expects " + (variadic ? "at least " : "") + std::to_string(fixed) +
                      " argument(s), got " + std::to_string(node->args.size()));
        return;
    }
    for (size_t i = 0; i < fixed; ++i) {
        std::string at = getExpressionType(node->args[i].get());
        std::string e = assignabilityError(paramTypes[i], at, node->args[i].get());
        if (!e.empty())
            errorAt(node, "argument " + std::to_string(i + 1) + " type mismatch: expected " +
                          paramTypes[i] + ", got " + at + " (" + e + ")");
    }
}

std::string TypeChecker::checkFnValueCall(CallExpr* node, const std::string& what, const std::string& fnType) {
    ty::Type ft = ty::Type::parse(normalizeType(fnType));
    if (!ft.isFn()) return "unknown";
    std::vector<std::string> pts;
    for (const auto& p : ft.params) pts.push_back(p.str());
    checkCallArgs(node, what, pts);
    return ft.ret ? normalizeType(ft.ret->str()) : "unknown";
}

void TypeChecker::visit(IndexExpr* node) {
    node->base->accept(this);
    node->index->accept(this);
    if (node->highIndex) node->highIndex->accept(this);
    checkNullableDeref(node->base.get(), "index");

    std::string baseType = getExpressionType(node->base.get());
    std::string indexType = getExpressionType(node->index.get());

    if (indexType != "unknown" && !isIntType(indexType)) {
        errorAt(node,"array index must be integer, got " + indexType);
    }
    if (node->highIndex) {
        std::string hiType = getExpressionType(node->highIndex.get());
        if (hiType != "unknown" && !isIntType(hiType))
            errorAt(node, "slice bound must be integer, got " + hiType);
    }

    // Determine the element type of the base (array / slice / pointer / string).
    ty::Type bt = ty::Type::parse(baseType);
    std::string elem;
    bool haveElem = false;
    if (baseType == "string") { elem = "char"; haveElem = true; }
    else if (bt.kind == ty::Type::Kind::Array || bt.kind == ty::Type::Kind::Slice) {
        elem = bt.elem->str(); haveElem = true;
        // Constant slice bounds `a[lo..hi]` into a fixed array: 0 <= lo <= hi <= N.
        if (node->highIndex && bt.kind == ty::Type::Kind::Array) {
            auto litVal = [](Expr* e, long long& out) {
                auto* l = dynamic_cast<LiteralExpr*>(e);
                if (!l || l->kind != LiteralExpr::Kind::INT) return false;
                try { out = std::stoll(l->value, nullptr, 0); } catch (...) { return false; }
                return true;
            };
            const std::string& dim = bt.dim;
            bool dimNum = !dim.empty() &&
                std::all_of(dim.begin(), dim.end(), [](unsigned char c){ return std::isdigit(c); });
            long long lo = 0, hi = 0;
            bool haveLo = litVal(node->index.get(), lo), haveHi = litVal(node->highIndex.get(), hi);
            if ((haveLo && lo < 0) || (haveHi && hi < 0))
                errorAt(node, "slice bound is negative");
            else if (haveLo && haveHi && lo > hi)
                errorAt(node, "slice bounds out of order: " + std::to_string(lo) + ".." + std::to_string(hi));
            else if (dimNum && ((haveHi && hi > std::stoll(dim)) || (haveLo && lo > std::stoll(dim))))
                errorAt(node, "slice bound " + std::to_string(haveHi && hi > std::stoll(dim) ? hi : lo) +
                              " is out of bounds for array of size " + dim);
        }
        // Constant-index bounds check: only a plain index into a fixed array with a
        // numeric dimension is checkable at compile time.
        if (!node->highIndex && bt.kind == ty::Type::Kind::Array) {
            if (auto* ix = dynamic_cast<LiteralExpr*>(node->index.get());
                ix && ix->kind == LiteralExpr::Kind::INT) {
                const std::string& dim = bt.dim;
                try {
                    long long idx = std::stoll(ix->value, nullptr, 0);
                    bool dimNum = !dim.empty() &&
                        std::all_of(dim.begin(), dim.end(), [](unsigned char c){ return std::isdigit(c); });
                    if (idx < 0)
                        errorAt(node, "array index " + ix->value + " is out of bounds");
                    else if (dimNum && idx >= std::stoll(dim))
                        errorAt(node, "array index " + ix->value +
                                      " is out of bounds for array of size " + dim);
                } catch (...) { /* unparized literal — skip */ }
            }
        }
    } else if (isPointerType(baseType)) {
        elem = getPointeeType(baseType); haveElem = true;
    }

    // Overloaded subscript: `base[i]` on a non-built-in indexable resolves to a user
    // `operator [](Base, Index)` (read/rvalue form; a slice `base[lo..hi]` is not overloaded).
    if (!haveElem && !node->highIndex) {
        std::string ret, opFn = resolveOperator("[]", {baseType, indexType}, ret);
        if (!opFn.empty()) { if (!inInstance) node->opFunc = opFn; calledFns.insert(opFn); expressionTypes[node] = ret; return; }
    }

    if (!haveElem) {
        // Only a base certain to be unindexable is reported: a number, or a struct/sum-type
        // value with no `operator []` (other spellings, e.g. an array of fn values, are
        // left to the element resolution in codegen).
        std::string nb = normalizeType(tyq::strip(baseType));
        if (isNumericType(nb) || nb.rfind("struct:", 0) == 0 || adtEnums.count(nb))
            errorAt(node, "cannot index into a value of type '" + ty::Type::parse(baseType).nominalName() + "'");
        expressionTypes[node] = "unknown";
        return;
    }

    // `base[lo..hi]` yields a slice of the element type; `base[i]` yields the element.
    expressionTypes[node] = node->highIndex ? (elem + "[]") : elem;
}

void TypeChecker::visit(MemberExpr* node) {
    node->base->accept(this);
    checkNullableDeref(node->base.get(), "access a member of");

    std::string baseType = getExpressionType(node->base.get());
    if (!baseType.empty() && baseType[0] == '?') baseType = baseType.substr(1);   // `?*T` derefs like `*T`

    // Slice `.len` → int64 (the fat pointer's length field).
    if (node->member == "len" && ty::Type::parse(baseType).kind == ty::Type::Kind::Slice) {
        expressionTypes[node] = "int64";
        return;
    }

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
    validateStructType(normalizedType, node);
    // A cast converts between scalars: numbers (incl. bool/char/enums), pointers, and
    // integer<->pointer. An aggregate (struct, union, sum type, array, slice, closure)
    // casts only to its own type; float<->pointer has no meaning.
    {
        std::string from = normalizeType(tyq::strip(getExpressionType(node->expr.get())));
        std::string to = normalizeType(tyq::strip(normalizedType));
        if (!from.empty() && from[0] == '?') from = from.substr(1);
        if (!to.empty() && to[0] == '?') to = to.substr(1);
        bool fnName = false;   // a top-level function cast to a pointer is its raw C address
        if (auto* id = dynamic_cast<IdentExpr*>(node->expr.get()))
            fnName = lookupSymbol(id->name).empty() && functionSignatures.count(id->name);
        auto isPtr = [&](const std::string& t) { return isPointerType(t) || t == "null"; };
        bool ok = from == "unknown" || to == "unknown" || from == to || (fnName && isPtr(to)) ||
                  (isNumericType(from) && isNumericType(to)) ||
                  (isPtr(from) && isPtr(to)) ||
                  (isIntType(from) && isPtr(to)) || (isPtr(from) && isIntType(to));
        if (!ok)
            errorAt(node, "cannot cast '" + getExpressionType(node->expr.get()) + "' to '" + node->targetType + "'");
    }
    expressionTypes[node] = normalizedType;
}

void TypeChecker::visit(LiteralExpr* node) {
    switch (node->kind) {
        case LiteralExpr::Kind::INT:
            expressionTypes[node] = "int";
            break;
        case LiteralExpr::Kind::FLOAT:
            // A float literal lowers to a `double` constant (the lexer has no
            // float/double distinction; codegen emits ConstantFP::getDoubleTy).
            // Sema previously said "float" — a latent disagreement with codegen.
            expressionTypes[node] = "double";
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
        if (inPrimaryFile()) useLocations[{node->line, node->col}] = node->name;
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
        if (inPrimaryFile()) useLocations[{node->line, node->col}] = node->name;
        return;
    }
    if (type.empty()) {
        errorAt(node,"undefined variable '" + node->name + "'");
        expressionTypes[node] = "unknown";
    } else if (type[0] == '?' && narrowedNonNull.count(narrowKey(node->name))) {
        expressionTypes[node] = type.substr(1);   // proven non-null here: a plain `*T`
    } else {
        expressionTypes[node] = type;
    }
    // Record use-site so go-to-definition can map cursor → definition: a local or
    // parameter maps to the exact symbol found; a global maps by name.
    if (inPrimaryFile()) {
        const Symbol* sym = findSymbol(node->name);
        auto global = scopes.front().find(node->name);
        bool isGlobal = global != scopes.front().end() && &global->second == sym;
        if (sym && !isGlobal && sym->line > 0)
            useDefs[{node->line, node->col}] = {(int)node->name.size(), {sym->line, sym->col, sym->file}};
        else
            useLocations[{node->line, node->col}] = node->name;
    }

    // Capture detection: inside a lambda, a name is captured when it resolves to
    // a variable in an enclosing scope (below the lambda's own scopes). We key off
    // the variable's defining scope index, NOT functionSignatures — a param or
    // local that shadows a same-named top-level function must still be captured.
    if (!captureStack.empty() && !type.empty()) {
        int defIdx = scopeOf(node->name);
        // Capture only enclosing-function scopes: index >= 1 (the global scope
        // at 0 is module-level and accessed directly, not captured by value).
        // A variable must be captured by EVERY enclosing lambda it is outer to,
        // not just the innermost one, so a nested lambda's use is threaded
        // through each intervening lambda's environment (transitive capture).
        // Without this, an inner lambda that references a variable two scopes up
        // would read the enclosing lambda's non-captured value and miscompile
        // ("Referring to an instruction in another function").
        // A `static` local has static storage: like a global, the lambda body refers
        // to the one cell directly instead of capturing a copy.
        if (defIdx >= 1 && !scopes[defIdx][node->name].isStatic) {
            for (size_t k = 0; k < captureStack.size(); ++k) {
                if (defIdx < captureBoundary[k])
                    captureStack[k][node->name] = type;
            }
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
    // The body is its own function: a break/continue there cannot target a loop (or
    // switch) of the enclosing function.
    std::vector<std::string> savedLoops = std::move(loopLabelStack);
    loopLabelStack.clear();
    int savedSwitch = switchDepth;
    switchDepth = 0;
    // A lambda is its own (non-async) function: an `await` in its body does not belong to
    // an enclosing async function.
    bool savedAsync = inAsyncFn, savedAwait = awaitSeenInFn;
    inAsyncFn = false;
    // Captures are by value: an assignment to a captured name inside the body changes the
    // lambda's copy, so it must not end a narrowing of the enclosing variable.
    std::set<std::string> savedNarrowed = narrowedNonNull;
    for (const auto& p : node->params) {
        if (scopes.back().count(p.second)) errorAt(node, "duplicate parameter '" + p.second + "' in lambda");
        defineSymbol(p.second, normalizeType(p.first), node->line, node->col, /*isParam=*/true);
    }
    // Mark param names so IdentExpr doesn't treat them as captures
    std::set<std::string> paramNames;
    for (const auto& p : node->params) paramNames.insert(p.second);

    if (node->body) node->body->accept(this);
    currentFunctionReturnType = savedReturn;
    loopLabelStack = std::move(savedLoops);
    switchDepth = savedSwitch;
    inAsyncFn = savedAsync;
    awaitSeenInFn = savedAwait;
    narrowedNonNull = savedNarrowed;
    popScope();

    // Harvest captures: only outer-scope vars, not params. A generic body's lambdas
    // keep the source-form captures TemplateCapturePass recorded (shared by instances).
    if (!inInstance) {
        node->captures.clear();
        for (const auto& [name, type] : captureStack.back()) {
            if (!paramNames.count(name))
                node->captures.push_back({name, type});
        }
    }
    captureStack.pop_back();
    captureBoundary.pop_back();

    expressionTypes[node] = sig;
}

void TypeChecker::visit(SizeofExpr* node) {
    // `sizeof(x)` of a variable measures the variable's type (C semantics). The parser
    // reads the operand as a type spelling, so resolve a bare name that is a variable
    // (not a type) here and rewrite the operand to that variable's type for codegen.
    const std::string tn = node->typeName;
    std::string resolved = tn;
    bool isTypeName = isPrimitiveType(tn) || structs.count(tn) || typeAliases.count(tn) ||
                      enumTypes.count(tn) || interfaceDecls.count(tn) || tn == "va_list";
    if (!isTypeName) {
        std::string vt = lookupSymbol(tn);
        if (!vt.empty() && vt != "unknown" && vt != "struct:" + tn) resolved = tyq::strip(vt);
    }
    if (!inInstance) node->typeName = resolved;   // a generic body's nodes are shared by its instances
    validateStructType(normalizeType(resolved), node);
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
        if (!inInstance) node->resolvedType = res;   // consumed by the async transform
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
    size_t errsBefore = errors.size();
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < node->typeArgs.size(); ++i)
        subs[tp[i]] = resolveInstType(node->typeArgs[i]);
    if (node->typeArgs.size() != tp.size())
        errorAt(node, "generic function '" + node->templateName + "' expects " + std::to_string(tp.size()) +
                      " type argument(s), got " + std::to_string(node->typeArgs.size()));
    for (const auto& ta : node->typeArgs) validateStructType(normalizeType(ta), node);
    bool variadic = !fd->params.empty() && fd->params.back().first == "...";
    size_t fixed = variadic ? fd->params.size() - 1 : fd->params.size();
    if (variadic ? node->args.size() < fixed : node->args.size() != fixed)
        errorAt(node, "function '" + node->templateName + "' expects " + (variadic ? "at least " : "") +
                      std::to_string(fixed) + " argument(s), got " + std::to_string(node->args.size()));
    for (size_t i = fixed; i < node->args.size(); ++i) node->args[i]->accept(this);

    checkConstraints(node, fd->constraints, subs);

    // Type-check arguments
    for (size_t i = 0; i < node->args.size() && i < fixed; ++i) {
        node->args[i]->accept(this);
        std::string expected = substType(fd->params[i].first, subs);
        std::string got      = getExpressionType(node->args[i].get());
        std::string e = assignabilityError(expected, got, node->args[i].get());
        if (!e.empty())
            errorAt(node,"argument " + std::to_string(i+1) + ": expected " + expected +
                        ", got " + got + " (" + e + ")");
    }

    std::string retType = normalizeType(substType(fd->returnType, subs));
    expressionTypes[node] = retType;
    if (errors.size() == errsBefore && node->typeArgs.size() == tp.size()) {
        std::string mangled = node->templateName;
        for (const auto& t : node->typeArgs) mangled += "_" + mangleTemplate(resolveInstType(t));
        queueInstance(fd, tp, subs, node->templateName, "", mangled, "", fd->sourceFile);
    }
}

void TypeChecker::visit(AllocWithExpr* node) {
    node->allocator->accept(this);
    node->count->accept(this);
    std::string countType = getExpressionType(node->count.get());
    if (countType != "unknown" && !isIntType(countType))
        errorAt(node,"alloc_with count must be integer, got " + countType);
    expressionTypes[node] = "*" + node->elemType;
}

void TypeChecker::visit(ArrayLitExpr* node) {
    // Untyped: the element type / size come from the target at the declaration
    // site (checked in visit(VarDecl)). Here we just walk the elements.
    for (auto& el : node->elements) el->accept(this);
    expressionTypes[node] = "array-literal";
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
    std::set<std::string> seenFields;

    for (size_t i = 0; i < node->fieldInits.size(); ++i) {
        const auto& [fname, expr] = node->fieldInits[i];
        expr->accept(this);
        ASTNode* at = (expr->line > 0) ? static_cast<ASTNode*>(expr.get()) : node;

        std::string fieldType, shownName;
        if (named) {
            for (const auto& f : fields) {
                if (f.name == fname) { fieldType = f.type; break; }
            }
            shownName = fname;
            if (fieldType.empty())
                errorAt(at, "struct '" + node->structName + "' has no field '" + fname + "'");
            else if (!seenFields.insert(fname).second)
                errorAt(at, "field '" + fname + "' is initialized more than once in '" +
                            node->structName + "' literal");
        } else if (i < fields.size()) {
            fieldType = fields[i].type;
            shownName = fields[i].name;
        } else if (i == fields.size()) {
            errorAt(at, "too many initializers for struct '" + node->structName + "' (it has " +
                        std::to_string(fields.size()) + " field(s), got " +
                        std::to_string(node->fieldInits.size()) + ")");
        }

        if (!fieldType.empty()) {
            std::string valType = getExpressionType(expr.get());
            if (valType == "array-literal") {
                // `{...}` fills an array field (its elements are checked like an array init).
                if (ty::Type::parse(normalizeType(fieldType)).kind != ty::Type::Kind::Array)
                    errorAt(at, "field '" + shownName + "': an array literal '{...}' can only initialize an array type, not '" +
                                fieldType + "'");
            } else if (valType != "unknown") {
                std::string e = dropsConstQual(fieldType, valType)
                    ? "conversion discards a const qualifier ('" + valType + "' to '" + fieldType + "')"
                    : assignabilityError(fieldType, valType, expr.get());
                if (!e.empty()) errorAt(at, "field '" + shownName + "': " + e);
            }
        }
    }

    expressionTypes[node] = "struct:" + sname;
}
