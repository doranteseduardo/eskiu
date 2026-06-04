#include "parser.h"
#include "../lexer/lexer.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

// Stamp any AST node with position from tok
template<typename T>
static std::shared_ptr<T> withPos(std::shared_ptr<T> node, const Token& tok) {
    node->line = tok.line; node->col = tok.column; return node;
}

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
    // Function pointer type: fn(T,U,...)->R
    if (match(TokenType::FN)) {
        type = "fn(";
        consume(TokenType::LPAREN, "Expected '(' in fn type");
        bool first = true;
        while (!check(TokenType::RPAREN) && !is_at_end()) {
            if (!first) type += ",";
            first = false;
            type += parseType();
        }
        consume(TokenType::RPAREN, "Expected ')' in fn type");
        type += ")->";
        consume(TokenType::ARROW, "Expected '->' in fn type");
        type += parseType();
        // No trailing pointer handling needed for fn types — return early
        for (int lp = 0; lp < leading_pointers; ++lp) type = "*" + type;
        return type;
    }
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

    // Owned import set if caller didn't provide one
    std::set<std::string> ownedSet;
    if (!importedFiles) importedFiles = &ownedSet;

    while (!is_at_end()) {
        // Handle import "path/to/file.esk";
        if (match(TokenType::IMPORT)) {
            try {
                std::string path = consume(TokenType::STRING_LIT,
                    "Expected filename string after import").value;
                consume(TokenType::SEMICOLON, "Expected ';' after import");

                // Resolve path relative to basedir
                std::string fullPath = (!basedir.empty() && path[0] != '/')
                    ? basedir + "/" + path
                    : path;

                if (!importedFiles->count(fullPath)) {
                    importedFiles->insert(fullPath);

                    std::ifstream file(fullPath);
                    if (!file.is_open())
                        throw std::runtime_error("Cannot open import: '" + fullPath + "'");
                    std::ostringstream ss;
                    ss << file.rdbuf();
                    std::string src = ss.str();

                    Lexer lexer(src);
                    std::vector<Token> itoks;
                    Token t = lexer.next_token();
                    while (t.type != TokenType::EOF_TOKEN) { itoks.push_back(t); t = lexer.next_token(); }
                    itoks.push_back(t);

                    Parser sub(itoks);
                    // Resolve basedir for the sub-file
                    size_t slash = fullPath.rfind('/');
                    sub.basedir = (slash != std::string::npos) ? fullPath.substr(0, slash) : ".";
                    sub.importedFiles = importedFiles;

                    auto subProg = sub.parse();
                    declarations.insert(declarations.end(),
                        subProg->declarations.begin(), subProg->declarations.end());
                }
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << std::endl;
                while (!is_at_end() && !check(TokenType::SEMICOLON)) advance();
                if (check(TokenType::SEMICOLON)) advance();
            }
            continue;
        }

        try {
            DeclPtr decl = parseDeclaration();
            if (decl) declarations.push_back(decl);
        } catch (const std::exception& e) {
            while (!is_at_end() && !check(TokenType::SEMICOLON)) advance();
            if (check(TokenType::SEMICOLON)) advance();
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
        if (match(TokenType::INTERFACE)) {
            std::string name = consume(TokenType::IDENT, "Expected interface name").value;
            consume(TokenType::LBRACE, "Expected '{'");
            auto decl = std::make_shared<InterfaceDecl>(name);
            while (!check(TokenType::RBRACE) && !is_at_end()) {
                InterfaceDecl::MethodSig sig;
                sig.returnType = parseType();
                sig.name = consume(TokenType::IDENT, "Expected method name").value;
                consume(TokenType::LPAREN, "Expected '('");
                sig.params = parseParameterList();
                consume(TokenType::RPAREN, "Expected ')'");
                consume(TokenType::SEMICOLON, "Expected ';'");
                decl->methods.push_back(sig);
            }
            consume(TokenType::RBRACE, "Expected '}'");
            return decl;
        }

        // Handle 'let' variable declarations (optionally volatile)
        if (match(TokenType::LET)) {
            bool isVol = match(TokenType::VOLATILE);
            Token letNameTok = peek();
            std::string name = consume(TokenType::IDENT, "Expected identifier after 'let'").value;
            consume(TokenType::COLON, "Expected ':' after variable name");
            std::string type = parseType();

            ExprPtr init = nullptr;
            if (match(TokenType::EQ)) {
                init = parseExpression();
            }
            consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
            auto vd = std::make_shared<VarDecl>(name, type, init);
            vd->line = letNameTok.line; vd->col = letNameTok.column;
            vd->isVolatile = isVol;
            return vd;
        }

        // Try to parse as type declaration (function or variable)
        // Optionally prefixed with 'volatile'
        bool declIsVolatile = false;
        if (check(TokenType::VOLATILE)) { declIsVolatile = true; advance(); }

        if (check(TokenType::INT) || check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
            check(TokenType::BOOL) || check(TokenType::CHAR) || check(TokenType::STRING) ||
            check(TokenType::VOID) || check(TokenType::STAR) || check(TokenType::IDENT) ||
            check(TokenType::INT8) || check(TokenType::INT16) || check(TokenType::INT32) ||
            check(TokenType::INT64) || check(TokenType::UINT) || check(TokenType::UINT8) ||
            check(TokenType::UINT16) || check(TokenType::UINT32) || check(TokenType::UINT64)) {

            size_t savePos = current;
            std::string type = parseType();

            if (check(TokenType::IDENT)) {
                Token nameTok2 = peek();
                std::string name = advance().value;

                if (match(TokenType::LPAREN) || check(TokenType::LT)) {
                    // Function declaration (possibly template: name<T,E>(...))
                    current = savePos;
                    return parseFunctionDecl();
                } else if (match(TokenType::SEMICOLON) || match(TokenType::EQ)) {
                    // Variable declaration
                    ExprPtr init = nullptr;
                    if (tokens[current - 1].type == TokenType::EQ) {
                        init = parseExpression();
                        consume(TokenType::SEMICOLON, "Expected ';'");
                    }
                    auto vd = std::make_shared<VarDecl>(name, type, init);
                    vd->line = nameTok2.line; vd->col = nameTok2.column;
                    vd->isVolatile = declIsVolatile;
                    return vd;
                }
            }
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Error parsing declaration: ") + e.what());
    }

    throw std::runtime_error("Expected declaration");
}

DeclPtr Parser::parseFunctionDecl() {
    Token startTok = peek(); // capture position before parsing return type
    std::string returnType = parseType();
    Token nameTok = peek();
    std::string name = consume(TokenType::IDENT, "Expected function name").value;

    // Optional type parameters: int max<T>(T a, T b) { ... }
    std::vector<std::string> typeParams;
    if (match(TokenType::LT)) {
        do {
            typeParams.push_back(consume(TokenType::IDENT, "Expected type parameter").value);
        } while (match(TokenType::COMMA));
        consume(TokenType::GT, "Expected '>'");
    }

    consume(TokenType::LPAREN, "Expected '('");
    auto params = parseParameterList();
    consume(TokenType::RPAREN, "Expected ')'");

    StmtPtr body = parseBlockStatement();

    auto decl = std::make_shared<FunctionDecl>(name, returnType, params, body);
    decl->typeParams = typeParams;
    decl->line = nameTok.line; decl->col = nameTok.column;
    return decl;
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
    if (check(TokenType::SWITCH)) {
        return parseSwitchStatement();
    }
    if (match(TokenType::RETURN)) {
        return parseReturnStatement();
    }
    if (match(TokenType::BREAK)) {
        return parseBreakStatement();
    }
    if (match(TokenType::CONTINUE)) {
        return parseContinueStatement();
    }
    // asm("string") or asm("string" : : "constraint"(expr), ... : "clobber", ...)
    if (check(TokenType::ASM)) {
        Token asmTok = advance();
        consume(TokenType::LPAREN, "Expected '(' after asm");
        std::string asmStr = consume(TokenType::STRING_LIT, "Expected asm string").value;

        std::vector<std::pair<std::string, ExprPtr>> inputs;
        std::vector<std::string> clobbers;

        if (match(TokenType::COLON)) {        // outputs (we skip — not yet supported)
            if (match(TokenType::COLON)) {    // inputs
                while (!check(TokenType::RPAREN) && !check(TokenType::COLON) && !is_at_end()) {
                    std::string constraint = consume(TokenType::STRING_LIT,
                        "Expected constraint string").value;
                    consume(TokenType::LPAREN, "Expected '(' after constraint");
                    ExprPtr expr = parseExpression();
                    consume(TokenType::RPAREN, "Expected ')'");
                    inputs.push_back({constraint, expr});
                    if (!match(TokenType::COMMA)) break;
                }
                if (match(TokenType::COLON)) { // clobbers
                    while (!check(TokenType::RPAREN) && !is_at_end()) {
                        clobbers.push_back(consume(TokenType::STRING_LIT,
                            "Expected clobber string").value);
                        if (!match(TokenType::COMMA)) break;
                    }
                }
            }
        }

        consume(TokenType::RPAREN, "Expected ')'");
        consume(TokenType::SEMICOLON, "Expected ';' after asm");
        auto stmt = std::make_shared<AsmStmt>(asmStr, inputs, clobbers);
        stmt->line = asmTok.line; stmt->col = asmTok.column;
        return stmt;
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
        // Try declaration (e.g. int i = 0;) then fall back to expression
        size_t savePos = current;
        try {
            DeclPtr decl = parseDeclaration();
            init = std::make_shared<BlockStmt>(std::vector<BlockItem>{decl});
        } catch (...) {
            current = savePos;
            init = parseExpressionStatement();
        }
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
    Token retTok = tokens[current - 1]; // the 'return' token
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        value = parseExpression();
    }
    consume(TokenType::SEMICOLON, "Expected ';'");
    auto stmt = std::make_shared<ReturnStmt>(value);
    stmt->line = retTok.line; stmt->col = retTok.column;
    return stmt;
}

StmtPtr Parser::parseBreakStatement() {
    consume(TokenType::SEMICOLON, "Expected ';'");
    return std::make_shared<BreakStmt>();
}

StmtPtr Parser::parseContinueStatement() {
    Token tok = tokens[current - 1]; // the 'continue' token
    consume(TokenType::SEMICOLON, "Expected ';'");
    auto stmt = std::make_shared<ContinueStmt>();
    stmt->line = tok.line; stmt->col = tok.column;
    return stmt;
}

StmtPtr Parser::parseExpressionStatement() {
    ExprPtr expr = parseExpression();
    consume(TokenType::SEMICOLON, "Expected ';'");
    return std::make_shared<ExprStmt>(expr);
}

// ============================================================================
// Expressions (Precedence Climbing)
// ============================================================================

StmtPtr Parser::parseSwitchStatement() {
    consume(TokenType::SWITCH, "Expected 'switch'");
    consume(TokenType::LPAREN, "Expected '('");
    ExprPtr subject = parseExpression();
    consume(TokenType::RPAREN, "Expected ')'");
    consume(TokenType::LBRACE, "Expected '{'");

    std::vector<SwitchStmt::Case> cases;
    while (!check(TokenType::RBRACE) && !is_at_end()) {
        SwitchStmt::Case c;
        if (match(TokenType::CASE)) {
            c.value = parseExpression();
            consume(TokenType::COLON, "Expected ':' after case value");
        } else if (match(TokenType::DEFAULT)) {
            consume(TokenType::COLON, "Expected ':' after default");
            c.value = nullptr;
        } else {
            break;
        }
        // Collect statements until the next case/default/}
        while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) &&
               !check(TokenType::RBRACE) && !is_at_end()) {
            c.stmts.push_back(parseStatement());
        }
        cases.push_back(std::move(c));
    }
    consume(TokenType::RBRACE, "Expected '}'");
    return std::make_shared<SwitchStmt>(subject, std::move(cases));
}

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

    // Compound assignments: desugar x += y  →  x = x + y
    static const std::unordered_map<TokenType, std::string> compound = {
        {TokenType::PLUS_EQ,    "+"}, {TokenType::MINUS_EQ,   "-"},
        {TokenType::STAR_EQ,    "*"}, {TokenType::SLASH_EQ,   "/"},
        {TokenType::PERCENT_EQ, "%"},
    };
    for (auto& [tt, op] : compound) {
        if (match(tt)) {
            Token opTok = tokens[current - 1];
            ExprPtr rhs  = parseAssignment();
            auto binOp   = withPos(std::make_shared<BinaryExpr>(expr, op,  rhs),  opTok);
            return withPos(std::make_shared<BinaryExpr>(expr, "=", binOp), opTok);
        }
    }

    if (match(TokenType::EQ)) {
        Token opTok = tokens[current - 1];
        ExprPtr value = parseAssignment();
        return withPos(std::make_shared<BinaryExpr>(expr, "=", value), opTok);
    }

    return expr;
}

ExprPtr Parser::parseLogicalOr() {
    ExprPtr expr = parseLogicalAnd();

    while (match(TokenType::OR)) {
        Token opTok = tokens[current - 1];
        ExprPtr right = parseLogicalAnd();
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, right), opTok);
    }

    return expr;
}

ExprPtr Parser::parseLogicalAnd() {
    ExprPtr expr = parseBitwiseOr();

    while (match(TokenType::AND)) {
        Token opTok = tokens[current - 1];
        ExprPtr right = parseBitwiseOr();
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, right), opTok);
    }

    return expr;
}

ExprPtr Parser::parseBitwiseOr() {
    ExprPtr expr = parseBitwiseXor();
    while (match(TokenType::PIPE)) {
        Token opTok = tokens[current - 1];
        expr = withPos(std::make_shared<BinaryExpr>(expr, "|", parseBitwiseXor()), opTok);
    }
    return expr;
}

ExprPtr Parser::parseBitwiseXor() {
    ExprPtr expr = parseBitwiseAnd();
    while (match(TokenType::CARET)) {
        Token opTok = tokens[current - 1];
        expr = withPos(std::make_shared<BinaryExpr>(expr, "^", parseBitwiseAnd()), opTok);
    }
    return expr;
}

ExprPtr Parser::parseBitwiseAnd() {
    ExprPtr expr = parseEquality();
    while (match(TokenType::AMPERSAND)) {
        Token opTok = tokens[current - 1];
        expr = withPos(std::make_shared<BinaryExpr>(expr, "&", parseEquality()), opTok);
    }
    return expr;
}

ExprPtr Parser::parseEquality() {
    ExprPtr expr = parseComparison();

    while (match({TokenType::EQEQ, TokenType::NE})) {
        Token opTok = tokens[current - 1];
        ExprPtr right = parseComparison();
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, right), opTok);
    }

    return expr;
}

ExprPtr Parser::parseShift() {
    ExprPtr expr = parseAddition();
    while (match({TokenType::LSHIFT, TokenType::RSHIFT})) {
        Token opTok = tokens[current - 1];
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, parseAddition()), opTok);
    }
    return expr;
}

ExprPtr Parser::parseComparison() {
    ExprPtr expr = parseShift();

    while (match({TokenType::LT, TokenType::GT, TokenType::LE, TokenType::GE})) {
        Token opTok = tokens[current - 1];
        ExprPtr right = parseShift();
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, right), opTok);
    }

    return expr;
}

ExprPtr Parser::parseAddition() {
    ExprPtr expr = parseMultiplication();

    while (match({TokenType::PLUS, TokenType::MINUS})) {
        Token opTok = tokens[current - 1];
        ExprPtr right = parseMultiplication();
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, right), opTok);
    }

    return expr;
}

ExprPtr Parser::parseMultiplication() {
    ExprPtr expr = parseUnary();

    while (match({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
        Token opTok = tokens[current - 1];
        ExprPtr right = parseUnary();
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, right), opTok);
    }

    return expr;
}

ExprPtr Parser::parseUnary() {
    // Fold -N and -N.N into a negative literal directly (avoids UnaryExpr for constants)
    if (check(TokenType::MINUS)) {
        TokenType next = peek_ahead(1).type;
        if (next == TokenType::INT_LIT || next == TokenType::FLOAT_LIT) {
            Token minusTok = advance(); // consume '-'
            Token numTok   = advance(); // consume number
            if (numTok.type == TokenType::INT_LIT)
                return withPos(std::make_shared<LiteralExpr>(
                    LiteralExpr::Kind::INT, "-" + numTok.value), minusTok);
            else
                return withPos(std::make_shared<LiteralExpr>(
                    LiteralExpr::Kind::FLOAT, "-" + numTok.value), minusTok);
        }
    }

    if (match({TokenType::NOT, TokenType::MINUS, TokenType::PLUS, TokenType::AMPERSAND, TokenType::STAR, TokenType::TILDE})) {
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
        // Template function call: ident<TypeArg, ...>(args)
        if (auto* ident = dynamic_cast<IdentExpr*>(expr.get())) {
            if (check(TokenType::LT)) {
                size_t savePos = current;
                try {
                    advance(); // consume <
                    std::vector<std::string> typeArgs;
                    do { typeArgs.push_back(parseType()); } while (match(TokenType::COMMA));
                    if (match(TokenType::GT) && match(TokenType::LPAREN)) {
                        std::vector<ExprPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(parseExpression()); } while (match(TokenType::COMMA));
                        }
                        consume(TokenType::RPAREN, "Expected ')'");
                        expr = std::make_shared<TemplateCallExpr>(ident->name, typeArgs, std::move(args));
                        continue;
                    }
                } catch (...) {}
                current = savePos;
            }
        }
        if (match(TokenType::LPAREN)) {
            Token callTok = tokens[current - 1];
            std::vector<ExprPtr> args;
            if (!check(TokenType::RPAREN)) {
                do { args.push_back(parseExpression()); } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN, "Expected ')'");
            expr = withPos(std::make_shared<CallExpr>(expr, args), callTok);
        } else if (match(TokenType::LBRACKET)) {
            Token idxTok = tokens[current - 1];
            ExprPtr index = parseExpression();
            consume(TokenType::RBRACKET, "Expected ']'");
            expr = withPos(std::make_shared<IndexExpr>(expr, index), idxTok);
        } else if (match(TokenType::DOT)) {
            Token dotTok = tokens[current - 1];
            std::string member = consume(TokenType::IDENT, "Expected member name").value;
            expr = withPos(std::make_shared<MemberExpr>(expr, member), dotTok);
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parsePrimary() {
    Token tok = peek();

    if (match(TokenType::TRUE)) {
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::BOOL, "true"), tok);
    }
    if (match(TokenType::FALSE)) {
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::BOOL, "false"), tok);
    }
    if (match(TokenType::NULL_KW)) {
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::NULL_VAL, "null"), tok);
    }
    if (match(TokenType::INT_LIT)) {
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::INT, tok.value), tok);
    }
    if (match(TokenType::FLOAT_LIT)) {
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::FLOAT, tok.value), tok);
    }
    if (match(TokenType::STRING_LIT)) {
        // Adjacent string literal concatenation: "abc" "def" → "abcdef"
        std::string combined = tok.value;
        while (check(TokenType::STRING_LIT)) {
            combined += peek().value;
            advance();
        }
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::STRING, combined), tok);
    }
    if (match(TokenType::CHAR_LIT)) {
        return withPos(std::make_shared<LiteralExpr>(LiteralExpr::Kind::CHAR, tok.value), tok);
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

    // Lambda: int(int a, int b) { return a + b; }
    // Detected when a type keyword is followed by '(' and the content looks like params + '{'
    {
        bool isTypeKw = (tok.type == TokenType::INT    || tok.type == TokenType::FLOAT  ||
                         tok.type == TokenType::DOUBLE  || tok.type == TokenType::BOOL   ||
                         tok.type == TokenType::CHAR    || tok.type == TokenType::STRING  ||
                         tok.type == TokenType::VOID    || tok.type == TokenType::UINT   ||
                         tok.type == TokenType::INT8    || tok.type == TokenType::INT16  ||
                         tok.type == TokenType::INT32   || tok.type == TokenType::INT64  ||
                         tok.type == TokenType::UINT8   || tok.type == TokenType::UINT16 ||
                         tok.type == TokenType::UINT32  || tok.type == TokenType::UINT64);
        if (isTypeKw && peek_ahead(1).type == TokenType::LPAREN) {
            // Disambiguate from a cast-like usage: try to parse as lambda, backtrack on failure
            size_t savePos = current;
            try {
                std::string retType = parseType();           // consume return type
                consume(TokenType::LPAREN, "");
                auto params = parseParameterList();
                consume(TokenType::RPAREN, "");
                if (check(TokenType::LBRACE)) {              // confirmed: it's a lambda
                    StmtPtr body = parseBlockStatement();
                    auto lambda = std::make_shared<LambdaExpr>(params, retType, body);
                    lambda->line = tok.line; lambda->col = tok.column;
                    return lambda;
                }
            } catch (...) {}
            current = savePos; // not a lambda, fall through
        }
    }

    if (match(TokenType::IDENT)) {
        if (check(TokenType::LBRACE)) {
            return withPos(parseStructInit(tok.value), tok);
        }
        return withPos(std::make_shared<IdentExpr>(tok.value), tok);
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
