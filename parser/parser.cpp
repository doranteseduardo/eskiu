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

    // Handle pointers
    while (match(TokenType::STAR)) {
        type += "*";
    }

    if (is_at_end()) {
        throw std::runtime_error("Unexpected end of file while parsing type");
    }

    Token typeToken = peek();
    if (match({TokenType::INT, TokenType::FLOAT, TokenType::DOUBLE,
               TokenType::BOOL, TokenType::CHAR, TokenType::STRING, TokenType::VOID})) {
        type = typeToken.value + type;
    } else if (check(TokenType::IDENT)) {
        type = advance().value + type;
    } else {
        throw std::runtime_error("Expected type, got " + tokenTypeToString(peek().type));
    }

    // Handle array syntax [N]
    while (match(TokenType::LBRACKET)) {
        if (!check(TokenType::RBRACKET)) {
            // Skip array size expression (we don't need it for now)
            int depth = 1;
            while (!is_at_end() && depth > 0) {
                if (check(TokenType::LBRACKET)) depth++;
                else if (check(TokenType::RBRACKET)) depth--;
                if (depth > 0) advance();
            }
        }
        if (!match(TokenType::RBRACKET)) {
            throw std::runtime_error("Expected ']'");
        }
        type += "[]";
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
            check(TokenType::VOID) || check(TokenType::STAR) || check(TokenType::IDENT)) {

            size_t savePos = current;
            try {
                std::string type = parseType();

                if (check(TokenType::IDENT)) {
                    std::string name = advance().value;

                    if (match(TokenType::LPAREN)) {
                        // Function declaration
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
            } catch (...) {
                current = savePos;
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
    consume(TokenType::LBRACE, "Expected '{'");

    std::vector<StructDecl::Field> fields;
    while (!check(TokenType::RBRACE) && !is_at_end()) {
        std::string type = parseType();
        std::string fieldName = consume(TokenType::IDENT, "Expected field name").value;
        consume(TokenType::SEMICOLON, "Expected ';'");
        fields.push_back({type, fieldName});
    }

    consume(TokenType::RBRACE, "Expected '}'");

    return std::make_shared<StructDecl>(name, fields);
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
    std::vector<DeclPtr> declarations;
    std::vector<StmtPtr> statements;

    while (!check(TokenType::RBRACE) && !is_at_end()) {
        // Try to parse as declaration first
        if (check(TokenType::LET) ||
            check(TokenType::INT) || check(TokenType::FLOAT) ||
            check(TokenType::DOUBLE) || check(TokenType::BOOL) ||
            check(TokenType::CHAR) || check(TokenType::STRING) ||
            check(TokenType::VOID) || check(TokenType::STAR) ||
            check(TokenType::IDENT)) {

            size_t savePos = current;
            try {
                DeclPtr decl = parseDeclaration();
                if (decl) {
                    declarations.push_back(decl);
                    continue;
                }
            } catch (...) {
                current = savePos;
            }
        }

        // Otherwise parse as statement
        statements.push_back(parseStatement());
    }

    consume(TokenType::RBRACE, "Expected '}'");
    return std::make_shared<BlockStmt>(declarations, statements);
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

    // Skip cast handling for now to avoid crashes
    // TODO: Implement casts later with better lookahead strategy

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
    if (match(TokenType::IDENT)) {
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
