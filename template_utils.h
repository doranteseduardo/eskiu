#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include "sema/type.h"

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
// through pointers, arrays, function types, and nested templates. Delegates to
// the structured `Type` IR (`Type::substitute`) — one parser/renderer instead of
// the hand-rolled string surgery this used to be. The names appearing as `subs`
// keys parse as type parameters so they substitute structurally.
inline std::string substType(const std::string& t,
                             const std::map<std::string, std::string>& subs) {
    std::set<std::string> keys;
    for (const auto& kv : subs) keys.insert(kv.first);
    return ty::Type::parse(t, keys).substitute(subs).str();
}
