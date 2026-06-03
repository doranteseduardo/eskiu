#include "parser.h"
#include "../lexer/lexer.h"
#include <stdexcept>
#include <iostream>

Parser::Parser(const std::vector<Token>& tok)
    : tokens(tok), current(0) {}

// ============================================================================
// Helper Methods
// ============================================================================

Token Parser::peek() const {
    if (is_at_end()) {
        return tokens.back();
    }
    return tokens[current];
}

Token Parser::peek_ahead(int n) const {
    size_t pos = current + n;
    if (pos >= tokens.size()) {
        return tokens.back();
    }
    return tokens[pos];
}

Token Parser::advance() {
    if (current < tokens.size()) {
        return tokens[current++];
    }
    if (!tokens.empty()) {
        return tokens.back();
    }
    throw std::runtime_error("No tokens available");
}

bool Parser::check(TokenType type) const {
    if (is_at_end()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(const std::vector<TokenType>& types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    throw std::runtime_error(message);
}

bool Parser::is_at_end() const {
    if (current >= tokens.size()) {
        return true;
    }
    if (tokens.empty()) {
        return true;
    }
    return false;
}

// ============================================================================
// Type Parsing
// ============================================================================

std::string Parser::parseType() {
    std::string type;

    // Handle leading pointers (Rust-style: *i32)
    int leading_pointers = 0;
    while (match(TokenType::STAR)) {
        leading_pointers++;
    }

    if (is_at_end()) {
        throw std::runtime_error("Unexpected end of file while parsing type");
    }

    Token typeToken = peek();
    if (match({TokenType::INT, TokenType::FLOAT, TokenType::DOUBLE,
               TokenType::BOOL, TokenType::CHAR, TokenType::STRING, TokenType::VOID,
               TokenType::INT8, TokenType::INT16, TokenType::INT32, TokenType::INT64,
               TokenType::UINT, TokenType::UINT8, TokenType::UINT16,
               TokenType::UINT32, TokenType::UINT64})) {
        type = typeToken.value;
    } else if (check(TokenType::IDENT)) {
        type = advance().value;
        // Template instantiation: Name<TypeArg, ...>  e.g. Result<int, string>
        if (match(TokenType::LT)) {
            type += "<";
            bool first = true;
            do {
                if (!first) type += ",";
                first = false;
                type += parseType();
            } while (match(TokenType::COMMA));
            consume(TokenType::GT, "Expected '>' after template arguments");
            type += ">";
        }
    } else {
        throw std::runtime_error("Expected type, got " + tokenTypeToString(peek().type));
    }

    // Handle trailing pointers (C-style: i32*)
    while (match(TokenType::STAR)) {
        type += "*";
    }

    // Add leading pointers at the beginning
    for (int i = 0; i < leading_pointers; i++) {
        type = "*" + type;
    }

    // Handle array syntax [N] — capture the size literal
    while (match(TokenType::LBRACKET)) {
        std::string sizeStr;
        while (!is_at_end() && !check(TokenType::RBRACKET)) {
            sizeStr += peek().value;
            advance();
        }
        if (!match(TokenType::RBRACKET)) {
            throw std::runtime_error("Expected ']'");
        }
        type += "[" + sizeStr + "]";
    }

    return type;
}

std::vector<std::pair<std::string, std::string>> Parser::parseParameterList() {
    std::vector<std::pair<std::string, std::string>> params;

    if (!check(TokenType::RPAREN)) {
        do {
            // Handle variadic parameters (...)
            if (match(TokenType::ELLIPSIS)) {
                params.push_back({"...", "..."});
                break;
            }

            std::string type = parseType();
            std::string name = consume(TokenType::IDENT, "Expected parameter name").value;
            params.push_back({type, name});
        } while (match(TokenType::COMMA));
    }

    return params;
}

// ============================================================================
// Declarations
// ============================================================================

std::shared_ptr<Program> Parser::parse() {
    auto decls = parseProgram();
    return std::make_shared<Program>(decls);
}

std::vector<DeclPtr> Parser::parseProgram() {
    std::vector<DeclPtr> declarations;

    while (!is_at_end()) {
        try {
            DeclPtr decl = parseDeclaration();
            if (decl) {
                declarations.push_back(decl);
            }
        } catch (const std::exception& e) {
            // Skip to next semicolon
            while (!is_at_end() && !check(TokenType::SEMICOLON)) {
                advance();
            }
            if (check(TokenType::SEMICOLON)) {
                advance();
            }
        }
    }

    return declarations;
}

DeclPtr Parser::parseDeclaration() {
    try {
        if (match(TokenType::EXTERN)) {
            return parseExternDecl();
        }
        if (match(TokenType::STRUCT)) {
            return parseStructDecl();
        }

        // Handle 'let' variable declarations
        if (match(TokenType::LET)) {
            std::string name = consume(TokenType::IDENT, "Expected identifier after 'let'").value;
            consume(TokenType::COLON, "Expected ':' after variable name");
            std::string type = parseType();

            ExprPtr init = nullptr;
            if (match(TokenType::EQ)) {
                init = parseExpression();
            }
            consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
            return std::make_shared<VarDecl>(name, type, init);
        }

        // Try to parse as type declaration (function or variable)
        if (check(TokenType::INT) || check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
            check(TokenType::BOOL) || check(TokenType::CHAR) || check(TokenType::STRING) ||
            check(TokenType::VOID) || check(TokenType::STAR) || check(TokenType::IDENT) ||
            check(TokenType::INT8) || check(TokenType::INT16) || check(TokenType::INT32) ||
            check(TokenType::INT64) || check(TokenType::UINT) || check(TokenType::UINT8) ||
            check(TokenType::UINT16) || check(TokenType::UINT32) || check(TokenType::UINT64)) {

            size_t savePos = current;
            std::string type = parseType();

            if (check(TokenType::IDENT)) {
                std::string name = advance().value;

                if (match(TokenType::LPAREN)) {
                    // Function declaration - reset and parse full function
                    current = savePos;
                    return parseFunctionDecl();
                } else if (match(TokenType::SEMICOLON) || match(TokenType::EQ)) {
                    // Variable declaration
                    ExprPtr init = nullptr;
                    if (tokens[current - 1].type == TokenType::EQ) {
                        init = parseExpression();
                        consume(TokenType::SEMICOLON, "Expected ';'");
                    }
                    return std::make_shared<VarDecl>(name, type, init);
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Error parsing declaration: ") + e.what());
    }

    throw std::runtime_error("Expected declaration");
}

DeclPtr Parser::parseFunctionDecl() {
    std::string returnType = parseType();
    std::string name = consume(TokenType::IDENT, "Expected function name").value;

    consume(TokenType::LPAREN, "Expected '('");
    auto params = parseParameterList();
    consume(TokenType::RPAREN, "Expected ')'");

    StmtPtr body = parseBlockStatement();

    return std::make_shared<FunctionDecl>(name, returnType, params, body);
}

DeclPtr Parser::parseExternDecl() {
    std::string returnType = parseType();
    std::string name = consume(TokenType::IDENT, "Expected function name").value;

    consume(TokenType::LPAREN, "Expected '('");
    auto params = parseParameterList();
    consume(TokenType::RPAREN, "Expected ')'");
    consume(TokenType::SEMICOLON, "Expected ';'");

    return std::make_shared<ExternDecl>(name, returnType, params);
}

DeclPtr Parser::parseStructDecl() {
    std::string name = consume(TokenType::IDENT, "Expected struct name").value;

    // Optional type parameters: struct List<T>  or  struct Result<T, E>
    std::vector<std::string> typeParams;
    if (match(TokenType::LT)) {
        do {
            typeParams.push_back(consume(TokenType::IDENT, "Expected type parameter name").value);
        } while (match(TokenType::COMMA));
        consume(TokenType::GT, "Expected '>'");
    }

    consume(TokenType::LBRACE, "Expected '{'");

    std::vector<StructDecl::Field> fields;
    std::vector<DeclPtr> methods;

    while (!check(TokenType::RBRACE) && !is_at_end()) {
        size_t savePos = current;
        try {
            std::string memberType = parseType();
            std::string memberName = consume(TokenType::IDENT, "Expected member name").value;

            if (check(TokenType::LPAREN)) {
                // Method — backtrack and parse as a full function declaration
                current = savePos;
                methods.push_back(parseFunctionDecl());
            } else {
                consume(TokenType::SEMICOLON, "Expected ';' after field");
                fields.push_back({memberType, memberName});
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("In struct '") + name + "': " + e.what());
        }
    }

    consume(TokenType::RBRACE, "Expected '}'");

    auto decl = std::make_shared<StructDecl>(name, fields);
    decl->methods  = methods;
    decl->typeParams = typeParams;
    return decl;
}

// ============================================================================
// Statements
// ============================================================================

StmtPtr Parser::parseStatement() {
    if (match(TokenType::LBRACE)) {
        current--;
        return parseBlockStatement();
    }
    if (check(TokenType::IF)) {
        return parseIfStatement();
    }
    if (check(TokenType::FOR)) {
        return parseForStatement();
    }
    if (check(TokenType::WHILE)) {
        return parseWhileStatement();
    }
    if (match(TokenType::RETURN)) {
        return parseReturnStatement();
    }
    if (match(TokenType::BREAK)) {
        return parseBreakStatement();
    }
    return parseExpressionStatement();
}

StmtPtr Parser::parseBlockStatement() {
    consume(TokenType::LBRACE, "Expected '{'");
    std::vector<BlockItem> items;

    while (!check(TokenType::RBRACE) && !is_at_end()) {
        // Check if this looks like a declaration
        if (check(TokenType::LET) ||
            check(TokenType::INT) || check(TokenType::FLOAT) ||
            check(TokenType::DOUBLE) || check(TokenType::BOOL) ||
            check(TokenType::CHAR) || check(TokenType::STRING) ||
            check(TokenType::VOID) || check(TokenType::STAR) ||
            check(TokenType::IDENT) ||
            check(TokenType::INT8) || check(TokenType::INT16) || check(TokenType::INT32) ||
            check(TokenType::INT64) || check(TokenType::UINT) || check(TokenType::UINT8) ||
            check(TokenType::UINT16) || check(TokenType::UINT32) || check(TokenType::UINT64)) {

            size_t savePos = current;
            try {
                DeclPtr decl = parseDeclaration();
                if (decl) {
                    items.push_back(decl);
                    continue;
                }
            } catch (...) {
                current = savePos;
            }
        }

        // Otherwise parse as statement
        StmtPtr stmt = parseStatement();
        items.push_back(stmt);
    }

    consume(TokenType::RBRACE, "Expected '}'");
    return std::make_shared<BlockStmt>(items);
}

StmtPtr Parser::parseIfStatement() {
    consume(TokenType::IF, "Expected 'if'");
    consume(TokenType::LPAREN, "Expected '('");
    ExprPtr condition = parseExpression();
    consume(TokenType::RPAREN, "Expected ')'");

    StmtPtr thenBranch = parseStatement();
    StmtPtr elseBranch = nullptr;

    if (match(TokenType::ELSE)) {
        elseBranch = parseStatement();
    }

    return std::make_shared<IfStmt>(condition, thenBranch, elseBranch);
}

StmtPtr Parser::parseForStatement() {
    consume(TokenType::FOR, "Expected 'for'");
    consume(TokenType::LPAREN, "Expected '('");

    StmtPtr init = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        init = parseExpressionStatement();
    } else {
        advance();
    }

    ExprPtr condition = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        condition = parseExpression();
    }
    consume(TokenType::SEMICOLON, "Expected ';'");

    ExprPtr step = nullptr;
    if (!check(TokenType::RPAREN)) {
        step = parseExpression();
    }
    consume(TokenType::RPAREN, "Expected ')'");

    StmtPtr body = parseStatement();

    return std::make_shared<ForStmt>(init, condition, step, body);
}

StmtPtr Parser::parseWhileStatement() {
    consume(TokenType::WHILE, "Expected 'while'");
    consume(TokenType::LPAREN, "Expected '('");
    ExprPtr condition = parseExpression();
    consume(TokenType::RPAREN, "Expected ')'");

    StmtPtr body = parseStatement();

    return std::make_shared<WhileStmt>(condition, body);
}

StmtPtr Parser::parseReturnStatement() {
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        value = parseExpression();
    }
    consume(TokenType::SEMICOLON, "Expected ';'");
    return std::make_shared<ReturnStmt>(value);
}

StmtPtr Parser::parseBreakStatement() {
    consume(TokenType::SEMICOLON, "Expected ';'");
    return std::make_shared<BreakStmt>();
}

StmtPtr Parser::parseExpressionStatement() {
    ExprPtr expr = parseExpression();
    consume(TokenType::SEMICOLON, "Expected ';'");
    return std::make_shared<ExprStmt>(expr);
}

// ============================================================================
// Expressions (Precedence Climbing)
// ============================================================================

ExprPtr Parser::parseStructInit(const std::string& structName) {
    consume(TokenType::LBRACE, "Expected '{'");
    std::vector<std::pair<std::string, ExprPtr>> inits;

    if (!check(TokenType::RBRACE)) {
        do {
            // Named: fieldName: expr
            if (check(TokenType::IDENT) && peek_ahead(1).type == TokenType::COLON) {
                std::string fieldName = advance().value;
                advance(); // consume ':'
                inits.push_back({fieldName, parseExpression()});
            } else {
                // Positional
                inits.push_back({"", parseExpression()});
            }
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RBRACE, "Expected '}'");
    return std::make_shared<StructInitExpr>(structName, std::move(inits));
}

ExprPtr Parser::parseExpression() {
    return parseAssignment();
}

ExprPtr Parser::parseAssignment() {
    ExprPtr expr = parseLogicalOr();

    if (match(TokenType::EQ)) {
        ExprPtr value = parseAssignment();
        return std::make_shared<BinaryExpr>(expr, "=", value);
    }

    return expr;
}

ExprPtr Parser::parseLogicalOr() {
    ExprPtr expr = parseLogicalAnd();

    while (match(TokenType::OR)) {
        std::string op = tokens[current - 1].value;
        ExprPtr right = parseLogicalAnd();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }

    return expr;
}

ExprPtr Parser::parseLogicalAnd() {
    ExprPtr expr = parseEquality();

    while (match(TokenType::AND)) {
        std::string op = tokens[current - 1].value;
        ExprPtr right = parseEquality();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }

    return expr;
}

ExprPtr Parser::parseEquality() {
    ExprPtr expr = parseComparison();

    while (match({TokenType::EQEQ, TokenType::NE})) {
        std::string op = tokens[current - 1].value;
        ExprPtr right = parseComparison();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }

    return expr;
}

ExprPtr Parser::parseComparison() {
    ExprPtr expr = parseAddition();

    while (match({TokenType::LT, TokenType::GT, TokenType::LE, TokenType::GE})) {
        std::string op = tokens[current - 1].value;
        ExprPtr right = parseAddition();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }

    return expr;
}

ExprPtr Parser::parseAddition() {
    ExprPtr expr = parseMultiplication();

    while (match({TokenType::PLUS, TokenType::MINUS})) {
        std::string op = tokens[current - 1].value;
        ExprPtr right = parseMultiplication();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }

    return expr;
}

ExprPtr Parser::parseMultiplication() {
    ExprPtr expr = parseUnary();

    while (match({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
        std::string op = tokens[current - 1].value;
        ExprPtr right = parseUnary();
        expr = std::make_shared<BinaryExpr>(expr, op, right);
    }

    return expr;
}

ExprPtr Parser::parseUnary() {
    if (match({TokenType::NOT, TokenType::MINUS, TokenType::PLUS, TokenType::AMPERSAND, TokenType::STAR})) {
        Token opToken = tokens[current - 1];
        ExprPtr expr = parseUnary();
        return std::make_shared<UnaryExpr>(opToken.value, expr);
    }

    // Cast expression: (TYPE) expr
    // Only trigger on unambiguous type keywords to avoid conflict with (expr).
    if (check(TokenType::LPAREN)) {
        TokenType inner = peek_ahead(1).type;
        bool isTypeKeyword = (inner == TokenType::INT    || inner == TokenType::FLOAT  ||
                              inner == TokenType::DOUBLE  || inner == TokenType::BOOL   ||
                              inner == TokenType::CHAR    || inner == TokenType::STRING  ||
                              inner == TokenType::VOID    || inner == TokenType::STAR    ||
                              inner == TokenType::INT8    || inner == TokenType::INT16   ||
                              inner == TokenType::INT32   || inner == TokenType::INT64   ||
                              inner == TokenType::UINT    || inner == TokenType::UINT8   ||
                              inner == TokenType::UINT16  || inner == TokenType::UINT32  ||
                              inner == TokenType::UINT64);
        if (isTypeKeyword) {
            size_t savePos = current;
            try {
                advance(); // consume (
                std::string castType = parseType();
                if (match(TokenType::RPAREN)) {
                    ExprPtr expr = parseUnary();
                    return std::make_shared<CastExpr>(castType, expr);
                }
            } catch (...) {}
            current = savePos;
        }
    }

    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();

    while (true) {
        if (match(TokenType::LPAREN)) {
            // Function call
            std::vector<ExprPtr> args;
            if (!check(TokenType::RPAREN)) {
                do {
                    args.push_back(parseExpression());
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN, "Expected ')'");
            expr = std::make_shared<CallExpr>(expr, args);
        } else if (match(TokenType::LBRACKET)) {
            // Array indexing
            ExprPtr index = parseExpression();
            consume(TokenType::RBRACKET, "Expected ']'");
            expr = std::make_shared<IndexExpr>(expr, index);
        } else if (match(TokenType::DOT)) {
            // Member access
            std::string member = consume(TokenType::IDENT, "Expected member name").value;
            expr = std::make_shared<MemberExpr>(expr, member);
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parsePrimary() {
    Token tok = peek();

    if (match(TokenType::TRUE)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::BOOL, "true");
    }
    if (match(TokenType::FALSE)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::BOOL, "false");
    }
    if (match(TokenType::NULL_KW)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::NULL_VAL, "null");
    }
    if (match(TokenType::INT_LIT)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::INT, tok.value);
    }
    if (match(TokenType::FLOAT_LIT)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::FLOAT, tok.value);
    }
    if (match(TokenType::STRING_LIT)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::STRING, tok.value);
    }
    if (match(TokenType::CHAR_LIT)) {
        return std::make_shared<LiteralExpr>(LiteralExpr::Kind::CHAR, tok.value);
    }
    // alloc(T, N) — type keyword as first argument requires special parsing
    if (match(TokenType::ALLOC)) {
        consume(TokenType::LPAREN, "Expected '(' after alloc");
        std::string elemType = parseType();
        consume(TokenType::COMMA, "Expected ',' after type in alloc");
        ExprPtr count = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        return std::make_shared<AllocExpr>(elemType, count);
    }

    // free(ptr) — keyword call, maps to the C free function
    if (match(TokenType::FREE)) {
        consume(TokenType::LPAREN, "Expected '(' after free");
        ExprPtr arg = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        auto callee = std::make_shared<IdentExpr>("free");
        return std::make_shared<CallExpr>(callee, std::vector<ExprPtr>{arg});
    }

    if (match(TokenType::IDENT)) {
        // Struct init: StructName { [field: expr, ...] }
        if (check(TokenType::LBRACE)) {
            return parseStructInit(tok.value);
        }
        return std::make_shared<IdentExpr>(tok.value);
    }
    if (match(TokenType::LPAREN)) {
        ExprPtr expr = parseExpression();
        if (!match(TokenType::RPAREN)) {
            throw std::runtime_error("Expected ')'");
        }
        return expr;
    }

    throw std::runtime_error(std::string("Expected expression, got ") + tokenTypeToString(tok.type));
}
