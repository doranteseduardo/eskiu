// C side of tests/const_fold_typed_c.esk: the same integer constant expressions as enum
// values, array dimensions and case labels, folded by the C compiler.
enum E { S2 = (1 << 31) >> 31, C1 = -1 < 0u, C2 = 0u - 1 > 0, D1 = 7u / (unsigned)-1, R1 = -8 >> 1,
         U1 = (unsigned)-8 >> 28, M1 = -7 % 3, A = (unsigned)-1 / 2 > 5, B = (int)((unsigned)1 << 31) >> 31,
         C = (unsigned)-1 >> 31, D = (int)(unsigned)-1, F = ((unsigned)3 - (unsigned)5) / 2 > 100,
         G = (signed char)-1 < (unsigned char)1, H = (unsigned short)65535 * 2 > 0, K = sizeof(int) - 5 > 0,
         L = 2147483647 + 1 < 0, T = (1 ? (unsigned)1 : -1) - 2 > 0 };
static int vals[] = {S2, C1, C2, D1, R1, U1, M1, A, B, C, D, F, G, H, K, L, T};
int c_val(int i) { return vals[i]; }
int c_count(void) { return (int)(sizeof(vals) / sizeof(vals[0])); }
int c_dim(void) { int arr[((unsigned)3 - (unsigned)5) > 100 ? 4 : 1]; return (int)sizeof(arr); }
