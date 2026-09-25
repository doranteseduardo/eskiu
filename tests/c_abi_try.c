// C side of tests/c_abi_try.esk.
#include <stdint.h>

typedef struct { int x; int8_t c; } Sm;
typedef struct { int64_t a, b, c, d, e; } Big;

Sm c_mksm(int x) { Sm s = {x, 7}; return s; }
int64_t c_sum(Big b) { return b.a + b.b + b.c + b.d + b.e; }
Big c_mk(int64_t s) { Big b = {s, s + 1, s + 2, s + 3, s + 4}; return b; }
