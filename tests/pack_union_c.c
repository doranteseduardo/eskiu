// C side of tests/pack_union_c.esk: the same `#pragma pack` unions, laid out by the C
// compiler, filled through pointers and passed and returned by value.
#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 2)
typedef union { char c[5]; int x; } U2;
typedef union { char c[3]; double d; } U2d;
#pragma pack(pop)
#pragma pack(push, 1)
typedef union { char c[5]; int x; } U1;
#pragma pack(pop)
#pragma pack(push, 4)
typedef union { char c[9]; int64_t v; } U4;
#pragma pack(pop)
typedef struct { char a; U2 u; char b; } S2;
typedef struct { char a; U1 u; char b; } S1;
typedef struct { char a; U4 u; U2d w; } S4;

int64_t c_layout(int i) {
    int64_t v[] = {
        sizeof(U2), sizeof(U2d), sizeof(U1), sizeof(U4),
        sizeof(S2), offsetof(S2, u), offsetof(S2, b),
        sizeof(S1), offsetof(S1, u), offsetof(S1, b),
        sizeof(S4), offsetof(S4, u), offsetof(S4, w),
        sizeof(U2[3]), sizeof(S4[2])
    };
    return v[i];
}

int c_take_u2(U2 u) { return u.x; }
double c_take_u2d(U2d u) { return u.d; }
int64_t c_take_u4(U4 u) { return u.v; }
int c_take_s2(S2 s) { return s.a * 1000 + s.u.c[4] * 10 + s.b; }
U2 c_ret_u2(int x) { U2 r; r.c[4] = 0; r.x = x; return r; }
S4 c_ret_s4(int k) { S4 r; r.a = (char)k; r.u.v = (int64_t)k * 100000; r.w.d = k * 0.5; return r; }
void c_fill_s1(S1* p) { p->a = 7; p->u.x = 123456; p->b = 9; }
