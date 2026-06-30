#include "parser.h"
#include "../lexer/lexer.h"
#include "../ast/type_qual.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include "parser_internal.h"

// Parser — statement parsing (blocks, control flow, match/switch, returns).
// Part of the parser.cpp split; see parser.h.

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
    if (check(TokenType::MATCH)) {
        return parseMatchStatement();
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
    // throw expr;
    if (match(TokenType::THROW)) {
        ExprPtr val = parseExpression();
        consume(TokenType::SEMICOLON, "Expected ';' after throw");
        auto s = std::make_shared<ThrowStmt>(val);
        s->line = tokens[current-1].line; s->col = tokens[current-1].column;
        return s;
    }

    // try { } catch (Type name) { } finally { }
    if (check(TokenType::TRY)) {
        Token tryTok = advance();
        StmtPtr body = parseBlockStatement();

        std::vector<TryStmt::CatchClause> catches;
        while (check(TokenType::CATCH)) {
            advance(); // consume 'catch'
            consume(TokenType::LPAREN, "Expected '(' after catch");
            std::string ctype = parseType();
            std::string cname = consume(TokenType::IDENT, "Expected variable name in catch").value;
            consume(TokenType::RPAREN, "Expected ')'");
            StmtPtr cbody = parseBlockStatement();
            catches.push_back({ctype, cname, cbody});
        }

        StmtPtr fin = nullptr;
        if (check(TokenType::FINALLY)) {
            advance();
            fin = parseBlockStatement();
        }

        auto s = std::make_shared<TryStmt>(body, catches, fin);
        s->line = tryTok.line; s->col = tryTok.column;
        return s;
    }

    // thread_join(tid);
    if (check(TokenType::THREAD_JOIN)) {
        Token jTok = advance();
        consume(TokenType::LPAREN, "Expected '(' after thread_join");
        ExprPtr tid = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        consume(TokenType::SEMICOLON, "Expected ';'");
        auto stmt = std::make_shared<ThreadJoinStmt>(tid);
        stmt->line = jTok.line; stmt->col = jTok.column;
        return stmt;
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
        if (check(TokenType::CONST) ||
            check(TokenType::VOLATILE) ||
            check(TokenType::LET) ||
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
                // Only an identifier or a leading '*' is ambiguous (it can also
                // begin an expression statement); fall back for those. A leading
                // type keyword / const / volatile / let is unambiguously a
                // declaration, so its error is real — surface it instead of
                // masking it with a misleading expression-parse error (keeps the
                // "expected a name, found keyword 'fn'" diagnostic for `int fn;`).
                TokenType startTok = tokens[savePos].type;
                if (startTok != TokenType::IDENT && startTok != TokenType::STAR) throw;
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

    // for (x in iterable) — element-wise iteration
    if (check(TokenType::IDENT) && peek_ahead(1).type == TokenType::IN) {
        Token nameTok = advance();                // x
        consume(TokenType::IN, "Expected 'in'");
        ExprPtr first = parseExpression();
        // for (i in A..B) — half-open numeric range [A, B). Desugar at parse time
        // into a counted `for (int i = A; i < B; i = i + 1)`, so it reuses all the
        // for-loop machinery (codegen, the async transform, break/continue).
        if (match(TokenType::RANGE)) {
            ExprPtr end = parseExpression();
            consume(TokenType::RPAREN, "Expected ')'");
            StmtPtr body = parseStatement();
            auto iv = [&]() { return withPos(std::make_shared<IdentExpr>(nameTok.value), nameTok); };
            auto idecl = std::make_shared<VarDecl>(nameTok.value, "int", first);
            idecl->line = nameTok.line; idecl->col = nameTok.column;
            StmtPtr init = std::make_shared<BlockStmt>(std::vector<BlockItem>{ DeclPtr(idecl) });
            ExprPtr cond = std::make_shared<BinaryExpr>(iv(), "<", end);
            ExprPtr one  = std::make_shared<LiteralExpr>(LiteralExpr::Kind::INT, "1");
            ExprPtr step = std::make_shared<BinaryExpr>(iv(), "=",
                               std::make_shared<BinaryExpr>(iv(), "+", one));
            auto fs = std::make_shared<ForStmt>(init, cond, step, body);
            fs->line = nameTok.line; fs->col = nameTok.column;
            return fs;
        }
        consume(TokenType::RPAREN, "Expected ')'");
        StmtPtr body = parseStatement();
        auto fin = std::make_shared<ForInStmt>(nameTok.value, first, body);
        fin->line = nameTok.line; fin->col = nameTok.column;
        return fin;
    }

    StmtPtr init = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        // Try declaration (e.g. int i = 0;) then fall back to expression
        size_t savePos = current;
        try {
            DeclPtr decl = parseDeclaration();
            init = std::make_shared<BlockStmt>(std::vector<BlockItem>{decl});
        } catch (...) {
            // Unambiguous decl starts (type keyword/const/volatile/let) surface
            // their real error; only IDENT/'*' fall back to an expression.
            TokenType startTok = tokens[savePos].type;
            if (startTok != TokenType::IDENT && startTok != TokenType::STAR) throw;
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

// match subject { Variant(b0, b1) -> stmt   Other -> stmt   _ -> stmt }
StmtPtr Parser::parseMatchStatement() {
    Token kw = consume(TokenType::MATCH, "Expected 'match'");
    // Parse the subject without treating a trailing `Name { ... }` as a struct
    // literal — the `{` opens the match body (cf. Rust's match/if rule). Wrap the
    // subject in parens if a struct literal is genuinely needed there.
    // RAII so the flag is restored even if parseExpression throws — otherwise a
    // parse error in the subject would leave struct literals disabled for the
    // rest of the file. Restores at the end of this block (before the arm bodies,
    // which legitimately contain struct literals).
    ExprPtr subject;
    {
        struct NslGuard { bool& f; bool saved; NslGuard(bool& x) : f(x), saved(x) { f = true; } ~NslGuard() { f = saved; } } guard(noStructLiteral);
        subject = parseExpression();
    }
    consume(TokenType::LBRACE, "Expected '{' after match subject");
    std::vector<MatchStmt::Arm> arms;
    while (!check(TokenType::RBRACE) && !is_at_end()) {
        MatchStmt::Arm arm;
        if (check(TokenType::IDENT) && peek().value == "_") {
            advance();                              // `_` default arm
        } else {
            arm.variant = consume(TokenType::IDENT, "Expected variant name or '_'").value;
            if (match(TokenType::LPAREN)) {         // payload bindings
                if (!check(TokenType::RPAREN)) {
                    do {
                        arm.bindings.push_back(consume(TokenType::IDENT, "Expected binding name").value);
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RPAREN, "Expected ')' after match bindings");
            }
        }
        consume(TokenType::ARROW, "Expected '->' after match pattern");
        arm.body = parseStatement();
        arms.push_back(std::move(arm));
    }
    consume(TokenType::RBRACE, "Expected '}' to close match");
    auto ms = std::make_shared<MatchStmt>(subject, std::move(arms));
    ms->line = kw.line; ms->col = kw.column;
    return ms;
}

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
