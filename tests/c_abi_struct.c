// C side of tests/c_abi_struct.esk: structs passed and returned by value across the
// C ABI (register-sized, HFA, mixed int/float, indirect and sret-sized aggregates).
#include <stdint.h>

typedef struct { int64_t a, b, c, d, e; } Big;
typedef struct { int x; int8_t c; } Sm;
typedef struct { float x, y; } F2;
typedef struct { double x, y, z; } D3;
typedef struct { float a, b, c, d; } F4;
typedef struct { float f; int i; } Mix;
typedef struct { int x, y, z; } P3;
typedef struct { char a, b, c; } C3;
typedef struct { double d; int64_t l; } DL;
typedef struct { float f; double d; } FD;
typedef struct { int tag; union { int64_t l; double d; } u; } TU;
typedef struct { uint32_t a : 3; uint32_t b : 5; uint32_t c : 24; } BF;
typedef struct __attribute__((packed)) { int8_t t; int64_t v; } PK;
typedef struct { Sm s; int k; } Nest;

int64_t c_sum(Big b) { return b.a + b.b + b.c + b.d + b.e; }
Big c_mk(int64_t s) { Big b = {s, s + 1, s + 2, s + 3, s + 4}; return b; }
int c_sm(Sm s) { return s.x * 10 + s.c; }
Sm c_mksm(int x) { Sm s = {x, 7}; return s; }
float c_f2(F2 f) { return f.x * f.y; }
F2 c_mkf2(float a) { F2 f = {a, a * 2}; return f; }
double c_d3(D3 d) { return d.x + d.y * 10 + d.z * 100; }
D3 c_mkd3(double a) { D3 d = {a, a + 1, a + 2}; return d; }
float c_f4(F4 f) { return f.a + f.b * 10 + f.c * 100 + f.d * 1000; }
F4 c_mkf4(float a) { F4 f = {a, a + 1, a + 2, a + 3}; return f; }
double c_mix(Mix m) { return m.f + m.i; }
Mix c_mkmix(int i) { Mix m = {0.5f, i}; return m; }
int c_p3(P3 p) { return p.x + p.y * 10 + p.z * 100; }
P3 c_mkp3(int b) { P3 p = {b, b + 1, b + 2}; return p; }
int c_c3(C3 c) { return c.a + c.b * 2 + c.c * 3; }
C3 c_mkc3(char b) { C3 c = {b, (char)(b + 1), (char)(b + 2)}; return c; }
double c_dl(DL v) { return v.d + (double)v.l; }
DL c_mkdl(int64_t l) { DL v = {0.25, l}; return v; }
double c_fd(FD v) { return v.f + v.d; }
FD c_mkfd(double d) { FD v = {1.5f, d}; return v; }
int64_t c_tu(TU t) { return t.tag == 0 ? t.u.l : (int64_t)t.u.d; }
TU c_mktu(int64_t l) { TU t; t.tag = 0; t.u.l = l; return t; }
int c_bf(BF b) { return (int)(b.a + b.b * 10 + b.c * 1000); }
BF c_mkbf(int c) { BF b; b.a = 5; b.b = 17; b.c = (uint32_t)c; return b; }
int64_t c_pk(PK p) { return p.t + p.v; }
PK c_mkpk(int64_t v) { PK p = {3, v}; return p; }
int c_nest(Nest n) { return n.s.x + n.s.c + n.k; }
Nest c_mknest(int k) { Nest n = {{k, 2}, k * 3}; return n; }

// Register pressure: nine 8-byte structs + scalars (later ones go on the stack).
int64_t c_many(Sm a, Sm b, Sm c, Sm d, Sm e, Sm f, Sm g, Sm h, Sm i) {
    return (int64_t)a.x + b.x * 2 + c.x * 3 + d.x * 4 + e.x * 5 + f.x * 6 + g.x * 7
         + h.x * 8 + i.x * 9 + a.c + i.c;
}
double c_mixed(int a, P3 p, double d, F2 f, Big b, int z, DL v) {
    return a + c_p3(p) + d + f.x + f.y + (double)c_sum(b) + z + v.d + (double)v.l;
}
Big c_mkbig2(Sm s, double scale) {
    Big b = {s.x, s.c, (int64_t)scale, s.x * 2, s.c * 2};
    return b;
}
