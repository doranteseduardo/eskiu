#include "type.h"

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

// Matching '[' for a string that ends in ']'.
size_t matchOpenBracket(const std::string& s) {
    int depth = 0;
    for (size_t i = s.size(); i-- > 0;) {
        if (s[i] == ']') ++depth;
        else if (s[i] == '[') { if (--depth == 0) return i; }
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

}  // namespace

Type Type::parse(const std::string& s) { return parse(s, {}); }

Type Type::parse(const std::string& s, const std::set<std::string>& tps) {
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

namespace {

Type parseCore(const std::string& in, const std::set<std::string>& tps) {
    Type r;
    std::string s = trim(in);
    if (s.empty()) { r.kind = Type::Kind::Unknown; r.name = ""; return r; }

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
                for (auto& p : splitTop(inner, ',')) r.params.push_back(Type::parse(p, tps));
            r.ret = std::make_shared<Type>(Type::parse(s.substr(close + 3), tps));
            return r;
        }
    }

    // Leading-star pointer.
    if (s[0] == '*') {
        r.kind = Type::Kind::Pointer;
        r.ptrLeading = true;
        r.pointee = std::make_shared<Type>(Type::parse(s.substr(1), tps));
        return r;
    }
    // Trailing binding-const pointer `T*const`.
    if (s.size() > 6 && s.compare(s.size() - 6, 6, "*const") == 0) {
        r.kind = Type::Kind::Pointer;
        r.bindingConst = true;
        r.pointee = std::make_shared<Type>(Type::parse(s.substr(0, s.size() - 6), tps));
        return r;
    }
    // Trailing-star pointer `T*`.
    if (s.back() == '*') {
        r.kind = Type::Kind::Pointer;
        r.pointee = std::make_shared<Type>(Type::parse(s.substr(0, s.size() - 1), tps));
        return r;
    }
    // Array `T[N]`.
    if (s.back() == ']') {
        size_t open = matchOpenBracket(s);
        if (open != std::string::npos) {
            r.kind = Type::Kind::Array;
            r.elem = std::make_shared<Type>(Type::parse(s.substr(0, open), tps));
            r.dim  = s.substr(open + 1, s.size() - open - 2);
            return r;
        }
    }
    // Template application `Name<args>`.
    size_t lt = topLevelAngle(s);
    if (lt != std::string::npos && s.back() == '>') {
        r.kind = Type::Kind::Template;
        r.name = trim(s.substr(0, lt));
        std::string inner = s.substr(lt + 1, s.size() - lt - 2);
        if (!trim(inner).empty())
            for (auto& a : splitTop(inner, ',')) r.args.push_back(Type::parse(a, tps));
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

std::string Type::str() const {
    std::string body;
    switch (kind) {
        case Kind::Pointer:
            if (ptrLeading) body = "*" + pointee->str();
            else            body = pointee->str() + (bindingConst ? "*const" : "*");
            break;
        case Kind::Array:
            body = elem->str() + "[" + dim + "]";
            break;
        case Kind::Fn: {
            body = "fn(";
            for (size_t i = 0; i < params.size(); ++i) {
                if (i) body += ",";
                body += params[i].str();
            }
            body += ")->" + ret->str();
            break;
        }
        case Kind::Template: {
            body = name + "<";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) body += ",";
                body += args[i].str();
            }
            body += ">";
            break;
        }
        case Kind::Struct:    body = "struct:" + name; break;
        case Kind::Interface: body = "interface:" + name; break;
        default:              body = name; break;   // Int/Float/.../Named/Param/sentinels
    }
    return leadingQuals + body;
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

}  // namespace ty
