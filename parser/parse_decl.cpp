#include "parser.h"
#include "../lexer/lexer.h"
#include "../ast/type_qual.h"
#include <stdexcept>
#include "parser_internal.h"

// Parser — declaration parsing (functions, externs, intrinsics, structs,
// pragmas).
// Part of the parser.cpp split; all methods are Parser members (parser.h).

void Parser::parseTypeParams(std::vector<std::string>& typeParams,
                             std::map<std::string, std::vector<std::string>>& typeConstraints) {
    if (!match(TokenType::LT)) return;
    do {
        std::string tp = consume(TokenType::IDENT, "Expected type parameter name").value;
        typeParams.push_back(tp);
        // Optional constraint(s): `<T: Iface>` or `<T: A + B>`.
        if (match(TokenType::COLON)) {
            do {
                typeConstraints[tp].push_back(
                    consume(TokenType::IDENT, "Expected constraint interface name").value);
            } while (match(TokenType::PLUS));
        }
    } while (match(TokenType::COMMA));
    consume(TokenType::GT, "Expected '>'");
}

DeclPtr Parser::parseDeclaration() {
    try {
        if (match(TokenType::EXTERN)) {
            return parseExternDecl();
        }
        if (match(TokenType::INTRINSIC)) {
            return parseIntrinsicDecl();
        }
        // `async T f(...) { ... }` — function modifier before the return type.
        if (match(TokenType::ASYNC)) {
            auto decl = parseFunctionDecl();
            if (auto* fd = dynamic_cast<FunctionDecl*>(decl.get())) fd->isAsync = true;
            return decl;
        }
        // `must_use T f(...) { ... }` — discarding a call to f is an error.
        if (match(TokenType::MUST_USE)) {
            auto decl = parseFunctionDecl();
            if (auto* fd = dynamic_cast<FunctionDecl*>(decl.get())) fd->mustUse = true;
            return decl;
        }
        if (match(TokenType::STRUCT)) {
            return parseStructDecl();
        }
        // `packed struct Foo { ... }` — explicit packed (pack(1)) layout.
        if (match(TokenType::PACKED)) {
            consume(TokenType::STRUCT, "Expected 'struct' after 'packed'");
            auto decl = parseStructDecl();
            if (auto* sd = dynamic_cast<StructDecl*>(decl.get())) { sd->isPacked = true; sd->packAlign = 1; }
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
            sharedTypeNames->insert(name);
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
            std::vector<std::string> enumTypeParams;
            if (match(TokenType::LT)) {                 // enum Option<T, U> { ... }
                do {
                    enumTypeParams.push_back(consume(TokenType::IDENT, "Expected type parameter name").value);
                } while (match(TokenType::COMMA));
                consume(TokenType::GT, "Expected '>' after enum type parameters");
            }
            consume(TokenType::LBRACE, "Expected '{'");
            std::vector<std::pair<std::string, long long>> members;
            std::vector<std::vector<std::string>> payloads;
            long long next = 0;
            while (!check(TokenType::RBRACE) && !is_at_end()) {
                std::string mname = consume(TokenType::IDENT,
                    "Expected enum member name").value;
                long long val = next;
                std::vector<std::string> payload;
                if (match(TokenType::LPAREN)) {
                    // Algebraic variant with a payload: `Circle(float)`, `Rect(float, float)`.
                    if (!check(TokenType::RPAREN)) {
                        do { payload.push_back(parseType()); } while (match(TokenType::COMMA));
                    }
                    consume(TokenType::RPAREN, "Expected ')' after variant payload");
                } else if (match(TokenType::EQ)) {
                    // Classic integer enum with an explicit value (payload-free only).
                    bool neg = match(TokenType::MINUS);
                    Token num = consume(TokenType::INT_LIT,
                        "Expected integer value for enum member");
                    val = std::stoll(num.value, nullptr, 0);
                    if (neg) val = -val;
                }
                members.push_back({mname, val});
                payloads.push_back(payload);
                next = val + 1;
                if (!match(TokenType::COMMA)) break;
            }
            consume(TokenType::RBRACE, "Expected '}'");
            sharedTypeNames->insert(name);
            auto ed = std::make_shared<EnumDecl>(name, members);
            ed->payloads = std::move(payloads);
            ed->typeParams = std::move(enumTypeParams);
            return ed;
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
            sharedTypeNames->insert(name);
            return std::make_shared<TypeAliasDecl>(name, underlying);
        }

        // Optional leading qualifiers, in any order: `volatile let`, `static int x`,
        // `let volatile`, `const let`, etc.
        bool leadingVol = false, leadingStatic = false;
        while (true) {
            if (match(TokenType::VOLATILE)) { leadingVol = true; continue; }
            if (match(TokenType::STATIC))   { leadingStatic = true; continue; }
            break;
        }

        // A `const` immediately before a `let` binding is a *binding* qualifier
        // (const binding). A `const` before a *type* (`const int x`,
        // `const int* foo()`) is part of the type and is left for parseType, so
        // const works uniformly for variables, params, fields and return types.
        bool constLet = false;
        if (check(TokenType::CONST)) {
            size_t k = 1;
            if (peek_ahead(k).type == TokenType::VOLATILE) k++;
            if (peek_ahead(k).type == TokenType::LET) { advance(); constLet = true; }
        }

        // Split a parsed type into its stored form (pointee-const preserved,
        // binding-only qualifiers removed) and a binding-const flag.
        auto finalizeVar = [](std::string t, bool bindFlag,
                              std::string& storedOut, bool& constOut) {
            constOut = bindFlag || tyq::bindingConst(t);
            size_t p;
            while ((p = t.find("*const")) != std::string::npos) t.erase(p + 1, 5);
            if (tyq::valueConst(t)) t = t.substr(6);   // "const int" -> "int" (flag carries it)
            storedOut = t;
        };

        // Handle 'let' variable declarations. The qualifier comes first
        // (`volatile let x`, like `const int`), captured by leadingVol above.
        if (match(TokenType::LET)) {
            bool isVol = leadingVol;
            Token letNameTok = peek();
            std::string name = consume(TokenType::IDENT, "Expected identifier after 'let'").value;
            consume(TokenType::COLON, "Expected ':' after variable name");
            std::string type = parseType();
            std::string stored; bool isConst;
            finalizeVar(type, constLet, stored, isConst);

            ExprPtr init = nullptr;
            if (match(TokenType::EQ)) {
                init = parseExpression();
            }
            consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
            auto vd = std::make_shared<VarDecl>(name, stored, init);
            vd->line = letNameTok.line; vd->col = letNameTok.column;
            vd->isVolatile = isVol;
            vd->isConst = isConst;
            vd->isStatic = leadingStatic;
            return vd;
        }

        // Try to parse as type declaration (function or variable)
        // Optionally prefixed with 'volatile' (also accepted as a leading qualifier above)
        bool declIsVolatile = leadingVol;
        if (check(TokenType::VOLATILE)) { declIsVolatile = true; advance(); }

        if (check(TokenType::CONST) ||
            check(TokenType::INT) || check(TokenType::FLOAT) || check(TokenType::DOUBLE) ||
            check(TokenType::BOOL) || check(TokenType::CHAR) || check(TokenType::STRING) ||
            check(TokenType::VOID) || check(TokenType::STAR) || check(TokenType::IDENT) ||
            check(TokenType::FN) || check(TokenType::QUESTION) ||   // `?*T` nullable pointer
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
                    // Variable declaration — split const into stored type + flag.
                    std::string stored; bool isConst;
                    finalizeVar(type, false, stored, isConst);
                    ExprPtr init = nullptr;
                    if (tokens[current - 1].type == TokenType::EQ) {
                        init = parseExpression();
                        consume(TokenType::SEMICOLON, "Expected ';'");
                    }
                    auto vd = std::make_shared<VarDecl>(name, stored, init);
                    vd->line = nameTok2.line; vd->col = nameTok2.column;
                    vd->isVolatile = declIsVolatile;
                    vd->isConst = isConst;
                    vd->isStatic = leadingStatic;
                    return vd;
                }
            } else {
                // Parsed a type but no name follows. If the next token is a
                // reserved keyword, the user used it as a variable name — report
                // that at the cause (e.g. `int fn = 3;`). A non-keyword falls
                // through so the caller can reinterpret the tokens.
                TokenType nt = peek().type;
                if (nt >= TokenType::LET && nt <= TokenType::UINT64) {
                    throw std::runtime_error(
                        "expected a name, found keyword '" + peek().value + "'");
                }
            }
        }
    } catch (const std::exception& e) {
        // Don't double-prefix when an inner declaration already wrapped the error
        // (e.g. a malformed local decl inside a function body).
        std::string m = e.what();
        if (m.rfind("Error parsing declaration: ", 0) == 0) throw;
        throw std::runtime_error("Error parsing declaration: " + m);
    }

    throw std::runtime_error("Expected declaration");
}

DeclPtr Parser::parseFunctionDecl() {
    std::string returnType = parseType();
    Token nameTok = peek();
    std::string name = consume(TokenType::IDENT, "Expected function name").value;

    // Optional type parameters: int max<T>(T a, T b) { ... }
    std::vector<std::string> typeParams;
    std::map<std::string, std::vector<std::string>> typeConstraints;
    parseTypeParams(typeParams, typeConstraints);

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
    decl->constraints = typeConstraints;
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
    std::map<std::string, std::vector<std::string>> typeConstraints;
    parseTypeParams(typeParams, typeConstraints);

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

    sharedTypeNames->insert(name);
    auto decl = std::make_shared<StructDecl>(name, fields);
    decl->methods  = methods;
    decl->typeParams = typeParams;
    decl->constraints = typeConstraints;
    if (currentPack >= 1) {                       // under #pragma pack(N)
        decl->packAlign = currentPack;
        if (currentPack == 1) decl->isPacked = true;
    }
    return decl;
}

// Interpret a `#pragma ...` directive. Only `#pragma pack` affects compilation;
// every other pragma is ignored. Supported forms:
//   #pragma pack(N)         cap field alignment at N for subsequent structs
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
