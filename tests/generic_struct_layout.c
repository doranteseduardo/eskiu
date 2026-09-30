// C side of tests/generic_struct_layout.esk: the concrete structs the generic
// instances must match, filled through C.
#include <stdint.h>

struct M { int v; int g[2][3]; int h[2][2][3]; };
struct G { double v; uint8_t f : 3; int8_t s : 4; uint16_t w : 9; };
struct Q { uint32_t a : 4; uint32_t b : 4; uint8_t c; };
#pragma pack(push, 1)
struct PG { uint8_t k; int64_t v; };
struct K { uint8_t k; int16_t v; int32_t w; };
#pragma pack(pop)

int c_size(int i) {
    int s[] = { sizeof(struct M), sizeof(struct G), sizeof(struct Q), sizeof(struct PG), sizeof(struct K) };
    return s[i];
}

void c_fill_m(struct M* p) {
    p->v = 7;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) p->g[i][j] = i * 10 + j;
    int n = 0;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) for (int k = 0; k < 3; k++) p->h[i][j][k] = n++;
}
void c_fill_g(struct G* p) { p->v = 2.5; p->f = 6; p->s = -7; p->w = 510; }
void c_fill_q(struct Q* p) { p->a = 9; p->b = 5; p->c = 200; }
void c_fill_p(struct PG* p) { p->k = 3; p->v = 1099511627776LL; }
void c_fill_k(struct K* p) { p->k = 4; p->v = -300; p->w = 70000; }
