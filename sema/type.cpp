#include "type.h"
#include <cctype>
#include <cstdint>

namespace ty {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// Split `s` on top-level occurrences of `sep`, respecting <>, (), [] nesting.
std::vector<std::string> splitTop(const std::string& s, char sep) {
    std::vector<std::string> out;
    int depthAngle = 0, depthParen = 0, depthBracket = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '<') ++depthAngle;
        else if (c == '>') { if (depthAngle) --depthAngle; }
        else if (c == '(') ++depthParen;
        else if (c == ')') { if (depthParen) --depthParen; }
        else if (c == '[') ++depthBracket;
        else if (c == ']') { if (depthBracket) --depthBracket; }
        else if (c == sep && !depthAngle && !depthParen && !depthBracket) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(s.substr(start));
    return out;
}

// Is there a top-level '<' (a template application, not a comparison)?
size_t topLevelAngle(const std::string& s) {
    int depthParen = 0, depthBracket = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '(') ++depthParen;
        else if (c == ')') { if (depthParen) --depthParen; }
        else if (c == '[') ++depthBracket;
        else if (c == ']') { if (depthBracket) --depthBracket; }
        else if (c == '<' && !depthParen && !depthBracket) return i;
    }
    return std::string::npos;
}

// The first top-level '[' that begins an array suffix, ignoring brackets nested
// inside template `<>` or fn `()`. For `int[N][M]` this is the '[' before N, so the
// leftmost dimension binds outermost (C order: `int[N][M]` is N arrays of M).
size_t firstArraySuffixBracket(const std::string& s) {
    int angle = 0, paren = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '<') ++angle;
        else if (c == '>' && !(i > 0 && s[i - 1] == '-')) { if (angle) --angle; }
        else if (c == '(') ++paren;
        else if (c == ')') { if (paren) --paren; }
        else if (c == '[' && angle == 0 && paren == 0) return i;
    }
    return std::string::npos;
}

// Matching ']' for the '[' at index `open`, respecting nested brackets.
size_t matchCloseBracket(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if (s[i] == '[') ++depth;
        else if (s[i] == ']') { if (--depth == 0) return i; }
    }
    return std::string::npos;
}

const std::set<std::string>& intSpellings() {
    static const std::set<std::string> s = {
        "int","int8","int16","int32","int64",
        "uint","uint8","uint16","uint32","uint64"};
    return s;
}

Type parseCore(const std::string& in, const std::set<std::string>& tps);
Type parseLegacy(const std::string& s, const std::set<std::string>& tps);

// Linear-time parser over one spelling. `close[i]` is the index of the bracket that
// closes the `<`, `(` or `[` at i (the `>` of an `->` arrow is not a bracket), so each
// top-level scan jumps over nested groups instead of re-reading them, and ranges are
// index pairs into the one string rather than substring copies. On a well-formed
// spelling it builds exactly the Type the reference grammar (parseLegacy) does; a
// spelling whose brackets do not nest falls back to parseLegacy.
struct FastParser {
    const std::string& s;
    const std::set<std::string>& tps;
    std::vector<size_t> close;

    FastParser(const std::string& src, const std::set<std::string>& t) : s(src), tps(t) {}

    bool buildMatches() {
        close.assign(s.size(), std::string::npos);
        std::vector<size_t> stack;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '<' || c == '(' || c == '[') { stack.push_back(i); continue; }
            char want = c == '>' ? '<' : c == ')' ? '(' : c == ']' ? '[' : 0;
            if (!want || (c == '>' && i > 0 && s[i - 1] == '-')) continue;
            if (stack.empty() || s[stack.back()] != want) return false;
            close[stack.back()] = i;
            stack.pop_back();
        }
        return stack.empty();
    }

    void trimRange(size_t& b, size_t& e) const {
        while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    }
    bool startsWith(size_t b, size_t e, const char* lit, size_t n) const {
        return e - b >= n && s.compare(b, n, lit) == 0;
    }

    // Type::parse over [b, e): trim, peel leading qualifiers, then the core grammar.
    Type parse(size_t b, size_t e) {
        trimRange(b, e);
        std::string quals;
        for (;;) {
            if (startsWith(b, e, "const ", 6))         { quals += "const ";    b += 6; trimRange(b, e); }
            else if (startsWith(b, e, "volatile ", 9)) { quals += "volatile "; b += 9; trimRange(b, e); }
            else break;
        }
        Type r = core(b, e);
        r.leadingQuals = quals;
        return r;
    }

    // Split [b, e) on top-level commas and parse each piece.
    void parseList(size_t b, size_t e, std::vector<Type>& out) {
        size_t start = b;
        for (size_t i = b; i < e; ++i) {
            char c = s[i];
            if ((c == '<' || c == '(' || c == '[') && close[i] != std::string::npos) { i = close[i]; continue; }
            if (c == ',') { out.push_back(parse(start, i)); start = i + 1; }
        }
        out.push_back(parse(start, e));
    }

    Type core(size_t b, size_t e) {
        Type r;
        trimRange(b, e);
        if (b == e) { r.kind = Type::Kind::Unknown; r.name = ""; return r; }

        if (s[b] == '?') {
            Type inner = core(b + 1, e);
            inner.nullable = true;
            return inner;
        }

        // An array suffix binds before a pointer or a fn type (see parseCore), and the
        // leftmost bracket group is the outermost dimension.
        if (s[e - 1] == ']') {
            size_t open = std::string::npos;
            for (size_t i = b; i < e; ++i) {
                char c = s[i];
                if ((c == '<' || c == '(') && close[i] != std::string::npos) { i = close[i]; continue; }
                if (c == '[') { open = i; break; }
            }
            if (open != std::string::npos && close[open] != std::string::npos && close[open] < e) {
                // `T[N][M]...`: when the suffix is a run of bracket groups, build the chain
                // once, outermost first, over one parse of the base.
                std::vector<std::pair<size_t, size_t>> dims;
                size_t i = open;
                while (i < e && s[i] == '[' && close[i] != std::string::npos && close[i] < e) {
                    dims.push_back({i, close[i]});
                    i = close[i] + 1;
                }
                if (i != e) {
                    size_t cl = close[open];
                    r.dim = s.substr(open + 1, cl - open - 1);
                    r.kind = r.dim.empty() ? Type::Kind::Slice : Type::Kind::Array;
                    r.elem = std::make_shared<Type>(
                        parseLegacy(s.substr(b, open - b) + s.substr(cl + 1, e - cl - 1), tps));
                    return r;
                }
                auto elem = std::make_shared<Type>(parse(b, open));
                for (size_t k = dims.size(); k-- > 0;) {
                    Type a;
                    a.dim = s.substr(dims[k].first + 1, dims[k].second - dims[k].first - 1);
                    a.kind = a.dim.empty() ? Type::Kind::Slice : Type::Kind::Array;
                    a.elem = elem;
                    if (k == 0) return a;
                    elem = std::make_shared<Type>(std::move(a));
                }
            }
        }

        if (startsWith(b, e, "fn(", 3)) {
            size_t cl = close[b + 2];
            if (cl != std::string::npos && cl < e && startsWith(cl + 1, e, "->", 2)) {
                r.kind = Type::Kind::Fn;
                size_t pb = b + 3, pe = cl;
                trimRange(pb, pe);
                if (pb < pe) parseList(b + 3, cl, r.params);
                r.ret = std::make_shared<Type>(parse(cl + 3, e));
                return r;
            }
        }
        if (s[b] == '*') {
            r.kind = Type::Kind::Pointer;
            r.ptrLeading = true;
            r.pointee = std::make_shared<Type>(parse(b + 1, e));
            return r;
        }
        if (e - b > 6 && s.compare(e - 6, 6, "*const") == 0) {
            r.kind = Type::Kind::Pointer;
            r.bindingConst = true;
            r.pointee = std::make_shared<Type>(parse(b, e - 6));
            return r;
        }
        if (s[e - 1] == '*') {
            r.kind = Type::Kind::Pointer;
            r.pointee = std::make_shared<Type>(parse(b, e - 1));
            return r;
        }
        size_t lt = std::string::npos;
        for (size_t i = b; i < e; ++i) {
            char c = s[i];
            if ((c == '(' || c == '[') && close[i] != std::string::npos) { i = close[i]; continue; }
            if (c == '<') { lt = i; break; }
        }
        if (lt != std::string::npos && s[e - 1] == '>') {
            r.kind = Type::Kind::Template;
            size_t nb = b, ne = lt;
            trimRange(nb, ne);
            r.name = s.substr(nb, ne - nb);
            size_t ib = lt + 1, ie = e - 1;
            trimRange(ib, ie);
            if (ib < ie) parseList(lt + 1, e - 1, r.args);
            return r;
        }
        if (startsWith(b, e, "struct:", 7))    { r.kind = Type::Kind::Struct;    r.name = s.substr(b + 7, e - b - 7); return r; }
        if (startsWith(b, e, "interface:", 10)) { r.kind = Type::Kind::Interface; r.name = s.substr(b + 10, e - b - 10); return r; }

        r.name = s.substr(b, e - b);
        const std::string& n = r.name;
        if (intSpellings().count(n))        r.kind = Type::Kind::Int;
        else if (n == "float" || n == "double") r.kind = Type::Kind::Float;
        else if (n == "bool")    r.kind = Type::Kind::Bool;
        else if (n == "char")    r.kind = Type::Kind::Char;
        else if (n == "string")  r.kind = Type::Kind::String;
        else if (n == "void")    r.kind = Type::Kind::Void;
        else if (n == "va_list") r.kind = Type::Kind::VaList;
        else if (n == "null")    r.kind = Type::Kind::Null;
        else if (n == "unknown") r.kind = Type::Kind::Unknown;
        else if (n == "error")   r.kind = Type::Kind::Error;
        else if (tps.count(n))   r.kind = Type::Kind::Param;
        else                     r.kind = Type::Kind::Named;
        return r;
    }
};

}  // namespace

Type Type::parse(const std::string& s) { return parse(s, {}); }

Type Type::parse(const std::string& s, const std::set<std::string>& tps) {
    FastParser fp(s, tps);
    if (fp.buildMatches()) return fp.parse(0, s.size());
    return parseLegacy(s, tps);
}

namespace {

// The reference grammar, used for a spelling whose brackets do not nest (and as the
// definition FastParser reproduces): quadratic on deep nesting, since each level
// re-scans and copies its substring.
Type parseLegacy(const std::string& s, const std::set<std::string>& tps) {
    std::string t = trim(s);
    std::string quals;
    // Peel leading value qualifiers verbatim (const / volatile), preserving order.
    for (;;) {
        if (t.rfind("const ", 0) == 0)        { quals += "const ";    t = trim(t.substr(6)); }
        else if (t.rfind("volatile ", 0) == 0){ quals += "volatile "; t = trim(t.substr(9)); }
        else break;
    }
    Type r = parseCore(t, tps);
    r.leadingQuals = quals;
    return r;
}

Type parseCore(const std::string& in, const std::set<std::string>& tps) {
    Type r;
    std::string s = trim(in);
    if (s.empty()) { r.kind = Type::Kind::Unknown; r.name = ""; return r; }

    // Nullable pointer `?*T` — a checked nullable pointer (deref requires a null-check).
    // The `?` applies to the pointer that follows; it is preserved as the `nullable` flag.
    if (s[0] == '?') {
        Type inner = parseCore(trim(s.substr(1)), tps);
        inner.nullable = true;
        return inner;
    }

    // Array `T[N]` — an array suffix `[N]` binds tighter than a leading pointer, so
    // `*Node[3]` is an array of 3 pointers (matching the postfix-array grammar and
    // codegen's IndexExpr lowering), NOT a pointer to an array. A pointer to an array
    // is still spellable with a trailing star (`Node[3]*`). For a chain `T[N][M]` the
    // *leftmost* bracket is the outer dimension (C order: N arrays of M), so the elem
    // is `T[M]`. Checked before the pointer suffixes for that reason, and before a fn
    // type for the same one: `fn(int)->int[2]` is an array of 2 fns (a function cannot
    // return an array), like `*T[N]` is an array of pointers.
    if (s.back() == ']') {
        size_t open = firstArraySuffixBracket(s);
        size_t close = open == std::string::npos ? std::string::npos
                                                 : matchCloseBracket(s, open);
        if (open != std::string::npos && close != std::string::npos) {
            r.dim  = s.substr(open + 1, close - open - 1);
            // Empty brackets `T[]` = a slice (fat pointer); `T[N]` = a fixed array.
            r.kind = r.dim.empty() ? Type::Kind::Slice : Type::Kind::Array;
            r.elem = std::make_shared<Type>(
                parseLegacy(s.substr(0, open) + s.substr(close + 1), tps));
            return r;
        }
    }
    // fn(params)->ret  — checked before pointer suffixes, since `ret` can end in '*'.
    if (s.rfind("fn(", 0) == 0) {
        int depth = 0; size_t close = std::string::npos;
        for (size_t i = 2; i < s.size(); ++i) {        // start at the '(' of fn(
            if (s[i] == '(') ++depth;
            else if (s[i] == ')') { if (--depth == 0) { close = i; break; } }
        }
        if (close != std::string::npos && s.compare(close + 1, 2, "->") == 0) {
            r.kind = Type::Kind::Fn;
            std::string inner = s.substr(3, close - 3);
            if (!trim(inner).empty())
                for (auto& p : splitTop(inner, ',')) r.params.push_back(parseLegacy(p, tps));
            r.ret = std::make_shared<Type>(parseLegacy(s.substr(close + 3), tps));
            return r;
        }
    }

    // Leading-star pointer.
    if (s[0] == '*') {
        r.kind = Type::Kind::Pointer;
        r.ptrLeading = true;
        r.pointee = std::make_shared<Type>(parseLegacy(s.substr(1), tps));
        return r;
    }
    // Trailing binding-const pointer `T*const`.
    if (s.size() > 6 && s.compare(s.size() - 6, 6, "*const") == 0) {
        r.kind = Type::Kind::Pointer;
        r.bindingConst = true;
        r.pointee = std::make_shared<Type>(parseLegacy(s.substr(0, s.size() - 6), tps));
        return r;
    }
    // Trailing-star pointer `T*`.
    if (s.back() == '*') {
        r.kind = Type::Kind::Pointer;
        r.pointee = std::make_shared<Type>(parseLegacy(s.substr(0, s.size() - 1), tps));
        return r;
    }
    // Template application `Name<args>`.
    size_t lt = topLevelAngle(s);
    if (lt != std::string::npos && s.back() == '>') {
        r.kind = Type::Kind::Template;
        r.name = trim(s.substr(0, lt));
        std::string inner = s.substr(lt + 1, s.size() - lt - 2);
        if (!trim(inner).empty())
            for (auto& a : splitTop(inner, ',')) r.args.push_back(parseLegacy(a, tps));
        return r;
    }
    // Decorated nominal prefixes.
    if (s.rfind("struct:", 0) == 0)    { r.kind = Type::Kind::Struct;    r.name = s.substr(7); return r; }
    if (s.rfind("interface:", 0) == 0) { r.kind = Type::Kind::Interface; r.name = s.substr(10); return r; }

    // Leaf names (spelling stored verbatim in `name`).
    r.name = s;
    if (intSpellings().count(s))        r.kind = Type::Kind::Int;
    else if (s == "float" || s == "double") r.kind = Type::Kind::Float;
    else if (s == "bool")    r.kind = Type::Kind::Bool;
    else if (s == "char")    r.kind = Type::Kind::Char;
    else if (s == "string")  r.kind = Type::Kind::String;
    else if (s == "void")    r.kind = Type::Kind::Void;
    else if (s == "va_list") r.kind = Type::Kind::VaList;
    else if (s == "null")    r.kind = Type::Kind::Null;
    else if (s == "unknown") r.kind = Type::Kind::Unknown;
    else if (s == "error")   r.kind = Type::Kind::Error;
    else if (tps.count(s))   r.kind = Type::Kind::Param;
    else                     r.kind = Type::Kind::Named;
    return r;
}

}  // namespace

namespace {
// Append the spelling of `t` to `out` (one buffer for the whole type, so rendering is
// linear in its length however deep it nests).
void appendStr(const Type& t, std::string& out) {
    using Kind = Type::Kind;
    out += t.leadingQuals;
    if (t.nullable) out += "?";                     // checked nullable pointer `?*T`
    switch (t.kind) {
        case Kind::Pointer:
            if (t.ptrLeading) { out += "*"; appendStr(*t.pointee, out); }
            else { appendStr(*t.pointee, out); out += t.bindingConst ? "*const" : "*"; }
            break;
        case Kind::Array:
        case Kind::Slice: {
            // `T[N][M]`: the element's own dimensions follow this one (C order), so the
            // innermost element comes first, then every dimension, outermost first.
            const Type* e = t.elem.get();
            while ((e->kind == Kind::Array || e->kind == Kind::Slice) &&
                   e->leadingQuals.empty() && !e->nullable)
                e = e->elem.get();
            appendStr(*e, out);
            for (const Type* d = &t; d != e; d = d->elem.get()) { out += "["; out += d->dim; out += "]"; }
            break;
        }
        case Kind::Fn:
            out += "fn(";
            for (size_t i = 0; i < t.params.size(); ++i) {
                if (i) out += ",";
                appendStr(t.params[i], out);
            }
            out += ")->";
            appendStr(*t.ret, out);
            break;
        case Kind::Template:
            out += t.name;
            out += "<";
            for (size_t i = 0; i < t.args.size(); ++i) {
                if (i) out += ",";
                appendStr(t.args[i], out);
            }
            out += ">";
            break;
        case Kind::Struct:    out += "struct:"; out += t.name; break;
        case Kind::Interface: out += "interface:"; out += t.name; break;
        default:              out += t.name; break;   // Int/Float/.../Named/Param/sentinels
    }
}
}  // namespace

std::string Type::str() const {
    std::string out;
    appendStr(*this, out);
    return out;
}

Type Type::substitute(const std::map<std::string, std::string>& subs) const {
    // Full-string hit at this node (mirrors substType's `subs.find(t)` at every
    // recursion level): a param/named/decorated spelling present as a key wins.
    auto it = subs.find(str());
    if (it != subs.end()) return parse(it->second);

    Type r = *this;
    switch (kind) {
        case Kind::Pointer:
            r.pointee = std::make_shared<Type>(pointee->substitute(subs));
            break;
        case Kind::Array:
        case Kind::Slice:
            r.elem = std::make_shared<Type>(elem->substitute(subs));
            break;            // dim is opaque text — never substituted
        case Kind::Fn:
            r.params.clear();
            for (const auto& p : params) r.params.push_back(p.substitute(subs));
            r.ret = std::make_shared<Type>(ret->substitute(subs));
            break;
        case Kind::Template:
            r.args.clear();
            for (const auto& a : args) r.args.push_back(a.substitute(subs));
            break;
        default: break;       // leaves substitute only via the full-string hit above
    }
    return r;
}

namespace {
// Integer rank (32 = int/uint, 64 = int64/uint64) after promotion; 0 = not an integer.
int promotedRank(const std::string& t, bool& isUnsigned) {
    isUnsigned = false;
    if (t == "int64") return 64;
    if (t == "uint64") { isUnsigned = true; return 64; }
    if (t == "uint" || t == "uint32") { isUnsigned = true; return 32; }
    if (t == "int" || t == "int32" || t == "int8" || t == "int16" ||
        t == "uint8" || t == "uint16" || t == "char") return 32;
    return 0;
}
}  // namespace

std::string rangeVarType(const std::string& a, const std::string& b) {
    bool ua = false, ub = false;
    int ra = promotedRank(trim(a), ua), rb = promotedRank(trim(b), ub);
    if (ra == 0 || rb == 0) return "";
    int r = ra > rb ? ra : rb;
    bool u = (ra == rb) ? (ua || ub) : (ra > rb ? ua : ub);
    if (r == 64) return u ? "uint64" : "int64";
    return u ? "uint" : "int";
}


namespace {

// Recursive descent over a dimension's text (see foldDim); C precedence, 64-bit values.
struct DimFolder {
    const std::string& s;
    const std::function<bool(const std::string&, long long&)>& name;
    size_t i = 0;
    bool ok = true;

    bool at(const std::string& op) const { return s.compare(i, op.size(), op) == 0; }
    static bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
    std::string ident() {
        size_t b = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) ++i;
        return s.substr(b, i - b);
    }
    static bool castTo(const std::string& t, long long v, long long& r) {
        if (t == "int8")   { r = (int8_t)v;  return true; }
        if (t == "uint8" || t == "char") { r = (uint8_t)v; return true; }
        if (t == "int16")  { r = (int16_t)v; return true; }
        if (t == "uint16") { r = (uint16_t)v; return true; }
        if (t == "int" || t == "int32")   { r = (int32_t)v; return true; }
        if (t == "uint" || t == "uint32") { r = (uint32_t)v; return true; }
        if (t == "int64" || t == "uint64") { r = v; return true; }
        if (t == "bool")   { r = v != 0; return true; }
        return false;
    }
    long long unary() {
        if (!ok || i >= s.size()) { ok = false; return 0; }
        char c = s[i];
        if (c == '-') { ++i; return -unary(); }
        if (c == '+') { ++i; return unary(); }
        if (c == '~') { ++i; return ~unary(); }
        if (c == '!') { ++i; return !unary(); }
        if (c == '(') {
            ++i;
            size_t save = i;
            if (i < s.size() && identStart(s[i])) {           // `(type)x`, a cast
                std::string t = ident();
                long long probe = 0;
                if (at(")") && castTo(t, 0, probe)) {
                    ++i;
                    long long r = 0;
                    castTo(t, unary(), r);
                    return r;
                }
                i = save;
            }
            long long v = ternary();
            if (!at(")")) { ok = false; return 0; }
            ++i;
            return v;
        }
        if (std::isdigit((unsigned char)c)) {
            size_t b = i;
            while (i < s.size() && std::isalnum((unsigned char)s[i])) ++i;
            std::string lit = s.substr(b, i - b);
            // C: a leading 0 is octal (`010` is 8), `0x` hex.
            int base = lit.size() > 1 && lit[0] == '0' && (lit[1] == 'x' || lit[1] == 'X') ? 16
                     : lit.size() > 1 && lit[0] == '0' ? 8 : 10;
            try {
                size_t used = 0;
                unsigned long long v = std::stoull(lit, &used, base);
                if (used != lit.size()) ok = false;
                return (long long)v;
            } catch (...) { ok = false; return 0; }
        }
        if (identStart(c)) {
            long long v = 0;
            std::string n = ident();
            // `sizeof(T)` goes to `name` whole, as the text "sizeof(T)".
            if (n == "sizeof" && at("(")) {
                size_t b = i, depth = 0;
                for (; i < s.size(); ++i) {
                    if (s[i] == '(') ++depth;
                    else if (s[i] == ')' && --depth == 0) break;
                }
                if (i >= s.size()) { ok = false; return 0; }
                ++i;
                n += s.substr(b, i - b);
            }
            if (!name(n, v)) ok = false;
            return v;
        }
        ok = false;
        return 0;
    }
    // The binary operator at the cursor for precedence `level`, or "".
    std::string opAt(int level) const {
        static const std::vector<std::vector<std::string>> ops = {
            {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<=", ">=", "<", ">"},
            {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        static const std::vector<std::string> two = {"||", "&&", "==", "!=", "<=", ">=", "<<", ">>"};
        std::string two_here;
        for (const auto& t : two) if (at(t)) two_here = t;
        for (const auto& o : ops[level]) {
            if (!at(o)) continue;
            if (o.size() == 1 && !two_here.empty()) continue;   // `|` of `||`, `<` of `<<`
            return o;
        }
        return "";
    }
    long long binary(int level) {
        if (level == 10) return unary();
        long long l = binary(level + 1);
        for (;;) {
            if (!ok) return 0;
            std::string o = opAt(level);
            if (o.empty()) return l;
            i += o.size();
            long long r = binary(level + 1);
            if (o == "||") l = l || r;       else if (o == "&&") l = l && r;
            else if (o == "|") l |= r;       else if (o == "^") l ^= r;   else if (o == "&") l &= r;
            else if (o == "==") l = l == r;  else if (o == "!=") l = l != r;
            else if (o == "<=") l = l <= r;  else if (o == ">=") l = l >= r;
            else if (o == "<") l = l < r;    else if (o == ">") l = l > r;
            else if (o == "<<") { if (r < 0 || r > 63) ok = false; else l = (long long)((unsigned long long)l << r); }
            else if (o == ">>") { if (r < 0 || r > 63) ok = false; else l >>= r; }
            else if (o == "+") l += r;       else if (o == "-") l -= r;   else if (o == "*") l *= r;
            else if (r == 0) ok = false;
            else if (o == "/") l /= r;       else l %= r;
        }
    }
    long long ternary() {
        long long c = binary(0);
        if (!ok || !at("?")) return c;
        ++i;
        long long a = ternary();
        if (!at(":")) { ok = false; return 0; }
        ++i;
        long long b = ternary();
        return c ? a : b;
    }
};

}  // namespace

bool foldDim(const std::string& dim, const std::function<bool(const std::string&, long long&)>& name,
             long long& out) {
    std::string t;
    for (char c : dim) if (!std::isspace((unsigned char)c)) t += c;
    if (t.empty()) return false;
    DimFolder f{t, name};
    long long v = f.ternary();
    if (!f.ok || f.i != t.size()) return false;
    out = v;
    return true;
}

}  // namespace ty
