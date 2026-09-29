// C side of tests/variadic_regs.esk: C calls the Eskiu variadic functions.
#include <stdint.h>

int64_t isum(int n, ...);
double dsum(int n, ...);
int64_t mixed(const char* pat, ...);

int c_drive(void) {
    int k = 3;
    int64_t a = isum(10, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
    double d = dsum(10, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
    int64_t m = mixed("pdlisdddddddddil", &k, 1.5, (int64_t)2, 3, "z", 1.0, 2.0, 3.0, 4.0,
                      5.0, 6.0, 7.0, 8.0, 9.0, 4, (int64_t)5);
    return (int)(a % 1000) + (int)d + (int)(m % 1000);
}
