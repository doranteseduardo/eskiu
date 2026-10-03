#!/usr/bin/env bash
#
# Generate the deep-input tests into OUTDIR: programs too large to check in, that
# exercise long operator chains (handled by loops in every pass) and deep nesting
# (handled on the compiler's large stack, up to the parser's nesting limit).
#
#   NAME.esk + NAME.expected   run test: compile, run, stdout must match
#   NAME.esk                   error test: first line is `// EXPECT-ERROR: <text>`
#
# Used by tests/run.sh (C++ compiler) and tests/selfhost/driver_parity.sh (both).
# Usage: tests/deep/gen.sh OUTDIR
set -eu
out="${1:?usage: gen.sh OUTDIR}"
mkdir -p "$out"

# rep STR N: STR repeated N times.
rep() { awk -v s="$1" -v n="$2" 'BEGIN { for (i = 0; i < n; i++) printf "%s", s }'; }

run_test() { # name expected-output < program
    cat > "$out/$1.esk"
    printf '%s\n' "$2" > "$out/$1.expected"
}

# A 100000-operand chain.
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    printf '    int x = 1'; rep ' + 1' 99999; echo ';'
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test chain_100k 100000

# A 20000-operand `&&` chain (each right operand short-circuits).
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    echo '    int a = 1;'
    printf '    bool b = a == 1'; rep ' && a == 1' 19999; echo ';'
    echo '    if (b) { printf("yes\n"); }'
    echo '    return 0;'
    echo '}'
} | run_test and_chain_20k yes

# Long chains inside a generic body (operand types derived per instantiation) and an
# async body (rewritten by the async transform).
{
    echo 'import <future>;'
    echo 'extern int printf(string fmt, ...);'
    printf 'T sum<T>(T a) { return a'; rep ' + a' 9999; echo '; }'
    echo 'Future<int>* ready(int v) {'
    echo '    Future<int>* f = future_new<int>();'
    echo '    future_complete<int>(f, v);'
    echo '    return f;'
    echo '}'
    echo 'async int twice(int k) {'
    echo '    int n = await ready(k);'
    printf '    return n'; rep ' + n - n' 5000; echo ' + n;'
    echo '}'
    echo 'int main() {'
    echo '    Future<int>* r = twice(21);'
    echo '    printf("%d %d\n", sum<int>(1), r.value);'
    echo '    free_future<int>(r);'
    echo '    return 0;'
    echo '}'
} | run_test generic_async_chain "10000 42"

# A 10000-link `else if` chain.
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    echo '    int x = 9999;'
    echo '    int r = -1;'
    awk 'BEGIN { printf "    if (x == 0) { r = 0; }"; for (i = 1; i < 10000; i++) printf " else if (x == %d) { r = %d; }", i, i; print "" }'
    echo '    printf("%d\n", r);'
    echo '    return 0;'
    echo '}'
} | run_test else_if_10k 9999

# 10000 nested parentheses.
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    printf '    int x = '; rep '(' 10000; printf '7'; rep ')' 10000; echo ';'
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test parens_10k 7

# 10000 binary expressions nested on the right (each typed once, in both compilers).
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    echo '    int a = 1;'
    printf '    int x = '; rep 'a + (' 10000; printf 'a'; rep ')' 10000; echo ';'
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test nested_binary_10k 10001

# 10000 nested blocks, and 10000 nested ifs.
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    echo '    int x = 0;'
    printf '    '; rep '{ ' 10000; printf 'x = x + 1;'; rep ' }' 10000; echo
    printf '    '; rep 'if (x == 1) { ' 10000; printf 'x = 5;'; rep ' }' 10000; echo
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test blocks_10k 5

# 5000 ternaries nested in their then-arms (each `?` finds its `:` in constant time).
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    echo '    int a = 1;'
    printf '    int x = '; rep 'a == 1 ? ' 5000; printf '7'; rep ' : 0' 5000; echo ';'
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test ternary_then_5k 7

# 20000 ternaries nesting alternately in then- and else-arms (each `?:` typed once, not
# once per enclosing level: quadratic, this took minutes in the self-hosted compiler).
{
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    echo '    int a = 1;'
    printf '    int x = '; rep 'a == 1 ? a == 0 ? 0 : ' 10000; printf '7'; rep ' : 0' 10000; echo ';'
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test ternary_mixed_20k 7

# A 50000-link member chain (each base typed once, not once per level).
{
    echo 'extern int printf(string fmt, ...);'
    echo 'struct S { int n; *S s; }'
    echo 'int main() {'
    echo '    S s; s.n = 4; s.s = &s;'
    printf '    int x = s'; rep '.s' 50000; echo '.n;'
    echo '    printf("%d\n", x);'
    echo '    return 0;'
    echo '}'
} | run_test member_chain_50k 4

# 5000 nested-generic declarations, each closed by a lexed `>>` (split in O(1)).
{
    echo 'import <list>;'
    echo 'extern int printf(string fmt, ...);'
    echo 'int main() {'
    awk 'BEGIN { for (i = 0; i < 5000; i++) printf "    List<List<int>>* v%d = null;\n", i }'
    echo '    printf("%d\n", (int)(v4999 == null));'
    echo '    return 0;'
    echo '}'
} | run_test rshift_close_5k 1

# Types just under the 1000-level type nesting limit (the innermost type counts one).
{
    echo 'extern int printf(string fmt, ...);'
    echo 'struct B<T> { T v; }'
    echo 'int main() {'
    printf '    '; rep '*' 999; echo 'int p = null;'
    printf '    int'; rep '[1]' 999; echo ' a;'
    printf '    '; rep 'fn()->' 999; echo 'int f;'
    printf '    '; rep 'B<' 300; printf 'int'; rep '>' 300; echo ' b;'
    echo '    printf("%d\n", (int)(p == null));'
    echo '    return 0;'
    echo '}'
} | run_test types_deep_999 1

# Past the nesting limit: a clean error, not a stack overflow.
{
    echo '// EXPECT-ERROR: nesting too deep'
    echo 'int main() {'
    printf '    int x = '; rep '(' 34000; printf '1'; rep ')' 34000; echo ';'
    echo '    return x;'
    echo '}'
} > "$out/nesting_too_deep.esk"
{
    echo '// EXPECT-ERROR: nesting too deep'
    echo 'int main() {'
    printf '    '; rep '{ ' 100001; rep ' }' 100001; echo
    echo '    return 0;'
    echo '}'
} > "$out/blocks_too_deep.esk"
# A cast chain past the limit: the error surfaces from the speculative cast parse.
{
    echo '// EXPECT-ERROR: nesting too deep'
    echo 'int main() {'
    echo '    int a = 3;'
    printf '    int x = '; rep '(int)' 100001; echo 'a;'
    echo '    return x;'
    echo '}'
} > "$out/casts_too_deep.esk"
{
    echo '// EXPECT-ERROR: type nesting too deep'
    echo 'struct B<T> { T v; }'
    echo 'int main() {'
    printf '    '; rep 'B<' 100001; printf 'int'; rep '>' 100001; echo ' b;'
    echo '    return 0;'
    echo '}'
} > "$out/generic_too_deep.esk"
