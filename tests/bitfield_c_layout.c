// C side of tests/bitfield_c_layout.esk: the same bitfield structs, laid out by the C
// compiler. Each c_fill_* writes fields through C, each c_sum_* reads them back, and
// c_sizes reports sizeof, so the Eskiu side can check it agrees on every bit.
#include <stdint.h>

struct A { uint8_t a : 4; uint32_t w : 12; };
struct B { uint16_t x : 3; uint8_t y : 2; uint32_t z : 20; };
struct C { uint8_t a : 4; uint8_t b : 6; uint32_t c : 30; char d; };
struct D { char c; uint32_t x : 20; uint16_t y : 9; };
struct F { uint8_t a : 3; int64_t big : 40; uint8_t t : 7; };
struct G { uint32_t a : 4; char c; int16_t k; };
struct H { char name[5]; uint32_t x : 4; };
struct I { uint16_t a : 4; char s[3]; int16_t k; };
struct S { int8_t s : 3; int32_t t : 20; int16_t u : 9; };
#pragma pack(push, 1)
struct P { uint8_t a : 4; uint32_t w : 12; };
struct Q { uint8_t a : 3; uint64_t v : 50; uint8_t b : 7; char e; };
#pragma pack(pop)
#pragma pack(push, 2)
struct R { uint8_t a : 4; uint32_t w : 12; char c; uint16_t k : 3; };
#pragma pack(pop)
struct N { char c; struct D d; char e; };
struct M { double d; uint8_t a : 3; uint32_t w : 20; };

int c_sizes(int i) {
    int s[] = { sizeof(struct A), sizeof(struct B), sizeof(struct C), sizeof(struct D),
                sizeof(struct F), sizeof(struct G), sizeof(struct H), sizeof(struct I),
                sizeof(struct S), sizeof(struct P), sizeof(struct Q), sizeof(struct N), sizeof(struct R) };
    return s[i];
}

void c_fill_a(struct A* p) { p->a = 9; p->w = 3000; }
void c_fill_b(struct B* p) { p->x = 5; p->y = 2; p->z = 999999; }
void c_fill_c(struct C* p) { p->a = 13; p->b = 45; p->c = 1000000007; p->d = 'q'; }
void c_fill_d(struct D* p) { p->c = 'Z'; p->x = 1048000; p->y = 300; }
void c_fill_f(struct F* p) { p->a = 6; p->big = -123456789012LL; p->t = 99; }
void c_fill_g(struct G* p) { p->a = 11; p->c = 'g'; p->k = -3000; }
void c_fill_h(struct H* p) { for (int i = 0; i < 5; ++i) p->name[i] = (char)('a' + i); p->x = 14; }
void c_fill_i(struct I* p) { p->a = 7; p->s[0] = 'x'; p->s[1] = 'y'; p->s[2] = 'z'; p->k = 1234; }
void c_fill_s(struct S* p) { p->s = -3; p->t = -500000; p->u = -200; }
void c_fill_p(struct P* p) { p->a = 10; p->w = 4001; }
void c_fill_q(struct Q* p) { p->a = 5; p->v = 1000000000000000ULL; p->b = 100; p->e = 'e'; }
void c_fill_r(struct R* p) { p->a = 12; p->w = 2222; p->c = 'r'; p->k = 6; }
void c_fill_n(struct N* p) { p->c = 'n'; c_fill_d(&p->d); p->e = 'E'; }

int64_t c_sum_a(const struct A* p) { return p->a * 100000 + p->w; }
int64_t c_sum_b(const struct B* p) { return p->x * 10000000 + p->y * 1000000 + p->z; }
int64_t c_sum_c(const struct C* p) { return (int64_t)p->a * 100 + p->b + (int64_t)p->c * 1000 + p->d; }
int64_t c_sum_d(const struct D* p) { return (int64_t)p->c * 100000000 + (int64_t)p->x * 10 + p->y; }
int64_t c_sum_f(const struct F* p) { return p->a + p->big * 1000 + p->t * 10; }
int64_t c_sum_g(const struct G* p) { return p->a * 1000000 + p->c * 10000 + p->k; }
int64_t c_sum_h(const struct H* p) { int64_t s = 0; for (int i = 0; i < 5; ++i) s = s * 100 + p->name[i]; return s * 100 + p->x; }
int64_t c_sum_i(const struct I* p) { return p->a * 100000000LL + p->s[0] * 1000000 + p->s[1] * 10000 + p->s[2] * 100 + p->k; }
int64_t c_sum_s(const struct S* p) { return p->s * 1000000000LL + p->t * 1000LL + p->u; }
int64_t c_sum_p(const struct P* p) { return p->a * 100000 + p->w; }
int64_t c_sum_q(const struct Q* p) { return (int64_t)(p->v / 1000000) + p->a * 10 + p->b * 1000 + p->e; }
int64_t c_sum_r(const struct R* p) { return p->a * 10000000 + p->w * 1000 + p->c * 10 + p->k; }
// By value across the C ABI: the classification sees the C layout.
int64_t c_byval_d(struct D d) { return c_sum_d(&d); }
struct D c_mk_d(int k) { struct D d; d.c = 'M'; d.x = (uint32_t)k; d.y = 257; return d; }
double c_byval_m(struct M m) { return m.d + m.a * 1000 + m.w; }
struct M c_mk_m(int k) { struct M m; m.d = 0.5; m.a = 5; m.w = (uint32_t)k; return m; }
int64_t c_sum_n(const struct N* p) { return p->c * 1000 + c_sum_d(&p->d) % 1000 + p->e; }
