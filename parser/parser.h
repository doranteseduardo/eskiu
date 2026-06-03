#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../lexer/lexer.h"
#include "../ast/ast.h"

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    std::shared_ptr<Program> parse();

private:
    std::vector<Token> tokens;
    size_t current;

    // Helper methods
    Token peek() const;
    Token peek_ahead(int n = 1) const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool match(const std::vector<TokenType>& types);
    Token consume(TokenType type, const std::string& message);
    bool is_at_end() const;

    // Parsing methods
    std::vector<DeclPtr> parseProgram();

    DeclPtr parseDeclaration();
    DeclPtr parseFunctionDecl();
    DeclPtr parseVarDecl();
    DeclPtr parseStructDecl();
    DeclPtr parseExternDecl();

    StmtPtr parseStatement();
    StmtPtr parseBlockStatement();
    StmtPtr parseIfStatement();
    StmtPtr parseForStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseReturnStatement();
    StmtPtr parseBreakStatement();
    StmtPtr parseContinueStatement();
    StmtPtr parseSwitchStatement();
    StmtPtr parseExpressionStatement();

    ExprPtr parseStructInit(const std::string& structName);
    ExprPtr parseExpression();
    ExprPtr parseBitwiseOr();
    ExprPtr parseBitwiseXor();
    ExprPtr parseBitwiseAnd();
    ExprPtr parseShift();
    ExprPtr parseAssignment();
    ExprPtr parseLogicalOr();
    ExprPtr parseLogicalAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseAddition();
    ExprPtr parseMultiplication();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();

    std::string parseType();
    std::vector<std::pair<std::string, std::string>> parseParameterList();
};
