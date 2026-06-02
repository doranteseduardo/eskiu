#include "type_checker.h"
#include <iostream>
#include <sstream>
#include <algorithm>

TypeChecker::TypeChecker() {
    pushScope();  // Global scope
}

bool TypeChecker::check(Program* program) {
    hasErrors = false;
    errors.clear();

    // First pass: register all function declarations
    for (const auto& decl : program->declarations) {
        if (auto funcDecl = dynamic_cast<FunctionDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : funcDecl->params) {
                paramTypes.push_back(param.second);
            }
            defineFunction(funcDecl->name, funcDecl->returnType, paramTypes);
        } else if (auto externDecl = dynamic_cast<ExternDecl*>(decl.get())) {
            std::vector<std::string> paramTypes;
            for (const auto& param : externDecl->params) {
                paramTypes.push_back(param.second);
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
    defineSymbol(node->name, node->type);
}

void TypeChecker::visit(StructDecl* node) {
    // Basic struct support - just track the struct name
    // Full struct type checking deferred to Phase 5
    defineSymbol(node->name, "struct:" + node->name);
}

void TypeChecker::visit(ExternDecl* node) {
    // Extern functions are already registered in first pass
    // Just verify they have valid signatures
}

// Statement visitors
void TypeChecker::visit(BlockStmt* node) {
    pushScope();
    for (const auto& stmt : node->statements) {
        stmt->accept(this);
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
    // Get function name
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

    // Check argument count
    if (node->args.size() != expectedParamTypes.size()) {
        error(0, 0, "function '" + funcName + "' expects " +
                    std::to_string(expectedParamTypes.size()) + " arguments, got " +
                    std::to_string(node->args.size()));
        expressionTypes[node] = sig.first;
        return;
    }

    // Type check arguments
    for (size_t i = 0; i < node->args.size(); ++i) {
        node->args[i]->accept(this);
        std::string argType = getExpressionType(node->args[i].get());
        if (argType != "unknown" && !isValidAssignment(expectedParamTypes[i], argType)) {
            error(0, 0, "argument " + std::to_string(i + 1) + " type mismatch: expected " +
                        expectedParamTypes[i] + ", got " + argType);
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
    // Full struct member checking deferred to Phase 5
    expressionTypes[node] = "unknown";
}

void TypeChecker::visit(CastExpr* node) {
    node->expr->accept(this);
    // Explicit casts are always allowed
    expressionTypes[node] = node->targetType;
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
    // Comparison operators return bool
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        return "bool";
    }
    if (op == "&&" || op == "||") {
        return "bool";
    }

    // Arithmetic operators
    if (!isNumericType(leftType) || !isNumericType(rightType)) {
        return "error";
    }

    // Promote to wider type
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

// Type checking utilities
bool TypeChecker::isValidAssignment(const std::string& lhsType, const std::string& rhsType) {
    if (lhsType == rhsType) {
        return true;
    }

    // Numeric type promotion
    if (isNumericType(lhsType) && isNumericType(rhsType)) {
        // Allow any numeric to numeric conversion
        return true;
    }

    // Pointer compatibility
    if (lhsType == "null" || rhsType == "null") {
        return isPointerType(lhsType) || isPointerType(rhsType);
    }

    return false;
}

bool TypeChecker::isNumericType(const std::string& type) {
    return isIntType(type) || isFloatType(type);
}

bool TypeChecker::isIntType(const std::string& type) {
    return type == "int" || type == "int8" || type == "int16" || type == "int32" ||
           type == "int64" || type == "uint" || type == "uint8" || type == "uint16" ||
           type == "uint32" || type == "uint64" || type == "char" || type == "bool";
}

bool TypeChecker::isFloatType(const std::string& type) {
    return type == "float" || type == "double";
}

bool TypeChecker::isPrimitiveType(const std::string& type) {
    return isNumericType(type) || type == "bool" || type == "void" || type == "string";
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

// Type promotion
std::string TypeChecker::promoteType(const std::string& type1, const std::string& type2) {
    if (type1 == type2) {
        return type1;
    }

    // Prefer wider types
    if (type1 == "double" || type2 == "double") {
        return "double";
    }
    if (type1 == "float" || type2 == "float") {
        return "float";
    }
    if (type1 == "int64" || type2 == "int64") {
        return "int64";
    }
    if (type1 == "int32" || type2 == "int32") {
        return "int32";
    }

    return type1;
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
