#include "lexer.h"
#include <iostream>
#include <cctype>
#include <sstream>
#include <map>
#include <vector>
#include <set>
#include "preprocessor.h"

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

// Copy a quoted string/char literal from s starting at s[i] (the opening quote)
// into out, advancing i past the closing quote. Backslash escapes are copied
// verbatim so a quote inside them does not end the literal.
static void ppCopyLiteral(const std::string& s, size_t& i, std::string& out) {
    size_t n = s.size();
    char q = s[i]; out += s[i]; i++;
    while (i < n) {
        if (s[i]=='\\'&&i+1<n){out+=s[i];out+=s[i+1];i+=2;continue;}
        out += s[i]; if (s[i]==q){i++;break;} i++;
    }
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
            ppCopyLiteral(body, i, out);
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
            ppCopyLiteral(text, i, out);
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

// Does a line ending in '\' genuinely continue onto the next physical line?
// Only if the trailing '\' is real code — NOT inside a // or /* */ comment, nor a
// string/char literal. Otherwise a comment ending in '\' would silently swallow
// the following source line (a footgun: it eats a `return`, an `else`, etc.).
// Note: cross-line block-comment state isn't tracked here (a '\' on an interior
// line of a multi-line /* */ may still splice — harmless, it only drops a newline
// inside comment text). Precondition: line.back() == '\\'.
static bool backslashContinuesLine(const std::string& line) {
    bool inStr = false, inChr = false, inBlock = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inBlock) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') { inBlock = false; ++i; }
            continue;
        }
        if (inStr) {
            if (c == '\\' && i + 1 < line.size()) { ++i; continue; }   // skip escaped char
            if (c == '"') inStr = false;
            continue;
        }
        if (inChr) {
            if (c == '\\' && i + 1 < line.size()) { ++i; continue; }
            if (c == '\'') inChr = false;
            continue;
        }
        if (c == '"')  { inStr = true; continue; }
        if (c == '\'') { inChr = true; continue; }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return false;  // line comment
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') { inBlock = true; ++i; continue; }
    }
    // The trailing '\' is reached in this state: only code-context splices.
    return !inStr && !inChr && !inBlock;
}

void preprocess(const std::string& src,
                       std::map<std::string, Macro>& defines,
                       std::string& result,
                       const std::string& filename,
                       bool& hadErr) {
    // Predefined `__FILE__` (constant for this file). `__LINE__` is refreshed each
    // line below. Both are ordinary object-like macros so ppExpand handles them
    // with correct identifier boundaries.
    { Macro m; m.body = "\"" + filename + "\""; defines["__FILE__"] = m; }
    struct Cond { bool parentActive; bool branchActive; };
    std::vector<Cond> stack;
    auto active = [&]() {
        return stack.empty() ? true : (stack.back().parentActive && stack.back().branchActive);
    };

    std::istringstream in(src);
    std::ostringstream out;
    std::string line; bool first = true;
    int curLine = 0;
    while (std::getline(in, line)) {
        curLine++;                       // physical line of this logical line
        int lineNo = curLine;            // __LINE__ for this logical line
        // Line splicing: a trailing backslash continues onto the next physical
        // line, so a #define (or any line) may span several lines. The joined
        // logical line is emitted as one line followed by `extra` blank lines,
        // keeping every later source line on its original line number.
        int extra = 0;
        while (!line.empty() && line.back() == '\\' && backslashContinuesLine(line)) {
            line.pop_back();
            std::string cont;
            if (!std::getline(in, cont)) break;
            line += cont;
            extra++;
            curLine++;                   // each continuation is a physical line too
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
            } else if (kw == "error") {
                // #error <message> — abort compilation with the message (only on
                // an active branch, so it can guard #ifdef blocks).
                if (active()) {
                    std::string msg; std::getline(ds, msg); msg = ppTrim(msg);
                    std::cerr << "error: " << (filename.empty() ? "<input>" : filename)
                              << ":" << lineNo << ": #error " << msg << std::endl;
                    hadErr = true;
                }
            }
            // any other directive emits a blank line
        }

        if (!handled && active()) {
            { Macro m; m.body = std::to_string(lineNo); defines["__LINE__"] = m; }
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
        case TokenType::OPERATOR: return "OPERATOR";
        case TokenType::ASM: return "ASM";
        case TokenType::VOLATILE:      return "VOLATILE";
        case TokenType::STATIC:        return "STATIC";
        case TokenType::ESCAPING:      return "ESCAPING";
        case TokenType::MUST_USE:      return "MUST_USE";
        case TokenType::ASYNC:         return "ASYNC";
        case TokenType::AWAIT:         return "AWAIT";
        case TokenType::CONST:         return "CONST";
        case TokenType::THREAD_CREATE: return "THREAD_CREATE";
        case TokenType::THREAD_JOIN:   return "THREAD_JOIN";
        case TokenType::FOR: return "FOR";
        case TokenType::IN: return "IN";
        case TokenType::WHILE: return "WHILE";
        case TokenType::DO: return "DO";
        case TokenType::IF: return "IF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::SWITCH: return "SWITCH";
        case TokenType::MATCH: return "MATCH";
        case TokenType::CASE: return "CASE";
        case TokenType::DEFAULT: return "DEFAULT";
        case TokenType::BREAK: return "BREAK";
        case TokenType::RETURN: return "RETURN";
        case TokenType::IMPORT: return "IMPORT";
        case TokenType::EXTERN: return "EXTERN";
        case TokenType::INTRINSIC: return "INTRINSIC";
        case TokenType::ALLOC_WITH: return "ALLOC_WITH";
        case TokenType::NULL_KW: return "NULL";
        case TokenType::TRUE: return "TRUE";
        case TokenType::FALSE: return "FALSE";
        case TokenType::SIZEOF: return "SIZEOF";
        case TokenType::FREE_CLOSURE: return "FREE_CLOSURE";
        case TokenType::TRY: return "TRY";
        case TokenType::CATCH: return "CATCH";
        case TokenType::FINALLY: return "FINALLY";
        case TokenType::THROW: return "THROW";
        case TokenType::DEFER: return "DEFER";
        case TokenType::ERRDEFER: return "ERRDEFER";
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
        case TokenType::PLUS_PLUS: return "PLUS_PLUS";
        case TokenType::MINUS_MINUS: return "MINUS_MINUS";
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
        case TokenType::RANGE: return "RANGE";
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
