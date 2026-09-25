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

// Error state shared by one top-level ppExpand call and its nested expansions: the
// first malformed macro invocation (arity mismatch, unterminated argument list) and
// the column of the top-level token whose expansion produced it.
struct PPExpandCtx {
    std::string err;
    size_t errCol = 0;
    size_t topCol = 0;
    int depth = 0;
};

static bool ppIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
static bool ppIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Skip spaces, tabs and closed `/* */` comments on one line starting at p.
static size_t ppSkipBlank(const std::string& s, size_t p) {
    size_t n = s.size();
    while (p < n) {
        if (s[p] == ' ' || s[p] == '\t') { p++; continue; }
        if (s[p] == '/' && p + 1 < n && s[p + 1] == '*') {
            size_t e = s.find("*/", p + 2);
            if (e == std::string::npos) return p;
            p = e + 2; continue;
        }
        break;
    }
    return p;
}

// Expand all macros in `text`, recursively. `expanding` guards against a macro
// re-expanding within its own expansion (prevents infinite loops). `inBlock`, when
// given, carries `/* ... */` comment state across source lines: comment text is
// copied verbatim (an apostrophe in a comment must not open a char literal).
// `ctx`, when given, receives the first malformed-invocation error. A replacement
// that ends in the name of a function-like macro is rescanned together with the
// rest of the text, so `#define CALLF F` makes `CALLF(2)` call F (C semantics).
static std::string ppExpand(const std::string& input,
                            const std::map<std::string, Macro>& defines,
                            std::set<std::string>& expanding,
                            bool* inBlock = nullptr,
                            PPExpandCtx* ctx = nullptr) {
    bool blk = inBlock ? *inBlock : false;
    std::string text = input;
    long long colShift = 0;
    size_t keepTop = 0;
    std::string out; size_t i = 0, n = text.size();
    auto fail = [&](const std::string& msg) {
        if (ctx && ctx->err.empty()) { ctx->err = msg; ctx->errCol = ctx->topCol; }
    };
    while (i < n) {
        char c = text[i];
        if (blk) {
            size_t e = text.find("*/", i);
            if (e == std::string::npos) { out += text.substr(i); i = n; break; }
            out += text.substr(i, e + 2 - i); i = e + 2; blk = false;
            continue;
        }
        if (c == '/' && i+1<n && text[i+1]=='*') { out += "/*"; i += 2; blk = true; continue; }
        if (c == '"' || c == '\'') {
            ppCopyLiteral(text, i, out);
            continue;
        }
        if (c == '/' && i+1<n && text[i+1]=='/') { out += text.substr(i); break; }
        if (ppIdentStart(c)) {
            size_t j = i; while (j<n && ppIdentChar(text[j])) j++;
            std::string id = text.substr(i, j-i);
            if (ctx && ctx->depth == 0 && i >= keepTop) ctx->topCol = (size_t)((long long)i + colShift + 1);
            auto it = defines.find(id);
            if (it != defines.end() && !expanding.count(id)) {
                const Macro& mac = it->second;
                std::string res;
                size_t after = j;
                bool expanded = false;
                if (!mac.isFunction) {
                    expanding.insert(id);
                    if (ctx) ctx->depth++;
                    res = ppExpand(mac.body, defines, expanding, nullptr, ctx);
                    if (ctx) ctx->depth--;
                    expanding.erase(id);
                    expanded = true;
                } else {
                    size_t k = ppSkipBlank(text, j);
                    if (k < n && text[k] == '(') {           // function-like call
                        // Split the arguments at top-level commas. String and char
                        // literals are copied whole, so a ',' or ')' inside one does
                        // not end an argument; a comment counts as a space.
                        std::vector<std::string> args; std::string cur; int depth = 0;
                        bool sawAny = false, closed = false; size_t p = k + 1;
                        while (p < n) {
                            char d = text[p];
                            if (d == '"' || d == '\'') { ppCopyLiteral(text, p, cur); sawAny = true; continue; }
                            if (d == '/' && p + 1 < n && text[p + 1] == '*') {
                                size_t e = text.find("*/", p + 2);
                                if (e == std::string::npos) { p = n; break; }
                                cur += ' '; p = e + 2; continue;
                            }
                            if (d == '/' && p + 1 < n && text[p + 1] == '/') { p = n; break; }
                            if (d == '(') { depth++; cur += d; sawAny = true; }
                            else if (d == ')') { if (depth==0) { p++; closed = true; break; } depth--; cur += d; }
                            else if (d == ',' && depth==0) { args.push_back(ppTrim(cur)); cur.clear(); sawAny = true; }
                            else { cur += d; if (d != ' ' && d != '\t') sawAny = true; }
                            p++;
                        }
                        if (!closed) {
                            fail("unterminated argument list invoking macro '" + id + "'");
                            out += id; i = j; continue;
                        }
                        if (sawAny) args.push_back(ppTrim(cur));
                        if (args.empty() && mac.params.size() == 1) args.push_back("");
                        if (args.size() != mac.params.size()) {
                            size_t want = mac.params.size();
                            if (args.size() < want)
                                fail("macro '" + id + "' requires " + std::to_string(want) + " argument" +
                                     (want == 1 ? "" : "s") + ", but only " + std::to_string(args.size()) + " given");
                            else
                                fail("macro '" + id + "' passed " + std::to_string(args.size()) + " argument" +
                                     (args.size() == 1 ? "" : "s") + ", but takes just " + std::to_string(want));
                            out += text.substr(i, p - i); i = p; continue;
                        }
                        // Arguments are fully macro-expanded before substitution (C
                        // rule), so a nested call like F(F(3)) expands the inner one.
                        if (ctx) ctx->depth++;
                        for (auto& a : args) a = ppExpand(a, defines, expanding, nullptr, ctx);
                        std::string sub = ppSubstParams(mac.body, mac.params, args);
                        expanding.insert(id);
                        res = ppExpand(sub, defines, expanding, nullptr, ctx);
                        expanding.erase(id);
                        if (ctx) ctx->depth--;
                        after = p;
                        expanded = true;
                    }
                    // function-like name not followed by '(' → leave as-is
                }
                if (expanded) {
                    // A replacement ending in a function-like macro name followed by
                    // '(' in the remaining text: rescan that name with the rest.
                    size_t t = res.size();
                    while (t > 0 && (res[t-1] == ' ' || res[t-1] == '\t')) t--;
                    size_t s0 = t;
                    while (s0 > 0 && ppIdentChar(res[s0-1])) s0--;
                    if (s0 < t && ppIdentStart(res[s0])) {
                        std::string tid = res.substr(s0, t - s0);
                        auto ft = defines.find(tid);
                        size_t k2 = ppSkipBlank(text, after);
                        if (ft != defines.end() && ft->second.isFunction && !expanding.count(tid)
                            && k2 < n && text[k2] == '(') {
                            out += res.substr(0, s0);
                            colShift += (long long)after - (long long)tid.size();
                            text = tid + text.substr(after);
                            keepTop = tid.size();
                            i = 0; n = text.size();
                            continue;
                        }
                    }
                    out += res; i = after; continue;
                }
            }
            out += id; i = j; continue;
        }
        out += c; i++;
    }
    if (inBlock) *inBlock = blk;
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

// Scan `line` for comment state: starting inside a `/* */` comment when `inBlock`,
// return whether the line ends inside one. String/char literals and `//` comments
// are skipped, in the same order ppExpand applies.
static bool endsInBlockComment(const std::string& line, bool inBlock) {
    size_t n = line.size();
    for (size_t i = 0; i < n; ++i) {
        char c = line[i];
        if (inBlock) {
            if (c == '*' && i + 1 < n && line[i + 1] == '/') { inBlock = false; ++i; }
            continue;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '*') { inBlock = true; ++i; continue; }
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            while (i < n && line[i] != q) { if (line[i] == '\\') ++i; ++i; }
            continue;
        }
        if (c == '/' && i + 1 < n && line[i + 1] == '/') break;
    }
    return inBlock;
}

// ── #if / #elif constant expressions ─────────────────────────────────────────
// The C subset: integer and char literals, macros (expanded first), `defined X` /
// `defined(X)`, the unary `! ~ - +`, the binary arithmetic, shift, relational,
// equality, bitwise and logical operators, `?:`, and parentheses. An identifier
// left after expansion is 0, as in C. Evaluated on 64-bit signed integers with
// two's-complement wraparound (`+ - *`, unary `-`, and INT64_MIN / -1 wrap; never
// host UB). `&&`, `||` and `?:` short-circuit: an operand that is not evaluated
// cannot raise a division-by-zero or shift-count error. An integer literal that
// does not fit in 64 bits and a shift count outside 0..63 are errors.
namespace {
struct PPExprError { std::string msg; };

struct PPExprEval {
    std::vector<std::string> toks;
    size_t pos = 0;
    int skip = 0;   // > 0 inside an operand a short-circuit leaves unevaluated

    const std::string& peek() const { static const std::string end; return pos < toks.size() ? toks[pos] : end; }
    bool eat(const char* t) { if (peek() == t) { pos++; return true; } return false; }
    void fail(const std::string& msg) { if (skip == 0) throw PPExprError{msg}; }

    static long long wrap(unsigned long long v) { return (long long)v; }

    long long primary() {
        if (pos >= toks.size()) throw PPExprError{"expected a value"};
        std::string t = toks[pos++];
        if (t == "(") {
            long long v = ternary();
            if (!eat(")")) throw PPExprError{"expected ')'"};
            return v;
        }
        if (std::isdigit((unsigned char)t[0])) {
            std::string d = t;
            while (!d.empty() && (d.back() == 'u' || d.back() == 'U' || d.back() == 'l' || d.back() == 'L')) d.pop_back();
            bool hex = d.size() > 1 && d[0] == '0' && (d[1] == 'x' || d[1] == 'X');
            bool oct = !hex && d.size() > 1 && d[0] == '0';
            unsigned base = hex ? 16 : oct ? 8 : 10;
            size_t start = hex ? 2 : 0;
            if (start >= d.size()) throw PPExprError{"invalid integer '" + t + "'"};
            unsigned long long v = 0;
            for (size_t i = start; i < d.size(); ++i) {
                char c = d[i];
                unsigned dv = std::isdigit((unsigned char)c) ? c - '0'
                            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
                if (dv >= base) throw PPExprError{"invalid integer '" + t + "'"};
                if (v > (~0ULL - dv) / base) throw PPExprError{"integer constant '" + t + "' is too large"};
                v = v * base + dv;
            }
            return wrap(v);
        }
        if (t[0] == '\'') {
            int v = 0; std::string err;
            if (!decodeCharLiteral(t, v, err)) throw PPExprError{err};
            return v;
        }
        if (std::isalpha((unsigned char)t[0]) || t[0] == '_') return 0;
        throw PPExprError{"unexpected '" + t + "'"};
    }
    long long unary() {
        if (eat("!")) return !unary();
        if (eat("~")) return ~unary();
        if (eat("-")) return wrap(0ULL - (unsigned long long)unary());
        if (eat("+")) return unary();
        return primary();
    }
    long long mul() {
        long long v = unary();
        while (true) {
            if (eat("*")) v = wrap((unsigned long long)v * (unsigned long long)unary());
            else if (peek() == "/" || peek() == "%") {
                bool div = peek() == "/"; pos++;
                long long r = unary();
                if (r == 0) { fail("division by zero"); v = 0; continue; }
                if (r == -1) v = div ? wrap(0ULL - (unsigned long long)v) : 0;
                else v = div ? v / r : v % r;
            } else return v;
        }
    }
    long long add() {
        long long v = mul();
        while (true) {
            if (eat("+")) v = wrap((unsigned long long)v + (unsigned long long)mul());
            else if (eat("-")) v = wrap((unsigned long long)v - (unsigned long long)mul());
            else return v;
        }
    }
    long long shift() {
        long long v = add();
        while (true) {
            bool left = false;
            if (eat("<<")) left = true;
            else if (!eat(">>")) return v;
            long long r = add();
            if (r < 0 || r > 63) { fail("shift count " + std::to_string(r) + " is out of range"); v = 0; continue; }
            v = left ? wrap((unsigned long long)v << r) : v >> r;
        }
    }
    long long rel() {
        long long v = shift();
        while (true) {
            if (eat("<")) v = v < shift();
            else if (eat(">")) v = v > shift();
            else if (eat("<=")) v = v <= shift();
            else if (eat(">=")) v = v >= shift();
            else return v;
        }
    }
    long long eq() {
        long long v = rel();
        while (true) {
            if (eat("==")) v = v == rel();
            else if (eat("!=")) v = v != rel();
            else return v;
        }
    }
    long long band() { long long v = eq();   while (eat("&")) v = v & eq();   return v; }
    long long bxor() { long long v = band(); while (eat("^")) v = v ^ band(); return v; }
    long long bor()  { long long v = bxor(); while (eat("|")) v = v | bxor(); return v; }
    long long land() {
        long long v = bor();
        while (eat("&&")) {
            bool dead = v == 0;
            if (dead) skip++;
            long long r = bor();
            if (dead) skip--;
            v = v && r;
        }
        return v;
    }
    long long lor() {
        long long v = land();
        while (eat("||")) {
            bool dead = v != 0;
            if (dead) skip++;
            long long r = land();
            if (dead) skip--;
            v = v || r;
        }
        return v;
    }
    long long ternary() {
        long long c = lor();
        if (!eat("?")) return c;
        if (c == 0) skip++;
        long long a = ternary();
        if (c == 0) skip--;
        if (!eat(":")) throw PPExprError{"expected ':' in '?:'"};
        if (c != 0) skip++;
        long long b = ternary();
        if (c != 0) skip--;
        return c ? a : b;
    }
};
// Split an (already macro-expanded) #if expression into tokens.
std::vector<std::string> ppExprTokens(const std::string& e) {
    std::vector<std::string> out;
    size_t i = 0, n = e.size();
    while (i < n) {
        char c = e[i];
        if (c == ' ' || c == '\t') { i++; continue; }
        if (c == '/' && i + 1 < n && e[i + 1] == '/') break;
        if (c == '/' && i + 1 < n && e[i + 1] == '*') {
            size_t end = e.find("*/", i + 2);
            i = end == std::string::npos ? n : end + 2;
            continue;
        }
        if (std::isalnum((unsigned char)c) || c == '_') {
            size_t j = i; while (j < n && (std::isalnum((unsigned char)e[j]) || e[j] == '_')) j++;
            out.push_back(e.substr(i, j - i)); i = j; continue;
        }
        if (c == '\'') {
            size_t j = i + 1;
            while (j < n && e[j] != '\'') { if (e[j] == '\\') j++; j++; }
            out.push_back(e.substr(i, std::min(j + 1, n) - i)); i = j + 1; continue;
        }
        static const char* two[] = {"||", "&&", "==", "!=", "<=", ">=", "<<", ">>"};
        bool matched = false;
        for (const char* t : two) {
            if (i + 1 < n && e[i] == t[0] && e[i + 1] == t[1]) { out.push_back(t); i += 2; matched = true; break; }
        }
        if (matched) continue;
        if (std::string("()!~-+*/%<>&^|?:").find(c) != std::string::npos) { out.push_back(std::string(1, c)); i++; continue; }
        throw PPExprError{std::string("unexpected character '") + c + "'"};
    }
    return out;
}
} // namespace

// Replace `defined X` / `defined(X)` with 1/0, macro-expand, then evaluate.
// Returns false (and sets err) on a malformed expression.
static bool ppEvalIf(const std::string& expr, const std::map<std::string, Macro>& defines,
                     long long& value, std::string& err) {
    std::string pre;
    size_t i = 0, n = expr.size();
    while (i < n) {
        char c = expr[i];
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i; while (j < n && (std::isalnum((unsigned char)expr[j]) || expr[j] == '_')) j++;
            std::string id = expr.substr(i, j - i);
            if (id != "defined") { pre += id; i = j; continue; }
            size_t k = j; while (k < n && (expr[k] == ' ' || expr[k] == '\t')) k++;
            bool paren = k < n && expr[k] == '(';
            if (paren) { k++; while (k < n && (expr[k] == ' ' || expr[k] == '\t')) k++; }
            size_t m = k; while (m < n && (std::isalnum((unsigned char)expr[m]) || expr[m] == '_')) m++;
            if (m == k) { err = "expected a macro name after 'defined'"; return false; }
            std::string name = expr.substr(k, m - k);
            if (paren) {
                while (m < n && (expr[m] == ' ' || expr[m] == '\t')) m++;
                if (m >= n || expr[m] != ')') { err = "expected ')' after 'defined(" + name + "'"; return false; }
                m++;
            }
            pre += defines.count(name) ? " 1 " : " 0 ";
            i = m; continue;
        }
        if (c == '\'') {
            size_t j = i + 1;
            while (j < n && expr[j] != '\'') { if (expr[j] == '\\') j++; j++; }
            size_t e = std::min(j + 1, n);
            pre += expr.substr(i, e - i); i = e; continue;
        }
        pre += c; i++;
    }
    std::set<std::string> expanding;
    PPExpandCtx ctx;
    std::string expanded = ppExpand(pre, defines, expanding, nullptr, &ctx);
    if (!ctx.err.empty()) { err = ctx.err; return false; }
    try {
        PPExprEval ev;
        ev.toks = ppExprTokens(expanded);
        if (ev.toks.empty()) { err = "#if with no expression"; return false; }
        value = ev.ternary();
        if (ev.pos != ev.toks.size()) { err = "unexpected '" + ev.toks[ev.pos] + "' in #if expression"; return false; }
    } catch (const PPExprError& e) {
        err = e.msg + " in #if expression";
        return false;
    }
    return true;
}

// Does a macro body use `#` (stringification) or `##` (token pasting) outside a
// string/char literal? Neither is supported.
static bool ppBodyHasHash(const std::string& body) {
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            while (i < body.size() && body[i] != q) { if (body[i] == '\\') ++i; ++i; }
            continue;
        }
        if (c == '#') return true;
    }
    return false;
}

// A directive line with its comments replaced by whitespace (C translation phase 3
// runs before directives): each closed `/* */` becomes one space and a `//` comment
// is dropped, string and char literals are kept whole. A `/*` left open drops the
// rest of the line and sets `openAtEnd`.
static std::string ppStripComments(const std::string& line, bool& openAtEnd) {
    std::string out; size_t i = 0, n = line.size();
    openAtEnd = false;
    while (i < n) {
        char c = line[i];
        if (c == '"' || c == '\'') { ppCopyLiteral(line, i, out); continue; }
        if (c == '/' && i + 1 < n && line[i + 1] == '/') break;
        if (c == '/' && i + 1 < n && line[i + 1] == '*') {
            size_t e = line.find("*/", i + 2);
            if (e == std::string::npos) { openAtEnd = true; break; }
            out += ' '; i = e + 2; continue;
        }
        out += c; i++;
    }
    return out;
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
    const std::string fileLabel = filename.empty() ? "<input>" : filename;
    auto ppError = [&](int ln, int col, const std::string& msg) {
        std::cerr << "error: " << fileLabel << ":" << ln << ":" << col << ": " << msg << std::endl;
        hadErr = true;
    };
    // One entry per open conditional. `anyTaken`: some branch of this #if chain
    // has already been selected (so a later #elif/#else is skipped).
    struct Cond { bool parentActive; bool branchActive; bool anyTaken; bool sawElse; int line; int col; };
    std::vector<Cond> stack;
    auto active = [&]() {
        return stack.empty() ? true : (stack.back().parentActive && stack.back().branchActive);
    };

    std::istringstream in(src);
    std::ostringstream out;
    std::string line; bool first = true;
    int curLine = 0;
    bool inBlockComment = false;     // a /* */ comment is open at the start of this line
    // CRLF input: getline leaves the '\r', which would hide a trailing '\'
    // continuation and leak into directive operands. Drop it up front.
    auto stripCR = [](std::string& l) { if (!l.empty() && l.back() == '\r') l.pop_back(); };
    while (std::getline(in, line)) {
        stripCR(line);
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
            stripCR(cont);
            line += cont;
            extra++;
            curLine++;                   // each continuation is a physical line too
        }

        if (!first) out << "\n";
        first = false;

        size_t h = line.find_first_not_of(" \t");
        bool handled = false;
        // A `#!` first line is a shebang (`#!/usr/bin/env eskiuc run`), not a directive.
        bool shebang = lineNo == 1 && line.compare(0, 2, "#!") == 0;
        bool dirOpen = false;            // the directive line leaves a /* comment open
        if (!inBlockComment && h != std::string::npos && line[h] == '#') {
            handled = true;
            if (!shebang) line = ppStripComments(line, dirOpen);
            int col = (int)h + 1;
            size_t kp = h + 1;
            while (kp < line.size() && (line[kp] == ' ' || line[kp] == '\t')) kp++;
            size_t ks = kp;
            while (kp < line.size() && (std::isalnum((unsigned char)line[kp]) || line[kp] == '_')) kp++;
            std::string kw = line.substr(ks, kp - ks);
            std::string rest = ppTrim(line.substr(kp));
            std::string operand = rest.substr(0, rest.find_first_of(" \t"));
            if (kw == "define") {
                if (active()) {
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
                    if (name.empty()) ppError(lineNo, col, "expected a macro name after #define");
                    else if (ppBodyHasHash(mac.body))
                        ppError(lineNo, col, "macro '" + name + "': '#' stringification and '##' token pasting are not supported");
                    else defines[name] = mac;
                }
            } else if (kw == "undef") {
                if (active()) defines.erase(operand);
            } else if (kw == "ifdef" || kw == "ifndef") {
                bool on = false;
                if (active()) {
                    if (operand.empty()) ppError(lineNo, col, "expected a macro name after #" + kw);
                    on = (defines.count(operand) > 0) == (kw == "ifdef");
                }
                stack.push_back({active(), on, on, false, lineNo, col});
            } else if (kw == "if") {
                bool on = false;
                if (active()) {
                    long long v = 0; std::string err;
                    if (ppEvalIf(rest, defines, v, err)) on = v != 0;
                    else ppError(lineNo, col, err);
                }
                stack.push_back({active(), on, on, false, lineNo, col});
            } else if (kw == "elif") {
                if (stack.empty()) ppError(lineNo, col, "#elif without #if");
                else if (stack.back().sawElse) ppError(lineNo, col, "#elif after #else");
                else {
                    Cond& c = stack.back();
                    bool on = false;
                    if (c.parentActive && !c.anyTaken) {
                        long long v = 0; std::string err;
                        if (ppEvalIf(rest, defines, v, err)) on = v != 0;
                        else ppError(lineNo, col, err);
                    }
                    c.branchActive = on;
                    c.anyTaken = c.anyTaken || on;
                }
            } else if (kw == "else") {
                if (stack.empty()) ppError(lineNo, col, "#else without #if");
                else if (stack.back().sawElse) ppError(lineNo, col, "#else after #else");
                else {
                    Cond& c = stack.back();
                    c.branchActive = !c.anyTaken;
                    c.anyTaken = true;
                    c.sawElse = true;
                }
            } else if (kw == "endif") {
                if (stack.empty()) ppError(lineNo, col, "#endif without #if");
                else stack.pop_back();
            } else if (kw == "pragma") {
                // #pragma is a compiler directive, not a preprocessor one: pass
                // it through unchanged so the lexer/parser can act on it (e.g.
                // `#pragma pack`). Unknown pragmas are ignored downstream.
                if (active()) out << ppTrim(line.substr(h));
            } else if (kw == "error") {
                // #error <message> — abort compilation with the message (only on
                // an active branch, so it can guard #ifdef blocks).
                if (active()) ppError(lineNo, col, "#error " + rest);
            } else if (kw == "include") {
                if (active())
                    ppError(lineNo, col, "#include is not supported; use `import \"file.esk\";` "
                                         "or `import <module>;` instead");
            } else if (kw.empty() && (rest.empty() || shebang)) {
                // `#` alone is the null directive; a `#!` first line is a shebang.
            } else if (active()) {
                ppError(lineNo, col, "unknown preprocessor directive '#" + (kw.empty() ? rest : kw) + "'");
            }
        }

        if (!handled && active()) {
            { Macro m; m.body = std::to_string(lineNo); defines["__LINE__"] = m; }
            std::set<std::string> expanding;
            PPExpandCtx ctx;
            std::string expanded = ppExpand(line, defines, expanding, &inBlockComment, &ctx);
            if (!ctx.err.empty()) ppError(lineNo, (int)ctx.errCol, ctx.err);
            out << expanded;
        } else {
            // Inactive and directive lines emit blank. When one opens or closes a
            // `/* */` comment, emit just the delimiter so the lexer's view of the
            // comment matches the source (a directive's `/* ...` continuing onto
            // the next lines must not leave a stray `*/` behind).
            bool was = inBlockComment;
            inBlockComment = handled ? dirOpen : endsInBlockComment(line, inBlockComment);
            if (!was && inBlockComment) out << "/*";
            else if (was && !inBlockComment) out << "*/";
        }

        for (int e = 0; e < extra; ++e) out << "\n";  // preserve line numbers
    }
    for (const Cond& c : stack)
        ppError(c.line, c.col, "unterminated conditional directive (missing #endif)");
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
