// Stage-0 round-trip test for the typed `Type` IR (sema/type.{h,cpp}).
// Invariant 1 (structural): parse(s).str() == s for every canonical spelling.
// Invariant 2 (idempotence): parse(str(parse(s))).str() == parse(s).str().
//
// type.cpp has no LLVM / project dependencies, so this links standalone:
//   clang++ -std=c++17 roundtrip_test.cpp ../../sema/type.cpp -I../../sema -o rt && ./rt
#include "type.h"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void check(const std::string& s) {
    std::string got = ty::Type::parse(s).str();
    if (got != s) {
        std::printf("FAIL round-trip: parse(%s).str() = %s\n", s.c_str(), got.c_str());
        ++failures;
        return;
    }
    // idempotence through the IR
    std::string twice = ty::Type::parse(got).str();
    if (twice != got) {
        std::printf("FAIL idempotence: %s -> %s\n", s.c_str(), twice.c_str());
        ++failures;
    }
}

int main() {
    const std::vector<std::string> corpus = {
        // primitives (spelling preserved: int != int32)
        "void", "int", "int8", "int16", "int32", "int64",
        "uint", "uint8", "uint16", "uint32", "uint64",
        "char", "bool", "float", "double", "string", "va_list",
        // sentinels
        "null", "unknown", "error",
        // unresolved nominal
        "Color", "MyEnum",
        // pointers — leading, trailing, multi-level, mixed
        "*int", "int*", "**int", "int**", "*int*",
        "*Point", "Point*", "*string",
        // decorated structs / interfaces
        "struct:Point", "*struct:Point", "struct:Point*", "struct:List_int",
        "interface:Drawable", "*interface:Ord",
        // const / volatile (preserved positionally)
        "const int", "const int*", "int*const", "const int*const",
        "const *int", "volatile int", "const volatile int", "volatile int*",
        // templates (canonical = no spaces after commas)
        "List<int>", "Map<string,bool>", "Result<int,string>",
        "Option<int>", "List<Point>", "Map<string,List<int>>",
        "*List<int>", "List<int>*", "Box<struct:Point>",
        // function types — nested, callback params, pointer/template ret
        "fn(int)->void", "fn()->void", "fn(int,string)->bool",
        "fn(int)->int*", "fn(*K)->uint64", "fn(fn(int)->void)->int",
        "fn(int)->fn(string)->bool", "fn(int)->*Future<int>",
        // arrays — literal + symbolic dims, with pointers/structs
        "int[10]", "int[MAX]", "struct:Point[8]", "*int[4]", "int[N]",
        "int[2][3]", "T[2][2][3]", "int[3][]", "*int[2][5]",
    };

    for (const auto& s : corpus) check(s);

    // Param-vs-Named: a name in the typeParams set parses as Param, else Named.
    {
        ty::Type p = ty::Type::parse("T", {"T"});
        if (!p.isParam()) { std::printf("FAIL: T should be Param\n"); ++failures; }
        ty::Type n = ty::Type::parse("T", {});
        if (n.kind != ty::Type::Kind::Named) { std::printf("FAIL: T should be Named\n"); ++failures; }
        // Param round-trips its spelling too.
        if (p.str() != "T") { std::printf("FAIL: Param str\n"); ++failures; }
    }

    // substitute(): structural type-param substitution (the substType engine).
    {
        std::map<std::string, std::string> s1 = {{"T", "int"}};
        auto sub = [](const std::string& t, const std::map<std::string,std::string>& m) {
            std::set<std::string> keys; for (auto& kv : m) keys.insert(kv.first);
            return ty::Type::parse(t, keys).substitute(m).str();
        };
        struct Case { std::string in; std::map<std::string,std::string> subs; std::string want; };
        std::vector<Case> cases = {
            {"T", {{"T","int"}}, "int"},
            {"*T", {{"T","int"}}, "*int"},
            {"T*", {{"T","int"}}, "int*"},
            {"T[8]", {{"T","int"}}, "int[8]"},          // dim untouched
            {"T[2][3]", {{"T","int"}}, "int[2][3]"},    // C order kept
            {"List<T>", {{"T","int"}}, "List<int>"},
            {"fn(*T)->T", {{"T","int"}}, "fn(*int)->int"},
            {"Map<K,V>", {{"K","string"},{"V","bool"}}, "Map<string,bool>"},
            {"fn(K,V)->K", {{"K","int"},{"V","char"}}, "fn(int,char)->int"},
            {"List<Box<T>>", {{"T","int"}}, "List<Box<int>>"},
            {"U", {{"T","int"}}, "U"},                   // no key → unchanged
        };
        for (auto& c : cases) {
            std::string got = sub(c.in, c.subs);
            if (got != c.want) {
                std::printf("FAIL substitute: %s -> %s (want %s)\n", c.in.c_str(), got.c_str(), c.want.c_str());
                ++failures;
            }
        }
    }

    // A fn type among several template arguments: the `>` of `->` closes nothing.
    {
        ty::Type t = ty::Type::parse("A<B<fn()->int,C>>");
        if (t.args.size() != 1 || t.args[0].args.size() != 2 || t.args[0].args[1].str() != "C") {
            std::printf("FAIL: A<B<fn()->int,C>> splits its arguments wrong\n"); ++failures;
        }
        check("Map<fn(int)->int,string>");
    }

    // Array shapes the linear parser must build like the reference grammar: the leftmost
    // bracket is the outer dimension, and an array suffix binds before `*` and `fn`.
    {
        ty::Type m = ty::Type::parse("int[2][3]");
        if (m.kind != ty::Type::Kind::Array || m.dim != "2" || !m.elem ||
            m.elem->kind != ty::Type::Kind::Array || m.elem->dim != "3") {
            std::printf("FAIL: int[2][3] is not 2 arrays of 3\n"); ++failures;
        }
        ty::Type p = ty::Type::parse("*int[3]");
        if (p.kind != ty::Type::Kind::Array || !p.elem || p.elem->kind != ty::Type::Kind::Pointer) {
            std::printf("FAIL: *int[3] is not an array of pointers\n"); ++failures;
        }
        ty::Type f = ty::Type::parse("fn(int)->int[2]");
        if (f.kind != ty::Type::Kind::Array || !f.elem || f.elem->kind != ty::Type::Kind::Fn) {
            std::printf("FAIL: fn(int)->int[2] is not an array of fns\n"); ++failures;
        }
        ty::Type s = ty::Type::parse("int[][4]");
        if (s.kind != ty::Type::Kind::Slice || !s.elem || s.elem->dim != "4") {
            std::printf("FAIL: int[][4] is not a slice of int[4]\n"); ++failures;
        }
        for (const char* sp : {"int[2][3]", "*int[3]", "fn(int)->int[2]", "int[][4]", "Box<int[2]>[3]",
                               "int[K < 4 ? 2 : 1]", "const int[2][3]", "int[3]*"})
            check(sp);
    }

    // Deep spellings round-trip (parse and str run in time linear in the length).
    {
        const int n = 1000;
        std::string tmpl, arr = "int", ptr, fn;
        for (int i = 0; i < n; ++i) tmpl += "B<";
        tmpl += "int";
        for (int i = 0; i < n; ++i) tmpl += ">";
        for (int i = 0; i < n; ++i) arr += "[1]";
        for (int i = 0; i < n; ++i) ptr += "*";
        ptr += "int";
        for (int i = 0; i < n; ++i) fn += "fn()->";
        fn += "int";
        for (const std::string& s : {tmpl, arr, ptr, fn}) {
            if (ty::Type::parse(s).str() != s) {
                std::printf("FAIL deep round-trip: %.20s...\n", s.c_str()); ++failures;
            }
        }
    }

    if (failures == 0) std::printf("type round-trip: %zu spellings OK\n", corpus.size());
    else               std::printf("type round-trip: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
