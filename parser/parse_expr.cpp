#include "parser.h"
#include "../lexer/lexer.h"
#include <stdexcept>
#include "parser_internal.h"

// Parser — expression parsing: the precedence ladder, unary/postfix/primary,
// and struct-init literals.
// Part of the parser.cpp split; see parser.h.

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

// True if the `?` at `current` opens a ternary — a matching `:` follows at the same
// bracket nesting before a statement/argument terminator — rather than the postfix
// Result-propagation operator (`expr?`). Propagation `?` is never followed by a
// same-level `:`, so the colon reliably signals a ternary.
bool Parser::ternaryColonAhead() const {
    int depth = 0;
    for (size_t i = current + 1; i < tokens.size(); ++i) {
        TokenType t = tokens[i].type;
        if (t == TokenType::LPAREN || t == TokenType::LBRACKET || t == TokenType::LBRACE)
            depth++;
        else if (t == TokenType::RPAREN || t == TokenType::RBRACKET || t == TokenType::RBRACE) {
            if (depth == 0) return false;   // closed the enclosing group before any ':'
            depth--;
        } else if (depth == 0) {
            if (t == TokenType::COLON) return true;
            if (t == TokenType::SEMICOLON || t == TokenType::COMMA ||
                t == TokenType::EOF_TOKEN) return false;
        }
    }
    return false;
}

ExprPtr Parser::parseTernary() {
    ExprPtr cond = parseLogicalOr();
    if (check(TokenType::QUESTION) && ternaryColonAhead()) {
        Token qTok = advance();                       // consume '?'
        ExprPtr thenE = parseAssignment();            // then-arm: a full expression
        consume(TokenType::COLON, "Expected ':' in ternary expression");
        ExprPtr elseE = parseTernary();               // else-arm: right-associative
        return withPos(std::make_shared<TernaryExpr>(cond, thenE, elseE), qTok);
    }
    return cond;
}

ExprPtr Parser::parseAssignment() {
    ExprPtr expr = parseTernary();

    // Compound assignments: desugar x += y  →  x = x + y
    static const std::unordered_map<TokenType, std::string> compound = {
        {TokenType::PLUS_EQ,    "+"},  {TokenType::MINUS_EQ,   "-"},
        {TokenType::STAR_EQ,    "*"},  {TokenType::SLASH_EQ,   "/"},
        {TokenType::PERCENT_EQ, "%"},
        {TokenType::AMP_EQ,     "&"},  {TokenType::PIPE_EQ,    "|"},
        {TokenType::CARET_EQ,   "^"},  {TokenType::LSHIFT_EQ,  "<<"},
        {TokenType::RSHIFT_EQ,  ">>"},
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

ExprPtr Parser::parseBinaryLevel(ExprPtr (Parser::*next)(), const std::vector<TokenType>& ops) {
    ExprPtr expr = (this->*next)();
    while (match(ops)) {
        Token opTok = tokens[current - 1];
        expr = withPos(std::make_shared<BinaryExpr>(expr, opTok.value, (this->*next)()), opTok);
    }
    return expr;
}

// The precedence ladder, lowest-binding first: each rung folds left-associatively
// over its operators, then defers to the next-tighter rung.
ExprPtr Parser::parseLogicalOr()      { return parseBinaryLevel(&Parser::parseLogicalAnd,     {TokenType::OR}); }
ExprPtr Parser::parseLogicalAnd()     { return parseBinaryLevel(&Parser::parseBitwiseOr,      {TokenType::AND}); }
ExprPtr Parser::parseBitwiseOr()      { return parseBinaryLevel(&Parser::parseBitwiseXor,     {TokenType::PIPE}); }
ExprPtr Parser::parseBitwiseXor()     { return parseBinaryLevel(&Parser::parseBitwiseAnd,     {TokenType::CARET}); }
ExprPtr Parser::parseBitwiseAnd()     { return parseBinaryLevel(&Parser::parseEquality,       {TokenType::AMPERSAND}); }
ExprPtr Parser::parseEquality()       { return parseBinaryLevel(&Parser::parseComparison,     {TokenType::EQEQ, TokenType::NE}); }
ExprPtr Parser::parseShift()          { return parseBinaryLevel(&Parser::parseAddition,       {TokenType::LSHIFT, TokenType::RSHIFT}); }
ExprPtr Parser::parseComparison()     { return parseBinaryLevel(&Parser::parseShift,          {TokenType::LT, TokenType::GT, TokenType::LE, TokenType::GE}); }
ExprPtr Parser::parseAddition()       { return parseBinaryLevel(&Parser::parseMultiplication, {TokenType::PLUS, TokenType::MINUS}); }
ExprPtr Parser::parseMultiplication() { return parseBinaryLevel(&Parser::parseUnary,          {TokenType::STAR, TokenType::SLASH, TokenType::PERCENT}); }

ExprPtr Parser::parseUnary() {
    // await E — prefix operator; binds like a unary operator.
    if (check(TokenType::AWAIT)) {
        Token awaitTok = advance();
        ExprPtr operand = parseUnary();
        return withPos(std::make_shared<AwaitExpr>(operand), awaitTok);
    }
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

    // Prefix ++x / --x
    if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
        bool dec = tokens[current - 1].type == TokenType::MINUS_MINUS;
        ExprPtr operand = parseUnary();
        return std::make_shared<IncDecExpr>(operand, dec, /*prefix=*/true);
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
        bool isTypeKeyword = isPrimitiveTypeToken(inner) || inner == TokenType::STAR;
        // Also a cast when the inner token names a declared type — a struct,
        // enum, union, or alias — as `(Name)x`, `(Name*)x`, or `(Name<...>)x`.
        if (!isTypeKeyword && inner == TokenType::IDENT &&
            sharedTypeNames->count(peek_ahead(1).value)) {
            isTypeKeyword = true;
        }
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
                    if (check(TokenType::GT) || check(TokenType::RSHIFT)) {
                        consumeTemplateClose("Expected '>'");
                        if (match(TokenType::LPAREN)) {
                            // Template function call: Name<T,...>(args)
                            std::vector<ExprPtr> args;
                            if (!check(TokenType::RPAREN)) {
                                do { args.push_back(parseExpression()); } while (match(TokenType::COMMA));
                            }
                            consume(TokenType::RPAREN, "Expected ')'");
                            expr = std::make_shared<TemplateCallExpr>(ident->name, typeArgs, std::move(args));
                            continue;
                        }
                        if (check(TokenType::LBRACE)) {
                            // Template struct literal: Name<T,...> { ... }
                            std::string typeStr = ident->name + "<";
                            for (size_t i = 0; i < typeArgs.size(); ++i) {
                                if (i) typeStr += ",";
                                typeStr += typeArgs[i];
                            }
                            typeStr += ">";
                            expr = parseStructInit(typeStr);
                            continue;
                        }
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
            // `base[lo..hi]` is a slice expression (half-open); `base[i]` a plain index.
            ExprPtr highIndex = nullptr;
            if (match(TokenType::RANGE)) highIndex = parseExpression();
            consume(TokenType::RBRACKET, "Expected ']'");
            expr = withPos(std::make_shared<IndexExpr>(expr, index, highIndex), idxTok);
        } else if (match(TokenType::DOT)) {
            Token dotTok = tokens[current - 1];
            std::string member = consume(TokenType::IDENT, "Expected member name").value;
            expr = withPos(std::make_shared<MemberExpr>(expr, member), dotTok);
        } else if (check(TokenType::QUESTION) && !ternaryColonAhead()) {
            // Postfix Result-propagation `expr?` — but only when this `?` does not open
            // a ternary (no same-level `:` ahead); the ternary is handled lower down.
            Token qTok = advance();
            expr = withPos(std::make_shared<QuestionExpr>(expr), qTok);
        } else if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
            Token pTok = tokens[current - 1];
            bool dec = pTok.type == TokenType::MINUS_MINUS;
            expr = withPos(std::make_shared<IncDecExpr>(expr, dec, /*prefix=*/false), pTok);
        } else {
            break;
        }
    }

    return expr;
}

ExprPtr Parser::parsePrimary() {
    Token tok = peek();

    // Array literal `{ e0, e1, ... }` (untyped; target-typed at the declaration).
    if (check(TokenType::LBRACE)) {
        advance();  // '{'
        std::vector<ExprPtr> elems;
        if (!check(TokenType::RBRACE)) {
            elems.push_back(parseExpression());
            while (match(TokenType::COMMA)) {
                if (check(TokenType::RBRACE)) break;   // trailing comma
                elems.push_back(parseExpression());
            }
        }
        consume(TokenType::RBRACE, "Expected '}' after array literal");
        return withPos(std::make_shared<ArrayLitExpr>(std::move(elems)), tok);
    }

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
    // alloc_with(&allocator, T, N) — like alloc, but from an explicit allocator
    if (match(TokenType::ALLOC_WITH)) {
        consume(TokenType::LPAREN, "Expected '(' after alloc_with");
        ExprPtr allocator = parseExpression();
        consume(TokenType::COMMA, "Expected ',' after allocator in alloc_with");
        std::string elemType = parseType();
        consume(TokenType::COMMA, "Expected ',' after type in alloc_with");
        ExprPtr count = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        return std::make_shared<AllocWithExpr>(allocator, elemType, count);
    }


    // sizeof(T) -> int64
    if (match(TokenType::SIZEOF)) {
        consume(TokenType::LPAREN, "Expected '(' after sizeof");
        std::string typeName = parseType();
        consume(TokenType::RPAREN, "Expected ')'");
        return withPos(std::make_shared<SizeofExpr>(typeName), tok);
    }

    // free_closure(closureExpr) -> void — release an escaping closure's env.
    if (match(TokenType::FREE_CLOSURE)) {
        consume(TokenType::LPAREN, "Expected '(' after free_closure");
        ExprPtr c = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        return withPos(std::make_shared<FreeClosureExpr>(c), tok);
    }

    // thread_create(fn()->void worker) -> *void
    if (match(TokenType::THREAD_CREATE)) {
        consume(TokenType::LPAREN, "Expected '(' after thread_create");
        ExprPtr worker = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        return withPos(std::make_shared<ThreadCreateExpr>(worker), tok);
    }

    // Lambda: int(int a, int b) { return a + b; }
    // Detected when a type keyword is followed by '(' and the content looks like params + '{'
    {
        bool isTypeKw = isPrimitiveTypeToken(tok.type);
        if (isTypeKw && peek_ahead(1).type == TokenType::LPAREN) {
            // Disambiguate from a cast-like usage: try to parse as lambda, backtrack on failure
            size_t savePos = current;
            try {
                std::string retType = parseType();           // consume return type
                consume(TokenType::LPAREN, "");
                std::vector<bool> esc;
                auto params = parseParameterList(&esc);
                consume(TokenType::RPAREN, "");
                if (check(TokenType::LBRACE)) {              // confirmed: it's a lambda
                    StmtPtr body = parseBlockStatement();
                    auto lambda = std::make_shared<LambdaExpr>(params, retType, body);
                    lambda->line = tok.line; lambda->col = tok.column;
                    lambda->paramEscaping = esc;
                    return lambda;
                }
            } catch (...) {}
            current = savePos; // not a lambda, fall through
        }
    }

    if (match(TokenType::IDENT)) {
        if (check(TokenType::LBRACE) && !noStructLiteral) {
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
