#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>

// ============================================================================
// Template type-name utilities — shared by the type checker and codegen.
// These operate purely on type-name strings (e.g. "Result<int,string>"); they
// were previously duplicated verbatim in sema/ and codegen/.
// ============================================================================

// "Result<int,string>" -> "Result_int_string"
inline std::string mangleTemplate(const std::string& type) {
    std::string out;
    for (char c : type) {
        if (c == '<' || c == '>' || c == ',') out += '_';
        else if (c != ' ')                   out += c;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

// "Result<int,string>" -> {"Result", {"int","string"}}
inline std::pair<std::string, std::vector<std::string>>
splitTemplateType(const std::string& type) {
    size_t lt = type.find('<');
    if (lt == std::string::npos) return {type, {}};
    std::string name = type.substr(0, lt);
    std::string inner = type.substr(lt + 1, type.size() - lt - 2);
    std::vector<std::string> args;
    int depth = 0; std::string cur;
    for (char c : inner) {
        if (c == '<') { depth++; cur += c; }
        else if (c == '>') { depth--; cur += c; }
        else if (c == ',' && depth == 0) { args.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) args.push_back(cur);
    return {name, args};
}

// Substitute type parameters in `t` using `subs` (e.g. T->int), recursively
// through pointers, arrays, and nested template types.
inline std::string substType(const std::string& t,
                             const std::map<std::string, std::string>& subs) {
    auto it = subs.find(t);
    if (it != subs.end()) return it->second;
    if (!t.empty() && t.front() == '*') return "*" + substType(t.substr(1), subs);
    if (!t.empty() && t.back()  == '*') return substType(t.substr(0, t.size()-1), subs) + "*";
    size_t lb = t.rfind('[');
    if (lb != std::string::npos && t.back() == ']')
        return substType(t.substr(0, lb), subs) + t.substr(lb);
    // Template type: Name<T, E> → substitute type args
    size_t lt = t.find('<');
    if (lt != std::string::npos && t.back() == '>') {
        std::string name  = t.substr(0, lt);
        std::string inner = t.substr(lt + 1, t.size() - lt - 2);
        std::vector<std::string> args;
        int depth = 0; std::string cur;
        for (char c : inner) {
            if      (c == '<') { depth++; cur += c; }
            else if (c == '>') { depth--; cur += c; }
            else if (c == ',' && depth == 0) { args.push_back(substType(cur, subs)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) args.push_back(substType(cur, subs));
        std::string result = name + "<";
        for (size_t i = 0; i < args.size(); ++i) { if (i) result += ","; result += args[i]; }
        return result + ">";
    }
    return t;
}
