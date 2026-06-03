#include "type_checker.h"
#include <iostream>
#include <sstream>
#include <algorithm>

// ============================================================================
// Template utilities (file-local helpers)
// ============================================================================

static std::string mangleTemplate(const std::string& type) {
    // "Result<int,string>" → "Result_int_string"
    std::string out;
    for (char c : type) {
        if (c == '<' || c == '>' || c == ',') out += '_';
        else if (c != ' ')                   out += c;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

static std::pair<std::string, std::vector<std::string>>
splitTemplateType(const std::string& type) {
    size_t lt = type.find('<');
    if (lt == std::string::npos) return {type, {}};
    std::string name = type.substr(0, lt);
    std::string inner = type.substr(lt + 1, type.size() - lt - 2);
    std::vector<std::string> args;
    int depth = 0; std::string cur;
    for (char c : inner) {
        if (c == '<') { depth++; cur += c; }
        else if (c == '>') { depth--; cur += c; }
        else if (c == ',' && depth == 0) { args.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) args.push_back(cur);
    return {name, args};
}

static std::string substType(const std::string& t,
                             const std::map<std::string, std::string>& subs) {
    auto it = subs.find(t);
    if (it != subs.end()) return it->second;
    if (!t.empty() && t.front() == '*') return "*" + substType(t.substr(1), subs);
    if (!t.empty() && t.back()  == '*') return substType(t.substr(0, t.size()-1), subs) + "*";
    size_t lb = t.rfind('[');
    if (lb != std::string::npos && t.back() == ']')
        return substType(t.substr(0, lb), subs) + t.substr(lb);
    size_t lt = t.find('<');
    if (lt != std::string::npos && t.back() == '>') {
        std::string name  = t.substr(0, lt);
        std::string inner = t.substr(lt + 1, t.size() - lt - 2);
        std::vector<std::string> args;
        int depth = 0; std::string cur;
        for (char c : inner) {
            if      (c == '<') { depth++; cur += c; }
            else if (c == '>') { depth--; cur += c; }
            else if (c == ',' && depth == 0) { args.push_back(substType(cur, subs)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) args.push_back(substType(cur, subs));
        std::string result = name + "<";
        for (size_t i = 0; i < args.size(); ++i) { if (i) result += ","; result += args[i]; }
        return result + ">";
    }
    return t;
}

// ============================================================================

TypeChecker::TypeChecker() {
    pushScope();  // Global scope
    // Pre-register C runtime builtins available without explicit extern
    defineFunction("free", "void", {"..."});  // accepts any pointer
}

bool TypeChecker::check(Program* program) {
    hasErrors = false;
    errors.clear();

    // First pass: register all struct declarations and function signatures
    for (const auto& decl : program->declarations) {
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
        }
    }

    // Second pass: type check all declarations
    for (const auto& decl : program->declarations) {
        decl->accept(this);
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

void TypeChecker::visit(FunctionDecl* node) {
    if (!node->typeParams.empty()) return; // Template body checked on instantiation

    currentFunctionReturnType = node->returnType;
    pushScope();

    // Define parameters
    for (const auto& param : node->params) {
        defineSymbol(param.second, param.first);  // param.second = name, param.first = type
    }

    // Type check body
    if (node->body) {
        node->body->accept(this);
    }

    popScope();
    currentFunctionReturnType = "";
}

void TypeChecker::visit(VarDecl* node) {
    if (node->initializer) {
        node->initializer->accept(this);
        std::string initType = getExpressionType(node->initializer.get());
        if (initType != "unknown" && !isValidAssignment(node->type, initType)) {
            warning(0, 0, "implicit conversion from " + initType + " to " + node->type);
        }
    }
    // Normalize the type (e.g., "Point" -> "struct:Point")
    std::string normalizedType = normalizeType(node->type);

    // Validate that struct types exist before use
    validateStructType(normalizedType);

    defineSymbol(node->name, normalizedType);
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
            for (const auto& p : func->params) defineSymbol(p.second, p.first);
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
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            error(0, 0, "condition must be boolean or numeric, got " + condType);
        }
    }
    if (node->thenBranch) {
        node->thenBranch->accept(this);
    }
    if (node->elseBranch) {
        node->elseBranch->accept(this);
    }
}

void TypeChecker::visit(WhileStmt* node) {
    if (node->condition) {
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            error(0, 0, "condition must be boolean or numeric, got " + condType);
        }
    }
    if (node->body) {
        node->body->accept(this);
    }
}

void TypeChecker::visit(ForStmt* node) {
    pushScope();

    // Type check init
    if (node->init) {
        node->init->accept(this);
    }

    // Type check condition
    if (node->condition) {
        node->condition->accept(this);
        std::string condType = getExpressionType(node->condition.get());
        if (condType != "unknown" && condType != "bool" && !isNumericType(condType)) {
            error(0, 0, "condition must be boolean or numeric, got " + condType);
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
            error(0, 0, "return type mismatch: expected " + currentFunctionReturnType +
                        ", got " + valueType);
        }
    } else if (currentFunctionReturnType != "void") {
        error(0, 0, "return type mismatch: expected " + currentFunctionReturnType +
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

    std::string leftType = getExpressionType(node->left.get());
    std::string rightType = getExpressionType(node->right.get());

    if (leftType == "unknown" || rightType == "unknown") {
        expressionTypes[node] = "unknown";
        return;
    }

    std::string resultType = inferBinaryExprType(leftType, node->op, rightType);

    if (resultType == "error") {
        error(0, 0, "invalid operands for operator: " + leftType + " and " + rightType);
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = resultType;
    }
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
        error(0, 0, "invalid operand for unary operator: " + operandType);
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
                error(0, 0, "method '" + member->member + "' expects " +
                            std::to_string(fixedCount) + " argument(s), got " +
                            std::to_string(node->args.size()));
            }
            for (size_t i = 0; i < node->args.size(); ++i) {
                node->args[i]->accept(this);
                if (i < fixedCount) {
                    std::string argType = getExpressionType(node->args[i].get());
                    size_t pi = i + selfSkip;
                    if (argType != "unknown" && !isValidAssignment(paramTypes[pi], argType)) {
                        error(0, 0, "argument " + std::to_string(i + 1) + " type mismatch");
                    }
                }
            }
            expressionTypes[node] = sig.first;
            return;
        }
        // Method not found — fall through to regular error
        error(0, 0, "undefined method '" + member->member + "' on type '" + baseType + "'");
        expressionTypes[node] = "unknown";
        return;
    }

    // Regular function call
    std::string funcName;
    if (auto identExpr = dynamic_cast<IdentExpr*>(node->callee.get())) {
        funcName = identExpr->name;
    } else {
        expressionTypes[node] = "unknown";
        return;
    }

    // Look up function signature
    auto it = functionSignatures.find(funcName);
    if (it == functionSignatures.end()) {
        error(0, 0, "undefined function '" + funcName + "'");
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
            error(0, 0, "function '" + funcName + "' expects at least " +
                        std::to_string(fixedCount) + " arguments, got " +
                        std::to_string(node->args.size()));
            expressionTypes[node] = sig.first;
            return;
        }
    } else if (node->args.size() != fixedCount) {
        error(0, 0, "function '" + funcName + "' expects " +
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
                error(0, 0, "argument " + std::to_string(i + 1) + " type mismatch: expected " +
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
        error(0, 0, "array index must be integer, got " + indexType);
    }

    // For arrays, return element type (simplified)
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
        error(0, 0, "struct '" + structName + "' has no member '" + node->member + "'");
        expressionTypes[node] = "unknown";
    } else if (baseType == "unknown") {
        // Base type is unknown, can't validate member access
        expressionTypes[node] = "unknown";
    } else {
        // Base is not a struct
        error(0, 0, "cannot access member '" + node->member + "' on non-struct type '" + baseType + "'");
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
    std::string type = lookupSymbol(node->name);
    if (type.empty()) {
        error(0, 0, "undefined variable '" + node->name + "'");
        expressionTypes[node] = "unknown";
    } else {
        expressionTypes[node] = type;
    }
}

void TypeChecker::visit(InterfaceDecl* node) {
    // Interface registered in first pass; no body to type-check
}

void TypeChecker::visit(SwitchStmt* node) {
    node->subject->accept(this);
    std::string subjType = getExpressionType(node->subject.get());
    if (subjType != "unknown" && !isIntType(subjType))
        error(0, 0, "switch subject must be integer type, got " + subjType);
    for (auto& c : node->cases) {
        if (c.value) {
            c.value->accept(this);
        }
        for (auto& s : c.stmts) s->accept(this);
    }
}

void TypeChecker::visit(TemplateCallExpr* node) {
    auto templ = funcTemplateDecls.find(node->templateName);
    if (templ == funcTemplateDecls.end()) {
        error(0, 0, "undefined template function '" + node->templateName + "'");
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
            error(0, 0, "argument " + std::to_string(i+1) + ": expected " + expected + ", got " + got);
    }

    std::string retType = normalizeType(substType(fd->returnType, subs));
    expressionTypes[node] = retType;
}

void TypeChecker::visit(AllocExpr* node) {
    node->count->accept(this);
    std::string countType = getExpressionType(node->count.get());
    if (countType != "unknown" && !isIntType(countType))
        error(0, 0, "alloc count must be integer, got " + countType);
    expressionTypes[node] = "*" + node->elemType;
}

void TypeChecker::visit(StructInitExpr* node) {
    auto it = structs.find(node->structName);
    if (it == structs.end()) {
        error(0, 0, "undefined struct '" + node->structName + "'");
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
                error(0, 0, "struct '" + node->structName + "' has no field '" + fname + "'");
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

    expressionTypes[node] = "struct:" + node->structName;
}

// Scope management
void TypeChecker::pushScope() {
    scopes.push_back(std::map<std::string, Symbol>());
}

void TypeChecker::popScope() {
    if (!scopes.empty()) {
        scopes.pop_back();
    }
}

void TypeChecker::defineSymbol(const std::string& name, const std::string& type) {
    if (!scopes.empty()) {
        scopes.back()[name] = {type, true};
    }
}

std::string TypeChecker::lookupSymbol(const std::string& name) {
    // Search from innermost to outermost scope
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto sym = it->find(name);
        if (sym != it->end()) {
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
    std::string baseType = type;
    // Strip pointer modifiers — both trailing T* and leading *T
    if (hasPointerSuffix(baseType)) {
        baseType = extractBaseType(baseType);
    } else if (!baseType.empty() && baseType.front() == '*') {
        baseType = baseType.substr(1);
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
        // Check if it's an unknown type that might be a struct
        // Primitive types: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64, bool
        if (structs.find(baseType) == structs.end()) {
            // Not a primitive, not a known struct -> might be undefined struct
            error(0, 0, "undefined struct '" + baseType + "'");
        }
    }
}

// Type checking utilities
bool TypeChecker::isValidAssignment(const std::string& lhsType, const std::string& rhsType) {
    // Normalize both sides so "Point" == "struct:Point"
    std::string lhs = normalizeType(lhsType);
    std::string rhs = normalizeType(rhsType);

    if (lhs == rhs) return true;
    if (isNumericType(lhs) && isNumericType(rhs)) return true;
    if (lhs == "null" || rhs == "null") return isPointerType(lhs) || isPointerType(rhs);
    // Any pointer is assignable to any pointer (C interop)
    if (isPointerType(lhs) && isPointerType(rhs)) return true;

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
    return isNumericType(type) || type == "void" || type == "string";
}

bool TypeChecker::isPointerType(const std::string& type) {
    return !type.empty() && type[0] == '*';
}

std::string TypeChecker::getPointeeType(const std::string& pointerType) {
    if (isPointerType(pointerType)) {
        return pointerType.substr(1);
    }
    return "";
}

// Type normalization
std::string TypeChecker::normalizeType(const std::string& type) {
    if (hasPointerSuffix(type)) {
        return addPointerSuffix(normalizeType(extractBaseType(type)));
    }
    if (type.find("struct:") == 0 || type.find("interface:") == 0) {
        return type;
    }
    // Template instantiation: Result<int,string> → struct:Result_int_string
    if (type.find('<') != std::string::npos) {
        auto [tname, args] = splitTemplateType(type);
        auto templ = templateDecls.find(tname);
        if (templ != templateDecls.end()) {
            std::string mangled = mangleTemplate(type);
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
    ss << "file.esk:" << line << ":" << col << ": " << message;
    errors.push_back(ss.str());
}

void TypeChecker::warning(int line, int col, const std::string& message) {
    std::stringstream ss;
    ss << "file.esk:" << line << ":" << col << ": warning: " << message;
    std::cerr << ss.str() << "\n";
}
