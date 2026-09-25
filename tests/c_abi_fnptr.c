// C side of tests/c_abi_fnptr.esk: C functions taking C function pointers, called from
// Eskiu with top-level functions (some taking and returning structs by value).
#include <stddef.h>
#include <stdint.h>

typedef struct { int x, y; } P2;
typedef struct { float x, y; } F2;
typedef struct { int64_t a, b, c, d, e; } Big;

int c_apply_p2(int (*cb)(P2), int x, int y) { P2 p = {x, y}; return cb(p); }
float c_apply_f2(float (*cb)(F2), float x) { F2 f = {x, x * 2}; return cb(f); }
int64_t c_make_big(Big (*cb)(int64_t), int64_t k) {
    Big b = cb(k);
    return b.a + b.b + b.c + b.d + b.e;
}
int c_count(int (*pred)(int), int n) {
    int c = 0;
    for (int i = 0; i < n; ++i) if (pred(i)) ++c;
    return c;
}
int c_maybe(int (*cb)(int), int v) { return cb ? cb(v) : -1; }
