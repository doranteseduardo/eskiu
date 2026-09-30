/* C side of c_abi_x86_32.esk: the aggregate shapes the 32-bit x86 rules tell apart
   (register-sized results, single-element structs, expandable arguments). */
struct C1 { char a; };
struct CC { char a; char b; };
struct CS { char a; short b; };
struct C3 { char a, b, c; };
struct F1 { float f; };
struct D1 { double d; };
struct P1 { int* p; };
struct NF { struct F1 in; };
struct AF { float a[1]; };
struct A2 { short a[2]; };
struct ID { int a; double d; };
struct I4 { int a, b, c, d; };
struct I5 { int a, b, c, d, e; };
struct L1 { long long a; };
struct CI { char c; int i; };
union UI { int i; };
union UF { int i; float f; };

struct C1 x_c1(int v) { struct C1 r = { (char)v }; return r; }
int x_c1_get(struct C1 s) { return s.a; }
struct CC x_cc(int v) { struct CC r = { (char)v, (char)(v + 1) }; return r; }
int x_cc_get(struct CC s) { return s.a + s.b; }
struct CS x_cs(int v) { struct CS r = { (char)v, (short)(v * 10) }; return r; }
int x_cs_get(struct CS s) { return s.a + s.b; }
struct C3 x_c3(int v) { struct C3 r = { (char)v, (char)(v + 1), (char)(v + 2) }; return r; }
int x_c3_get(struct C3 s) { return s.a + s.b + s.c; }
struct F1 x_f1(float v) { struct F1 r = { v }; return r; }
double x_f1_get(struct F1 s) { return s.f; }
struct D1 x_d1(double v) { struct D1 r = { v }; return r; }
double x_d1_get(struct D1 s) { return s.d; }
struct P1 x_p1(int* p) { struct P1 r = { p }; return r; }
int x_p1_get(struct P1 s) { return *s.p; }
struct NF x_nf(float v) { struct NF r = { { v } }; return r; }
double x_nf_get(struct NF s) { return s.in.f; }
struct AF x_af(float v) { struct AF r = { { v } }; return r; }
double x_af_get(struct AF s) { return s.a[0]; }
struct A2 x_a2(int v) { struct A2 r = { { (short)v, (short)(v + 1) } }; return r; }
int x_a2_get(struct A2 s) { return s.a[0] + s.a[1]; }
struct ID x_id(int a, double d) { struct ID r = { a, d }; return r; }
double x_id_get(struct ID s) { return s.a + s.d; }
struct I4 x_i4(int v) { struct I4 r = { v, v + 1, v + 2, v + 3 }; return r; }
int x_i4_get(struct I4 s) { return s.a + s.b + s.c + s.d; }
struct I5 x_i5(int v) { struct I5 r = { v, v + 1, v + 2, v + 3, v + 4 }; return r; }
int x_i5_get(struct I5 s) { return s.a + s.b + s.c + s.d + s.e; }
struct L1 x_l1(long long v) { struct L1 r = { v }; return r; }
long long x_l1_get(struct L1 s) { return s.a; }
struct CI x_ci(int v) { struct CI r = { (char)v, v * 100 }; return r; }
int x_ci_get(struct CI s) { return s.c + s.i; }
union UI x_ui(int v) { union UI r; r.i = v; return r; }
int x_ui_get(union UI s) { return s.i; }
union UF x_uf(float v) { union UF r; r.f = v; return r; }
double x_uf_get(union UF s) { return s.f; }
int x_mixed(int a, struct ID b, char c, struct C3 d, struct I4 e) {
    return a + (int)b.d + c + d.c + e.d;
}
