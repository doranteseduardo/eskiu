/* C side of c_abi_union.esk: unions by value in both directions. */
union ID { int i; double d; };
union CI { char c; int i; };
union FD { float f; double d; };
union IFD { int i; float f; double d; };
struct Mix { float a; union IFD u; };
struct Pair { union CI x; union FD y; };

double id_get(union ID u) { return u.d; }
union ID id_mk(double d) { union ID u; u.d = d; return u; }
int ci_get(union CI u) { return u.i; }
union CI ci_mk(int i) { union CI u; u.i = i; return u; }
double fd_get(union FD u) { return u.d; }
union FD fd_mk(double d) { union FD u; u.d = d; return u; }
double mix_get(struct Mix m) { return m.a + m.u.d; }
struct Mix mix_mk(float a, double d) { struct Mix m; m.a = a; m.u.d = d; return m; }
double pair_get(struct Pair p) { return p.x.i + p.y.d; }
struct Pair pair_mk(int i, double d) { struct Pair p; p.x.i = i; p.y.d = d; return p; }
