#pragma once
#include <string>

// ---------------------------------------------------------------------------
// const-qualifier handling for Eskiu's string-encoded types.
//
// Canonical encoding (produced by the parser, consumed here):
//   const X        a read-only value of type X
//   const X*       a pointer to const X  (pointee read-only, pointer writable)
//   X*const        a const pointer to X  (pointer read-only, pointee writable)
//   const X*const  a const pointer to const X
//
// A leading "const " (with the trailing space) qualifies the base/pointee.
// A "*const" (a star immediately followed by const, no space) qualifies that
// pointer level. const has no runtime or ABI meaning, so codegen and the bulk
// of the type checker operate on the stripped form (tyq::strip); const survives
// only in the *stored* declared types, where the const-correctness checks read
// it back.
// ---------------------------------------------------------------------------
namespace tyq {

// Remove every const qualifier (base and per-pointer). The result is the type
// as the rest of the compiler (layout, codegen, arithmetic) sees it.
inline std::string strip(std::string t) {
    size_t p;
    while ((p = t.find("*const")) != std::string::npos) t.erase(p + 1, 5); // "*const" -> "*"
    if (t.rfind("const ", 0) == 0) t = t.substr(6);                        // leading "const "
    return t;
}

// True if the type carries a leading base/pointee const (`const X`, `const X*`).
inline bool baseConst(const std::string& t) {
    return t.rfind("const ", 0) == 0;
}

// True if assigning to a binding of this type is forbidden: a non-pointer
// `const X` value, or a pointer whose outermost level is const (`X*const`).
inline bool bindingConst(const std::string& t) {
    if (t.size() >= 6 && t.compare(t.size() - 6, 6, "*const") == 0) return true;
    return baseConst(t) && t.find('*') == std::string::npos;
}

// True if the *value denoted by* a type is read-only at its outermost level:
// a non-pointer `const X`. (A `const X*` is NOT — its outermost value is the
// writable pointer.) This is the test for "may I store into this lvalue?".
inline bool valueConst(const std::string& t) {
    return baseConst(t) && t.find('*') == std::string::npos;
}

// True if `t` is a pointer type (either spelling) ignoring const.
inline bool isPtr(const std::string& s) {
    std::string t = strip(s);
    return !t.empty() && (t.front() == '*' || t.back() == '*');
}

// The pointee type of a pointer, preserving the pointee's const. Removes one
// pointer level (and its `*const`, if present) from whichever end carries it.
inline std::string pointee(const std::string& s) {
    std::string t = s;
    if (t.size() >= 6 && t.compare(t.size() - 6, 6, "*const") == 0) return t.substr(0, t.size() - 6);
    if (!t.empty() && t.back() == '*')  return t.substr(0, t.size() - 1);
    if (!t.empty() && t.front() == '*') return t.substr(1);
    return t;
}

// True if assigning rhs to lhs would silently drop a const qualifier — both
// name the same stripped shape, but rhs's pointee is const and lhs's is not.
inline bool dropsConst(const std::string& lhs, const std::string& rhs) {
    if (strip(lhs) != strip(rhs)) return false;           // different shapes handled elsewhere
    if (!isPtr(lhs)) return false;                         // const on a copied value is fine
    return baseConst(pointee(rhs)) && !baseConst(pointee(lhs));
}

} // namespace tyq
