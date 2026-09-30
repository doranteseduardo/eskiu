#!/usr/bin/env python3
"""
neg_fuzz.py: a negative corpus for both Eskiu compilers.

Each program is a valid one from the C-oracle generator (c_oracle.py) with exactly
one error injected at a random point: an expression error (an undefined name, a
member of a scalar, calling a non-function, the wrong argument count or type, an
operator on a struct or a string, a struct cast, ...), a statement error (assigning
to an rvalue or a const, a stray break/continue, a duplicate local, reading an
uninitialized local, an out-of-range literal, division by a literal zero, an unknown
type, a bad struct or array literal, a duplicate or non-constant case, ...) or a
top-level error (a non-constant global initializer, a duplicate global or function,
a missing return, a duplicate parameter, an unchecked nullable deref, ...). The
injection sites range over every construct the generator emits: globals, function
bodies, nested and unbraced blocks, loops, switch cases, defer bodies, ternaries,
struct and array literals.

Both the C++ `eskiuc` and the self-hosted `eskiuc-esk` must reject the program
(nonzero exit), without crashing (no signal, assertion, LLVM verifier failure or
unlocated codegen error), with at least one `error: <file>:<line>:<col>:`
diagnostic, and one of the reported lines must be the line of the injected error.
Findings: ACCEPT (the compiler took the invalid program), CRASH, HANG/OOM,
UNLOCATED (no file:line:col diagnostic) and LOCATION (no diagnostic on the injected
line). Saved to tests/fuzz/findings/neg_<kind>_<seed>_<index>.esk.

Usage:
  python3 tests/fuzz/neg_fuzz.py --programs 300 --seed 1     # the CI gate
  python3 tests/fuzz/neg_fuzz.py --programs 20000 --seed 7   # a long run
  python3 tests/fuzz/neg_fuzz.py --repro 7:123               # print one program
"""

import argparse, os, pathlib, random, re, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FINDINGS = HERE / "findings"
sys.path.insert(0, str(HERE))
import c_oracle
from c_oracle import Gen, E, Var, I32, BOOL, render
from fuzz_util import run_limited, DEFAULT_JOBS, default_eskiuc_esk

CRASH_MARKERS = ("Assertion", "terminate called", "Stack dump", "PLEASE submit",
                 "libc++abi", "LLVM verification failed", "AddressSanitizer",
                 "code generation failed")


class NegGen(Gen):
    """The C-oracle generator with one injected error; records the error's line."""

    def __init__(self, rng):
        super().__init__(rng, defer=True)
        self.done = False
        self.kind = None
        self.err_gid = None
        self.pending = False
        self.bad_text = None        # an injected expression: the line that carries it
        self.countdown = rng.randint(1, 40)
        self.nid = 0

    def fresh_bad(self, p):
        self.nid += 1
        return f"{p}{self.nid}"

    def mark(self, kind, text=None):
        self.done = True
        self.kind = kind
        self.pending = True
        self.bad_text = text

    def emit(self, esk, c, ind, gid=None):
        g = super().emit(esk, c, ind, gid)
        # An injected expression may be generated and then dropped (the generator can
        # discard a candidate), so its line is the first one that actually contains it.
        if self.pending and esk is not None and (self.bad_text is None or self.bad_text in esk):
            self.err_gid = g
            self.pending = False
        return g

    # ── injection points ──
    def stmt(self, ind):
        if not self.done and self.fn is not None and not self.no_decl:
            self.countdown -= 1
            if self.countdown <= 0 and self.inject_stmt(ind):
                self.budget -= 1
                return
        return super().stmt(ind)

    def expr(self, depth, want=None, const_only=False, in_global=False):
        if (not self.done and self.fn is not None and not const_only and not in_global
                and self.rng.random() < 0.015):
            e = self.bad_expr()
            if e is not None:
                return e
        return super().expr(depth, want, const_only, in_global)

    def gen_global(self):
        if not self.done and self.rng.random() < 0.08 and self.inject_top():
            return
        return super().gen_global()

    def gen_func(self, pure):
        if not self.done and self.rng.random() < 0.08 and self.inject_top():
            return
        return super().gen_func(pure)

    def program(self):
        super().program()
        if self.err_gid is None:
            # nothing injected, or an injected expression never reached the output
            self.done = False
            self.fn = None
            self.inject_top(force=True)
        return self.items

    # ── the catalog ──
    def scalars(self, writable=False):
        return [v for v in self.visible() if v.kind == "scalar" and v.readable
                and (v.writable or not writable) and v.ty is not BOOL]

    def structs_vis(self):
        return [v for v in self.visible() if v.kind == "struct" and v.readable]

    def bad_expr(self):
        rng = self.rng
        sc, st = self.scalars(), self.structs_vis()
        pures = [f for f in self.funcs if f.pure and f.params]
        opts = ["undef", "call_unknown", "str_arith", "float_bitand", "shift_float",
                "addr_lit", "sizeof_unknown", "inc_lit"]
        if sc: opts += ["member_scalar", "call_nonfn", "index_scalar", "deref_int"]
        if st: opts += ["field_unknown", "struct_arith", "cast_struct", "cmp_struct",
                        "not_struct", "neg_struct", "index_struct", "method_unknown"]
        if pures: opts += ["argc"]
        if pures and st: opts += ["argtype"]
        k = rng.choice(opts)
        v = rng.choice(sc) if sc else None
        s = rng.choice(st) if st else None
        text = {
            "undef": lambda: self.fresh_bad("nope"),
            "call_unknown": lambda: f"{self.fresh_bad('nofn')}(1)",
            "str_arith": lambda: '("s" * 2)',
            "float_bitand": lambda: "(1.5 & 1)",
            "shift_float": lambda: "(1 << 2.0)",
            "addr_lit": lambda: "(&5)",
            "sizeof_unknown": lambda: "sizeof(NoType)",
            "inc_lit": lambda: "(5++)",
            "member_scalar": lambda: f"({v.esk}.nope)",
            "call_nonfn": lambda: f"{v.esk}(1)",
            "index_scalar": lambda: f"{v.esk}[0]",
            "deref_int": lambda: f"(* {v.esk})",   # spaced: the generator never writes it
            "field_unknown": lambda: f"({s.esk}.nope)",
            "struct_arith": lambda: f"({s.esk} + 1)",
            "cast_struct": lambda: f"((int32)({s.esk}))",
            "cmp_struct": lambda: f"({s.esk} == {s.esk})",
            "not_struct": lambda: f"(!({s.esk}))",
            "neg_struct": lambda: f"(-({s.esk}))",
            "index_struct": lambda: f"{s.esk}[0]",
            "method_unknown": lambda: f"{s.esk}.nomethod()",
            "argc": lambda: self.call_text(rng.choice(pures), extra=True),
            "argtype": lambda: self.call_text(rng.choice(pures), struct_arg=s),
        }[k]()
        self.mark("expr_" + k, text)
        return E(text, "0", I32)

    def call_text(self, f, extra=False, struct_arg=None):
        args = ["1" for _ in f.params]
        if extra: args.append("1")
        if struct_arg is not None: args[0] = struct_arg.esk
        return f"{f.name}({', '.join(args)})"

    def inject_stmt(self, ind):
        rng = self.rng
        sc, wsc, st = self.scalars(), self.scalars(True), self.structs_vis()
        consts = [g for g in self.globals if g.const_c is not None]
        in_loop, in_sw = self.loop_depth > 0, self.in_switch_case
        opts = ["lit_range", "unknown_type", "void_var", "defer_return", "slit_unknown_struct",
                "arr_over", "const_noinit", "const_assign", "incdec_float", "ptr_from_int",
                "int_from_str", "float_to_int", "return_novalue"]
        mut = [x for x in sc if x.const_c is None]
        if sc: opts += ["assign_rvalue", "switch_dup"]
        if mut: opts += ["case_nonconst"]
        if wsc: opts += ["div_zero", "struct_to_int" if st else "div_zero"]
        if st: opts += ["int_to_struct"]
        if consts: opts += ["const_assign"]
        if not in_loop and not in_sw: opts += ["break_out"]
        if in_sw and not in_loop: opts += ["continue_switch"]
        if in_loop: opts += ["label_unknown"]
        if self.structs: opts += ["slit_field", "slit_many"]
        if self.scopes and [n for n, v in self.scopes[-1].items() if not n.startswith("k")]:
            opts += ["dup_local"]
        if mut and not self.fn.pure: opts += ["static_nonconst"]
        k = rng.choice(opts)
        z = self.fresh_bad("zz")
        v = rng.choice(sc) if sc else None
        w = rng.choice(wsc) if wsc else None
        s = rng.choice(st) if st else None
        if k == "const_assign":
            if consts:
                self.mark(k); self.emit(f"{rng.choice(consts).esk} = 1;", None, ind)
            else:
                self.emit(f"const int32 {z} = 1;", None, ind)
                self.mark(k); self.emit(f"{z} = 2;", None, ind)
            return True
        if k == "incdec_float":
            self.emit(f"double {z} = 1.5;", None, ind)
            self.mark(k); self.emit(f"{z}++;", None, ind)
            return True
        if k == "switch_dup":
            self.emit(f"switch ({v.esk} & 3) {{", None, ind)
            self.emit("case 1: break;", None, ind + 1)
            self.mark(k); self.emit("case 1: break;", None, ind + 1)
            self.emit("}", None, ind)
            return True
        if k == "case_nonconst":
            m = rng.choice(mut)
            self.emit(f"switch ({m.esk} & 3) {{", None, ind)
            self.mark(k); self.emit(f"case {m.esk}: break;", None, ind + 1)
            self.emit("}", None, ind)
            return True
        if k == "dup_local":
            name = rng.choice([n for n in self.scopes[-1] if not n.startswith("k")])
            self.mark(k); self.emit(f"int32 {name} = 1;", None, ind)
            return True
        if k == "slit_field" or k == "slit_many":
            sd = rng.choice(self.structs)
            if k == "slit_field":
                body = "nope: 1"
            else:
                body = ", ".join("0" for _ in range(len(sd.fields) + 1))
            self.mark(k); self.emit(f"{sd.name} {z} = {sd.name} {{ {body} }};", None, ind)
            return True
        line = {
            "lit_range": f"int8 {z} = 300;",
            "unknown_type": f"NoType {z};",
            "void_var": f"void {z};",
            "defer_return": "defer { return 1; }",
            "slit_unknown_struct": f"NoStruct {z} = NoStruct {{ a: 1 }};",
            "arr_over": f"int32[2] {z} = {{1, 2, 3}};",
            "const_noinit": f"const int32 {z};",
            "ptr_from_int": f"*int32 {z} = 5;",
            "int_from_str": f'int32 {z} = "s";',
            "float_to_int": f"int32 {z} = 1.5;",
            "return_novalue": "return;",
            "assign_rvalue": f"({v.esk} + 1) = 2;" if v else "",
            "div_zero": f"{w.esk} = {w.esk} / 0;" if w else "",
            "struct_to_int": f"{w.esk} = {s.esk};" if (w and s) else "",
            "int_to_struct": f"{s.esk} = 5;" if s else "",
            "break_out": "break;",
            "continue_switch": "continue;",
            "label_unknown": "if (true) { break nolabel; }",
            "static_nonconst": f"static int32 {z} = {rng.choice(mut).esk};" if mut else "",
        }[k]
        if not line:
            return False
        self.mark(k); self.emit(line, None, ind)
        return True

    def inject_top(self, force=False):
        rng = self.rng
        opts = ["global_static", "missing_return", "dup_param", "unknown_param",
                "deref_nullable", "unknown_global_type", "void_param", "uninit"]
        if self.funcs: opts += ["global_nonconst_call", "dup_fn"]
        mut = [g for g in self.globals if g.kind == "scalar" and g.const_c is None]
        if mut: opts += ["global_nonconst_read"]
        if self.globals: opts += ["dup_global"]
        k = rng.choice(opts)
        z = self.fresh_bad("gz")
        f = rng.choice(self.funcs) if self.funcs else None
        line = {
            "global_static": f"static int32 {z} = 1;",
            "missing_return": f"int32 {z}(int32 a) {{ if (a > 0) {{ return 1; }} }}",
            "dup_param": f"int32 {z}(int32 a, int32 a) {{ return a; }}",
            "unknown_param": f"int32 {z}(NoType a) {{ return 1; }}",
            "void_param": f"int32 {z}(void a) {{ return 1; }}",
            "deref_nullable": f"int32 {z}(?*int32 q) {{ return *q; }}",
            "unknown_global_type": f"NoType {z};",
            # the uninitialized-read check scans a function's straight-line prefix only
            "uninit": f"int32 {z}() {{ int32 u; return u; }}",
            "global_nonconst_call": (f"int32 {z} = {f.name}({', '.join('1' for _ in f.params)});" if f else ""),
            "dup_fn": (f"int32 {f.name}(int32 a) {{ return a; }}" if f else ""),
            "global_nonconst_read": (f"int32 {z} = {rng.choice(mut).esk} + 1;" if mut else ""),
            "dup_global": (f"int32 {rng.choice(self.globals).esk} = 1;" if self.globals else ""),
        }[k]
        if not line:
            return False
        self.mark(k)
        self.top(line, None)
        return True


def gen_neg(seed, idx):
    rng = random.Random(f"neg:{seed}:{idx}")
    g = NegGen(rng)
    items = g.program()
    esk, _ = render(items)
    line = None
    n = 0
    for gid, e, _ in items:
        if e is None: continue
        n += 1
        if gid == g.err_gid:
            line = n
            break
    return esk, g.kind, line

LOC_RE = re.compile(r"error: [^\n]*?\.esk:(\d+):(\d+):")

def check(esk, line, compilers, timeout=60):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "neg.esk")
        with open(p, "w") as f: f.write(esk)
        for label, argv, env in compilers:
            status, rc, out, err = run_limited(argv + [p, "-o", os.path.join(d, label + ".bin")],
                                               timeout, env)
            text = out + err
            if status != "OK":
                return (f"{label}-{'HANG' if status == 'TIMEOUT' else 'OOM'}", status.lower())
            if rc < 0 or any(m in text for m in CRASH_MARKERS):
                return (f"{label}-CRASH", f"rc={rc}: {text[-400:]}")
            if rc == 0:
                return (f"{label}-ACCEPT", "the invalid program compiled")
            locs = [int(m.group(1)) for m in LOC_RE.finditer(text)]
            if not locs:
                return (f"{label}-UNLOCATED", text.strip()[-300:])
            if line is not None and line not in locs:
                return (f"{label}-LOCATION", f"want line {line}, got {sorted(set(locs))[:6]}: "
                                             f"{text.strip().splitlines()[0][:200]}")
    return ("OK", "")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--programs", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    ap.add_argument("--eskiuc", default=os.environ.get("ESKIUC", str(ROOT / "build" / "eskiuc")))
    ap.add_argument("--eskiuc-esk", default=default_eskiuc_esk())
    ap.add_argument("--repro", metavar="SEED:INDEX")
    args = ap.parse_args()
    if args.repro:
        sd, ix = args.repro.split(":")
        esk, kind, line = gen_neg(int(sd), int(ix))
        print(f"// kind {kind}, error expected on line {line}")
        print(esk); return
    clang = os.environ.get("ESKIU_CLANG") or os.environ.get("CLANG") or "clang"
    env = dict(os.environ, CC=clang, CLANG=clang, ESKIU_ROOT=str(ROOT))
    comps = [("cpp", [args.eskiuc], env)]
    if os.path.exists(args.eskiuc_esk):
        comps.append(("self", [args.eskiuc_esk], env))
    print(f"negative corpus: {args.programs} programs, compilers: {', '.join(c[0] for c in comps)}")
    def one(idx):
        esk, kind, line = gen_neg(args.seed, idx)
        res, detail = check(esk, line, comps)
        return idx, kind, res, detail, esk
    counts, kinds, findings = {}, {}, 0
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for idx, kind, res, detail, esk in ex.map(one, range(args.programs)):
            counts[res] = counts.get(res, 0) + 1
            kinds[kind] = kinds.get(kind, 0) + 1
            if res != "OK":
                findings += 1
                FINDINGS.mkdir(exist_ok=True)
                fn = FINDINGS / f"neg_{res.lower()}_{args.seed}_{idx}.esk"
                with open(fn, "w") as f: f.write(esk + f"// kind {kind}\n")
                print(f"[NEG {res}] {args.seed}:{idx} ({kind}) -> {fn}\n    {detail.strip()[:300]}")
    print(f"negative corpus: {args.programs} programs over {len(kinds)} error kinds, " +
          ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    sys.exit(1 if findings else 0)

if __name__ == "__main__":
    main()
