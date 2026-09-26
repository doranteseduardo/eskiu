// C side of tests/c_abi_narrow.esk: narrow integer params and results.
#include <stdbool.h>
#include <stdint.h>

int64_t c_s8(int8_t a) { return a; }
int64_t c_s16(int16_t a) { return a; }
int64_t c_u8(uint8_t a) { return a; }
int64_t c_u16(uint16_t a) { return a; }
int64_t c_b(bool a) { return a; }
int8_t c_ret_s8(int k) { return (int8_t)(k * 1000003 + 200); }
uint16_t c_ret_u16(int k) { return (uint16_t)(k * 1000003 + 60000); }
int64_t c_call_s8(int8_t (*f)(int), int k) { return f(k); }
int64_t c_call_u16(uint16_t (*f)(int), int k) { return f(k); }
int16_t esk_s16(int k);
int64_t c_call_named(int k) { return esk_s16(k); }
