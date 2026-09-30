// C side of tests/pack_nested_c.esk: the same structs, laid out by the C compiler.
#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 2)
typedef struct { char a; int b; } P2;
typedef struct { char a; double d; } P2d;
#pragma pack(pop)
#pragma pack(push, 4)
typedef struct { char a; int64_t b; char c; } P4;
#pragma pack(pop)
#pragma pack(push, 8)
typedef struct { float f; double d; } P8;
#pragma pack(pop)
typedef struct { char c; P2 p; char e; } O1;
typedef struct { char c; P4 q; int16_t s; } O2;
typedef struct { char c; P2 arr[3]; char e; } O3;
typedef struct { int16_t h; O1 o; P8 z; } O4;
typedef union { char c; P4 q; } U;
typedef struct { char c; U u; } O5;
typedef struct { uint8_t a : 3; P4 q; uint8_t b : 2; } O6;
typedef struct { char c; P2d d; } O7;
#pragma pack(push, 2)
typedef struct { char c; P4 q; } PO;
#pragma pack(pop)
typedef struct { char c; P4 v; } G;

int64_t c_layout(int i) {
    int64_t v[] = {
        sizeof(P2), sizeof(P2d), sizeof(P4), sizeof(P8),
        sizeof(O1), offsetof(O1, p), offsetof(O1, e),
        sizeof(O2), offsetof(O2, q), offsetof(O2, s),
        sizeof(O3), offsetof(O3, arr), offsetof(O3, e),
        sizeof(O4), offsetof(O4, o), offsetof(O4, z),
        sizeof(U), sizeof(O5), offsetof(O5, u),
        sizeof(O6), offsetof(O6, q),
        sizeof(O7), offsetof(O7, d),
        sizeof(PO), offsetof(PO, q),
        sizeof(G), offsetof(G, v),
        sizeof(O4[2])
    };
    return v[i];
}

void c_fill_o4(O4* p) {
    p->h = -3; p->o.c = 1; p->o.p.a = 2; p->o.p.b = 70000; p->o.e = 4;
    p->z.f = 6.5f; p->z.d = 7.25;
}

int64_t c_sum_o2(O2* p) { return p->c + p->q.a + p->q.b + p->q.c + p->s; }
double c_take_p8(P8 v) { return v.f + v.d; }
int64_t c_take_p2(P2 v) { return v.a * 1000000 + v.b; }
int64_t c_take_o1(O1 v) { return v.c * 1000 + v.p.a * 100 + v.p.b + v.e; }
int64_t c_take_o2(O2 v) { return c_sum_o2(&v); }
O1 c_ret_o1(int k) { O1 r = { (char)k, { (char)(k + 1), k * 1000 }, (char)(k + 2) }; return r; }
P8 c_ret_p8(double d) { P8 r = { (float)(d * 3), d * 5 }; return r; }
