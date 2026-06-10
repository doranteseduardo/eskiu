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

// ============================================================================

TypeChecker::TypeChecker() {
    pushScope();  // Global scope
}

bool TypeChecker::check(Program* program) {
    hasErrors = false;
    errors.clear();

    // First pass: register all struct declarations and function signatures
    for (const auto& decl : program->declarations) {
        if (auto enumDecl = dynamic_cast<EnumDecl*>(decl.get())) {
            enumTypes.insert(enumDecl->name);
            for (const auto& m : enumDecl->members) enumConstants[m.first] = m.second;
            continue;
        }
        if (auto aliasDecl = dynamic_cast<TypeAliasDecl*>(decl.get())) {
            typeAliases[aliasDecl->name] = aliasDecl->aliased;
            continue;
        }
        if (auto ifaceDecl = dynamic_cast<InterfaceDecl*>(decl.get())) {
            interfaceDecls[ifaceDecl->name] = ifaceDecl;
            continue;
        }
        if (auto funcDecl = dynamic_cast<FunctionDecl*>(decl.get())) {
            if (!funcDecl->typeParams.empty()) {
                funcTemplateDecls[funcDecl->name] = funcDecl;
                // skip normal registration — will be registered on instantiation
                std::vector<std::string> paramTypes;
                for (const auto& p : funcDecl->params) paramTypes.push_back(p.first);
                // Don't register yet — template params are not real types
                continue;
            }
        }
        if (auto structDecl = dynamic_cast<StructDecl*>(decl.get())) {
            if (!structDecl->typeParams.empty()) {
                templateDecls[structDecl->name] = structDecl;
                continue;
            }

            StructInfo info;
            info.name = structDecl->name;
            info.fields = structDecl->fields;
            structs[structDecl->name] = info;

            // Register methods as mangled functions: StructName_methodName(self, ...)
            for (const auto& method : structDecl->methods) {
                if (auto func = dynamic_cast<FunctionDecl*>(method.get())) {
                    std::string mangled = structDecl->name + "_" + func->name;
                    std::vector<std::string> paramTypes;
                    paramTypes.push_back("*" + structDecl->name); // implicit self
                    for (const auto& p : func->params) paramTypes.push_back(p.first);
                    defineFunction(mangled, func->returnType, paramTypes);
                }
            }
        } else if (auto funcDecl = dynamic_cast<FunctionDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : funcDecl->params) {
                paramTypes.push_back(param.first);  // first = type, second = name
            }
            defineFunction(funcDecl->name, funcDecl->returnType, paramTypes);
        } else if (auto externDecl = dynamic_cast<ExternDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : externDecl->params) {
                paramTypes.push_back(param.first);  // first = type, second = name
            }
            defineFunction(externDecl->name, externDecl->returnType, paramTypes);
        } else if (auto intrinDecl = dynamic_cast<IntrinsicDecl*>(decl.get())) {
            // Intrinsics carry an ordinary signature; only codegen treats them
            // specially (inline lowering instead of a call).
            std::vector<std::string> paramTypes;
            for (const auto& param : intrinDecl->params) {
                paramTypes.push_back(param.first);
            }
            defineFunction(intrinDecl->name, intrinDecl->returnType, paramTypes);
        }
    }

    // Second pass: type check all declarations
    for (const auto& decl : program->declarations) {
        decl->accept(this);
    }

    // -Wall: top-level functions defined but never referenced.
    if (warnAll) {
        for (const auto& [name, loc] : definedFns) {
            if (!calledFns.count(name))
                warning(loc.first, loc.second, "unused function '" + name + "'");
        }
    }

    // Report errors
    for (const auto& err : errors) {
        std::cerr << "error: " << err << "\n";
    }

    return !hasErrors;
}

std::string TypeChecker::getExpressionType(Expr* expr) {
    auto it = expressionTypes.find(expr);
    if (it != expressionTypes.end()) {
        return it->second;
    }
    return "unknown";
}

// Declaration visitors
void TypeChecker::visit(Program* node) {
    // Program node is handled by check() method
    // This is called if someone visits it directly
}

std::string TypeChecker::getTypeAtPosition(int line, int col) const {
    Expr* best = nullptr;
    int   bestDist = INT_MAX;
    for (const auto& [node, type] : expressionTypes) {
        if (type == "unknown" || type.empty()) continue;
        if (node->line != line) continue;
        int dist = std::abs(node->col - col);
        if (dist < bestDist) { bestDist = dist; best = node; }
    }
    return best ? expressionTypes.at(best) : "";
}

std::string TypeChecker::getDefinitionAt(int line, int col) const {
    // 1. Cursor is on a use-site: look up the symbol name from use location
    auto useit = useLocations.find({line, col});
    if (useit != useLocations.end()) {
        auto defit = definitionLocations.find(useit->second);
        if (defit != definitionLocations.end()) {
            const auto& loc = defit->second;
            return loc.file + ":" + std::to_string(loc.line) + ":" +
                   std::to_string(loc.col);
        }
    }
    // 2. Cursor may be slightly off — check nearby columns for a use on same line
    for (int dc = -8; dc <= 8; ++dc) {
        auto it2 = useLocations.find({line, col + dc});
        if (it2 != useLocations.end()) {
            auto defit = definitionLocations.find(it2->second);
            if (defit != definitionLocations.end()) {
                const auto& loc = defit->second;
                return loc.file + ":" + std::to_string(loc.line) + ":" +
                       std::to_string(loc.col);
            }
        }
    }
    // 3. Cursor is directly on the declaration itself
    for (const auto& [name, loc] : definitionLocations) {
        if (loc.line == line &&
            col >= loc.col && col < loc.col + (int)name.size())
            return loc.file + ":" + std::to_string(loc.line) + ":" +
                   std::to_string(loc.col);
    }
    return "";
}

void TypeChecker::visit(FunctionDecl* node) {
    if (!node->typeParams.empty()) return; // Template body checked on instantiation

    // Record definition location
    definitionLocations[node->name] = {node->line, node->col, sourceFile};
    // -Wall: track top-level functions for unused-function reporting (skip main).
    if (node->name != "main") definedFns[node->name] = {node->line, node->col};

    currentFunctionReturnType = node->returnType;
    pushScope();

    // Define parameters
    for (const auto& param : node->params) {
        defineSymbol(param.second, normalizeType(param.first),
                     node->line, node->col, /*isParam=*/true);  // resolve aliases/enums
    }

    // Type check body
    if (node->body) {
        node->body->accept(this);
    }

    popScope();
    currentFunctionReturnType = "";
}

void TypeChecker::visit(VarDecl* node) {
    // Record definition location
    if (node->line > 0)
        definitionLocations[node->name] = {node->line, node->col, sourceFile};
    if (node->initializer) {
        node->initializer->accept(this);
        std::string initType = getExpressionType(node->initializer.get());
        if (initType != "unknown" && !isValidAssignment(node->type, initType)) {
            warning(0, 0, "implicit conversion from " + initType + " to " + node->type);
        }
    }
    // A const must be initialized — there is no later point to assign it.
    if (node->isConst && !node->initializer) {
        errorAt(node, "const '" + node->name + "' must be initialized");
    }

    // Normalize the type (e.g., "Point" -> "struct:Point")
    std::string normalizedType = normalizeType(node->type);

    // Validate that struct types exist before use
    validateStructType(normalizedType);

    defineSymbol(node->name, normalizedType, node->line, node->col, /*isParam=*/false);
    if (node->isConst && !scopes.empty()) scopes.back()[node->name].isConst = true;
}

void TypeChecker::visit(StructDecl* node) {
    defineSymbol(node->name, "struct:" + node->name);
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
    // Extern functions are already registered in first pass
    // Just verify they have valid signatures
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
    definitionLocations[node->name] = {node->line, node->col, sourceFile};
}

void TypeChecker::visit(TypeAliasDecl* node) {
    // The alias was registered in the first pass; validate the underlying type.
    validateStructType(normalizeType(node->aliased));
}

// Statement visitors
void TypeChecker::visit(BlockStmt* node) {
    pushScope();

    // Type check items in order, maintaining exact parse order
    // Declarations can be interleaved with statements
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
        }
    }

    popScope();
}

void TypeChecker::visit(IfStmt* node) {
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            errorAt(node,"condition must be boolean or numeric, got " + condType);
        }
    }
    if (node->thenBranch) {
        node->thenBranch->accept(this);
    }
    if (node->elseBranch) {
        node->elseBranch->accept(this);
    }
}

void TypeChecker::visit(ForInStmt* node) {
    node->iterable->accept(this);
    std::string itType = getExpressionType(node->iterable.get());

    // Determine the element type the loop variable will bind.
    std::string elemType;
    auto lb = itType.find('[');
    if (lb != std::string::npos && !itType.empty() && itType.back() == ']') {
        elemType = normalizeType(itType.substr(0, lb));      // fixed-size array
    } else {
        std::string s = itType;
        if (s.rfind("struct:", 0) == 0) s = s.substr(7);
        while (!s.empty() && s.front() == '*') s = s.substr(1);
        while (!s.empty() && s.back()  == '*') s.pop_back();
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

    pushScope();
    if (elemType.empty()) {
        errorAt(node, "for-in expects a fixed-size array or a List-like value "
                      "(with `data` and `size` fields), got " + itType);
        defineSymbol(node->varName, "unknown");
    } else {
        defineSymbol(node->varName, elemType, node->line, node->col, /*isParam=*/false);
    }
    if (node->body) node->body->accept(this);
    popScope();
}

void TypeChecker::visit(WhileStmt* node) {
    if (node->condition) {
        warnAssignInCondition(node->condition.get());
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            errorAt(node,"condition must be boolean or numeric, got " + condType);
        }
    }
    if (node->body) {
        node->body->accept(this);
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

    // Type check condition
    if (node->condition) {
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            errorAt(node,"condition must be boolean or numeric, got " + condType);
        }
    }

    // Type check step
    if (node->step) {
        node->step->accept(this);
    }

    // Type check body
    if (node->body) {
        node->body->accept(this);
    }

    popScope();
}

void TypeChecker::visit(ReturnStmt* node) {
    if (node->value) {
        node->value->accept(this);
        std::string valueType = getExpressionType(node->value.get());
        if (valueType != "unknown" && !isValidAssignment(currentFunctionReturnType, valueType)) {
            errorAt(node,"return type mismatch: expected " + currentFunctionReturnType +
                        ", got " + valueType);
        }
    } else if (currentFunctionReturnType != "void") {
        errorAt(node,"return type mismatch: expected " + currentFunctionReturnType +
                    ", got void");
    }
}

void TypeChecker::visit(BreakStmt* node) {
    // Break statements are valid in loops (checked at parse time)
}

void TypeChecker::visit(ExprStmt* node) {
    if (node->expr) {
        node->expr->accept(this);
    }
}

// Expression visitors
void TypeChecker::visit(BinaryExpr* node) {
    node->left->accept(this);
    node->right->accept(this);

    // Assigning to a `const` binding — or a field/element of a const value — is
    // an error (writing through a const pointer is allowed; see assignsToConst).
    if (node->op == "=") {
        std::string cname;
        if (assignsToConst(node->left.get(), cname))
            errorAt(node, "cannot assign to constant '" + cname + "'");
    }

    std::string leftType = getExpressionType(node->left.get());
    std::string rightType = getExpressionType(node->right.get());

    if (leftType == "unknown" || rightType == "unknown") {
        expressionTypes[node] = "unknown";
        return;
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

    if (resultType == "error") {
        errorAt(node,"invalid operand for unary operator: " + operandType);
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = resultType;
    }
}

void TypeChecker::visit(CallExpr* node) {
    // Method call: callee is MemberExpr (e.g. p.distance(q))
    if (auto member = dynamic_cast<MemberExpr*>(node->callee.get())) {
        member->base->accept(this);
        std::string baseType = getExpressionType(member->base.get());
        if (baseType.size() > 7 && baseType.substr(0, 7) == "struct:") baseType = baseType.substr(7);
        // Strip pointer decorators so *Rect and Rect both resolve to Rect_method
        while (!baseType.empty() && baseType.front() == '*') baseType = baseType.substr(1);
        while (!baseType.empty() && baseType.back()  == '*') baseType.pop_back();

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
            node->callee->accept(this);
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

    // Type check fixed arguments; visit (but do not type-check) variadic extras
    for (size_t i = 0; i < node->args.size(); ++i) {
        node->args[i]->accept(this);
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
            expressionTypes[node] = "float";
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

    std::string type = lookupSymbol(node->name);
    if (type.empty() && enumConstants.count(node->name)) {
        // Bare enum member, e.g. `Red` — an int constant.
        expressionTypes[node] = "int";
        useLocations[{node->line, node->col}] = node->name;
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
        int boundary = captureBoundary.back();
        int defIdx = -1;
        for (int si = (int)scopes.size() - 1; si >= 0; --si) {
            if (scopes[si].count(node->name)) { defIdx = si; break; }
        }
        // Capture only enclosing-function scopes: index >= 1 (the global scope
        // at 0 is module-level and accessed directly, not captured by value)
        // and below the lambda's own boundary.
        if (defIdx >= 1 && defIdx < boundary) {
            captureStack.back()[node->name] = type;
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

void TypeChecker::visit(InterfaceDecl* node) {
    // Interface registered in first pass; no body to type-check
}

void TypeChecker::visit(ContinueStmt* node) {
    // Valid inside loops — no type checking needed
}

void TypeChecker::visit(AsmStmt* node) {
    for (auto& [constraint, expr] : node->inputs)
        if (expr) expr->accept(this);
}

void TypeChecker::visit(UnionDecl* node) {
    // Register the union as a struct in the type system so field access works.
    // All fields are registered; the codegen handles the shared-offset layout.
    StructInfo info;
    info.name = node->name;
    for (const auto& f : node->fields)
        info.fields.push_back({f.type, f.name});
    structs[node->name] = info;
}

void TypeChecker::visit(SizeofExpr* node) {
    expressionTypes[node] = "int64";
}

void TypeChecker::visit(ThreadCreateExpr* node) {
    node->worker->accept(this);
    expressionTypes[node] = "*void";
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

void TypeChecker::visit(SwitchStmt* node) {
    node->subject->accept(this);
    std::string subjType = getExpressionType(node->subject.get());
    if (subjType != "unknown" && !isIntType(subjType))
        errorAt(node,"switch subject must be integer type, got " + subjType);
    for (auto& c : node->cases) {
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
        }
        for (auto& s : c.stmts) s->accept(this);
    }
}

void TypeChecker::visit(TemplateCallExpr* node) {
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

// Scope management
void TypeChecker::pushScope() {
    scopes.push_back(std::map<std::string, Symbol>());
}

void TypeChecker::popScope() {
    if (scopes.empty()) return;
    // -Wall: warn about names declared in this scope that were never referenced.
    // Skip the global scope (size 1) — unused globals are often intentional.
    if (warnAll && scopes.size() > 1) {
        for (const auto& [name, sym] : scopes.back()) {
            if (sym.used || name == "self" || name == "_") continue;
            warning(sym.line, sym.col,
                    std::string(sym.isParam ? "unused parameter '" : "unused variable '")
                    + name + "'");
        }
    }
    scopes.pop_back();
}

bool TypeChecker::isConstSymbol(const std::string& name) const {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return f->second.isConst;
    }
    return false;
}

bool TypeChecker::assignsToConst(Expr* lhs, std::string& nameOut) {
    Expr* cur = lhs;
    while (cur) {
        if (auto* id = dynamic_cast<IdentExpr*>(cur)) {
            if (isConstSymbol(id->name)) { nameOut = id->name; return true; }
            return false;
        }
        // Field/element of a *value* aggregate keeps the same const root; but a
        // member/index through a pointer writes the pointee, so stop there.
        if (auto* m = dynamic_cast<MemberExpr*>(cur)) {
            std::string bt = getExpressionType(m->base.get());
            if ((!bt.empty() && bt.front() == '*') || hasPointerSuffix(bt)) return false;
            cur = m->base.get(); continue;
        }
        if (auto* ix = dynamic_cast<IndexExpr*>(cur)) {
            std::string bt = getExpressionType(ix->base.get());
            if ((!bt.empty() && bt.front() == '*') || hasPointerSuffix(bt)) return false;
            cur = ix->base.get(); continue;
        }
        return false;  // *p, calls, etc. — not an in-place const mutation
    }
    return false;
}

void TypeChecker::defineSymbol(const std::string& name, const std::string& type) {
    defineSymbol(name, type, 0, 0, false);
}

void TypeChecker::defineSymbol(const std::string& name, const std::string& type,
                               int line, int col, bool isParam) {
    if (!scopes.empty()) {
        Symbol s;
        s.type = type; s.isDeclared = true;
        s.used = false; s.line = line; s.col = col; s.isParam = isParam;
        scopes.back()[name] = s;
    }
}

std::string TypeChecker::lookupSymbol(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto sym = it->find(name);
        if (sym != it->end()) {
            sym->second.used = true;  // -Wall: mark referenced
            return sym->second.type;
        }
    }
    return "";
}

void TypeChecker::defineFunction(const std::string& name, const std::string& returnType,
                                  const std::vector<std::string>& paramTypes) {
    functionSignatures[name] = {returnType, paramTypes};
}

// Type inference
std::string TypeChecker::inferBinaryExprType(const std::string& leftType, const std::string& op,
                                             const std::string& rightType) {
    if (op == "=") {
        return isValidAssignment(leftType, rightType) ? leftType : "error";
    }
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        return "bool";
    }
    if (op == "&&" || op == "||") {
        return "bool";
    }
    // Bitwise and shift operators work on integers
    if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        if (isIntType(leftType) && isIntType(rightType)) return promoteType(leftType, rightType);
        return "error";
    }
    // Pointer arithmetic: ptr + int / ptr - int → ptr; ptr - ptr → int64
    if (op == "-" && isPointerType(leftType) && isPointerType(rightType)) return "int64";
    if ((op == "+" || op == "-") && isPointerType(leftType) && isIntType(rightType)) {
        return leftType;
    }
    // Bitwise/shift ops work on integers of any width
    if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
        if (isIntType(leftType) && isIntType(rightType)) return promoteType(leftType, rightType);
        return "error";
    }
    if (!isNumericType(leftType) || !isNumericType(rightType)) {
        return "error";
    }
    return promoteType(leftType, rightType);
}

std::string TypeChecker::inferUnaryExprType(const std::string& op, const std::string& operandType) {
    if (op == "!") {
        return "bool";
    }
    if (op == "-" || op == "+") {
        if (isNumericType(operandType)) {
            return operandType;
        }
        return "error";
    }
    if (op == "~") {
        if (isIntType(operandType)) return operandType;
        return "error";
    }
    if (op == "&") {
        return "*" + operandType;
    }
    if (op == "*") {
        if (isPointerType(operandType)) {
            return getPointeeType(operandType);
        }
        return "error";
    }
    return "error";
}

// Type validation
void TypeChecker::validateStructType(const std::string& type) {
    // Function pointer types are always valid
    if (type.size() > 3 && type.substr(0, 3) == "fn(") return;
    std::string baseType = type;
    // Strip a fixed-size array suffix (T[N]) — the element type is what matters
    // here; the dimension (a literal, enum, or const) is resolved in codegen.
    if (!baseType.empty() && baseType.back() == ']') {
        size_t lb = baseType.rfind('[');
        if (lb != std::string::npos) baseType = baseType.substr(0, lb);
    }
    // Strip ALL pointer decorators (*T, T*, **T, etc.)
    bool stripped = true;
    while (stripped && !baseType.empty()) {
        stripped = false;
        if (hasPointerSuffix(baseType)) { baseType = extractBaseType(baseType); stripped = true; }
        else if (baseType.front() == '*') { baseType = baseType.substr(1); stripped = true; }
    }

    // Check if it's an explicit struct type (struct: prefix)
    if (baseType.find("struct:") == 0) {
        // Extract struct name (remove "struct:" prefix)
        std::string structName = baseType.substr(7);  // strlen("struct:") = 7

        // Look up struct in registry
        if (structs.find(structName) == structs.end()) {
            error(0, 0, "undefined struct '" + structName + "'");
        }
    } else if (!isPrimitiveType(baseType)) {
        // Valid if it's a known struct, type alias, or enum type — anything else
        // is an undefined type. (Aliases/enums are resolved later in codegen.)
        if (structs.find(baseType) == structs.end() &&
            typeAliases.find(baseType) == typeAliases.end() &&
            enumTypes.find(baseType) == enumTypes.end()) {
            error(0, 0, "undefined struct '" + baseType + "'");
        }
    }
}

// Type checking utilities
// Check if a struct satisfies an interface (structural typing)
static bool structSatisfiesInterface(
        const std::map<std::string, std::pair<std::string, std::vector<std::string>>>& funcs,
        const std::string& structName,
        InterfaceDecl* iface) {
    for (const auto& method : iface->methods) {
        std::string mangled = structName + "_" + method.name;
        if (funcs.find(mangled) == funcs.end()) return false;
    }
    return true;
}

bool TypeChecker::isValidAssignment(const std::string& lhsType, const std::string& rhsType) {
    // Normalize both sides so "Point" == "struct:Point"
    std::string lhs = normalizeType(lhsType);
    std::string rhs = normalizeType(rhsType);

    if (lhs == rhs) return true;
    if (isNumericType(lhs) && isNumericType(rhs)) return true;
    if (lhs == "null" || rhs == "null") return isPointerType(lhs) || isPointerType(rhs);
    if (isPointerType(lhs) && isPointerType(rhs)) return true;

    // Interface satisfaction: assigning a struct to an interface type
    auto ifaceIt = interfaceDecls.find(lhs);
    if (ifaceIt != interfaceDecls.end()) {
        std::string structName = rhs;
        // Strip leading * and struct: prefix to get bare struct name
        if (!structName.empty() && structName.front() == '*') structName = structName.substr(1);
        if (structName.size() > 7 && structName.substr(0, 7) == "struct:") structName = structName.substr(7);
        while (!structName.empty() && structName.back() == '*') structName.pop_back();
        if (structSatisfiesInterface(functionSignatures, structName, ifaceIt->second))
            return true;
    }

    return false;
}

bool TypeChecker::isNumericType(const std::string& type) {
    return isIntType(type) || isFloatType(type);
}

bool TypeChecker::isIntType(const std::string& type) {
    return type == "int"   || type == "int8"  || type == "int16"  ||
           type == "int32" || type == "int64" ||
           type == "uint"  || type == "uint8" || type == "uint16" ||
           type == "uint32"|| type == "uint64"||
           type == "char"  || type == "bool";
}

bool TypeChecker::isFloatType(const std::string& type) {
    return type == "float" || type == "double";
}

bool TypeChecker::isPrimitiveType(const std::string& type) {
    if (type.size() > 3 && type.substr(0, 3) == "fn(") return true;
    return isNumericType(type) || type == "void" || type == "string";
}

bool TypeChecker::isPointerType(const std::string& type) {
    return !type.empty() && (type[0] == '*' || type.back() == '*' || type == "string");
}

std::string TypeChecker::getPointeeType(const std::string& pointerType) {
    // Accept both pointer spellings: *T (canonical) and T* (trailing-star).
    if (!pointerType.empty() && pointerType.front() == '*')
        return pointerType.substr(1);
    if (!pointerType.empty() && pointerType.back() == '*')
        return pointerType.substr(0, pointerType.size() - 1);
    return "";
}

// Type normalization
void TypeChecker::unifyTypeParam(std::string pattern, std::string concrete,
                                 const std::set<std::string>& tps,
                                 std::map<std::string, std::string>& subs) {
    auto stripStruct = [](std::string s) {
        return s.rfind("struct:", 0) == 0 ? s.substr(7) : s;
    };
    auto canon = [](std::string t) {           // move trailing '*' to leading
        int stars = 0;
        while (!t.empty() && t.back()  == '*') { t.pop_back();    stars++; }
        while (!t.empty() && t.front() == '*') { t = t.substr(1); stars++; }
        return std::string(stars, '*') + t;
    };
    pattern  = canon(stripStruct(pattern));
    concrete = canon(stripStruct(concrete));
    size_t pi = 0, ci = 0;
    while (pi < pattern.size() && pattern[pi] == '*' &&
           ci < concrete.size() && concrete[ci] == '*') { pi++; ci++; }
    pattern = pattern.substr(pi);
    concrete = stripStruct(concrete.substr(ci));
    if (pattern.empty() || concrete.empty()) return;

    if (tps.count(pattern)) {                   // bare type parameter
        if (!subs.count(pattern)) subs[pattern] = concrete;
        return;
    }
    if (pattern.find('<') == std::string::npos) return;
    auto [pbase, pargs] = splitTemplateType(pattern);
    std::string cbase; std::vector<std::string> cargs;
    if (concrete.find('<') != std::string::npos) {
        auto pr = splitTemplateType(concrete); cbase = pr.first; cargs = pr.second;
    } else {
        auto it = templateInstanceArgs.find(concrete);
        if (it != templateInstanceArgs.end()) { cbase = it->second.first; cargs = it->second.second; }
    }
    if (cbase != pbase) return;
    for (size_t i = 0; i < pargs.size() && i < cargs.size(); ++i)
        unifyTypeParam(pargs[i], cargs[i], tps, subs);
}

std::string TypeChecker::normalizeType(const std::string& type) {
    if (hasPointerSuffix(type)) {
        return addPointerSuffix(normalizeType(extractBaseType(type)));
    }
    // Resolve a type alias to its underlying type.
    if (auto it = typeAliases.find(type); it != typeAliases.end())
        return normalizeType(it->second);
    // An enum type is an integer.
    if (enumTypes.count(type)) return "int";
    if (type.find("struct:") == 0 || type.find("interface:") == 0) {
        return type;
    }
    // Template instantiation: Result<int,string> → struct:Result_int_string
    if (type.find('<') != std::string::npos) {
        auto [tname, args] = splitTemplateType(type);
        auto templ = templateDecls.find(tname);
        if (templ != templateDecls.end()) {
            std::string mangled = mangleTemplate(type);
            templateInstanceArgs[mangled] = {tname, args};  // for type-arg inference
            // Instantiate if not already done
            if (structs.find(mangled) == structs.end()) {
                auto& tp = templ->second->typeParams;
                std::map<std::string, std::string> subs;
                for (size_t i = 0; i < tp.size() && i < args.size(); ++i)
                    subs[tp[i]] = args[i];
                StructInfo info;
                info.name = mangled;
                for (const auto& f : templ->second->fields)
                    info.fields.push_back({substType(f.type, subs), f.name});
                structs[mangled] = info;
            }
            return "struct:" + mangled;
        }
        return type; // unknown template — will error elsewhere
    }
    if (structs.find(type) != structs.end()) {
        return "struct:" + type;
    }
    return type;
}

// Type promotion
std::string TypeChecker::promoteType(const std::string& type1, const std::string& type2) {
    if (type1 == type2) return type1;
    if (type1 == "double"  || type2 == "double")  return "double";
    if (type1 == "float"   || type2 == "float")   return "float";
    if (type1 == "int64"   || type2 == "int64")   return "int64";
    if (type1 == "uint64"  || type2 == "uint64")  return "uint64";
    if (type1 == "int32"   || type2 == "int32")   return "int32";
    if (type1 == "uint32"  || type2 == "uint32")  return "uint32";
    if (type1 == "int16"   || type2 == "int16")   return "int16";
    if (type1 == "uint16"  || type2 == "uint16")  return "uint16";
    return type1;
}

// Pointer type utilities
bool TypeChecker::hasPointerSuffix(const std::string& type) const {
    return !type.empty() && type.back() == '*';
}

std::string TypeChecker::extractBaseType(const std::string& pointerType) const {
    if (hasPointerSuffix(pointerType)) {
        // Remove the trailing '*'
        return pointerType.substr(0, pointerType.length() - 1);
    }
    return pointerType;
}

std::string TypeChecker::addPointerSuffix(const std::string& baseType) const {
    return baseType + "*";
}

// Error reporting
void TypeChecker::error(int line, int col, const std::string& message) {
    hasErrors = true;
    std::stringstream ss;
    ss << sourceFile << ":" << line << ":" << col << ": " << message;
    errors.push_back(ss.str());
}

void TypeChecker::warning(int line, int col, const std::string& message) {
    std::stringstream ss;
    ss << sourceFile << ":" << line << ":" << col << ": warning: " << message;
    std::cerr << ss.str() << "\n";
}

void TypeChecker::warnAssignInCondition(Expr* cond) {
    if (!warnAll) return;
    if (auto* b = dynamic_cast<BinaryExpr*>(cond); b && b->op == "=")
        warning(b->line, b->col,
                "assignment used as a condition — did you mean '=='?");
}
