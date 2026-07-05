#include "type_checker.h"
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

// TypeChecker — expression visitors (operators, calls, member/index access,
// literals, lambdas, await, template calls, struct init).
// Part of the type_checker.cpp split; see type_checker.h.

// Expression visitors
void TypeChecker::visit(BinaryExpr* node) {
    node->left->accept(this);
    node->right->accept(this);

    // Assigning to a `const` binding, a field/element of a const value, or
    // through a pointer-to-const (`const T*`) is an error. See assignsToConst.
    if (node->op == "=") {
        std::string cname;
        if (assignsToConst(node->left.get(), cname))
            errorAt(node, "cannot assign to read-only location '" + cname + "'");
        std::string lt = getExpressionType(node->left.get());
        std::string rt = getExpressionType(node->right.get());
        if (tyq::dropsConst(lt, rt))
            errorAt(node, "assignment discards a const qualifier ('" + rt + "' to '" + lt + "')");
    }

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

    std::string resultType = inferBinaryExprType(leftType, node->op, rightType);

    if (resultType == "error") {
        errorAt(node,"invalid operands for operator: " + leftType + " and " + rightType);
        expressionTypes[node] = "unknown";
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

void TypeChecker::visit(UnaryExpr* node) {
    node->operand->accept(this);
    std::string operandType = getExpressionType(node->operand.get());

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
        errorAt(node,"invalid operand for unary operator: " + operandType);
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = resultType;
    }
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
                        expressionTypes[node] = normalizeType(sig.returnType);
                        return;
                    }
                }
                errorAt(node, "interface '" + baseType + "' has no method '" + member->member + "'");
                expressionTypes[node] = "unknown";
                return;
            }
        }
        // Not a method — maybe a struct field holding a fn pointer: o.op(args).
        member->accept(this);
        std::string fieldTy = getExpressionType(member);
        if (fieldTy.size() > 3 && fieldTy.substr(0, 3) == "fn(") {
            for (auto& arg : node->args) arg->accept(this);
            size_t rp = fieldTy.find(")->");
            expressionTypes[node] = (rp != std::string::npos)
                ? normalizeType(fieldTy.substr(rp + 3)) : "unknown";
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
        useLocations[{identExpr->line, identExpr->col}] = funcName;
    } else {
        expressionTypes[node] = "unknown";
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
            // Extract return type from fn(T,...)->R
            size_t rp = varType.find(")->");
            std::string retType = (rp != std::string::npos) ? varType.substr(rp + 3) : "unknown";
            for (auto& a : node->args) a->accept(this);
            expressionTypes[node] = retType;
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
            bool allBound = true;
            for (const auto& tpName : fd->typeParams)
                if (!subs.count(tpName)) { allBound = false; break; }
            if (allBound) {
                checkConstraints(node, fd->constraints, subs);
                expressionTypes[node] = normalizeType(substType(fd->returnType, subs));
                return;
            }
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
            if (!paramEscapes) lam->escapes = false;
        }
        if (i < fixedCount) {
            std::string argType = getExpressionType(node->args[i].get());
            if (argType != "unknown" && !isValidAssignment(expectedParamTypes[i], argType)) {
                errorAt(node,"argument " + std::to_string(i + 1) + " type mismatch: expected " +
                            expectedParamTypes[i] + ", got " + argType);
            }
        }
    }

    expressionTypes[node] = sig.first;
}

void TypeChecker::visit(IndexExpr* node) {
    node->base->accept(this);
    node->index->accept(this);

    std::string baseType = getExpressionType(node->base.get());
    std::string indexType = getExpressionType(node->index.get());

    if (indexType != "unknown" && !isIntType(indexType)) {
        errorAt(node,"array index must be integer, got " + indexType);
    }

    // string[i] → char
    if (baseType == "string") { expressionTypes[node] = "char"; return; }

    // Array type T[N]: element is T
    size_t lb = baseType.rfind('[');
    if (lb != std::string::npos && baseType.back() == ']') {
        expressionTypes[node] = baseType.substr(0, lb);
        return;
    }

    // Pointer: *T → T
    if (isPointerType(baseType)) {
        expressionTypes[node] = getPointeeType(baseType);
    } else {
        expressionTypes[node] = "unknown";
    }
}

void TypeChecker::visit(MemberExpr* node) {
    node->base->accept(this);

    std::string baseType = getExpressionType(node->base.get());

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
    validateStructType(normalizedType);
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
        useLocations[{node->line, node->col}] = node->name;
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
        useLocations[{node->line, node->col}] = node->name;
        return;
    }
    if (type.empty()) {
        errorAt(node,"undefined variable '" + node->name + "'");
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = type;
    }
    // Record use-site so go-to-definition can map cursor → definition
    useLocations[{node->line, node->col}] = node->name;

    // Capture detection: inside a lambda, a name is captured when it resolves to
    // a variable in an enclosing scope (below the lambda's own scopes). We key off
    // the variable's defining scope index, NOT functionSignatures — a param or
    // local that shadows a same-named top-level function must still be captured.
    if (!captureStack.empty() && !type.empty()) {
        int defIdx = -1;
        for (int si = (int)scopes.size() - 1; si >= 0; --si) {
            if (scopes[si].count(node->name)) { defIdx = si; break; }
        }
        // Capture only enclosing-function scopes: index >= 1 (the global scope
        // at 0 is module-level and accessed directly, not captured by value).
        // A variable must be captured by EVERY enclosing lambda it is outer to,
        // not just the innermost one, so a nested lambda's use is threaded
        // through each intervening lambda's environment (transitive capture).
        // Without this, an inner lambda that references a variable two scopes up
        // would read the enclosing lambda's non-captured value and miscompile
        // ("Referring to an instruction in another function").
        if (defIdx >= 1) {
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
    for (const auto& p : node->params)
        defineSymbol(p.second, normalizeType(p.first));
    // Mark param names so IdentExpr doesn't treat them as captures
    std::set<std::string> paramNames;
    for (const auto& p : node->params) paramNames.insert(p.second);

    // Store param set temporarily so IdentExpr can consult it
    // We use a simple approach: after defining params, record the scope depth
    if (node->body) node->body->accept(this);
    currentFunctionReturnType = savedReturn;
    popScope();

    // Harvest captures: only outer-scope vars, not params
    node->captures.clear();
    for (const auto& [name, type] : captureStack.back()) {
        if (!paramNames.count(name))
            node->captures.push_back({name, type});
    }
    captureStack.pop_back();
    captureBoundary.pop_back();

    expressionTypes[node] = sig;
}

void TypeChecker::visit(SizeofExpr* node) {
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
        node->resolvedType = res;          // consumed by the async transform
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
    std::map<std::string, std::string> subs;
    for (size_t i = 0; i < tp.size() && i < node->typeArgs.size(); ++i)
        subs[tp[i]] = node->typeArgs[i];

    checkConstraints(node, fd->constraints, subs);

    // Type-check arguments
    for (size_t i = 0; i < node->args.size() && i < fd->params.size(); ++i) {
        node->args[i]->accept(this);
        std::string expected = substType(fd->params[i].first, subs);
        std::string got      = getExpressionType(node->args[i].get());
        if (got != "unknown" && !isValidAssignment(expected, got))
            errorAt(node,"argument " + std::to_string(i+1) + ": expected " + expected + ", got " + got);
    }

    std::string retType = normalizeType(substType(fd->returnType, subs));
    expressionTypes[node] = retType;
}

void TypeChecker::visit(AllocWithExpr* node) {
    node->allocator->accept(this);
    node->count->accept(this);
    std::string countType = getExpressionType(node->count.get());
    if (countType != "unknown" && !isIntType(countType))
        errorAt(node,"alloc_with count must be integer, got " + countType);
    expressionTypes[node] = "*" + node->elemType;
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

    for (size_t i = 0; i < node->fieldInits.size(); ++i) {
        const auto& [fname, expr] = node->fieldInits[i];
        expr->accept(this);

        std::string fieldType;
        if (named) {
            for (const auto& f : fields) {
                if (f.name == fname) { fieldType = f.type; break; }
            }
            if (fieldType.empty())
                errorAt(node,"struct '" + node->structName + "' has no field '" + fname + "'");
        } else if (i < fields.size()) {
            fieldType = fields[i].type;
        }

        if (!fieldType.empty()) {
            std::string valType = getExpressionType(expr.get());
            if (valType != "unknown" && !isValidAssignment(fieldType, valType))
                warning(0, 0, "field '" + (named ? fname : fields[i].name) +
                              "': implicit conversion from " + valType + " to " + fieldType);
        }
    }

    expressionTypes[node] = "struct:" + sname;
}
