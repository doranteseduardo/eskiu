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

}  // namespace ty
