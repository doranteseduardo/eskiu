#include "parser.h"
#include "../lexer/lexer.h"
#include "../ast/type_qual.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
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

ExprPtr Parser::parseAssignment() {
    ExprPtr expr = parseLogicalOr();

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
        bool isTypeKeyword = (inner == TokenType::INT    || inner == TokenType::FLOAT  ||
                              inner == TokenType::DOUBLE  || inner == TokenType::BOOL   ||
                              inner == TokenType::CHAR    || inner == TokenType::STRING  ||
                              inner == TokenType::VOID    || inner == TokenType::STAR    ||
                              inner == TokenType::INT8    || inner == TokenType::INT16   ||
                              inner == TokenType::INT32   || inner == TokenType::INT64   ||
                              inner == TokenType::UINT    || inner == TokenType::UINT8   ||
                              inner == TokenType::UINT16  || inner == TokenType::UINT32  ||
                              inner == TokenType::UINT64);
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
            consume(TokenType::RBRACKET, "Expected ']'");
            expr = withPos(std::make_shared<IndexExpr>(expr, index), idxTok);
        } else if (match(TokenType::DOT)) {
            Token dotTok = tokens[current - 1];
            std::string member = consume(TokenType::IDENT, "Expected member name").value;
            expr = withPos(std::make_shared<MemberExpr>(expr, member), dotTok);
        } else if (match(TokenType::QUESTION)) {
            Token qTok = tokens[current - 1];
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
