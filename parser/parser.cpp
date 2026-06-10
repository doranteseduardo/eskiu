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

                    Lexer lexer(src, macros);  // share macros into the imported file
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

                    auto subProg = sub.parse();
                    if (!subProg) {
                        hadError = true;
                    } else {
                        declarations.insert(declarations.end(),
                            subProg->declarations.begin(), subProg->declarations.end());
                        // Imported type names must be visible here so that a cast
                        // to an imported type — e.g. `(FutureHdr*)p` — is parsed as
                        // a cast, not as `FutureHdr * p`. (sub already merged its
                        // own imports' names, so this is transitive.)
                        for (const auto& tn : sub.declaredTypeNames)
                            declaredTypeNames.insert(tn);
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

DeclPtr Parser::parseDeclaration() {
    try {
        if (match(TokenType::EXTERN)) {
            return parseExternDecl();
        }
        if (match(TokenType::INTRINSIC)) {
            return parseIntrinsicDecl();
        }
        if (match(TokenType::STRUCT)) {
            return parseStructDecl();
        }
        // `packed struct Foo { ... }` — explicit packed (pack(1)) layout.
        if (match(TokenType::PACKED)) {
            consume(TokenType::STRUCT, "Expected 'struct' after 'packed'");
            auto decl = parseStructDecl();
            if (auto* sd = dynamic_cast<StructDecl*>(decl.get())) sd->isPacked = true;
            return decl;
        }

        if (match(TokenType::UNION)) {
            std::string name = consume(TokenType::IDENT, "Expected union name").value;
            consume(TokenType::LBRACE, "Expected '{'");
            std::vector<StructDecl::Field> fields;
            while (!check(TokenType::RBRACE) && !is_at_end()) {
                std::string fieldType = parseType();
                std::string fieldName = consume(TokenType::IDENT,
                    "Expected field name").value;
                consume(TokenType::SEMICOLON, "Expected ';'");
                fields.push_back({fieldType, fieldName});
            }
            consume(TokenType::RBRACE, "Expected '}'");
            declaredTypeNames.insert(name);
            return std::make_shared<UnionDecl>(name, fields);
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

        // enum Color { Red, Green = 5, Blue }
        if (match(TokenType::ENUM)) {
            std::string name = consume(TokenType::IDENT, "Expected enum name").value;
            consume(TokenType::LBRACE, "Expected '{'");
            std::vector<std::pair<std::string, long long>> members;
            long long next = 0;
            while (!check(TokenType::RBRACE) && !is_at_end()) {
                std::string mname = consume(TokenType::IDENT,
                    "Expected enum member name").value;
                long long val = next;
                if (match(TokenType::EQ)) {
                    bool neg = match(TokenType::MINUS);
                    Token num = consume(TokenType::INT_LIT,
                        "Expected integer value for enum member");
                    val = std::stoll(num.value, nullptr, 0);
                    if (neg) val = -val;
                }
                members.push_back({mname, val});
                next = val + 1;
                if (!match(TokenType::COMMA)) break;
            }
            consume(TokenType::RBRACE, "Expected '}'");
            declaredTypeNames.insert(name);
            return std::make_shared<EnumDecl>(name, members);
        }

        // type Alias = UnderlyingType;  (contextual — 'type' stays a usable identifier)
        if (check(TokenType::IDENT) && peek().value == "type" &&
            peek_ahead(1).type == TokenType::IDENT &&
            peek_ahead(2).type == TokenType::EQ) {
            advance();                                  // 'type'
            std::string name = advance().value;         // alias name
            advance();                                  // '='
            std::string underlying = parseType();
            consume(TokenType::SEMICOLON, "Expected ';' after type alias");
            declaredTypeNames.insert(name);
            return std::make_shared<TypeAliasDecl>(name, underlying);
        }

        // Optional leading qualifiers, in either order: `volatile let`,
        // `let volatile`, `volatile T x`, `const let`, etc.
        bool leadingVol = match(TokenType::VOLATILE);
        bool isConst = match(TokenType::CONST);

        // Handle 'let' variable declarations. The qualifier comes first
        // (`volatile let x`, like `const int`), captured by leadingVol above.
        if (match(TokenType::LET)) {
            bool isVol = leadingVol;
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
            vd->isConst = isConst;
            return vd;
        }

        // Try to parse as type declaration (function or variable)
        // Optionally prefixed with 'volatile' (also accepted as a leading qualifier above)
        bool declIsVolatile = leadingVol;
        if (check(TokenType::VOLATILE)) { declIsVolatile = true; advance(); }

        if (check(TokenType::INT) || check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
            check(TokenType::BOOL) || check(TokenType::CHAR) || check(TokenType::STRING) ||
            check(TokenType::VOID) || check(TokenType::STAR) || check(TokenType::IDENT) ||
            check(TokenType::FN) ||
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
                    vd->isConst = isConst;
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
    std::vector<bool> esc;
    auto params = parseParameterList(&esc);
    consume(TokenType::RPAREN, "Expected ')'");

    // A bare ';' marks a forward declaration (prototype only, no body).
    StmtPtr body = nullptr;
    if (!match(TokenType::SEMICOLON)) {
        body = parseBlockStatement();
    }

    auto decl = std::make_shared<FunctionDecl>(name, returnType, params, body);
    decl->typeParams = typeParams;
    decl->paramEscaping = esc;
    decl->line = nameTok.line; decl->col = nameTok.column;
    return decl;
}

DeclPtr Parser::parseExternDecl() {
    std::string returnType = parseType();
    std::string name = consume(TokenType::IDENT, "Expected function name").value;

    consume(TokenType::LPAREN, "Expected '('");
    std::vector<bool> esc;
    auto params = parseParameterList(&esc);
    consume(TokenType::RPAREN, "Expected ')'");
    consume(TokenType::SEMICOLON, "Expected ';'");

    auto d = std::make_shared<ExternDecl>(name, returnType, params);
    d->paramEscaping = esc;
    return d;
}

DeclPtr Parser::parseIntrinsicDecl() {
    // Same prototype syntax as extern, but a distinct node: the call lowers to
    // inline IR rather than a call to an external C symbol.
    Token startTok = peek();   // return-type token — stamps the decl's position
    std::string returnType = parseType();
    std::string name = consume(TokenType::IDENT, "Expected intrinsic name").value;

    consume(TokenType::LPAREN, "Expected '('");
    std::vector<bool> esc;
    auto params = parseParameterList(&esc);
    consume(TokenType::RPAREN, "Expected ')'");
    consume(TokenType::SEMICOLON, "Expected ';'");

    auto d = std::make_shared<IntrinsicDecl>(name, returnType, params);
    d->paramEscaping = esc;
    return withPos(d, startTok);
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
                // Optional bitfield width:  uint32 flags : 3;
                int bitWidth = 0;
                if (match(TokenType::COLON)) {
                    Token w = consume(TokenType::INT_LIT,
                        "Expected bit width after ':' in bitfield");
                    bitWidth = (int)std::stoll(w.value, nullptr, 0);
                }
                consume(TokenType::SEMICOLON, "Expected ';' after field");
                fields.push_back({memberType, memberName, bitWidth});
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("In struct '") + name + "': " + e.what());
        }
    }

    consume(TokenType::RBRACE, "Expected '}'");

    declaredTypeNames.insert(name);
    auto decl = std::make_shared<StructDecl>(name, fields);
    decl->methods  = methods;
    decl->typeParams = typeParams;
    if (currentPack == 1) decl->isPacked = true;  // under #pragma pack(1)
    return decl;
}

// Interpret a `#pragma ...` directive. Only `#pragma pack` affects compilation;
// every other pragma is ignored. Supported forms:
//   #pragma pack(N)         set current alignment (N==1 packs subsequent structs)
//   #pragma pack()          reset to default
//   #pragma pack(push, N)   save current, then set to N
//   #pragma pack(pop)       restore the last saved value
void Parser::applyPragma(const std::string& text) {
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return std::string();
        return s.substr(a, s.find_last_not_of(" \t") - a + 1);
    };
    // Must be `pragma pack...`; anything else is ignored.
    if (text.find("pragma") == std::string::npos) return;
    size_t pk = text.find("pack");
    if (pk == std::string::npos) return;

    std::vector<std::string> args;
    size_t lp = text.find('(', pk);
    if (lp != std::string::npos) {
        size_t rp = text.find(')', lp);
        std::string inner = text.substr(lp + 1, (rp == std::string::npos ? text.size() : rp) - lp - 1);
        std::string cur;
        for (char c : inner) {
            if (c == ',') { std::string t = trim(cur); if (!t.empty()) args.push_back(t); cur.clear(); }
            else cur += c;
        }
        std::string t = trim(cur); if (!t.empty()) args.push_back(t);
    }
    auto toInt = [](const std::string& s, int def) {
        try { return std::stoi(s); } catch (...) { return def; }
    };

    if (args.empty()) { currentPack = 0; return; }          // #pragma pack() / pack
    if (args[0] == "push") {
        packStack.push_back(currentPack);
        if (args.size() >= 2) currentPack = toInt(args[1], currentPack);
        return;
    }
    if (args[0] == "pop") {
        if (!packStack.empty()) { currentPack = packStack.back(); packStack.pop_back(); }
        return;
    }
    currentPack = toInt(args[0], 0);                        // #pragma pack(N)
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
        ExprPtr iterable = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        StmtPtr body = parseStatement();
        auto fin = std::make_shared<ForInStmt>(nameTok.value, iterable, body);
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
        // Also a cast when the inner token names a declared type — a struct,
        // enum, union, or alias — as `(Name)x`, `(Name*)x`, or `(Name<...>)x`.
        if (!isTypeKeyword && inner == TokenType::IDENT &&
            declaredTypeNames.count(peek_ahead(1).value)) {
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
