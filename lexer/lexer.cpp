#include "lexer.h"
#include <iostream>
#include <cctype>
#include <sstream>
#include <map>
#include <vector>
#include <set>

// ── Preprocessor ────────────────────────────────────────────────────────────
// Text pass run before lexing: object-like and function-like #define/#undef,
// and #ifdef/#ifndef/#else/#endif conditionals. Directive and skipped lines
// become blank lines so source line numbers are preserved. The macro table is
// supplied by the caller and shared across files, so #defines propagate through
// import / multi-file compilation. Substitution is identifier-aware (skips
// string/char literals and line comments) and recursive (a macro is not
// re-expanded within its own expansion). Function-like macro calls must fit on
// one line.

static std::string ppTrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// Replace each parameter name in a function-like macro body with its argument.
static std::string ppSubstParams(const std::string& body,
                                 const std::vector<std::string>& params,
                                 const std::vector<std::string>& args) {
    std::map<std::string, std::string> m;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) m[params[i]] = args[i];
    std::string out; size_t i = 0, n = body.size();
    while (i < n) {
        char c = body[i];
        if (c == '"' || c == '\'') {
            char q = c; out += c; i++;
            while (i < n) {
                if (body[i]=='\\'&&i+1<n){out+=body[i];out+=body[i+1];i+=2;continue;}
                out += body[i]; if (body[i]==q){i++;break;} i++;
            }
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i; while (j<n && (std::isalnum((unsigned char)body[j])||body[j]=='_')) j++;
            std::string id = body.substr(i, j-i);
            auto it = m.find(id);
            out += (it != m.end()) ? it->second : id;
            i = j; continue;
        }
        out += c; i++;
    }
    return out;
}

// Expand all macros in `text`, recursively. `expanding` guards against a macro
// re-expanding within its own expansion (prevents infinite loops).
static std::string ppExpand(const std::string& text,
                            const std::map<std::string, Macro>& defines,
                            std::set<std::string>& expanding) {
    if (defines.empty()) return text;
    std::string out; size_t i = 0, n = text.size();
    while (i < n) {
        char c = text[i];
        if (c == '"' || c == '\'') {
            char q = c; out += c; i++;
            while (i < n) {
                if (text[i]=='\\'&&i+1<n){out+=text[i];out+=text[i+1];i+=2;continue;}
                out += text[i]; if (text[i]==q){i++;break;} i++;
            }
            continue;
        }
        if (c == '/' && i+1<n && text[i+1]=='/') { out += text.substr(i); break; }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i; while (j<n && (std::isalnum((unsigned char)text[j])||text[j]=='_')) j++;
            std::string id = text.substr(i, j-i);
            auto it = defines.find(id);
            if (it != defines.end() && !expanding.count(id)) {
                const Macro& mac = it->second;
                if (!mac.isFunction) {
                    expanding.insert(id);
                    out += ppExpand(mac.body, defines, expanding);
                    expanding.erase(id);
                    i = j; continue;
                }
                size_t k = j; while (k<n && (text[k]==' '||text[k]=='\t')) k++;
                if (k < n && text[k] == '(') {           // function-like call
                    std::vector<std::string> args; std::string cur; int depth = 0;
                    bool sawAny = false; size_t p = k + 1;
                    for (; p < n; ++p) {
                        char d = text[p];
                        if (d == '(') { depth++; cur += d; sawAny = true; }
                        else if (d == ')') { if (depth==0) { p++; break; } depth--; cur += d; }
                        else if (d == ',' && depth==0) { args.push_back(ppTrim(cur)); cur.clear(); }
                        else { cur += d; sawAny = true; }
                    }
                    if (sawAny || !args.empty()) args.push_back(ppTrim(cur));
                    std::string sub = ppSubstParams(mac.body, mac.params, args);
                    expanding.insert(id);
                    out += ppExpand(sub, defines, expanding);
                    expanding.erase(id);
                    i = p; continue;
                }
                // function-like name not followed by '(' → leave as-is
            }
            out += id; i = j; continue;
        }
        out += c; i++;
    }
    return out;
}

static void preprocess(const std::string& src,
                       std::map<std::string, Macro>& defines,
                       std::string& result) {
    struct Cond { bool parentActive; bool branchActive; };
    std::vector<Cond> stack;
    auto active = [&]() {
        return stack.empty() ? true : (stack.back().parentActive && stack.back().branchActive);
    };

    std::istringstream in(src);
    std::ostringstream out;
    std::string line; bool first = true;
    while (std::getline(in, line)) {
        // Line splicing: a trailing backslash continues onto the next physical
        // line, so a #define (or any line) may span several lines. The joined
        // logical line is emitted as one line followed by `extra` blank lines,
        // keeping every later source line on its original line number.
        int extra = 0;
        while (!line.empty() && line.back() == '\\') {
            line.pop_back();
            std::string cont;
            if (!std::getline(in, cont)) break;
            line += cont;
            extra++;
        }

        if (!first) out << "\n";
        first = false;

        size_t h = line.find_first_not_of(" \t");
        bool handled = false;
        if (h != std::string::npos && line[h] == '#') {
            handled = true;
            std::istringstream ds(line.substr(h + 1));
            std::string kw; ds >> kw;
            if (kw == "define") {
                if (active()) {
                    std::string rest; std::getline(ds, rest);
                    rest = ppTrim(rest);
                    size_t p = 0;
                    while (p < rest.size() && (std::isalnum((unsigned char)rest[p]) || rest[p]=='_')) p++;
                    std::string name = rest.substr(0, p);
                    Macro mac;
                    if (p < rest.size() && rest[p] == '(') {        // function-like
                        mac.isFunction = true;
                        size_t q = p + 1; std::string cur;
                        for (; q < rest.size(); ++q) {
                            char d = rest[q];
                            if (d == ')') { q++; break; }
                            if (d == ',') { std::string t = ppTrim(cur); if (!t.empty()) mac.params.push_back(t); cur.clear(); }
                            else cur += d;
                        }
                        std::string t = ppTrim(cur); if (!t.empty()) mac.params.push_back(t);
                        mac.body = ppTrim(q < rest.size() ? rest.substr(q) : "");
                    } else {
                        mac.body = ppTrim(p < rest.size() ? rest.substr(p) : "");
                    }
                    if (!name.empty()) defines[name] = mac;
                }
            } else if (kw == "undef") {
                std::string name; ds >> name;
                if (active()) defines.erase(name);
            } else if (kw == "ifdef") {
                std::string name; ds >> name;
                stack.push_back({active(), defines.count(name) > 0});
            } else if (kw == "ifndef") {
                std::string name; ds >> name;
                stack.push_back({active(), defines.count(name) == 0});
            } else if (kw == "else") {
                if (!stack.empty()) stack.back().branchActive = !stack.back().branchActive;
            } else if (kw == "endif") {
                if (!stack.empty()) stack.pop_back();
            } else if (kw == "pragma") {
                // #pragma is a compiler directive, not a preprocessor one: pass
                // it through unchanged so the lexer/parser can act on it (e.g.
                // `#pragma pack`). Unknown pragmas are ignored downstream.
                if (active()) out << ppTrim(line.substr(h));
            }
            // any other directive emits a blank line
        }

        if (!handled && active()) {
            std::set<std::string> expanding; out << ppExpand(line, defines, expanding);
        }
        // inactive / directive lines emit blank

        for (int e = 0; e < extra; ++e) out << "\n";  // preserve line numbers
    }
    result = out.str();
}

std::string tokenTypeToString(TokenType type) {
    switch (type) {
        // Keywords
        case TokenType::LET: return "LET";
        case TokenType::INT: return "INT";
        case TokenType::FLOAT: return "FLOAT";
        case TokenType::DOUBLE: return "DOUBLE";
        case TokenType::BOOL: return "BOOL";
        case TokenType::CHAR: return "CHAR";
        case TokenType::STRING: return "STRING";
        case TokenType::VOID: return "VOID";
        case TokenType::STRUCT: return "STRUCT";
        case TokenType::PACKED: return "PACKED";
        case TokenType::UNION:  return "UNION";
        case TokenType::INTERFACE: return "INTERFACE";
        case TokenType::ENUM: return "ENUM";
        case TokenType::FN: return "FN";
        case TokenType::ASM: return "ASM";
        case TokenType::VOLATILE:      return "VOLATILE";
        case TokenType::CONST:         return "CONST";
        case TokenType::THREAD_CREATE: return "THREAD_CREATE";
        case TokenType::THREAD_JOIN:   return "THREAD_JOIN";
        case TokenType::FOR: return "FOR";
        case TokenType::IN: return "IN";
        case TokenType::WHILE: return "WHILE";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::SWITCH: return "SWITCH";
        case TokenType::CASE: return "CASE";
        case TokenType::DEFAULT: return "DEFAULT";
        case TokenType::BREAK: return "BREAK";
        case TokenType::RETURN: return "RETURN";
        case TokenType::IMPORT: return "IMPORT";
        case TokenType::EXTERN: return "EXTERN";
        case TokenType::ALLOC_WITH: return "ALLOC_WITH";
        case TokenType::NULL_KW: return "NULL";
        case TokenType::TRUE: return "TRUE";
        case TokenType::FALSE: return "FALSE";
        case TokenType::THREAD: return "THREAD";
        case TokenType::SPAWN: return "SPAWN";
        case TokenType::MUTEX: return "MUTEX";
        case TokenType::SIZEOF: return "SIZEOF";
        case TokenType::TRY: return "TRY";
        case TokenType::CATCH: return "CATCH";
        case TokenType::FINALLY: return "FINALLY";
        case TokenType::THROW: return "THROW";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::INT8: return "INT8";
        case TokenType::INT16: return "INT16";
        case TokenType::INT32: return "INT32";
        case TokenType::INT64: return "INT64";
        case TokenType::UINT: return "UINT";
        case TokenType::UINT8: return "UINT8";
        case TokenType::UINT16: return "UINT16";
        case TokenType::UINT32: return "UINT32";
        case TokenType::UINT64: return "UINT64";
        // Operators
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::PLUS_EQ: return "PLUS_EQ";
        case TokenType::MINUS_EQ: return "MINUS_EQ";
        case TokenType::STAR_EQ: return "STAR_EQ";
        case TokenType::SLASH_EQ: return "SLASH_EQ";
        case TokenType::PERCENT_EQ: return "PERCENT_EQ";
        case TokenType::EQ: return "EQ";
        case TokenType::EQEQ: return "EQEQ";
        case TokenType::NE: return "NE";
        case TokenType::LT: return "LT";
        case TokenType::GT: return "GT";
        case TokenType::LE: return "LE";
        case TokenType::GE: return "GE";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::AMPERSAND: return "AMPERSAND";
        case TokenType::PIPE: return "PIPE";
        case TokenType::CARET: return "CARET";
        case TokenType::TILDE: return "TILDE";
        case TokenType::LSHIFT: return "LSHIFT";
        case TokenType::RSHIFT: return "RSHIFT";
        case TokenType::AMP_EQ: return "AMP_EQ";
        case TokenType::PIPE_EQ: return "PIPE_EQ";
        case TokenType::CARET_EQ: return "CARET_EQ";
        case TokenType::LSHIFT_EQ: return "LSHIFT_EQ";
        case TokenType::RSHIFT_EQ: return "RSHIFT_EQ";
        // Delimiters
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COMMA: return "COMMA";
        case TokenType::DOT: return "DOT";
        case TokenType::COLON: return "COLON";
        case TokenType::QUESTION: return "QUESTION";
        case TokenType::ARROW: return "ARROW";
        case TokenType::ELLIPSIS: return "ELLIPSIS";
        // Literals
        case TokenType::INT_LIT: return "INT_LIT";
        case TokenType::FLOAT_LIT: return "FLOAT_LIT";
        case TokenType::STRING_LIT: return "STRING_LIT";
        case TokenType::CHAR_LIT: return "CHAR_LIT";
        case TokenType::IDENT: return "IDENT";
        // Special
        case TokenType::EOF_TOKEN: return "EOF";
        case TokenType::PRAGMA: return "PRAGMA";
        case TokenType::UNKNOWN: return "UNKNOWN";
        // No default: -Wswitch flags any TokenType that is missing a case here.
    }
    return "???";
}

std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"let", TokenType::LET},
    {"int", TokenType::INT},
    {"float", TokenType::FLOAT},
    {"double", TokenType::DOUBLE},
    {"bool", TokenType::BOOL},
    {"char", TokenType::CHAR},
    {"string", TokenType::STRING},
    {"void", TokenType::VOID},
    {"struct", TokenType::STRUCT},
    {"packed", TokenType::PACKED},
    {"union",  TokenType::UNION},
    {"interface", TokenType::INTERFACE},
    {"enum", TokenType::ENUM},
    {"fn", TokenType::FN},
    {"asm", TokenType::ASM},
    {"volatile",       TokenType::VOLATILE},
    {"const",          TokenType::CONST},
    {"sizeof",         TokenType::SIZEOF},
    {"thread_create",  TokenType::THREAD_CREATE},
    {"thread_join",    TokenType::THREAD_JOIN},
    {"for", TokenType::FOR},
    {"in", TokenType::IN},
    {"while", TokenType::WHILE},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"switch", TokenType::SWITCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"break", TokenType::BREAK},
    {"return", TokenType::RETURN},
    {"import", TokenType::IMPORT},
    {"extern", TokenType::EXTERN},
    {"alloc_with", TokenType::ALLOC_WITH},
    {"null", TokenType::NULL_KW},
    {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},
    {"thread", TokenType::THREAD},
    {"spawn", TokenType::SPAWN},
    {"mutex", TokenType::MUTEX},
    {"try", TokenType::TRY},
    {"catch", TokenType::CATCH},
    {"finally", TokenType::FINALLY},
    {"throw",    TokenType::THROW},
    {"continue", TokenType::CONTINUE},
    {"int8",   TokenType::INT8},
    {"int16",  TokenType::INT16},
    {"int32",  TokenType::INT32},
    {"int64",  TokenType::INT64},
    {"uint",   TokenType::UINT},
    {"uint8",  TokenType::UINT8},
    {"uint16", TokenType::UINT16},
    {"uint32", TokenType::UINT32},
    {"uint64", TokenType::UINT64},
};

Lexer::Lexer(const std::string& source, std::map<std::string, Macro>* macros)
    : current(0), line(1), column(1) {
    std::map<std::string, Macro> local;
    preprocess(source, macros ? *macros : local, this->source);
}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source[current];
}

char Lexer::peek_next() const {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

char Lexer::advance() {
    char c = peek();
    current++;
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

bool Lexer::is_at_end() const {
    return current >= source.length();
}

void Lexer::skip_whitespace() {
    while (!is_at_end() && std::isspace(peek())) {
        advance();
    }
}

void Lexer::skip_comment() {
    if (peek() == '/' && peek_next() == '/') {
        // Single line comment
        while (!is_at_end() && peek() != '\n') {
            advance();
        }
    } else if (peek() == '/' && peek_next() == '*') {
        // Multi-line comment
        int start_line = line, start_col = column;
        advance(); // /
        advance(); // *
        bool closed = false;
        while (!is_at_end()) {
            if (peek() == '*' && peek_next() == '/') {
                advance(); // *
                advance(); // /
                closed = true;
                break;
            }
            advance();
        }
        if (!closed) lexError(start_line, start_col, "unterminated block comment");
    }
}

Token Lexer::read_number() {
    int start_line = line;
    int start_col = column;
    std::string num;

    // Handle floats with leading dot
    if (peek() == '.') {
        num += advance();
        while (!is_at_end() && std::isdigit(peek())) {
            num += advance();
        }
        return Token(TokenType::FLOAT_LIT, num, start_line, start_col);
    }

    // Integer part
    while (!is_at_end() && std::isdigit(peek())) {
        num += advance();
    }

    // Hex literal: 0x... (e.g. 0xFF)
    if (num == "0" && !is_at_end() && (peek() == 'x' || peek() == 'X')) {
        num += advance(); // consume 'x'
        while (!is_at_end() && std::isxdigit(peek())) {
            num += advance();
        }
        return Token(TokenType::INT_LIT, num, start_line, start_col);
    }

    // Check for decimal point or exponent
    if (!is_at_end() && peek() == '.' && std::isdigit(peek_next())) {
        num += advance(); // .
        while (!is_at_end() && std::isdigit(peek())) {
            num += advance();
        }
        return Token(TokenType::FLOAT_LIT, num, start_line, start_col);
    }

    return Token(TokenType::INT_LIT, num, start_line, start_col);
}

void Lexer::lexError(int errLine, int errCol, const std::string& msg) {
    std::cerr << "error: " << errLine << ":" << errCol << ": " << msg << std::endl;
    hadError = true;
}

Token Lexer::read_string() {
    int start_line = line;
    int start_col = column;
    std::string str;

    advance(); // consume opening "

    while (!is_at_end() && peek() != '"') {
        if (peek() == '\\' && peek_next() != '\0') {
            advance();
            char escaped = advance();
            // Handle escape sequences
            switch (escaped) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '"': str += '"'; break;
                default: str += escaped;
            }
        } else {
            str += advance();
        }
    }

    if (is_at_end()) {
        lexError(start_line, start_col, "unterminated string literal");
    } else {
        advance(); // consume closing "
    }

    return Token(TokenType::STRING_LIT, str, start_line, start_col);
}

Token Lexer::read_char() {
    int start_line = line;
    int start_col = column;
    advance(); // consume opening '

    std::string ch;
    if (is_at_end()) {
        lexError(start_line, start_col, "unterminated character literal");
        return Token(TokenType::CHAR_LIT, ch, start_line, start_col);
    }
    if (peek() == '\'') {
        lexError(start_line, start_col, "empty character literal");
        advance(); // consume closing '
        return Token(TokenType::CHAR_LIT, ch, start_line, start_col);
    }
    if (peek() == '\\') {
        advance();
        char escaped = advance();
        switch (escaped) {
            case 'n': ch += '\n'; break;
            case 't': ch += '\t'; break;
            case '\\': ch += '\\'; break;
            default: ch += escaped;
        }
    } else {
        ch += advance();
    }

    if (!is_at_end() && peek() == '\'') {
        advance(); // consume closing '
    } else {
        // Unterminated, or more than one character before the closing quote.
        bool unterminated = is_at_end() || peek() == '\n';
        lexError(start_line, start_col,
                 unterminated ? "unterminated character literal"
                              : "character literal must contain a single character");
        // Recover: skip to the closing ' (or end of line) so the rest of the
        // malformed literal does not mis-lex into stray tokens.
        while (!is_at_end() && peek() != '\'' && peek() != '\n') advance();
        if (!is_at_end() && peek() == '\'') advance();
    }

    return Token(TokenType::CHAR_LIT, ch, start_line, start_col);
}

Token Lexer::read_identifier() {
    int start_line = line;
    int start_col = column;
    std::string ident;

    while (!is_at_end() && (std::isalnum(peek()) || peek() == '_')) {
        ident += advance();
    }

    // Check if it's a keyword
    auto it = keywords.find(ident);
    if (it != keywords.end()) {
        return Token(it->second, ident, start_line, start_col);
    }

    return Token(TokenType::IDENT, ident, start_line, start_col);
}

Token Lexer::next_token() {
    skip_whitespace();

    // Handle comments
    while (!is_at_end() && peek() == '/' && (peek_next() == '/' || peek_next() == '*')) {
        skip_comment();
        skip_whitespace();
    }

    if (is_at_end()) {
        return Token(TokenType::EOF_TOKEN, "", line, column);
    }

    int start_line = line;
    int start_col = column;
    char c = peek();

    // Numbers
    if (std::isdigit(c)) {
        return read_number();
    }

    // Strings
    if (c == '"') {
        return read_string();
    }

    // Characters
    if (c == '\'') {
        return read_char();
    }

    // Identifiers and keywords
    if (std::isalpha(c) || c == '_') {
        return read_identifier();
    }

    // Compiler directive surviving the preprocessor (e.g. `#pragma pack(1)`).
    // The whole line after '#' becomes the token value, e.g. "pragma pack(1)".
    if (c == '#') {
        advance();  // consume '#'
        std::string text;
        while (!is_at_end() && peek() != '\n') text += advance();
        return Token(TokenType::PRAGMA, text, start_line, start_col);
    }

    // Operators and delimiters
    advance();

    switch (c) {
        case '+':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::PLUS_EQ,    "+=", start_line, start_col); }
            return Token(TokenType::PLUS, "+", start_line, start_col);
        case '-':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::MINUS_EQ,   "-=", start_line, start_col); }
            if (!is_at_end() && peek() == '>') { advance(); return Token(TokenType::ARROW,       "->", start_line, start_col); }
            return Token(TokenType::MINUS, "-", start_line, start_col);
        case '*':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::STAR_EQ,    "*=", start_line, start_col); }
            return Token(TokenType::STAR, "*", start_line, start_col);
        case '/':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::SLASH_EQ,   "/=", start_line, start_col); }
            return Token(TokenType::SLASH, "/", start_line, start_col);
        case '%':
            if (!is_at_end() && peek() == '=') { advance(); return Token(TokenType::PERCENT_EQ, "%=", start_line, start_col); }
            return Token(TokenType::PERCENT, "%", start_line, start_col);
        case '=':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::EQEQ, "==", start_line, start_col);
            }
            return Token(TokenType::EQ, "=", start_line, start_col);
        case '!':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::NE, "!=", start_line, start_col);
            }
            return Token(TokenType::NOT, "!", start_line, start_col);
        case '<':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::LE, "<=", start_line, start_col);
            }
            if (!is_at_end() && peek() == '<') {
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    return Token(TokenType::LSHIFT_EQ, "<<=", start_line, start_col);
                }
                return Token(TokenType::LSHIFT, "<<", start_line, start_col);
            }
            return Token(TokenType::LT, "<", start_line, start_col);
        case '>':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::GE, ">=", start_line, start_col);
            }
            if (!is_at_end() && peek() == '>') {
                advance();
                if (!is_at_end() && peek() == '=') {
                    advance();
                    return Token(TokenType::RSHIFT_EQ, ">>=", start_line, start_col);
                }
                return Token(TokenType::RSHIFT, ">>", start_line, start_col);
            }
            return Token(TokenType::GT, ">", start_line, start_col);
        case '&':
            if (!is_at_end() && peek() == '&') {
                advance();
                return Token(TokenType::AND, "&&", start_line, start_col);
            }
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::AMP_EQ, "&=", start_line, start_col);
            }
            return Token(TokenType::AMPERSAND, "&", start_line, start_col);
        case '|':
            if (!is_at_end() && peek() == '|') {
                advance();
                return Token(TokenType::OR, "||", start_line, start_col);
            }
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::PIPE_EQ, "|=", start_line, start_col);
            }
            return Token(TokenType::PIPE, "|", start_line, start_col);
        case '^':
            if (!is_at_end() && peek() == '=') {
                advance();
                return Token(TokenType::CARET_EQ, "^=", start_line, start_col);
            }
            return Token(TokenType::CARET, "^", start_line, start_col);
        case '~': return Token(TokenType::TILDE, "~", start_line, start_col);
        case '{': return Token(TokenType::LBRACE, "{", start_line, start_col);
        case '}': return Token(TokenType::RBRACE, "}", start_line, start_col);
        case '(': return Token(TokenType::LPAREN, "(", start_line, start_col);
        case ')': return Token(TokenType::RPAREN, ")", start_line, start_col);
        case '[': return Token(TokenType::LBRACKET, "[", start_line, start_col);
        case ']': return Token(TokenType::RBRACKET, "]", start_line, start_col);
        case ';': return Token(TokenType::SEMICOLON, ";", start_line, start_col);
        case ',': return Token(TokenType::COMMA, ",", start_line, start_col);
        case ':': return Token(TokenType::COLON, ":", start_line, start_col);
        case '?': return Token(TokenType::QUESTION, "?", start_line, start_col);
        case '.':
            if (!is_at_end() && peek() == '.' && peek_next() == '.') {
                advance();
                advance();
                return Token(TokenType::ELLIPSIS, "...", start_line, start_col);
            }
            return Token(TokenType::DOT, ".", start_line, start_col);
        default:
            return Token(TokenType::UNKNOWN, std::string(1, c), start_line, start_col);
    }
}

void Lexer::print_all_tokens() {
    current = 0;
    line = 1;
    column = 1;

    Token tok = next_token();
    while (tok.type != TokenType::EOF_TOKEN) {
        std::cout << "Line " << tok.line << ", Col " << tok.column << ": "
                  << "TokenType::" << static_cast<int>(tok.type)
                  << " Value: '" << tok.value << "'" << std::endl;
        tok = next_token();
    }
}
