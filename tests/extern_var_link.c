// C side of tests/extern_var_link.esk: a C global the Eskiu side reads through
// `extern`, and C code that reads and writes Eskiu globals by name.
extern int esk_value;
extern double esk_scale;

int c_counter = 3;

int c_read_esk(void) { return esk_value; }

void c_bump_esk(void) { esk_value += 100; esk_scale *= 2.0; }
