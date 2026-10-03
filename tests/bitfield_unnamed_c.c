// C side of tests/bitfield_unnamed_c.esk: the same structs with unnamed bitfields, laid
// out by the C compiler. c_fill_* writes the named fields, c_sum_* reads them back,
// c_info reports sizeof and alignment, and the by-value functions cross the C ABI.
#include <stdint.h>

struct A { uint8_t a : 3; uint32_t : 0; uint8_t b : 2; };
struct B { char c; int : 4; char d; };
struct C { uint32_t x : 5; uint16_t : 7; uint32_t y : 9; uint64_t : 0; uint8_t z; };
struct D { char c; long long : 0; char d; };
struct E { uint16_t : 9; uint8_t v; uint32_t : 30; uint8_t w : 4; };
struct F { double d; uint8_t : 3; uint32_t k : 20; };
struct G { uint8_t a : 2; uint8_t : 0; int : 0; uint8_t b : 2; };
#pragma pack(push, 1)
struct P { char c; int : 0; uint8_t a : 3; uint32_t : 0; uint8_t b : 4; };
#pragma pack(pop)
#pragma pack(push, 2)
struct Q { char c; int : 4; uint16_t k : 3; long long : 0; char e; };
#pragma pack(pop)
union U { uint8_t b; uint32_t : 20; };
union V { uint16_t h; uint8_t : 4; long long : 0; };

static int al_a(void) { struct W { char c; struct A s; }; return sizeof(struct W) - sizeof(struct A); }
static int al_b(void) { struct W { char c; struct B s; }; return sizeof(struct W) - sizeof(struct B); }
static int al_c(void) { struct W { char c; struct C s; }; return sizeof(struct W) - sizeof(struct C); }
static int al_d(void) { struct W { char c; struct D s; }; return sizeof(struct W) - sizeof(struct D); }
static int al_e(void) { struct W { char c; struct E s; }; return sizeof(struct W) - sizeof(struct E); }
static int al_f(void) { struct W { char c; struct F s; }; return sizeof(struct W) - sizeof(struct F); }
static int al_g(void) { struct W { char c; struct G s; }; return sizeof(struct W) - sizeof(struct G); }
static int al_p(void) { struct W { char c; struct P s; }; return sizeof(struct W) - sizeof(struct P); }
static int al_q(void) { struct W { char c; struct Q s; }; return sizeof(struct W) - sizeof(struct Q); }
static int al_u(void) { struct W { char c; union U s; }; return sizeof(struct W) - sizeof(union U); }
static int al_v(void) { struct W { char c; union V s; }; return sizeof(struct W) - sizeof(union V); }

int c_info(int i) {
    int s[] = { sizeof(struct A), sizeof(struct B), sizeof(struct C), sizeof(struct D),
                sizeof(struct E), sizeof(struct F), sizeof(struct G), sizeof(struct P),
                sizeof(struct Q), sizeof(union U), sizeof(union V),
                al_a(), al_b(), al_c(), al_d(), al_e(), al_f(), al_g(), al_p(), al_q(), al_u(), al_v() };
    return s[i];
}

void c_fill_a(struct A* p) { p->a = 5; p->b = 2; }
void c_fill_b(struct B* p) { p->c = 'b'; p->d = 'B'; }
void c_fill_c(struct C* p) { p->x = 21; p->y = 300; p->z = 77; }
void c_fill_d(struct D* p) { p->c = 'd'; p->d = 'D'; }
void c_fill_e(struct E* p) { p->v = 99; p->w = 9; }
void c_fill_f(struct F* p) { p->d = 1.5; p->k = 765432; }
void c_fill_g(struct G* p) { p->a = 3; p->b = 1; }
void c_fill_p(struct P* p) { p->c = 'p'; p->a = 6; p->b = 13; }
void c_fill_q(struct Q* p) { p->c = 'q'; p->k = 5; p->e = 'Q'; }

int64_t c_sum_a(const struct A* p) { return p->a * 10 + p->b; }
int64_t c_sum_b(const struct B* p) { return p->c * 1000 + p->d; }
int64_t c_sum_c(const struct C* p) { return p->x * 1000000 + p->y * 1000 + p->z; }
int64_t c_sum_d(const struct D* p) { return p->c * 1000 + p->d; }
int64_t c_sum_e(const struct E* p) { return p->v * 100 + p->w; }
int64_t c_sum_f(const struct F* p) { return (int64_t)(p->d * 10) * 10000000 + p->k; }
int64_t c_sum_g(const struct G* p) { return p->a * 10 + p->b; }
int64_t c_sum_p(const struct P* p) { return p->c * 10000 + p->a * 100 + p->b; }
int64_t c_sum_q(const struct Q* p) { return p->c * 10000 + p->k * 1000 + p->e; }

// By value across the C ABI.
int64_t c_byval_b(struct B b) { return c_sum_b(&b); }
int64_t c_byval_c(struct C c) { return c_sum_c(&c); }
int64_t c_byval_f(struct F f) { return c_sum_f(&f); }
int64_t c_byval_u(union U u) { return u.b; }
int64_t c_byval_v(union V v) { return v.h; }
struct C c_mk_c(int k) { struct C c = {0}; c.x = 3; c.y = (uint32_t)k; c.z = 4; return c; }
struct F c_mk_f(int k) { struct F f = {0}; f.d = 2.5; f.k = (uint32_t)k; return f; }
union U c_mk_u(int k) { union U u = {0}; u.b = (uint8_t)k; return u; }

// By value with unnamed bitfields in the way of the register classification: an eightbyte
// holding only unnamed bits takes no register on x86-64, an unnamed bitfield's bytes count
// toward the integer widths, and a union or struct holding one is no FP aggregate.
struct X1 { double d; uint32_t : 20; };
struct X2 { float f; int : 0; };
struct X3 { uint64_t : 60; uint8_t x; };
struct X4 { float f; uint8_t : 4; float g; };
struct X5 { int a; uint64_t : 0; int b; };
struct X6 { uint8_t a; uint16_t : 9; };
struct X7 { double d; int : 0; };
struct X8 { char c; int : 4; };
union X9 { float f; int : 9; };
union X10 { double d; uint64_t : 60; };
struct X11 { float f; uint32_t : 32; float g; };
int64_t c_byval_x1(struct X1 v) { return (int64_t)(v.d * 10); }
int64_t c_byval_x2(struct X2 v) { return (int64_t)(v.f * 10); }
int64_t c_byval_x3(struct X3 v) { return v.x; }
int64_t c_byval_x4(struct X4 v) { return (int64_t)(v.f * 10) * 1000 + (int64_t)(v.g * 10); }
int64_t c_byval_x5(struct X5 v) { return v.a * 1000 + v.b; }
int64_t c_byval_x6(struct X6 v) { return v.a; }
int64_t c_byval_x7(struct X7 v) { return (int64_t)(v.d * 10); }
int64_t c_byval_x8(struct X8 v) { return v.c; }
int64_t c_byval_x9(union X9 v) { return (int64_t)(v.f * 10); }
int64_t c_byval_x10(union X10 v) { return (int64_t)(v.d * 10); }
int64_t c_byval_x11(struct X11 v) { return (int64_t)(v.f * 10) * 1000 + (int64_t)(v.g * 10); }
int64_t c_byval_x3x1(int a, struct X3 v, struct X1 w, int b) { return a * 1000000 + v.x * 1000 + (int64_t)w.d * 10 + b; }
struct X1 c_mk_x1(int k) { struct X1 v = {0}; v.d = k + 0.5; return v; }
struct X2 c_mk_x2(int k) { struct X2 v = {0}; v.f = k + 0.5f; return v; }
struct X3 c_mk_x3(int k) { struct X3 v = {0}; v.x = (uint8_t)k; return v; }
struct X4 c_mk_x4(int k) { struct X4 v = {0}; v.f = 1.5f; v.g = k + 0.5f; return v; }
struct X5 c_mk_x5(int k) { struct X5 v = {0}; v.a = 7; v.b = k; return v; }
struct X6 c_mk_x6(int k) { struct X6 v = {0}; v.a = (uint8_t)k; return v; }
struct X7 c_mk_x7(int k) { struct X7 v = {0}; v.d = k + 0.5; return v; }
struct X8 c_mk_x8(int k) { struct X8 v = {0}; v.c = (char)k; return v; }
union X9 c_mk_x9(int k) { union X9 v = {0}; v.f = k + 0.5f; return v; }
union X10 c_mk_x10(int k) { union X10 v = {0}; v.d = k + 0.5; return v; }
struct X11 c_mk_x11(int k) { struct X11 v = {0}; v.f = 2.5f; v.g = k + 0.5f; return v; }
