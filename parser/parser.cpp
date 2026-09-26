#include "parser.h"
#include "../lexer/lexer.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "parser_internal.h"
#include "../lexer/preprocessor.h"

Parser::Parser(const std::vector<Token>& tok)
    : tokens(tok), current(0) {}

std::string Parser::canonicalPath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
    return ec ? path : c.string();
}

std::string Parser::resolveImport(const std::string& spec, bool isStdlib,
                                  const std::string& basedir, const std::string& stdlibPath) {
    std::string path = spec;
    // Allow bare name or name with path separator
    if (isStdlib && path.find('/') == std::string::npos) path = "stdlib/" + path + ".esk";
    if (isStdlib && !stdlibPath.empty()) return stdlibPath + "/" + path;
    if (!basedir.empty() && (path.empty() || path[0] != '/')) return basedir + "/" + path;
    return path;
}

PPImportHook ImportCache::hookFor(const std::string& basedir) {
    return [this, basedir](const std::string& spec, bool isStdlib) {
        if (spec.empty()) return;
        std::string full = Parser::resolveImport(spec, isStdlib, basedir, stdlibPath);
        std::string canon = Parser::canonicalPath(full);
        if (seen.count(canon)) return;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(full, ec)) return;
        std::ifstream file(full);
        if (!file.is_open()) return;
        seen.insert(canon);
        std::ostringstream ss;
        ss << file.rdbuf();
        size_t slash = full.rfind('/');
        PPImportHook sub = hookFor(slash != std::string::npos ? full.substr(0, slash) : ".");
        Entry e;
        preprocess(ss.str(), *macros, e.text, full, e.ppErr, &sub);
        files[canon] = std::move(e);
    };
}

// ============================================================================
// Helper Methods
// ============================================================================

Token Parser::peek() const {
    if (is_at_end()) {
        return tokens.back();
    }
    if (splitActive && current == splitPos) {
        Token t = tokens[current];
        t.type = TokenType::GT;
        t.value = ">";
        return t;
    }
    return tokens[current];
}

Token Parser::peek_ahead(int n) const {
    if (n == 0) return peek();
    size_t pos = current + n;
    if (pos >= tokens.size()) {
        return tokens.back();
    }
    return tokens[pos];
}

Token Parser::advance() {
    if (current < tokens.size()) {
        if (splitActive && current == splitPos) {
            Token t = peek();
            splitActive = false;
            current++;
            return t;
        }
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
            fail("expected a name, found keyword '" +
                 (kw.empty() ? tokenTypeToString(t) : kw) + "'");
        }
    }
    fail(message);
}

void Parser::fail(const std::string& message) { fail(message, peek()); }

void Parser::fail(const std::string& message, const Token& at) {
    errLine = at.line;
    errCol = at.column;
    throw std::runtime_error(message);
}

// Does `t` begin a top-level declaration (a type, a qualifier, a decl keyword)?
static bool startsTopLevelDecl(TokenType t) {
    switch (t) {
        case TokenType::IDENT: case TokenType::STRUCT: case TokenType::PACKED:
        case TokenType::UNION: case TokenType::INTERFACE: case TokenType::ENUM:
        case TokenType::EXTERN: case TokenType::INTRINSIC: case TokenType::IMPORT:
        case TokenType::ASYNC: case TokenType::MUST_USE: case TokenType::CONST:
        case TokenType::STATIC: case TokenType::VOLATILE: case TokenType::LET:
        case TokenType::STAR: case TokenType::QUESTION: case TokenType::FN:
        case TokenType::PRAGMA:
            return true;
        default:
            return isPrimitiveTypeToken(t);
    }
}

void Parser::skipToNextDecl(size_t declStart) {
    // The error token may itself start the next declaration (e.g. a missing `}`
    // detected at the following function); resume there rather than skipping it.
    if (current > declStart && !is_at_end() && peek().column == 1 && startsTopLevelDecl(peek().type))
        return;
    if (!is_at_end()) advance();
    while (!is_at_end()) {
        if (peek().column == 1 && startsTopLevelDecl(peek().type)) return;
        advance();
    }
}

bool Parser::is_at_end() const {
    if (current >= tokens.size()) {
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
    // A lexed ">>" (right-shift) closes two template levels at once. Its first
    // `>` is consumed by marking the token half-split: until the second `>` is
    // consumed, peek() presents that token as a lone `>`. The token vector is never
    // edited, so a split and its undo (rewindTo) are O(1).
    if (check(TokenType::RSHIFT)) {
        splitActive = true;
        splitPos = current;
        return;
    }
    consume(TokenType::GT, ctx);   // not a close — emit the standard error
}

void Parser::rewindTo(size_t pos) {
    if (splitActive && pos <= splitPos) splitActive = false;
    current = pos;
}

std::string Parser::parseType() {
    std::string type;
    parseTypeInto(type);
    return type;
}

// Append the type at the cursor to `out`. Every prefix (`?`, `const `, leading `*`s)
// is known before the base type is read, so the spelling is built left to right into
// one buffer: nested template arguments and fn types cost time linear in their length.
void Parser::parseTypeInto(std::string& out) {
    TypeLevelGuard guard(*this);

    // Leading `?` marks a checked nullable pointer `?*T`; re-attached as a `?` prefix.
    bool nullable = match(TokenType::QUESTION);

    // Optional leading `const` qualifies the base/pointee: `const int*` is a
    // pointer to const int, spelled with a `const ` prefix. (A `const` before a
    // `let`/decl binding is handled by the declaration parser, not here.)
    bool baseIsConst = match(TokenType::CONST);

    // Handle leading pointers (Rust-style: *i32)
    int leading_pointers = 0;
    while (check(TokenType::STAR)) {
        enterTypeLevel();
        advance();
        leading_pointers++;
    }

    if (is_at_end()) {
        fail("Unexpected end of file while parsing type");
    }

    Token typeToken = peek();
    // Function pointer type: fn(T,U,...)->R (no `?` prefix, no trailing pointers).
    if (match(TokenType::FN)) {
        if (baseIsConst) out += "const ";
        out.append(leading_pointers, '*');
        out += "fn(";
        consume(TokenType::LPAREN, "Expected '(' in fn type");
        bool first = true;
        while (!check(TokenType::RPAREN) && !is_at_end()) {
            if (!first) { consume(TokenType::COMMA, "Expected ',' between fn parameter types"); out += ","; }
            first = false;
            parseTypeInto(out);
        }
        consume(TokenType::RPAREN, "Expected ')' in fn type");
        out += ")->";
        consume(TokenType::ARROW, "Expected '->' in fn type");
        parseTypeInto(out);
        return;
    }
    if (nullable) out += "?";
    if (baseIsConst) out += "const ";
    out.append(leading_pointers, '*');
    if (isPrimitiveTypeToken(typeToken.type)) {
        advance();
        out += typeToken.value;
    } else if (check(TokenType::IDENT)) {
        out += advance().value;
        // Template instantiation: Name<TypeArg, ...>  e.g. Result<int, string>
        if (match(TokenType::LT)) {
            out += "<";
            bool first = true;
            do {
                if (!first) out += ",";
                first = false;
                parseTypeInto(out);
            } while (match(TokenType::COMMA));
            consumeTemplateClose("Expected '>' after template arguments");
            out += ">";
        }
    } else {
        fail("Expected type, got " + tokenTypeToString(peek().type));
    }

    // Handle trailing pointers (C-style: i32*). A `const` right after a star
    // makes that pointer level const (`int* const`), encoded as `*const`.
    while (check(TokenType::STAR)) {
        enterTypeLevel();
        advance();
        out += "*";
        if (match(TokenType::CONST)) out += "const";
    }

    // Handle array syntax [N] — capture the size literal
    while (check(TokenType::LBRACKET)) {
        enterTypeLevel();
        advance();
        std::string sizeStr;
        while (!is_at_end() && !check(TokenType::RBRACKET)) {
            sizeStr += peek().value;
            advance();
        }
        if (!match(TokenType::RBRACKET)) {
            fail("Expected ']'");
        }
        out += "[" + sizeStr + "]";
    }
}

std::vector<std::pair<std::string, std::string>> Parser::parseParameterList(
        std::vector<bool>* escaping, std::vector<std::pair<int, int>>* positions) {
    std::vector<std::pair<std::string, std::string>> params;

    if (!check(TokenType::RPAREN)) {
        do {
            // Handle variadic parameters (...)
            if (match(TokenType::ELLIPSIS)) {
                params.push_back({"...", "..."});
                if (escaping) escaping->push_back(false);
                if (positions) positions->push_back({tokens[current - 1].line, tokens[current - 1].column});
                break;
            }

            // Optional `escaping` qualifier: the parameter retains the closure
            // beyond the call (e.g. stores it), so closures passed here need a
            // heap environment.
            bool isEscaping = match(TokenType::ESCAPING);
            std::string type = parseType();
            Token nameTok = consume(TokenType::IDENT, "Expected parameter name");
            params.push_back({type, nameTok.value});
            if (escaping) escaping->push_back(isEscaping);
            if (positions) positions->push_back({nameTok.line, nameTok.column});
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
    auto prog = std::make_shared<Program>(decls);
    prog->linkLibs = linkLibs;
    return prog;
}

std::vector<DeclPtr> Parser::parseProgram() {
    std::vector<DeclPtr> declarations;

    // Owned import set if caller didn't provide one
    std::set<std::string> ownedSet;
    if (!importedFiles) importedFiles = &ownedSet;
    // The root parser owns the shared type-name set; sub-parsers point at it.
    if (!sharedTypeNames) sharedTypeNames = &declaredTypeNames;
    if (!sharedGenericNames) sharedGenericNames = &declaredGenericNames;

    // Panic-mode recovery shared by both parse paths: report the error at its
    // location, then skip to the next top-level declaration so one bad
    // declaration neither aborts the file nor cascades into spurious errors.
    size_t declStart = 0;
    auto recover = [&](const std::exception& e) {
        std::cerr << "error: " << (filename.empty() ? "<input>" : filename) << ":"
                  << errLine << ":" << errCol << ": " << e.what() << std::endl;
        hadError = true;
        skipToNextDecl(declStart);
    };

    while (!is_at_end()) {
        declStart = current;
        // Compiler directive (#pragma pack / link): updates parser state, emits no decl.
        if (check(TokenType::PRAGMA)) {
            Token pt = advance();
            try { applyPragma(pt); } catch (const std::exception& e) { recover(e); }
            continue;
        }
        // Handle import "path/to/file.esk"  or  import <stdlib_name>
        if (match(TokenType::IMPORT)) {
            try {
                std::string spec;
                bool isStdlib = false;

                Token pathTok = peek();
                if (check(TokenType::STRING_LIT)) {
                    // import "relative/path.esk"
                    spec = advance().value;
                } else if (check(TokenType::LT)) {
                    // import <name>  →  resolved against stdlibPath
                    advance(); // consume <
                    while (!check(TokenType::GT) && !is_at_end())
                        spec += advance().value;
                    consume(TokenType::GT, "Expected '>' after stdlib name");
                    isStdlib = true;
                } else {
                    fail("Expected filename or <name> after import");
                }
                consume(TokenType::SEMICOLON, "Expected ';' after import");

                std::string fullPath = resolveImport(spec, isStdlib, basedir, stdlibPath);
                std::string canon = canonicalPath(fullPath);
                if (!importedFiles->count(canon)) {
                    importedFiles->insert(canon);

                    std::error_code ec;
                    if (std::filesystem::is_directory(fullPath, ec))
                        fail("Cannot open import: '" + fullPath + "'", pathTok);
                    // The preprocessor already handled this import at its line (see
                    // ImportCache); otherwise read and preprocess it now.
                    auto cached = importCache ? importCache->files.find(canon)
                                              : std::map<std::string, ImportCache::Entry>::iterator();
                    bool haveCached = importCache && cached != importCache->files.end();
                    std::string src;
                    if (!haveCached) {
                        std::ifstream file(fullPath);
                        if (!file.is_open())
                            fail("Cannot open import: '" + fullPath + "'", pathTok);
                        std::ostringstream ss;
                        ss << file.rdbuf();
                        src = ss.str();
                    }
                    size_t slash = fullPath.rfind('/');
                    std::string subdir = (slash != std::string::npos) ? fullPath.substr(0, slash) : ".";
                    PPImportHook hook;
                    if (importCache) hook = importCache->hookFor(subdir);
                    Lexer lexer = haveCached
                        ? Lexer::fromPreprocessed(cached->second.text, fullPath, cached->second.ppErr)
                        : Lexer(src, macros, fullPath, importCache ? &hook : nullptr);
                    std::vector<Token> itoks;
                    Token t = lexer.next_token();
                    while (t.type != TokenType::EOF_TOKEN) { itoks.push_back(t); t = lexer.next_token(); }
                    itoks.push_back(t);
                    // A lexical error in the import stops here, like one in a root
                    // file: parsing the broken token stream would only cascade.
                    if (lexer.hadError) { hadError = true; continue; }

                    Parser sub(itoks);
                    sub.filename      = fullPath;
                    sub.basedir       = subdir;
                    sub.stdlibPath    = stdlibPath;
                    sub.importedFiles = importedFiles;
                    sub.macros        = macros;
                    sub.importCache   = importCache;
                    sub.sharedTypeNames = sharedTypeNames;   // one set for all parsers
                    sub.sharedGenericNames = sharedGenericNames;

                    auto subProg = sub.parse();
                    if (!subProg) {
                        hadError = true;
                    } else {
                        declarations.insert(declarations.end(),
                            subProg->declarations.begin(), subProg->declarations.end());
                        for (const auto& l : subProg->linkLibs) addLinkLib(l);
                        // Type names are recorded directly into the shared set as
                        // each file is parsed, so a cast to an imported type —
                        // `(FutureHdr*)p` — parses correctly here regardless of
                        // which import path defined it (no post-merge needed).
                    }
                }
            } catch (const std::exception& e) {
                recover(e);
            }
            continue;
        }

        try {
            DeclPtr decl = parseDeclaration();
            if (decl) {
                decl->sourceFile = filename;
                declarations.push_back(decl);
            }
        } catch (const std::exception& e) {
            recover(e);
        }
    }

    return declarations;
}
