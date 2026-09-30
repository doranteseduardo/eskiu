"""
c_oracle.py: an external (C) oracle for the Eskiu fuzzer.

The O0-vs-O2 differential and the self-host parity gates only compare Eskiu
compilers against each other, so a bug both compilers share passes silently.
This module generates random programs inside a C-translatable subset of Eskiu,
emits an equivalent C program for each, and requires the Eskiu builds (the C++
`eskiuc` and the self-hosted `eskiuc-esk`) to print exactly what the C program
prints when compiled by clang at -O0 with -fwrapv.

The subset: integers of every width and signedness (incl. bool and char),
casts, arithmetic / bitwise / shift / comparison / logical operators, ternary,
if / while / for / do / switch / break / continue, nested blocks that shadow,
arrays, structs, pointers (incl. pointer difference), pure and impure
functions, globals with constant initializers, static locals, printf, and
`defer` (Eskiu-only: the C side runs the deferred statements explicitly at every
block exit, which is the semantics the spec gives them).

Undefined behavior is avoided identically on both sides: division and
remainder divisors are guarded (0 and -1 become 7), shift counts are masked to
the promoted width, array indices are masked to a power-of-two length, signed
overflow wraps (Eskiu emits no nsw; C gets -fwrapv), and side effects are
limited to places where C fixes the evaluation order (a single side-effecting
lvalue index per statement, impure calls only as a whole statement or a plain
local initializer/assignment).

C locals get unique names (the Eskiu side keeps the shadowing names), so a
deferred statement re-emitted at an inner exit still refers to the variable it
named at the defer point.

Entry points: gen_program(rng) -> (esk_src, c_src); check_program(...).
"""

import os, tempfile
from fuzz_util import run_limited


# ── Types ─────────────────────────────────────────────────────────────────────

class IT:
    def __init__(self, esk, c, bits, signed):
        self.esk, self.c, self.bits, self.signed = esk, c, bits, signed
    def __repr__(self):
        return self.esk
    def lo(self):
        if self.bits == 1: return 0
        return -(1 << (self.bits - 1)) if self.signed else 0
    def hi(self):
        if self.bits == 1: return 1
        return (1 << (self.bits - 1)) - 1 if self.signed else (1 << self.bits) - 1

I8  = IT("int8",   "int8_t",   8,  True)
I16 = IT("int16",  "int16_t",  16, True)
I32 = IT("int32",  "int32_t",  32, True)
I64 = IT("int64",  "int64_t",  64, True)
U8  = IT("uint8",  "uint8_t",  8,  False)
U16 = IT("uint16", "uint16_t", 16, False)
U32 = IT("uint32", "uint32_t", 32, False)
U64 = IT("uint64", "uint64_t", 64, False)
BOOL = IT("bool",  "_Bool",    1,  False)
CHAR = IT("char",  "unsigned char", 8, False)
INTS = [I8, I16, I32, I64, U8, U16, U32, U64]
ALL = INTS + [BOOL, CHAR]

def esk_spelling(rng, t):
    # int / uint are aliases of int32 / uint32
    if t is I32 and rng.random() < 0.3: return "int"
    if t is U32 and rng.random() < 0.2: return "uint"
    return t.esk

def promote(t):
    return I32 if t.bits < 32 else t

def common(a, b):
    a, b = promote(a), promote(b)
    if a is b: return a
    if a.signed == b.signed: return a if a.bits >= b.bits else b
    u, s = (a, b) if not a.signed else (b, a)
    if u.bits >= s.bits: return u
    return s


# ── Expressions ───────────────────────────────────────────────────────────────

class E:
    """An expression rendered for both languages, with its C type."""
    def __init__(self, esk, c, ty, const=False, lit=False, size=1):
        self.esk, self.c, self.ty = esk, c, ty
        self.const = const      # a compile-time constant (both languages)
        self.lit = lit          # a bare literal / constant tree (range-checked by Eskiu)
        self.size = size

def c_int_lit(v):
    if v == -(1 << 63): return "(-9223372036854775807LL-1)"
    if v == -(1 << 31): return "(-2147483647-1)"
    if -(1 << 31) <= v < (1 << 31): return f"({v})" if v < 0 else str(v)
    return f"({v}LL)" if v < 0 else f"{v}LL"

def lit_int(v):
    """An unsuffixed decimal literal: int32 if it fits, else int64 (same in C)."""
    ty = I32 if -(1 << 31) <= v < (1 << 31) else I64
    return E(str(v), c_int_lit(v), ty, const=True, lit=True)

def cast(t, e, rng=None):
    sp = esk_spelling(rng, t) if rng else t.esk
    return E(f"(({sp})({e.esk}))", f"(({t.c})({e.c}))", t,
             const=e.const, lit=False, size=e.size + 1)

INTERESTING = [0, 1, 2, 3, 5, 7, 8, 15, 16, 31, 32, 63, 100, 127, 128, 200, 255, 256,
               1000, 32767, 32768, 65535, 65536, 100000, 2147483647, -1, -2, -7, -8,
               -128, -129, -255, -32768, -65536, -2147483647, -2147483648,
               2147483648, 4294967295, 4294967296, 3000000000, 1099511627776,
               9223372036854775807, -9223372036854775807, -9223372036854775808,
               -4294967296, 81985529216486895]

def rand_value(rng, t=None):
    if t is None or rng.random() < 0.15:
        v = rng.choice(INTERESTING) if rng.random() < 0.7 else rng.randint(-300, 300)
    else:
        r = rng.random()
        if r < 0.4: v = rng.choice([t.lo(), t.hi(), 0, 1, t.hi() - 1, t.lo() + 1])
        else: v = rng.randint(max(t.lo(), -1000), min(t.hi(), 1000))
    return v


class Var:
    def __init__(self, esk, c, ty, kind="scalar", n=0, struct=None, target=None,
                 writable=True, readable=True, is_global=False, const_c=None):
        self.esk, self.c, self.ty, self.kind = esk, c, ty, kind
        self.n = n                  # array length
        self.struct = struct        # StructDef for kind == "struct"/"sptr"
        self.target = target        # for pointers: the Var pointed at
        self.writable, self.readable = writable, readable
        self.is_global = is_global
        self.const_c = const_c      # C text of a const global's value (for global inits)


class StructDef:
    def __init__(self, name, fields):
        self.name = name
        self.fields = fields        # list of (fname, IT, arraylen or 0)


class Func:
    def __init__(self, name, ret, params, pure):
        self.name, self.ret, self.params, self.pure = name, ret, params, pure
        # params: list of (name, IT) or (name, ("ptr", IT))


# ── Generator ─────────────────────────────────────────────────────────────────

class Gen:
    def __init__(self, rng, defer=True):
        self.rng = rng
        self.use_defer = defer
        self.uid = 0
        self.structs = []
        self.globals = []           # Var
        self.funcs = []             # callable functions defined so far
        self.items = []             # [group, esk line | None, c line | None]
        # per-function state
        self.scopes = []            # list of dict name->Var (Eskiu visibility)
        self.blocks = []            # C-side block stack: {"defers": [...], "loop": bool}
        self.loop_depth = 0
        self.fn = None
        self.budget = 0

    # ── naming ──
    def fresh(self, prefix):
        self.uid += 1
        return f"{prefix}{self.uid}"

    # ── scope helpers ──
    def visible(self):
        seen, out = set(), []
        for sc in reversed(self.scopes):
            for name, v in sc.items():
                if name not in seen:
                    seen.add(name); out.append(v)
        for g in self.globals:
            if g.esk not in seen:
                seen.add(g.esk); out.append(g)
        return out

    def declare(self, v):
        self.scopes[-1][v.esk] = v

    def local_name(self, ty_hint=None):
        # sometimes reuse a visible outer name to create shadowing
        rng = self.rng
        if len(self.scopes) > 1 and rng.random() < 0.35:
            outer = [n for sc in self.scopes[:-1] for n in sc
                     if n not in self.scopes[-1] and not n.startswith("k")]
            if outer:
                return rng.choice(outer)
        return self.fresh("v")

    # ── expressions ──
    def leaf(self, want=None, const_only=False, in_global=False):
        rng = self.rng
        opts = []
        if not const_only:
            vs = [v for v in self.visible() if v.readable]
            if in_global:
                vs = [v for v in vs if v.const_c is not None]
            opts = vs
        r = rng.random()
        if opts and r < 0.6:
            v = rng.choice(opts)
            return self.read_var(v, in_global)
        if r < 0.7:
            t = rng.choice([CHAR, BOOL])
            if t is CHAR:
                ch = rng.choice("azAZ09 ~")
                return E(f"'{ch}'", f"((unsigned char)'{ch}')", CHAR, const=True, lit=True)
            b = rng.choice(["true", "false"])
            return E(b, f"((_Bool){1 if b == 'true' else 0})", BOOL, const=True, lit=True)
        if r < 0.75:
            t = rng.choice(ALL)
            if self.structs and rng.random() < 0.3:
                s = rng.choice(self.structs)
                return E(f"sizeof({s.name})", f"((int64_t)sizeof({s.name}))", I64, const=True)
            return E(f"sizeof({t.esk})", f"((int64_t)sizeof({t.c}))", I64, const=True)
        v = rand_value(rng, want)
        e = lit_int(v)
        if want is not None and rng.random() < 0.5:
            if want.lo() <= v <= want.hi() and rng.random() < 0.5:
                return e
            return cast(want, e, rng)
        return e

    def read_var(self, v, in_global=False):
        rng = self.rng
        if in_global and v.const_c is not None:
            return E(v.esk, f"(({v.ty.c})({v.const_c}))", v.ty, const=True, lit=True)
        if v.kind == "scalar":
            return E(v.esk, v.c, v.ty, const=v.const_c is not None and not v.writable)
        if v.kind == "array":
            idx = self.index_expr(v.n)
            return E(f"{v.esk}[{idx.esk}]", f"{v.c}[{idx.c}]", v.ty, size=idx.size + 1)
        if v.kind == "struct":
            fname, fty, alen = rng.choice(v.struct.fields)
            if alen:
                idx = self.index_expr(alen)
                return E(f"{v.esk}.{fname}[{idx.esk}]", f"{v.c}.{fname}[{idx.c}]", fty, size=idx.size + 1)
            return E(f"{v.esk}.{fname}", f"{v.c}.{fname}", fty)
        if v.kind == "sptr":
            fname, fty, alen = rng.choice(v.struct.fields)
            if alen:
                idx = self.index_expr(alen)
                return E(f"{v.esk}.{fname}[{idx.esk}]", f"{v.c}->{fname}[{idx.c}]", fty, size=idx.size + 1)
            return E(f"{v.esk}.{fname}", f"{v.c}->{fname}", fty)
        if v.kind == "ptr":
            t = v.target
            if t.kind == "array" and rng.random() < 0.7:
                idx = self.index_expr(t.n)
                return E(f"{v.esk}[{idx.esk}]", f"{v.c}[{idx.c}]", v.ty, size=idx.size + 1)
            return E(f"(*{v.esk})", f"(*{v.c})", v.ty)
        raise AssertionError(v.kind)

    def index_expr(self, n):
        rng = self.rng
        if rng.random() < 0.4:
            k = rng.randrange(n)
            return E(str(k), str(k), I32, const=True, lit=True)
        e = self.expr(1, None)
        return E(f"(({e.esk}) & {n - 1})", f"(({e.c}) & {n - 1})", common(e.ty, I32), size=e.size + 1)

    def expr(self, depth, want=None, const_only=False, in_global=False):
        rng = self.rng
        if depth <= 0 or rng.random() < 0.25:
            return self.leaf(want, const_only, in_global)
        k = rng.random()
        sub = lambda d=depth - 1: self.expr(d, None, const_only, in_global)
        if k < 0.30:
            a, b = sub(), sub()
            op = rng.choice(["+", "-", "*", "&", "|", "^"])
            return self.binop(a, op, b, common(a.ty, b.ty))
        if k < 0.40:
            a, b = sub(), self.expr(0, None, const_only, in_global) if rng.random() < 0.6 else sub()
            op = rng.choice(["/", "%"])
            guard_e = f"((({b.esk}) == 0 || ({b.esk}) == -1) ? 7 : ({b.esk}))"
            guard_c = f"((({b.c}) == 0 || ({b.c}) == -1) ? 7 : ({b.c}))"
            gty = common(I32, b.ty)
            ty = common(a.ty, gty)
            return E(f"(({a.esk}) {op} {guard_e})", f"(({a.c}) {op} {guard_c})", ty,
                     const=a.const and b.const, size=a.size + 3 * b.size)
        if k < 0.50:
            a, b = sub(), sub()
            op = rng.choice(["<<", ">>"])
            w = promote(a.ty).bits - 1
            return E(f"(({a.esk}) {op} (({b.esk}) & {w}))", f"(({a.c}) {op} (({b.c}) & {w}))",
                     promote(a.ty), const=a.const and b.const, size=a.size + b.size + 1)
        if k < 0.62:
            a, b = sub(), sub()
            op = rng.choice(["<", ">", "<=", ">=", "==", "!="])
            return self.binop(a, op, b, BOOL)
        if k < 0.68:
            a, b = sub(), sub()
            op = rng.choice(["&&", "||"])
            return self.binop(a, op, b, BOOL)
        if k < 0.76:
            a = sub()
            op = rng.choice(["-", "~", "!"])
            ty = BOOL if op == "!" else promote(a.ty)
            return E(f"({op}({a.esk}))", f"({op}({a.c}))", ty, const=a.const, size=a.size + 1)
        if k < 0.86:
            t = rng.choice(ALL)
            return cast(t, sub(), rng)
        if k < 0.94:
            c, a, b = sub(), sub(), sub()
            if rng.random() < 0.3:
                # a wide literal arm (the ternary-typing bug class)
                b = lit_int(rng.choice([4294967295, 4294967296, 3000000000, -2147483648,
                                        2147483648, 9223372036854775807, -1]))
            # Eskiu range-checks a literal arm against the target like a bare literal
            return E(f"(({c.esk}) ? ({a.esk}) : ({b.esk}))", f"(({c.c}) ? ({a.c}) : ({b.c}))",
                     common(a.ty, b.ty), const=c.const and a.const and b.const,
                     lit=a.lit or b.lit, size=a.size + b.size + c.size + 1)
        if not const_only and not in_global:
            pures = [f for f in self.funcs if f.pure]
            if pures and rng.random() < 0.6:
                return self.call(rng.choice(pures), depth - 1)
            pd = self.ptr_diff()
            if pd: return pd
        return self.leaf(want, const_only, in_global)

    def binop(self, a, op, b, ty):
        return E(f"(({a.esk}) {op} ({b.esk}))", f"(({a.c}) {op} ({b.c}))", ty,
                 const=a.const and b.const, size=a.size + b.size + 1)

    def ptr_diff(self):
        rng = self.rng
        arrs = [v for v in self.visible() if v.kind == "array" and v.readable]
        if not arrs: return None
        a = rng.choice(arrs)
        i, j = rng.randrange(a.n), rng.randrange(a.n)
        return E(f"(&{a.esk}[{i}] - &{a.esk}[{j}])", f"((int64_t)(&{a.c}[{i}] - &{a.c}[{j}]))", I64)

    def call(self, f, depth):
        args_e, args_c = [], []
        for pname, pty in f.params:
            a = self.conv(self.expr(depth, pty), pty)
            args_e.append(a.esk); args_c.append(a.c)
        return E(f"{f.name}({', '.join(args_e)})", f"{f.name}({', '.join(args_c)})", f.ret, size=4)

    def fits_lit(self, e, t):
        try:
            v = int(e.esk)
        except ValueError:
            return False
        return t.lo() <= v <= t.hi()

    def conv(self, e, t):
        """Ready e for an implicit conversion to t: Eskiu range-checks a literal
        (or a ternary's literal arm) against the target, so cast one that may not fit."""
        if e.lit:
            if self.fits_lit(e, t):
                return e
            return cast(t, e, self.rng)
        return e

    # ── output ──
    def emit(self, esk, c, ind, gid=None):
        """Append a line pair. Lines sharing a group are deleted together by the
        reducer (a defer's Eskiu line and every C copy of its body)."""
        pad = "    " * ind
        if gid is None:
            self.uid += 1; gid = self.uid
        self.items.append([gid, None if esk is None else pad + esk, None if c is None else pad + c])
        return gid

    def top(self, esk, c):
        self.emit(esk, c, 0)

    def printf(self, e, ind):
        if e.ty is U64 or (e.ty is U32 and self.rng.random() < 0.3):
            self.emit(f'printf("%llu\\n", (uint64)({e.esk}));',
                      f'printf("%llu\\n", (unsigned long long)(uint64_t)({e.c}));', ind)
        else:
            self.emit(f'printf("%lld\\n", (int64)({e.esk}));',
                      f'printf("%lld\\n", (long long)(int64_t)({e.c}));', ind)

    # ── lvalues ──
    def lvalue(self, allow_side=True):
        """(esk, c, ty, has_side_effect) for a writable location."""
        rng = self.rng
        vs = [v for v in self.visible() if v.writable]
        if not vs: return None
        v = rng.choice(vs)
        side = allow_side and not self.no_decl and rng.random() < 0.35
        def idx(n):
            if side:
                if rng.random() < 0.5 and self.fn is not None and not self.fn.pure:
                    return (f"(tick() & {n - 1})", f"(tick() & {n - 1})")
                kv = self.side_counter()
                op = rng.choice(["++", "--"])
                pre = rng.random() < 0.5
                ek = f"{op}{kv.esk}" if pre else f"{kv.esk}{op}"
                ck = f"{op}{kv.c}" if pre else f"{kv.c}{op}"
                return (f"(({ek}) & {n - 1})", f"(({ck}) & {n - 1})")
            e = self.index_expr(n)
            return (e.esk, e.c)
        if v.kind == "scalar":
            return (v.esk, v.c, v.ty, False)
        if v.kind == "array":
            ie, ic = idx(v.n)
            return (f"{v.esk}[{ie}]", f"{v.c}[{ic}]", v.ty, side)
        if v.kind in ("struct", "sptr"):
            fname, fty, alen = rng.choice(v.struct.fields)
            arrow = "." if v.kind == "struct" else "->"
            if alen:
                ie, ic = idx(alen)
                return (f"{v.esk}.{fname}[{ie}]", f"{v.c}{arrow}{fname}[{ic}]", fty, side)
            return (f"{v.esk}.{fname}", f"{v.c}{arrow}{fname}", fty, False)
        if v.kind == "ptr":
            t = v.target
            if t.kind == "array":
                ie, ic = idx(t.n)
                return (f"{v.esk}[{ie}]", f"{v.c}[{ic}]", v.ty, side)
            return (f"(*{v.esk})", f"(*{v.c})", v.ty, False)
        return None

    def side_counter(self):
        # a dedicated counter only ever touched through ++/-- in lvalue indices
        name = self.fresh("k")
        v = Var(name, name, I32, writable=False, readable=False)
        init = self.rng.randint(-3, 3)
        self.emit(f"int32 {name} = {init};", f"int32_t {name} = {init};", self.cur_ind)
        return v

    # ── statements ──
    def block(self, ind, n_stmts, loop=False, new_scope=True):
        if new_scope:
            self.scopes.append({})
        self.blocks.append(self.new_block(loop, loop))
        for _ in range(n_stmts):
            if self.budget <= 0: break
            self.stmt(ind)
        b = self.blocks.pop()
        for g, d in reversed(b["defers"]):
            self.emit(None, d, ind, g)
        if new_scope:
            self.scopes.pop()

    def run_defers_to(self, stop, ind):
        """C side: before a jump, run pending defers innermost first, up to and
        including the first block flagged `stop` ("brk"/"cont"; None = all)."""
        for b in reversed(self.blocks):
            for g, d in reversed(b["defers"]):
                self.emit(None, d, ind, g)
            if stop and b[stop]:
                break

    def stmt(self, ind):
        rng = self.rng
        self.budget -= 1
        self.cur_ind = ind
        r = rng.random()
        nested_ok = ind < 5 and self.budget > 3
        if r < 0.16:
            return self.decl_scalar(ind)
        if r < 0.20:
            return self.decl_array(ind)
        if r < 0.23 and self.structs:
            return self.decl_struct(ind)
        if r < 0.26:
            return self.decl_ptr(ind)
        if r < 0.38:
            return self.assign(ind)
        if r < 0.50:
            return self.compound(ind)
        if r < 0.54:
            return self.incdec(ind)
        if r < 0.62:
            if self.fn.pure: return self.compound(ind)
            e = self.expr(3)
            return self.printf(e, ind)
        if r < 0.66:
            if self.fn.pure: return self.assign(ind)
            return self.impure_call(ind)
        if r < 0.70 and self.use_defer:
            return self.defer(ind)
        if not nested_ok:
            return self.assign(ind)
        if r < 0.78:
            return self.if_stmt(ind)
        if r < 0.86:
            if self.loop_depth >= 3: return self.if_stmt(ind)
            return self.loop(ind)
        if r < 0.90:
            return self.switch(ind)
        if r < 0.94:
            self.emit("{", "{", ind)
            self.block(ind + 1, rng.randint(1, 4))
            self.emit("}", "}", ind)
            return
        if r < 0.97 and self.can_jump():
            return self.jump(ind)
        if self.fn is not None and self.fn.ret is not None and rng.random() < 0.3:
            return self.ret(ind, guarded=True)
        return self.compound(ind)

    in_switch_case = False
    no_decl = False

    def can_jump(self):
        return self.loop_depth > 0 or self.in_switch_case

    def new_block(self, brk=False, cont=False):
        return {"defers": [], "brk": brk, "cont": cont}

    def decl_scalar(self, ind):
        rng = self.rng
        t = rng.choice(ALL)
        e = self.conv(self.expr(3, t), t)
        name = self.local_name()
        v = Var(name, self.fresh("c_" + name + "_"), t)
        static = self.fn is not None and not self.fn.pure and rng.random() < 0.25
        if static:
            e = cast(t, self.expr(2, t, const_only=True), rng)
            self.emit(f"static {esk_spelling(rng, t)} {name} = {e.esk};",
                      f"static {t.c} {v.c} = {e.c};", ind)
        elif rng.random() < 0.3:
            self.emit(f"let {name}: {esk_spelling(rng, t)} = {e.esk};", f"{t.c} {v.c} = {e.c};", ind)
        else:
            self.emit(f"{esk_spelling(rng, t)} {name} = {e.esk};", f"{t.c} {v.c} = {e.c};", ind)
        self.declare(v)

    def decl_array(self, ind):
        rng = self.rng
        t = rng.choice(INTS + [CHAR])
        n = rng.choice([2, 4, 8])
        k = rng.randint(0, n)
        elems = [self.conv(self.expr(2, t), t) for _ in range(k)]
        name = self.local_name()
        v = Var(name, self.fresh("c_" + name + "_"), t, kind="array", n=n)
        self.emit(f"{t.esk}[{n}] {name} = {{{', '.join(e.esk for e in elems)}}};",
                  f"{t.c} {v.c}[{n}] = {{{', '.join(e.c for e in elems) or '0'}}};", ind)
        self.declare(v)

    def struct_lit(self, s, depth=2, const_only=False, in_global=False):
        rng = self.rng
        parts_e, parts_c = [], []
        named = rng.random() < 0.6
        # an array field cannot take a nested '{...}' inside a struct literal
        # (Eskiu: "an array literal is only valid as a variable initializer")
        if named:
            fields = [f for f in s.fields if not f[2] and rng.random() < 0.7]
        else:
            fields = s.fields[:rng.randint(0, len(s.fields))]
            k = next((i for i, f in enumerate(fields) if f[2]), len(fields))
            fields = fields[:k]
        for fname, fty, alen in fields:
            if alen:
                elems = [cast(fty, self.expr(depth, fty, const_only, in_global), rng)
                         for _ in range(rng.randint(0, alen))]
                ee = "{" + ", ".join(x.esk for x in elems) + "}"
                ce = "{" + (", ".join(x.c for x in elems) or "0") + "}"
            else:
                x = self.expr(depth, fty, const_only, in_global)
                x = cast(fty, x, rng) if (in_global or rng.random() < 0.5) else self.conv(x, fty)
                ee, ce = x.esk, x.c
            parts_e.append(f"{fname}: {ee}" if named else ee)
            parts_c.append(f".{fname} = {ce}")
        return (f"{s.name} {{ {', '.join(parts_e)} }}",
                "{" + (", ".join(parts_c) or "0") + "}")

    def decl_struct(self, ind):
        rng = self.rng
        s = rng.choice(self.structs)
        name = self.local_name()
        v = Var(name, self.fresh("c_" + name + "_"), None, kind="struct", struct=s)
        le, lc = self.struct_lit(s)
        self.emit(f"{s.name} {name} = {le};", f"{s.name} {v.c} = {lc};", ind)
        self.declare(v)
        if rng.random() < 0.4:
            pn = self.fresh("sp")
            p = Var(pn, pn, None, kind="sptr", struct=s)
            self.emit(f"*{s.name} {pn} = &{name};", f"{s.name} *{pn} = &{v.c};", ind)
            self.declare(p)

    def decl_ptr(self, ind):
        rng = self.rng
        tg = [v for v in self.visible() if v.writable and v.kind in ("scalar", "array")
              and v.ty is not BOOL]
        if not tg:
            return self.decl_scalar(ind)
        t = rng.choice(tg)
        pn = self.fresh("p")
        p = Var(pn, pn, t.ty, kind="ptr", target=t)
        if t.kind == "array":
            self.emit(f"*{t.ty.esk} {pn} = &{t.esk}[0];", f"{t.ty.c} *{pn} = &{t.c}[0];", ind)
        else:
            self.emit(f"*{t.ty.esk} {pn} = &{t.esk};", f"{t.ty.c} *{pn} = &{t.c};", ind)
        self.declare(p)

    def assign(self, ind):
        lv = self.lvalue()
        if lv is None: return self.fallback(ind)
        le, lc, lt, _ = lv
        e = self.conv(self.expr(3, lt), lt)
        self.emit(f"{le} = {e.esk};", f"{lc} = {e.c};", ind)

    def fallback(self, ind):
        """A statement that needs no writable variable (valid as an unbraced body)."""
        if self.no_decl or self.fn.pure:
            if self.fn.pure:
                return self.emit("{ }", "{ }", ind)
            e = self.expr(2)
            return self.emit(f"g_trace ^= (int64)({e.esk});", f"g_trace ^= (int64_t)({e.c});", ind)
        return self.decl_scalar(ind)

    def compound(self, ind):
        rng = self.rng
        lv = self.lvalue()
        if lv is None: return self.fallback(ind)
        le, lc, lt, _ = lv
        op = rng.choice(["+=", "-=", "*=", "&=", "|=", "^=", "<<=", ">>=", "/=", "%="])
        if lt is BOOL and op in ("/=", "%="):
            op = "|="
        e = self.conv(self.expr(2), lt)
        if op in ("<<=", ">>="):
            w = promote(lt).bits - 1
            ee, ce = f"(({e.esk}) & {w})", f"(({e.c}) & {w})"
        elif op in ("/=", "%="):
            ee = f"((({e.esk}) == 0 || ({e.esk}) == -1) ? 7 : ({e.esk}))"
            ce = f"((({e.c}) == 0 || ({e.c}) == -1) ? 7 : ({e.c}))"
        else:
            ee, ce = e.esk, e.c
        self.emit(f"{le} {op} {ee};", f"{lc} {op} {ce};", ind)

    def incdec(self, ind):
        rng = self.rng
        lv = self.lvalue()
        if lv is None: return self.fallback(ind)
        le, lc, lt, _ = lv
        if lt is BOOL:
            return self.compound(ind)
        op = rng.choice(["++", "--"])
        if rng.random() < 0.5:
            self.emit(f"{op}{le};", f"{op}{lc};", ind)
        else:
            self.emit(f"{le}{op};", f"{lc}{op};", ind)

    def impure_call(self, ind):
        rng = self.rng
        imp = [f for f in self.funcs if not f.pure]
        if not imp:
            return self.printf(self.expr(2), ind)
        f = rng.choice(imp)
        args_e, args_c = [], []
        for pname, pty in f.params:
            if isinstance(pty, tuple):
                tg = [v for v in self.visible() if v.writable and v.kind == "scalar" and v.ty is pty[1]]
                if not tg:
                    return self.printf(self.expr(2), ind)
                t = rng.choice(tg)
                args_e.append(f"&{t.esk}"); args_c.append(f"&{t.c}")
            else:
                a = self.conv(self.expr(2, pty), pty)
                args_e.append(a.esk); args_c.append(a.c)
        ce, cc = f"{f.name}({', '.join(args_e)})", f"{f.name}({', '.join(args_c)})"
        locs = [v for v in self.scopes[-1].values() if v.writable and v.kind == "scalar"] if self.scopes else []
        if locs and rng.random() < 0.5:
            v = rng.choice(locs)
            self.emit(f"{v.esk} = {ce};", f"{v.c} = {cc};", ind)
        else:
            self.emit(f"{ce};", f"{cc};", ind)

    def defer(self, ind):
        rng = self.rng
        # the deferred statement: fold a visible value into the trace, or print it
        e = self.expr(2)
        k = rng.randint(3, 97)
        if self.fn.pure:
            locs = [v for v in self.visible() if v.writable and v.kind == "scalar" and not v.is_global]
            if not locs:
                return self.compound(ind)
            v = rng.choice(locs)
            de = f"{v.esk} = {v.esk} ^ ({e.esk});"
            dc = f"{v.c} = {v.c} ^ ({e.c});"
        elif rng.random() < 0.5:
            de = f"g_trace = g_trace * {k} + (int64)({e.esk});"
            dc = f"g_trace = g_trace * {k} + (int64_t)({e.c});"
        else:
            de = f'printf("d%lld\\n", (int64)({e.esk}));'
            dc = f'printf("d%lld\\n", (long long)(int64_t)({e.c}));'
        body_e = de if rng.random() < 0.6 else "{ " + de + " }"
        g = self.emit(f"defer {body_e}", None, ind)
        self.blocks[-1]["defers"].append((g, dc))

    def cond(self):
        e = self.expr(2)
        return e

    def if_stmt(self, ind):
        rng = self.rng
        c = self.cond()
        self.if_chain(ind, c, depth=0)

    def if_chain(self, ind, c, depth):
        rng = self.rng
        self.emit(f"if ({c.esk})", f"if ({c.c})", ind)
        self.arm(ind)
        r = rng.random()
        if r < 0.3 and depth < 2:
            c2 = self.cond()
            self.emit(f"else if ({c2.esk})", f"else if ({c2.c})", ind)
            self.arm(ind)
            if rng.random() < 0.5:
                self.emit("else", "else", ind)
                self.arm(ind)
        elif r < 0.6:
            self.emit("else", "else", ind)
            self.arm(ind)

    def arm(self, ind):
        rng = self.rng
        if rng.random() < 0.3:
            self.unbraced(ind + 1)
        else:
            self.emit("{", "{", ind)
            self.block(ind + 1, rng.randint(1, 4))
            self.emit("}", "}", ind)

    def unbraced(self, ind, loop=False):
        """An unbraced single-statement body; C wraps it in braces (its own scope)."""
        rng = self.rng
        self.emit(None, "{", ind - 1)
        self.scopes.append({})
        self.blocks.append(self.new_block(loop, loop))
        self.budget -= 1
        self.cur_ind = ind
        self.no_decl = True
        k = rng.random()
        if k < 0.3 and self.use_defer:
            self.defer(ind)
        elif k < 0.5 and self.can_jump():
            self.jump(ind)
        elif k < 0.75 or self.fn.pure:
            self.compound(ind)
        else:
            self.printf(self.expr(2), ind)
        self.no_decl = False
        b = self.blocks.pop()
        for g, d in reversed(b["defers"]):
            self.emit(None, d, ind, g)
        self.scopes.pop()
        self.emit(None, "}", ind - 1)

    def jump(self, ind):
        rng = self.rng
        kw = rng.choice(["break", "continue"])
        c = self.cond()
        if self.loop_depth == 0: kw = "break"
        # as an unbraced body the `if` gets an explicit else, so an `else` that follows
        # the enclosing statement cannot bind to it (the C side wraps the body in braces)
        tail = " else { }" if self.no_decl else ""
        self.emit(f"if ({c.esk}) {{ {kw}; }}{tail}", f"if ({c.c}) {{", ind)
        self.run_defers_to("brk" if kw == "break" else "cont", ind + 1)
        self.emit(None, f"{kw}; }}{tail}", ind + 1)

    def loop(self, ind):
        rng = self.rng
        kind = rng.choice(["for", "while", "do"])
        n = rng.randint(0, 4)
        cv = self.fresh("i")
        cvar = Var(cv, cv, I32, writable=False)
        self.loop_depth += 1
        saved_case = self.in_switch_case
        self.in_switch_case = False
        if kind == "for":
            self.emit(f"for (int32 {cv} = 0; {cv} < {n}; {cv}++)", f"for (int32_t {cv} = 0; {cv} < {n}; {cv}++)", ind)
            self.scopes.append({cv: cvar})
            self.loop_body(ind)
            self.scopes.pop()
        elif kind == "while":
            self.emit(f"int32 {cv} = 0;", f"int32_t {cv} = 0;", ind)
            self.declare(cvar)
            self.emit(f"while ({cv} < {n}) {{", f"while ({cv} < {n}) {{", ind)
            self.emit(f"{cv}++;", f"{cv}++;", ind + 1)
            self.loop_block(ind + 1)
            self.emit("}", "}", ind)
        else:
            self.emit(f"int32 {cv} = 0;", f"int32_t {cv} = 0;", ind)
            self.declare(cvar)
            self.emit("do {", "do {", ind)
            self.emit(f"{cv}++;", f"{cv}++;", ind + 1)
            self.loop_block(ind + 1)
            self.emit(f"}} while ({cv} < {n});", f"}} while ({cv} < {n});", ind)
        self.in_switch_case = saved_case
        self.loop_depth -= 1

    def loop_body(self, ind):
        rng = self.rng
        if rng.random() < 0.25:
            self.unbraced(ind + 1, loop=True)
        else:
            self.emit("{", "{", ind)
            self.loop_block(ind + 1)
            self.emit("}", "}", ind)

    def loop_block(self, ind):
        self.block(ind, self.rng.randint(1, 5), loop=True)

    def switch(self, ind):
        rng = self.rng
        e = self.expr(2)
        subj_e, subj_c = f"(({e.esk}) & 7)", f"(({e.c}) & 7)"
        self.emit(f"switch ({subj_e}) {{", f"switch ({subj_c}) {{", ind)
        vals = rng.sample(range(8), rng.randint(1, 5))
        saved = self.in_switch_case
        self.in_switch_case = True
        # a `break` inside a case leaves the switch: defers of the case block only
        for i, v in enumerate(vals):
            self.emit(f"case {v}:", f"case {v}:", ind)
            self.emit("{", "{", ind + 1)
            self.scopes.append({})
            self.blocks.append(self.new_block(brk=True))
            for _ in range(rng.randint(0, 2)):
                if self.budget > 0: self.stmt(ind + 2)
            b = self.blocks.pop()
            for g, d in reversed(b["defers"]):
                self.emit(None, d, ind + 2, g)
            self.scopes.pop()
            self.emit("}", "}", ind + 1)
            if rng.random() < 0.7:
                self.emit("break;", "break;", ind + 1)
        if rng.random() < 0.6:
            self.emit("default:", "default:", ind)
            self.emit("{", "{", ind + 1)
            self.scopes.append({})
            self.blocks.append(self.new_block(brk=True))
            if self.budget > 0: self.stmt(ind + 2)
            b = self.blocks.pop()
            for g, d in reversed(b["defers"]):
                self.emit(None, d, ind + 2, g)
            self.scopes.pop()
            self.emit("}", "}", ind + 1)
            self.emit("break;", "break;", ind + 1)
        self.emit("}", "}", ind)
        self.in_switch_case = saved

    def ret(self, ind, guarded=False):
        f = self.fn
        e = self.conv(self.expr(2, f.ret), f.ret)
        rv = self.fresh("r")
        if guarded:
            c = self.cond()
            self.emit(f"if ({c.esk}) {{ return {e.esk}; }}", f"if ({c.c}) {{ {f.ret.c} {rv} = {e.c};", ind)
            self.run_defers_to(None, ind + 1)
            self.emit(None, f"return {rv}; }}", ind + 1)
        else:
            self.emit(f"return {e.esk};", f"{{ {f.ret.c} {rv} = {e.c};", ind)
            self.run_defers_to(None, ind + 1)
            self.emit(None, f"return {rv}; }}", ind + 1)

    # ── top level ──
    def gen_struct(self):
        rng = self.rng
        name = f"S{len(self.structs)}"
        fields = []
        for i in range(rng.randint(1, 5)):
            t = rng.choice(INTS + [CHAR, BOOL])
            alen = rng.choice([0, 0, 0, 2, 4]) if t is not BOOL else 0
            fields.append((f"f{i}", t, alen))
        s = StructDef(name, fields)
        fe = " ".join(f"{t.esk}[{a}] {n};" if a else f"{t.esk} {n};" for n, t, a in fields)
        fc = " ".join(f"{t.c} {n}[{a}];" if a else f"{t.c} {n};" for n, t, a in fields)
        self.top(f"struct {name} {{ {fe} }}", f"typedef struct {name} {{ {fc} }} {name};")
        self.structs.append(s)

    def gen_global(self):
        rng = self.rng
        self.scopes = []
        r = rng.random()
        if r < 0.6:
            t = rng.choice(ALL)
            e = cast(t, self.expr(3, t, const_only=False, in_global=True), rng)
            is_const = rng.random() < 0.3
            name = self.fresh("g")
            v = Var(name, name, t, is_global=True, writable=not is_const,
                    const_c=e.c if is_const else None)
            if is_const:
                self.top(f"const {t.esk} {name} = {e.esk};", f"const {t.c} {name} = {e.c};")
            else:
                self.top(f"{t.esk} {name} = {e.esk};", f"{t.c} {name} = {e.c};")
            self.globals.append(v)
        elif r < 0.8:
            t = rng.choice(INTS + [CHAR])
            n = rng.choice([2, 4, 8])
            elems = [cast(t, self.expr(2, t, in_global=True), rng) for _ in range(rng.randint(0, n))]
            name = self.fresh("ga")
            self.top(f"{t.esk}[{n}] {name} = {{{', '.join(e.esk for e in elems)}}};", f"{t.c} {name}[{n}] = {{{', '.join(e.c for e in elems) or '0'}}};")
            self.globals.append(Var(name, name, t, kind="array", n=n, is_global=True))
        elif self.structs:
            s = rng.choice(self.structs)
            name = self.fresh("gs")
            le, lc = self.struct_lit(s, 2, const_only=False, in_global=True)
            self.top(f"{s.name} {name} = {le};", f"{s.name} {name} = {lc};")
            self.globals.append(Var(name, name, None, kind="struct", struct=s, is_global=True))

    def gen_func(self, pure):
        rng = self.rng
        name = self.fresh("f")
        ret = rng.choice(ALL)
        params = []
        for i in range(rng.randint(0, 4)):
            if not pure and rng.random() < 0.3:
                params.append((f"a{i}", ("ptr", rng.choice(INTS))))
            else:
                params.append((f"a{i}", rng.choice(ALL)))
        f = Func(name, ret, params, pure)
        pe = ", ".join(f"*{t[1].esk} {n}" if isinstance(t, tuple) else f"{t.esk} {n}" for n, t in params)
        pc = ", ".join(f"{t[1].c} *{n}" if isinstance(t, tuple) else f"{t.c} {n}" for n, t in params) or "void"
        self.top(f"{ret.esk} {name}({pe}) {{", f"{ret.c} {name}({pc}) {{")
        self.fn = f
        sc = {}
        for n, t in params:
            if isinstance(t, tuple):
                tgt = Var("?", "?", t[1])
                sc[n] = Var(n, n, t[1], kind="ptr", target=tgt)
            else:
                sc[n] = Var(n, n, t)
        saved_globals = self.globals
        if pure:
            # pure functions may read globals but never write them
            self.globals = [Var(g.esk, g.c, g.ty, g.kind, g.n, g.struct, writable=False,
                                is_global=True, const_c=g.const_c) for g in saved_globals]
        self.scopes = [sc]
        self.blocks = [self.new_block()]
        self.budget = rng.randint(3, 14)
        self.in_switch_case = False
        self.loop_depth = 0
        self.cur_ind = 1
        nst = rng.randint(1, 6)
        for _ in range(nst):
            if self.budget <= 0: break
            self.stmt(1)
        self.ret(1)
        self.blocks = []
        self.scopes = []
        self.globals = saved_globals
        self.top("}", "}")
        self.fn = None
        self.funcs.append(f)

    def program(self):
        rng = self.rng
        self.top(None, "#include <stdio.h>")
        self.top(None, "#include <stdint.h>")
        self.top("extern int printf(string fmt, ...);", None)
        self.top("int64 g_trace = 0;", "int64_t g_trace = 0;")
        self.top("int32 g_tick = 0;", "int32_t g_tick = 0;")
        self.top("int32 tick() { g_tick = g_tick + 1; return g_tick; }",
                 "int32_t tick(void) { g_tick = g_tick + 1; return g_tick; }")
        for _ in range(rng.randint(0, 3)):
            self.gen_struct()
        for _ in range(rng.randint(0, 6)):
            self.gen_global()
        for _ in range(rng.randint(0, 4)):
            self.gen_func(pure=rng.random() < 0.6)
        # main: an impure function returning 0
        main = Func("main", I32, [], False)
        self.top("int main() {", "int main(void) {")
        self.fn = main
        self.scopes = [{}]
        self.blocks = [self.new_block()]
        self.budget = rng.randint(8, 40)
        self.in_switch_case = False
        self.loop_depth = 0
        self.cur_ind = 1
        while self.budget > 0:
            self.stmt(1)
        for g in self.globals:
            if g.kind == "scalar":
                self.printf(E(g.esk, g.c, g.ty), 1)
            elif g.kind == "array":
                for i in range(g.n):
                    self.printf(E(f"{g.esk}[{i}]", f"{g.c}[{i}]", g.ty), 1)
        self.emit('printf("%lld %d\\n", g_trace, g_tick);',
                  'printf("%lld %d\\n", (long long)g_trace, g_tick);', 1)
        self.emit("return 0;", None, 1)
        self.run_defers_to(None, 1)
        self.emit(None, "return 0;", 1)
        self.top("}", "}")
        return self.items


def render(items):
    esk = [e for _, e, _ in items if e is not None]
    c = [x for _, _, x in items if x is not None]
    return "\n".join(esk) + "\n", "\n".join(c) + "\n"

def gen_items(rng, defer=True):
    return Gen(rng, defer=defer).program()

def gen_program(rng, defer=True):
    return render(gen_items(rng, defer))


# ── Checking ──────────────────────────────────────────────────────────────────

CRASH_MARKERS = ("Assertion", "terminate called", "Stack dump", "PLEASE submit",
                 "libc++abi", "LLVM verification failed", "AddressSanitizer")

def _run(cmd, timeout, env=None):
    """(returncode | None, stdout, stderr); None means it timed out or blew its
    memory cap, which stderr names."""
    status, rc, out, err = run_limited(cmd, timeout, env)
    if status != "OK":
        return None, out, status.lower()
    return rc, out, err

def check_program(esk_src, c_src, compilers, clang, timeout=30):
    """Build the C reference and each Eskiu build; return (kind, detail).

    compilers: list of (label, argv-prefix, env). kind is OK, GENBUG (the C side
    failed: a generator bug), or <label>-REJECT / -CRASH / -HANG / -DIFF."""
    with tempfile.TemporaryDirectory() as d:
        ep, cp = os.path.join(d, "p.esk"), os.path.join(d, "p.c")
        with open(ep, "w") as f: f.write(esk_src)
        with open(cp, "w") as f: f.write(c_src)
        cbin = os.path.join(d, "c.bin")
        rc, _, err = _run([clang, "-O0", "-fwrapv", "-w", cp, "-o", cbin], timeout)
        if rc != 0:
            return ("GENBUG", "clang: " + err[-400:])
        rc, cout, cerr = _run([cbin], 10)
        if rc is None:
            return ("GENBUG", f"C program: {cerr}")
        want = (cout, rc)
        for label, argv, env in compilers:
            ebin = os.path.join(d, label + ".bin")
            brc, bout, berr = _run(argv + [ep, "-o", ebin], timeout, env)
            text = bout + berr
            if brc is None:
                return (f"{label}-HANG", f"compile: {berr}")
            if brc < 0 or any(m in text for m in CRASH_MARKERS):
                return (f"{label}-CRASH", f"rc={brc}: {text[-400:]}")
            if brc != 0:
                return (f"{label}-REJECT", text[-400:])
            rrc, rout, rerr = _run([ebin], 10)
            if rrc is None:
                return (f"{label}-HANG", f"program: {rerr}")
            if (rout, rrc) != want:
                return (f"{label}-DIFF", first_diff(want, (rout, rrc)))
    return ("OK", "")

def first_diff(want, got):
    wl, gl = want[0].split("\n"), got[0].split("\n")
    for i in range(max(len(wl), len(gl))):
        a = wl[i] if i < len(wl) else "<eof>"
        b = gl[i] if i < len(gl) else "<eof>"
        if a != b:
            return f"line {i + 1}: C={a!r} eskiu={b!r} (exit C={want[1]} eskiu={got[1]})"
    return f"exit code C={want[1]} eskiu={got[1]}"


# ── Reduction ─────────────────────────────────────────────────────────────────

def _brace_spans(items):
    """Group spans of a brace-balanced Eskiu construct (a block, a whole if/loop)."""
    spans, stack = [], []
    for i, (g, e, _) in enumerate(items):
        if e is None: continue
        opens, closes = e.count("{"), e.count("}")
        for _ in range(closes):
            if stack:
                j = stack.pop()
                spans.append((j, i))
        for _ in range(opens):
            stack.append(i)
    out = []
    for j, i in spans:
        gs = []
        for g, _, _ in items[j:i + 1]:
            if g not in gs: gs.append(g)
        out.append(gs)
    out.sort(key=len, reverse=True)
    return out

def reduce_items(items, pred, log=None):
    """Shrink by dropping whole brace-balanced constructs, then ddmin over line
    groups, repeated until nothing more can go."""
    while True:
        before = len(items)
        items = _reduce_spans(items, pred, log)
        items = _ddmin(items, pred, log)
        if len(items) == before:
            return items

def _reduce_spans(items, pred, log):
    changed = True
    while changed:
        changed = False
        for gs in _brace_spans(items):
            st = set(gs)
            cand = [it for it in items if it[0] not in st]
            if len(cand) < len(items) and pred(cand):
                items = cand
                changed = True
                if log: log(len(items))
                break
    return items

def _ddmin(items, pred, log=None):
    groups = []
    for g, _, _ in items:
        if g not in groups: groups.append(g)
    keep = list(groups)
    def build(gs):
        st = set(gs)
        return [it for it in items if it[0] in st]
    n = 2
    while len(keep) >= 2:
        size = max(1, len(keep) // n)
        chunks = [keep[i:i + size] for i in range(0, len(keep), size)]
        progressed = False
        for ch in chunks:
            cand = [g for g in keep if g not in set(ch)]
            if cand and pred(build(cand)):
                keep = cand
                n = max(n - 1, 2)
                progressed = True
                if log: log(len(keep))
                break
        if not progressed:
            if size == 1: break
            n = min(n * 2, len(keep))
    return build(keep)
