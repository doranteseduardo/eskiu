"""
c_oracle_ext.py: a second C-translatable generator for the C oracle, for what c_oracle.py
does not generate: float and double arithmetic and conversions, structs passed and
returned by value (small ones in registers, large ones through sret), arrays of
structs reached through pointers and pointer arithmetic, calls through function
pointers, unions read through another member and nested in a struct (sizeof checked),
and user variadic functions with mixed argument types.

Undefined and unspecified behavior is avoided on both sides: divisors are nonzero
literals (no NaN whose sign could depend on constant folding), a float converted to an
integer is in range, signed integer arithmetic stays small, union punning reads bytes
the write covered, and doubles print with %.17g (exact round trip).

Entry point: gen_program(rng) -> (esk_src, c_src); check with c_oracle.check_program.
"""

FIELD_TYPES = [("int", "int", "%d", "d"), ("int64", "long long", "%lld", "d"),
               ("double", "double", "%.17g", "f"), ("float", "float", "%.9g", "f"),
               ("uint8", "unsigned char", "%d", "u"), ("int16", "short", "%d", "d")]


def flit(rng):
    v = rng.choice([rng.uniform(-1000, 1000), rng.uniform(-1, 1), float(rng.randint(-50, 50)),
                    rng.uniform(1e-3, 1e3) * rng.choice([1, -1])])
    return repr(round(v, rng.randint(0, 6)))


class Out:
    def __init__(self):
        self.esk, self.c = [], []

    def both(self, esk, c=None):
        self.esk.append(esk)
        self.c.append(c if c is not None else esk)


def gen_floats(rng, o, k):
    n = rng.randint(3, 6)
    names = []
    for i in range(n):
        t = rng.choice(["double", "float"])
        nm = f"f{k}_{i}"
        o.both(f"    {t} {nm} = {flit(rng)};")
        names.append((nm, t))
    for _ in range(rng.randint(3, 7)):
        a, b = rng.choice(names), rng.choice(names)
        op = rng.choice(["+", "-", "*", "/"])
        rhs = b[0] if op != "/" else f"({b[0]} == 0.0 ? 3.0 : {b[0]})"
        expr = f"{a[0]} {op} {rhs}"
        if rng.random() < 0.3:
            expr = f"({expr}) {rng.choice(['+', '*'])} {flit(rng)}"
        dst = rng.choice(names)
        cast = "(float)" if dst[1] == "float" else ""
        o.both(f"    {dst[0]} = {cast}({expr});")
    for nm, t in names:
        o.both(f'    printf("%.9g\\n", (double){nm});')
        o.both(f'    printf("%d %d\\n", {nm} < {names[0][0]} ? 1 : 0, {nm} == {nm} ? 1 : 0);')


def gen_convs(rng, o, k):
    vals = [rng.randint(-100000, 100000) for _ in range(3)]
    for i, v in enumerate(vals):
        o.both(f"    int c{k}_{i} = {v};")
        o.both(f'    printf("%.17g %.9g\\n", (double)c{k}_{i} * 0.5, (double)(float)c{k}_{i});')
        o.both(f'    printf("%d %lld\\n", (int)((double)c{k}_{i} / 3.0), (int64)((double)c{k}_{i} * 1024.0));',
               f'    printf("%d %lld\\n", (int)((double)c{k}_{i} / 3.0), (long long)((double)c{k}_{i} * 1024.0));')
    u = rng.randint(0, 4000000000)
    o.esk.append(f"    uint32 u{k} = (uint32){u};")
    o.c.append(f"    unsigned u{k} = (unsigned){u}u;")
    o.both(f'    printf("%.17g\\n", (double)u{k});')


def gen_struct_family(rng, o, k, decls_esk, decls_c, funcs_esk, funcs_c):
    nf = rng.randint(1, 7)
    fields = [(f"m{i}",) + rng.choice(FIELD_TYPES) for i in range(nf)]
    S = f"S{k}"
    decls_esk.append(f"struct {S} {{ " + " ".join(f"{t} {n};" for n, t, _, _, _ in fields) + " }")
    decls_c.append(f"typedef struct {S} {{ " + " ".join(f"{ct} {n};" for n, _, ct, _, _ in fields) + f" }} {S};")

    def init_vals(seed_expr):
        out = []
        for i, (n, t, ct, fmt, kind) in enumerate(fields):
            if kind == "f":
                out.append(f"({t if t != 'double' else 'double'})({seed_expr} + {i}) * 0.25")
            elif t == "uint8":
                out.append(f"(uint8)(({seed_expr} * {i + 3}) & 255)")
            elif t == "int16":
                out.append(f"(int16)({seed_expr} * {i + 7} - 300)")
            elif t == "int64":
                out.append(f"(int64){seed_expr} * {rng.randint(1, 100000)} - {rng.randint(0, 99)}")
            else:
                out.append(f"{seed_expr} * {i + 2} - {rng.randint(0, 50)}")
        return out

    def c_cast(e):
        return (e.replace("(uint8)", "(unsigned char)").replace("(int16)", "(short)")
                 .replace("(int64)", "(long long)"))

    ev = init_vals("seed")
    funcs_esk.append(f"{S} mk{k}(int seed) {{ return {S}{{" + ", ".join(ev) + "}; }")
    funcs_c.append(f"{S} mk{k}(int seed) {{ return ({S}){{" + ", ".join(c_cast(e) for e in ev) + "}; }")
    mod_lines = []
    for n, t, ct, fmt, kind in fields:
        if kind == "f":
            mod_lines.append(f"s.{n} = s.{n} * 2.0 + 1.0;" if t == "double" else f"s.{n} = (float)(s.{n} * 2.0 + 1.0);")
        elif t in ("uint8", "int16"):
            mod_lines.append(f"s.{n} = ({t})(s.{n} + d);")
        else:
            mod_lines.append(f"s.{n} = s.{n} + d;")
    body = " ".join(mod_lines)
    funcs_esk.append(f"{S} mod{k}({S} s, int d) {{ {body} return s; }}")
    cbody = c_cast(body).replace("(uint8)", "(unsigned char)")
    funcs_c.append(f"{S} mod{k}({S} s, int d) {{ {cbody} return s; }}")
    prt_e, prt_c = [], []
    for n, t, ct, fmt, kind in fields:
        arg = f"(double)s.{n}" if t == "float" else (f"(int)s.{n}" if t in ("uint8", "int16") else f"s.{n}")
        prt_e.append((fmt if t != "float" else "%.9g", arg))
    fmt_all = " ".join(f for f, _ in prt_e)
    args = ", ".join(a for _, a in prt_e)
    funcs_esk.append(f'void show{k}({S} s) {{ printf("{fmt_all}\\n", {args}); }}')
    cargs = args.replace("(int64)", "(long long)")
    funcs_c.append(f'void show{k}({S} s) {{ printf("{fmt_all}\\n", {cargs}); }}')
    a, d = rng.randint(-20, 20), rng.randint(1, 9)
    o.both(f"    {S} a{k} = mk{k}({a});")
    o.both(f"    {S} b{k} = mod{k}(a{k}, {d});")
    o.both(f"    show{k}(a{k}); show{k}(b{k});")
    o.both(f"    {S}[3] arr{k};", f"    {S} arr{k}[3];")
    o.both(f"    for (int i = 0; i < 3; i++) {{ arr{k}[i] = mk{k}(i + {a}); }}")
    o.both(f"    arr{k}[1] = mod{k}(arr{k}[2], {d}); show{k}(arr{k}[1]); show{k}(arr{k}[2]);")
    # Through a pointer into the array (with pointer arithmetic) and a function pointer.
    ints = [n for n, t, ct, fmt, kind in fields if t in ("int", "int64")]
    o.esk.append(f"    *{S} p{k} = &arr{k}[0];"); o.c.append(f"    {S}* p{k} = &arr{k}[0];")
    o.both(f"    p{k} = p{k} + {rng.randint(1, 2)};")
    if ints:
        n = rng.choice(ints)
        v = rng.randint(1, 99)
        o.esk.append(f"    p{k}.{n} = p{k}.{n} + {v};")
        o.c.append(f"    p{k}->{n} = p{k}->{n} + {v};")
    o.both(f"    show{k}(*p{k}); show{k}(arr{k}[0]);")
    o.esk.append(f"    fn({S}, int)->{S} fp{k} = mod{k};"); o.c.append(f"    {S} (*fp{k})({S}, int) = mod{k};")
    o.both(f"    show{k}(fp{k}(a{k}, {rng.randint(1, 9)}));")


def gen_union(rng, o, k, decls_esk, decls_c):
    U = f"U{k}"
    decls_esk.append(f"union {U} {{ int64 i; double d; int32[2] h; uint8[8] b; }}")
    decls_c.append(f"typedef union {U} {{ long long i; double d; int h[2]; unsigned char b[8]; }} {U};")
    o.both(f"    {U} u{k};")
    if rng.random() < 0.5:
        o.both(f"    u{k}.d = {flit(rng)};")
    else:
        v = rng.randint(-2**40, 2**40)
        o.both(f"    u{k}.i = {v};", f"    u{k}.i = {v}LL;")
    o.both(f'    printf("%lld %d %d %d %d\\n", u{k}.i, u{k}.h[0], u{k}.h[1], (int)u{k}.b[0], (int)u{k}.b[7]);',
           f'    printf("%lld %d %d %d %d\\n", u{k}.i, u{k}.h[0], u{k}.h[1], (int)u{k}.b[0], (int)u{k}.b[7]);')
    # The union nested in a struct: layout (sizeof) and the members around it.
    W = f"W{k}"
    decls_esk.append(f"struct {W} {{ int16 tag; {U} u; uint8 tail; }}")
    decls_c.append(f"typedef struct {W} {{ short tag; {U} u; unsigned char tail; }} {W};")
    t, tl = rng.randint(-300, 300), rng.randint(0, 255)
    o.esk.append(f"    {W} w{k} = {W}{{tag: (int16){t}, tail: (uint8){tl}}};")
    o.c.append(f"    {W} w{k} = {{ .tag = (short){t}, .tail = (unsigned char){tl} }};")
    o.both(f"    w{k}.u = u{k};")
    o.both(f'    printf("%d %lld %d %d\\n", (int)w{k}.tag, w{k}.u.i, (int)w{k}.tail, (int)sizeof({W}));')


VARIADIC_ESK = """int64 vsum(int n, ...) {
    va_list ap; va_start(ap);
    int64 t = 0;
    for (int i = 0; i < n; i++) {
        int kind = va_arg<int>(ap);
        if (kind == 0) { t = t * 3 + (int64)va_arg<int>(ap); }
        else if (kind == 1) { t = t * 3 + va_arg<int64>(ap); }
        else { t = t * 3 + (int64)(va_arg<double>(ap) * 16.0); }
    }
    va_end(ap);
    return t;
}"""
VARIADIC_C = """long long vsum(int n, ...) {
    va_list ap; va_start(ap, n);
    long long t = 0;
    for (int i = 0; i < n; i++) {
        int kind = va_arg(ap, int);
        if (kind == 0) { t = t * 3 + (long long)va_arg(ap, int); }
        else if (kind == 1) { t = t * 3 + va_arg(ap, long long); }
        else { t = t * 3 + (long long)(va_arg(ap, double) * 16.0); }
    }
    va_end(ap);
    return t;
}"""


def gen_variadic(rng, o, k):
    n = rng.randint(1, 9)
    ea, ca = [], []
    for _ in range(n):
        kind = rng.randint(0, 2)
        if kind == 0:
            v = rng.randint(-1000, 1000); ea += ["0", str(v)]; ca += ["0", str(v)]
        elif kind == 1:
            v = rng.randint(-2**40, 2**40); ea += ["1", f"(int64){v}"]; ca += ["1", f"(long long){v}LL"]
        else:
            v = round(rng.uniform(-100, 100), 3)
            if rng.random() < 0.5:
                ea += ["2", f"(float){v}"]; ca += ["2", f"(float){v}"]
            else:
                ea += ["2", repr(v)]; ca += ["2", repr(v)]
    o.esk.append(f'    printf("%lld\\n", vsum({n}, {", ".join(ea)}));')
    o.c.append(f'    printf("%lld\\n", vsum({n}, {", ".join(ca)}));')


def gen_program(rng):
    o = Out()
    decls_esk, decls_c, funcs_esk, funcs_c = [], [], [], []
    uses_va = False
    for k in range(rng.randint(2, 5)):
        pick = rng.choice(["floats", "convs", "struct", "struct", "union", "variadic"])
        if pick == "floats": gen_floats(rng, o, k)
        elif pick == "convs": gen_convs(rng, o, k)
        elif pick == "struct": gen_struct_family(rng, o, k, decls_esk, decls_c, funcs_esk, funcs_c)
        elif pick == "union": gen_union(rng, o, k, decls_esk, decls_c)
        else:
            gen_variadic(rng, o, k); uses_va = True
    esk = ["extern int printf(string fmt, ...);"] + decls_esk + funcs_esk
    c = ["#include <stdio.h>", "#include <stdarg.h>"] + decls_c + funcs_c
    if uses_va:
        esk.append(VARIADIC_ESK); c.append(VARIADIC_C)
    esk += ["int main() {"] + o.esk + ["    return 0;", "}"]
    c += ["int main(void) {"] + o.c + ["    return 0;", "}"]
    return "\n".join(esk) + "\n", "\n".join(c) + "\n"
