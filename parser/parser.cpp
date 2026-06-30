#include "parser.h"
#include "../lexer/lexer.h"
#include "../ast/type_qual.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include "parser_internal.h"

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
    // Friendlier diagnostic: when a name is expected but the next token is a
    // reserved keyword (fn/in/match/type names/...), say so at the cause instead
    // of letting it surface far downstream ("Expected ';'", "Expected expression").
    if (type == TokenType::IDENT) {
        TokenType t = peek().type;
        if (t >= TokenType::LET && t <= TokenType::UINT64) {
            const std::string& kw = peek().value;
            throw std::runtime_error(
                "expected a name, found keyword '" +
                (kw.empty() ? tokenTypeToString(t) : kw) + "'");
        }
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
    // The lexer appends a sentinel EOF token; treat reaching it as end-of-input
    // so the declaration loop does not try to parse EOF as a declaration.
    return tokens[current].type == TokenType::EOF_TOKEN;
}

// ============================================================================
// Type Parsing
// ============================================================================

void Parser::consumeTemplateClose(const char* ctx) {
    if (check(TokenType::GT)) { advance(); return; }
    // A lexed ">>" (right-shift) closes two template levels at once. Split it
    // permanently into "> >" by rewriting this token to ">" and inserting a
    // second ">" after it, then consume the first. Insertion (vs. a destructive
    // rewrite) keeps the stream correct across the parser's backtracking — a
    // saved position is always before `current`, so it is unaffected.
    if (check(TokenType::RSHIFT)) {
        tokens[current].type  = TokenType::GT;
        tokens[current].value = ">";
        tokens.insert(tokens.begin() + current + 1, tokens[current]);
        advance();
        return;
    }
    consume(TokenType::GT, ctx);   // not a close — emit the standard error
}

std::string Parser::parseType() {
    std::string type;

    // Optional leading `const` qualifies the base/pointee: `const int*` is a
    // pointer to const int. Re-attached as a `const ` prefix after the type is
    // assembled. (A `const` before a `let`/decl binding is handled by the
    // declaration parser, not here.)
    bool baseIsConst = match(TokenType::CONST);

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
            if (!first) { consume(TokenType::COMMA, "Expected ',' between fn parameter types"); type += ","; }
            first = false;
            type += parseType();
        }
        consume(TokenType::RPAREN, "Expected ')' in fn type");
        type += ")->";
        consume(TokenType::ARROW, "Expected '->' in fn type");
        type += parseType();
        // No trailing pointer handling needed for fn types — return early
        for (int lp = 0; lp < leading_pointers; ++lp) type = "*" + type;
        if (baseIsConst) type = "const " + type;
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
            consumeTemplateClose("Expected '>' after template arguments");
            type += ">";
        }
    } else {
        throw std::runtime_error("Expected type, got " + tokenTypeToString(peek().type));
    }

    // Handle trailing pointers (C-style: i32*). A `const` right after a star
    // makes that pointer level const (`int* const`), encoded as `*const`.
    while (match(TokenType::STAR)) {
        type += "*";
        if (match(TokenType::CONST)) type += "const";
    }

    // Add leading pointers at the beginning
    for (int i = 0; i < leading_pointers; i++) {
        type = "*" + type;
    }

    // Re-attach the base/pointee const as a leading qualifier.
    if (baseIsConst) type = "const " + type;

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

std::vector<std::pair<std::string, std::string>> Parser::parseParameterList(
        std::vector<bool>* escaping) {
    std::vector<std::pair<std::string, std::string>> params;

    if (!check(TokenType::RPAREN)) {
        do {
            // Handle variadic parameters (...)
            if (match(TokenType::ELLIPSIS)) {
                params.push_back({"...", "..."});
                if (escaping) escaping->push_back(false);
                break;
            }

            // Optional `escaping` qualifier: the parameter retains the closure
            // beyond the call (e.g. stores it), so closures passed here need a
            // heap environment.
            bool isEscaping = match(TokenType::ESCAPING);
            std::string type = parseType();
            std::string name = consume(TokenType::IDENT, "Expected parameter name").value;
            params.push_back({type, name});
            if (escaping) escaping->push_back(isEscaping);
        } while (match(TokenType::COMMA));
    }

    return params;
}

// ============================================================================
// Declarations
// ============================================================================

std::shared_ptr<Program> Parser::parse() {
    auto decls = parseProgram();
    if (hadError) return nullptr;
    return std::make_shared<Program>(decls);
}

std::vector<DeclPtr> Parser::parseProgram() {
    std::vector<DeclPtr> declarations;

    // Owned import set if caller didn't provide one
    std::set<std::string> ownedSet;
    if (!importedFiles) importedFiles = &ownedSet;
    // The root parser owns the shared type-name set; sub-parsers point at it.
    if (!sharedTypeNames) sharedTypeNames = &declaredTypeNames;

    while (!is_at_end()) {
        // Compiler directive (e.g. #pragma pack) — updates parser state, emits no decl.
        if (check(TokenType::PRAGMA)) {
            applyPragma(advance().value);
            continue;
        }
        // Handle import "path/to/file.esk"  or  import <stdlib_name>
        if (match(TokenType::IMPORT)) {
            try {
                std::string path;
                bool isStdlib = false;

                if (check(TokenType::STRING_LIT)) {
                    // import "relative/path.esk"
                    path = advance().value;
                } else if (check(TokenType::LT)) {
                    // import <name>  →  resolved against stdlibPath
                    advance(); // consume <
                    std::string name;
                    while (!check(TokenType::GT) && !is_at_end())
                        name += advance().value;
                    consume(TokenType::GT, "Expected '>' after stdlib name");
                    // Allow bare name or name with path separator
                    if (name.find('/') == std::string::npos)
                        name = "stdlib/" + name + ".esk";
                    path = name;
                    isStdlib = true;
                } else {
                    throw std::runtime_error("Expected filename or <name> after import");
                }
                consume(TokenType::SEMICOLON, "Expected ';' after import");

                // Resolve full path
                std::string fullPath;
                if (isStdlib && !stdlibPath.empty()) {
                    // stdlib path: ESKIU_ROOT/stdlib/name.esk
                    fullPath = stdlibPath + "/" + path;
                } else if (!basedir.empty() && path[0] != '/') {
                    fullPath = basedir + "/" + path;
                } else {
                    fullPath = path;
                }

                if (!importedFiles->count(fullPath)) {
                    importedFiles->insert(fullPath);

                    std::ifstream file(fullPath);
                    if (!file.is_open())
                        throw std::runtime_error("Cannot open import: '" + fullPath + "'");
                    std::ostringstream ss;
                    ss << file.rdbuf();
                    std::string src = ss.str();

                    Lexer lexer(src, macros, fullPath);  // share macros; fullPath = __FILE__
                    std::vector<Token> itoks;
                    Token t = lexer.next_token();
                    while (t.type != TokenType::EOF_TOKEN) { itoks.push_back(t); t = lexer.next_token(); }
                    itoks.push_back(t);
                    if (lexer.hadError) hadError = true;  // propagate lexical errors from the import

                    Parser sub(itoks);
                    size_t slash = fullPath.rfind('/');
                    sub.basedir       = (slash != std::string::npos) ? fullPath.substr(0, slash) : ".";
                    sub.stdlibPath    = stdlibPath;
                    sub.importedFiles = importedFiles;
                    sub.macros        = macros;
                    sub.sharedTypeNames = sharedTypeNames;   // one set for all parsers

                    auto subProg = sub.parse();
                    if (!subProg) {
                        hadError = true;
                    } else {
                        declarations.insert(declarations.end(),
                            subProg->declarations.begin(), subProg->declarations.end());
                        // Type names are recorded directly into the shared set as
                        // each file is parsed, so a cast to an imported type —
                        // `(FutureHdr*)p` — parses correctly here regardless of
                        // which import path defined it (no post-merge needed).
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << std::endl;
                hadError = true;
                while (!is_at_end() && !check(TokenType::SEMICOLON)) advance();
                if (check(TokenType::SEMICOLON)) advance();
            }
            continue;
        }

        try {
            DeclPtr decl = parseDeclaration();
            if (decl) declarations.push_back(decl);
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << std::endl;
            hadError = true;
            while (!is_at_end() && !check(TokenType::SEMICOLON)) advance();
            if (check(TokenType::SEMICOLON)) advance();
        }
    }

    return declarations;
}
