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

    if (failures == 0) std::printf("type round-trip: %zu spellings OK\n", corpus.size());
    else               std::printf("type round-trip: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
