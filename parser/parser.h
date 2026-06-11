#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>
#include "../lexer/lexer.h"
#include "../ast/ast.h"

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);

    // Parse the token stream. Returns nullptr (and prints diagnostics to stderr)
    // if any declaration failed to parse.
    std::shared_ptr<Program> parse();

    // Set true when any declaration/import failed to parse. parse() returns
    // nullptr in that case so callers fail loudly instead of silently dropping code.
    bool hadError = false;

    // Directory of the current source file — used to resolve relative imports
    std::string basedir;
    // Root of the Eskiu installation — used to resolve <stdlib> imports
    // Set from $ESKIU_ROOT env var or dirname(argv[0])/../lib/eskiu
    std::string stdlibPath;
    // Shared set of already-imported canonical paths (prevents re-importing)
    std::set<std::string>* importedFiles = nullptr;
    // Shared preprocessor macro table — lets #defines propagate into imports
    std::map<std::string, Macro>* macros = nullptr;
    // Shared across all sub-parsers (like importedFiles): type names declared in
    // ANY file, so a cast to a type stays a cast even when that type's defining
    // import was deduplicated via a different path. Without sharing, a file that
    // imports an already-imported module never learned its type names and
    // misparsed `(Type*)x` casts (e.g. `(Future<T>*)0` after `import <future>`).
    std::set<std::string>* sharedTypeNames = nullptr;

private:
    // Backing store for sharedTypeNames in the root parser; sub-parsers point
    // sharedTypeNames at the root's. Names of declared types (structs, enums,
    // unions, aliases) — lets the cast parser recognize (TypeName)expr.
    std::set<std::string> declaredTypeNames;
    // Consume a template-closing '>'. Handles a lexed '>>' (right-shift) at the
    // close of nested templates (List<List<int>>) by splitting it: the inner
    // close turns '>>' into a single '>' left for the outer close.
    void consumeTemplateClose(const char* ctx);
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
    DeclPtr parseIntrinsicDecl();

    // #pragma pack state: structs declared while currentPack==1 are packed.
    int currentPack = 0;
    std::vector<int> packStack;
    void applyPragma(const std::string& text);

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
    std::vector<std::pair<std::string, std::string>> parseParameterList(
        std::vector<bool>* escaping = nullptr);
};
