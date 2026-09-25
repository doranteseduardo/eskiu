// C side of tests/c_abi_callback.esk: C calls Eskiu callbacks that take and return
// structs by value (register-sized, mixed int/char, HFA, mixed int/float, indirect and
// sret-sized aggregates, and a register-exhausting argument list).
#include <stdint.h>

typedef struct { int x, y; } P2;
typedef struct { int x; int8_t c; } Sm;
typedef struct { float x, y; } F2;
typedef struct { float a, b, c, d; } F4;
typedef struct { double x, y, z; } D3;
typedef struct { float f; int i; } Mix;
typedef struct { double d; int64_t l; } DL;
typedef struct { char a, b, c; } C3;
typedef struct { int64_t a, b, c, d, e; } Big;
typedef struct { const char* s; int n; } PS;

int c_call_p2(int (*cb)(P2), int x, int y) { P2 p = {x, y}; return cb(p); }
int c_call_sm(int (*cb)(Sm), int x, int c) { Sm s = {x, (int8_t)c}; return cb(s); }
float c_call_f2(float (*cb)(F2), float x) { F2 f = {x, x + 1}; return cb(f); }
float c_call_f4(float (*cb)(F4), float x) { F4 f = {x, x + 1, x + 2, x + 3}; return cb(f); }
double c_call_d3(double (*cb)(D3), double x) { D3 d = {x, x + 1, x + 2}; return cb(d); }
double c_call_mix(double (*cb)(Mix), float f, int i) { Mix m = {f, i}; return cb(m); }
double c_call_dl(double (*cb)(DL), double d, int64_t l) { DL v = {d, l}; return cb(v); }
int c_call_c3(int (*cb)(C3), char a) { C3 c = {a, (char)(a + 1), (char)(a + 2)}; return cb(c); }
int c_call_ps(int (*cb)(PS), int n) { PS p = {"xyz", n}; return cb(p); }
int c_ret_ps(PS (*cb)(int), int k) { PS p = cb(k); return p.n * 1000 + p.s[1]; }
int64_t c_call_big(int64_t (*cb)(Big), int64_t s) {
    Big b = {s, s + 1, s + 2, s + 3, s + 4};
    return cb(b);
}
int64_t c_call_many(int64_t (*cb)(Sm, Sm, Sm, Sm, Sm, Sm, Sm, Sm, Sm)) {
    Sm s[9];
    for (int i = 0; i < 9; ++i) { s[i].x = i + 1; s[i].c = (int8_t)(10 * (i + 1)); }
    return cb(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8]);
}
double c_call_mixed(double (*cb)(int, P2, double, F2, Big, int, DL)) {
    P2 p = {2, 3}; F2 f = {0.5f, 0.25f}; Big b = {1, 2, 3, 4, 5}; DL v = {1.5, 7};
    return cb(1, p, 4.0, f, b, 6, v);
}

int c_ret_p2(P2 (*cb)(int), int k) { P2 p = cb(k); return p.x * 100 + p.y; }
int c_ret_sm(Sm (*cb)(int), int k) { Sm s = cb(k); return s.x * 100 + s.c; }
float c_ret_f2(F2 (*cb)(float), float k) { F2 f = cb(k); return f.x * 10 + f.y; }
double c_ret_d3(D3 (*cb)(double), double k) { D3 d = cb(k); return d.x + d.y * 10 + d.z * 100; }
double c_ret_mix(Mix (*cb)(int), int k) { Mix m = cb(k); return m.f + m.i * 10; }
double c_ret_dl(DL (*cb)(int64_t), int64_t k) { DL v = cb(k); return v.d + (double)v.l; }
int c_ret_c3(C3 (*cb)(char), char k) { C3 c = cb(k); return c.a * 10000 + c.b * 100 + c.c; }
int64_t c_ret_big(Big (*cb)(int64_t), int64_t k) {
    Big b = cb(k);
    return b.a + b.b * 10 + b.c * 100 + b.d * 1000 + b.e * 10000;
}
int64_t c_ret_big2(Big (*cb)(Big, Sm), int64_t k) {
    Big in = {k, k, k, k, k}; Sm s = {3, 4};
    Big b = cb(in, s);
    return b.a + b.b + b.c + b.d + b.e;
}
