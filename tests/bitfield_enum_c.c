// C side of tests/bitfield_enum_c.esk: bool and enum bitfields laid out and read by C.
#include <stdbool.h>
#include <stdint.h>

enum Col { R, G, B };
struct F { uint8_t a : 7; bool c : 1; enum Col col : 2; uint8_t d : 6; };

void c_fill(struct F* f) { f->a = 100; f->c = true; f->col = B; f->d = 33; }
int c_read(struct F* f) { return f->a * 1000 + f->c * 100 + f->col * 10 + (f->d == 21); }
int c_size(void) { return (int)sizeof(struct F); }
