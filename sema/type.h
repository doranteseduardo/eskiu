#pragma once

// A structured, typed representation of an Eskiu type — the replacement for the
// ad-hoc `std::string` type surgery scattered across sema/ and codegen/.
//
// Stage 0 of the typed-Type migration (see the v0.2.3 plan): this is the pure,
// registry-free *syntactic* layer. `parse(s)` turns a canonical type spelling
// into a `Type`; `str()` renders it back. The invariant is an exact round-trip:
//   parse(s).str() == s   for every canonical spelling.
// To make that hold during migration, `str()` preserves the source spelling —
// the leading-vs-trailing pointer form, the int/float spelling (`int` vs
// `int32`), and `const`/`volatile` qualifiers (carried verbatim, NOT semantically
// resolved — const semantics stay in `ast/type_qual.h`/`tyq::`, unchanged).
//
// parse() does NOT consult registries: it does not mangle templates, add the
// `struct:` prefix, collapse classic enums to `int`, or resolve aliases. Those
// are normalization steps that belong to a later stage (a registry-taking
// `normalize`), kept separate so this layer is a faithful, testable round-trip.

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <functional>

namespace ty {

struct Type {
    enum class Kind {
        Void, Int, Float, Bool, Char, String,   // primitives (Int/Float carry spelling)
        Pointer, Array, Slice, Fn,               // composites (Slice = `T[]`, a {ptr,len} fat pointer)
        Struct, Interface, Template,             // nominal / generic (decorated spellings)
        Named, Param,                            // unresolved bare name / type parameter (T)
        VaList, Null, Unknown, Error             // builtins + in-band sentinels
    };

    Kind kind = Kind::Unknown;
    std::string name;            // Int/Float spelling; Struct/Interface/Named/Param/Template name
    std::string leadingQuals;    // verbatim leading qualifier prefix (e.g. "const ", "volatile ")

    // Pointer
    std::shared_ptr<Type> pointee;
    bool ptrLeading  = false;    // `*T` (true) vs `T*` (false) — spelling preserved
    bool bindingConst = false;   // `T*const`
    bool nullable    = false;    // `?*T` — a checked nullable pointer (deref requires a null-check)

    // Template
    std::vector<Type> args;

    // Fn
    std::vector<Type> params;
    std::shared_ptr<Type> ret;

    // Array
    std::shared_ptr<Type> elem;
    std::string dim;             // opaque text (enum-const / const-int); never substituted

    // --- construction / round-trip ---
    static Type parse(const std::string& s);
    // Same, but names in `typeParams` parse as Kind::Param instead of Kind::Named.
    static Type parse(const std::string& s, const std::set<std::string>& typeParams);
    std::string str() const;

    // Substitute type parameters using `subs` (e.g. T->int), recursively. Mirrors
    // the old free-function `substType`: a full-string hit on `str()` wins at each
    // node, else recurse into pointee / args / params / ret / elem.
    Type substitute(const std::map<std::string, std::string>& subs) const;

    // --- queries ---
    bool isPointer()   const { return kind == Kind::Pointer; }
    bool isStruct()    const { return kind == Kind::Struct; }
    bool isTemplate()  const { return kind == Kind::Template; }
    bool isFn()        const { return kind == Kind::Fn; }
    bool isArray()     const { return kind == Kind::Array; }
    bool isSlice()     const { return kind == Kind::Slice; }
    bool isParam()     const { return kind == Kind::Param; }
    // The undecorated nominal name: strips pointers and the struct:/interface:
    // decoration to the bare name used for method-mangling / registry lookups.
    // (`*struct:Point` → "Point", "struct:List_int" → "List_int", "int" → "int".)
    // Replaces the hand-rolled "strip struct: then leading/trailing *" surgery.
    std::string nominalName() const {
        if (kind == Kind::Pointer) return pointee->nominalName();
        return name;   // Struct/Interface/Named/Param/Template base, or leaf spelling
    }
    bool isPrimitive() const {
        switch (kind) {
            case Kind::Int: case Kind::Float: case Kind::Bool:
            case Kind::Char: case Kind::Void: return true;
            default: return false;
        }
    }
};

// `s` with every type alias in it replaced by its target (`aliases`: name -> spelling),
// recursively, inside pointers, arrays, template arguments and fn types: `Box<F>` with
// `type F = int` is `Box<int>`. `s` itself is returned when it names no alias.
std::string dealiasSpelling(const std::string& s, const std::map<std::string, std::string>& aliases);

// The loop-variable type of `for (i in a..b)` given the two bound types: C's usual
// arithmetic conversions over integers (each bound promoted to at least `int`, then the
// wider rank wins, and unsigned wins at equal rank). "" when a bound is not an integer.
std::string rangeVarType(const std::string& a, const std::string& b);

// A folded integer constant with its C type after the integer promotions: `rank` 32
// (int / uint) or 64 (int64 / uint64), `uns` for the unsigned ones. `v` holds the value
// in that type (a 32-bit one sign- or zero-extended). Constant folders (enum values,
// array dimensions, `case` labels) compute with these so they follow C's usual
// arithmetic conversions, 32-bit wraparound, unsigned comparison and division and the
// arithmetic shift of a signed value, as the generated code does.
struct CInt {
    long long v = 0;
    int rank = 32;
    bool uns = false;
};
// `v` as a value of the given type (wrapped to its width).
CInt cintMake(long long v, int rank, bool uns);
// An integer literal's value: `int` when it fits, else `int64` (`uint64` past it).
CInt cintLiteral(unsigned long long v);
// `(t)x` for an integer type `t` (`bool`, `char` and the int spellings); false otherwise.
bool cintCast(const std::string& t, const CInt& x, CInt& out);
// Unary `-`, `~`, `!`, `+`.
bool cintUnary(const std::string& op, const CInt& x, CInt& out);
// `x op y`; false when `op` does not fold or is undefined (division by zero, the most
// negative value divided by -1, a shift count outside 0..width-1).
bool cintBinary(const std::string& op, const CInt& x, const CInt& y, CInt& out);

// Fold an array dimension written as an integer constant expression. The parser keeps the
// tokens' text (`(uint8)258`, `N*2`): numbers, casts to an integer type (which truncate
// like C), unary and binary integer operators, `?:`, parentheses, and names, which `name`
// resolves (a `const` int or an enum member; false when it is not one; `sizeof(T)` reaches
// it as that whole text, the phase that knows the layout sizes it). False when `dim`
// is not such an expression (a division by zero included).
bool foldDim(const std::string& dim, const std::function<bool(const std::string&, long long&)>& name,
             long long& out);

}  // namespace ty
