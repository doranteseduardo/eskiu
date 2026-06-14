#!/usr/bin/env python3
"""
eskiu_fuzz.py — a small fuzzer for the Eskiu compiler.

It feeds programs to `eskiuc --test-codegen` (which runs codegen + the LLVM IR
verifier) and treats three outcomes as findings:

  CRASH    — the compiler died on a signal / abort (segfault, assertion, uncaught
             C++ exception).  A bug, always.
  VERIFIER — codegen produced IR the LLVM verifier rejected ("LLVM verification
             failed").  A miscompile the verifier caught — exactly the class that
             the sret-argument bug fell into.
  HANG     — the compiler exceeded the timeout.
  DIFF     — a verifier-clean program produced DIFFERENT runtime output at -O0 vs
             -O2 (the differential oracle below).  A miscompile the verifier
             missed: valid IR, wrong semantics that the optimizer diverged on.
  BUILDFAIL— clang could not build the emitted IR at some -O level (IR the LLVM
             verifier accepted but the real pipeline rejects).

A clean rejection (exit non-zero with an `error:` diagnostic) is NOT a finding —
the compiler is allowed to reject bad programs.

Inputs come from two sources:
  · mutation — small edits to the existing tests/*.esk corpus (realistic code).
  · generation — programs biased toward the known bug classes: deep loops with
    many locals, sret-returning (>16-byte) functions called with mixed-width int
    literals, generic functions/structs with fn-typed params, ADT enums + match,
    capturing closures, async/await across control flow, and nested structs.

Usage:
  python3 tests/fuzz/eskiu_fuzz.py --iterations 2000 [--seed 1] [--eskiuc PATH]
Findings are written to tests/fuzz/findings/ and the run exits non-zero if any.
"""

import argparse, os, random, re, subprocess, sys, glob, pathlib

HERE = pathlib.Path(__file__).resolve().parent
TESTS = HERE.parent
ROOT = TESTS.parent
FINDINGS = HERE / "findings"

INT_TYPES = ["int", "int8", "int16", "int32", "int64",
             "uint8", "uint16", "uint32", "uint64", "char", "bool"]
INT_LITERALS = ["0", "1", "-1", "255", "256", "-128", "127", "65535",
                "2147483647", "-2147483648", "4294967295", "9223372036854775807"]


# ── Mutators (operate on source text; parser-invalid results are pruned) ───────

def mut_swap_int_literal(src):
    nums = [m for m in re.finditer(r'(?<![\w.])-?\d+(?![\w.])', src)]
    if not nums: return None
    m = random.choice(nums)
    return src[:m.start()] + random.choice(INT_LITERALS) + src[m.end():]

def mut_swap_int_type(src):
    # change one integer type token to another width/signedness (exercises coercion)
    toks = [m for m in re.finditer(r'\b(' + '|'.join(INT_TYPES) + r')\b', src)]
    if not toks: return None
    m = random.choice(toks)
    return src[:m.start()] + random.choice(INT_TYPES) + src[m.end():]

def mut_dup_line(src):
    lines = src.split("\n")
    body = [i for i, l in enumerate(lines) if l.strip().endswith(";")]
    if not body: return None
    i = random.choice(body)
    lines.insert(i, lines[i])
    return "\n".join(lines)

def mut_dup_local_in_loop(src):
    # duplicate a `let`/decl line — if it lands in a loop body, stresses allocas
    lines = src.split("\n")
    decls = [i for i, l in enumerate(lines)
             if re.search(r'^\s*(let\s+\w+|int|int64|uint8|\*?\w+\s+\w+\s*=)', l)
             and l.strip().endswith(";")]
    if not decls: return None
    i = random.choice(decls)
    lines.insert(i + 1, lines[i])
    return "\n".join(lines)

def mut_delete_line(src):
    lines = src.split("\n")
    if len(lines) < 3: return None
    del lines[random.randrange(len(lines))]
    return "\n".join(lines)

MUTATORS = [mut_swap_int_literal, mut_swap_int_type, mut_dup_line,
            mut_dup_local_in_loop, mut_delete_line]


# ── Generators (biased toward the known bug classes) ───────────────────────────

def gen_loop_locals():
    n = random.randint(3, 12)
    decls = "\n".join(f"        int v{i} = i & {1 << (i % 20)};" for i in range(n))
    sums = " + ".join(f"v{i}" for i in range(n))
    return f"""extern int printf(string fmt, ...);
int main() {{
    int64 acc = 0;
    int i = 0;
    while (i < {random.choice([100000, 500000, 1000000])}) {{
{decls}
        acc = acc + (int64)({sums});
        i = i + 1;
    }}
    printf("%lld\\n", acc);
    return 0;
}}
"""

def gen_sret_mixed_args():
    # a >16-byte-returning function called with int literals of assorted widths
    fields = "\n".join(f"    int64 f{i};" for i in range(random.randint(3, 6)))
    args = ", ".join(random.choice(INT_LITERALS) for _ in range(3))
    return f"""extern int printf(string fmt, ...);
struct Big {{
{fields}
}}
Big make(int a, int64 b, uint8 c) {{
    let r: Big;
    r.f0 = (int64)a + b + (int64)c;
    return r;
}}
int main() {{
    let r: Big = make({args});
    printf("%lld\\n", r.f0);
    return 0;
}}
"""

def gen_generic_fn_param():
    return f"""extern int printf(string fmt, ...);
int apply<T>(fn(T)->T f, T x) {{ return (int)f(x); }}
int dbl(int n) {{ return n * 2; }}
int main() {{
    printf("%d\\n", apply<int>(dbl, {random.choice(INT_LITERALS[:6])}));
    return 0;
}}
"""

def gen_enum_match():
    # an ADT enum with random payload variants + a match that binds them.
    n = random.randint(2, 5)
    specs = [(f"V{i}", random.randint(0, 2)) for i in range(n)]
    variants, arms = [], []
    for name, pc in specs:
        if pc == 0:
            variants.append(f"    {name},")
            arms.append(f"        {name} -> return {random.choice(INT_LITERALS[:6])};")
        else:
            variants.append(f"    {name}({', '.join('int' for _ in range(pc))}),")
            binds = ", ".join(f"p{j}" for j in range(pc))
            body = " + ".join(f"p{j}" for j in range(pc))
            arms.append(f"        {name}({binds}) -> return {body};")
    cname, cpc = random.choice(specs)
    cargs = ", ".join(random.choice(INT_LITERALS[:6]) for _ in range(cpc))
    ctor = f"{cname}({cargs})" if cpc else cname
    nl = "\n"
    return f"""extern int printf(string fmt, ...);
enum E {{
{nl.join(variants)}
}}
int eval(E e) {{
    match e {{
{nl.join(arms)}
    }}
    return -1;
}}
int main() {{
    printf("%d\\n", eval({ctor}));
    return 0;
}}
"""

def gen_closure_capture():
    # a lambda capturing N locals of assorted widths, called directly + via a HOF.
    n = random.randint(1, 4)
    caps = "\n".join(f"    int c{i} = {random.choice(INT_LITERALS[:6])};" for i in range(n))
    capsum = " + ".join(f"c{i}" for i in range(n))
    return f"""extern int printf(string fmt, ...);
int apply(fn(int)->int f, int x) {{ return f(x); }}
int main() {{
{caps}
    let g: fn(int)->int = int(int x) {{ return x + {capsum}; }};
    printf("%d\\n", apply(g, {random.choice(INT_LITERALS[:6])}));
    return 0;
}}
"""

def gen_async_await():
    # an async fn awaiting a ready future, with control flow around the suspend.
    cf = random.choice(["plain", "if", "while"])
    if cf == "plain":
        body = "    int n = await produce();\n    return n + 1;"
    elif cf == "if":
        body = ("    int n = await produce();\n"
                "    if (n > 0) { int m = await produce(); return n + m; }\n"
                "    return n;")
    else:
        body = ("    int acc = 0;\n    int i = 0;\n"
                "    while (i < 3) { int n = await produce(); acc = acc + n; i = i + 1; }\n"
                "    return acc;")
    val = random.choice(["1", "7", "41"])
    return f"""import <future>;
extern int printf(string fmt, ...);
Future<int>* produce() {{
    Future<int>* f = future_new<int>();
    future_complete<int>(f, {val});
    return f;
}}
async int run() {{
{body}
}}
int main() {{
    Future<int>* r = run();
    printf("%d\\n", r.value);
    free_future<int>(r);
    return 0;
}}
"""

def gen_nested_struct():
    # struct-in-struct: nested member access + assignment (GEP / layout stress).
    return f"""extern int printf(string fmt, ...);
struct Inner {{ int a; int b; }}
struct Outer {{ Inner lo; Inner hi; int tag; }}
int main() {{
    let o: Outer;
    o.lo.a = {random.choice(INT_LITERALS[:6])};
    o.lo.b = {random.choice(INT_LITERALS[:6])};
    o.hi.a = {random.choice(INT_LITERALS[:6])};
    o.hi.b = {random.choice(INT_LITERALS[:6])};
    o.tag = o.lo.a + o.hi.b;
    printf("%d\\n", o.tag);
    return 0;
}}
"""

GENERATORS = [gen_loop_locals, gen_sret_mixed_args, gen_generic_fn_param,
              gen_enum_match, gen_closure_capture, gen_async_await,
              gen_nested_struct]


# ── Oracle ─────────────────────────────────────────────────────────────────────

CRASH_MARKERS = ("Assertion", "terminate called", "Stack dump", "PLEASE submit",
                 "libc++abi", "std::__")

def classify(eskiuc, src, timeout=15):
    """Return ('CRASH'|'VERIFIER'|'HANG'|'OK', detail)."""
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".esk", delete=False) as f:
        f.write(src); path = f.name
    try:
        p = subprocess.run([eskiuc, "--test-codegen", path],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        os.unlink(path); return ("HANG", "timeout")
    os.unlink(path)
    out = (p.stdout or "") + (p.stderr or "")
    if "LLVM verification failed" in out:
        return ("VERIFIER", out.split("LLVM verification failed", 1)[1][:300])
    if p.returncode < 0 or p.returncode in (134, 138, 139):
        return ("CRASH", f"rc={p.returncode}: {out[-300:]}")
    if any(mk in out for mk in CRASH_MARKERS):
        return ("CRASH", out[-300:])
    return ("OK", "")


# ── Differential oracle (O0 vs O2) ──────────────────────────────────────────────
#
# A program that passes the IR verifier can still be *miscompiled* — valid IR
# with wrong semantics. The verifier won't catch that, but LLVM's optimizer will
# often diverge on it: if eskiuc emits IR with latent UB (poison, bad attributes,
# aliasing the optimizer is allowed to exploit), -O2 produces different runtime
# behavior than -O0. So: emit the IR once, compile it BOTH at -O0 and -O2 with
# clang, run both, and compare (stdout, exit code). Divergence = a real bug.
#
# Only applied to *un-mutated generated* programs — they are UB-free by
# construction (always-initialized, no OOB, wrapping arithmetic), so any O0/O2
# divergence is the compiler's fault, not the program's. (Mutated programs can
# introduce genuine UB, which would diverge legitimately = false positives.)

def find_clang():
    import shutil
    cand = os.environ.get("ESKIU_CLANG")
    if cand:
        # accept either a full path or a bare command name (e.g. "clang-22")
        return cand if os.path.exists(cand) else shutil.which(cand)
    for name in ("clang", "clang-22", "clang-21", "clang-20"):
        p = shutil.which(name)
        if p:
            return p
    # Homebrew LLVM (matches the toolchain eskiuc itself emits for)
    for p in ("/opt/homebrew/opt/llvm/bin/clang", "/usr/local/opt/llvm/bin/clang"):
        if os.path.exists(p):
            return p
    return None

def emit_ir(eskiuc, src, timeout=15):
    """Compile to IR via --test-codegen and return the textual module, or None."""
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".esk", delete=False) as f:
        f.write(src); path = f.name
    try:
        p = subprocess.run([eskiuc, "--test-codegen", path],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        os.unlink(path); return None
    os.unlink(path)
    # The IR is printed between the two "====" banner separators.
    lines, depth, ir = (p.stdout or "").split("\n"), 0, []
    for ln in lines:
        if ln.startswith("===="):
            depth += 1; continue
        if depth == 1:
            ir.append(ln)
    return "\n".join(ir) if ir else None

def run_bin(path, timeout=10):
    try:
        r = subprocess.run([path], capture_output=True, text=True, timeout=timeout)
        return (r.stdout, r.returncode)
    except subprocess.TimeoutExpired:
        return (None, "timeout")

def differential(clang, ir, timeout=15):
    """Return ('DIFF'|'BUILDFAIL'|'OK', detail). Requires UB-free input."""
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        llp = os.path.join(d, "p.ll")
        with open(llp, "w") as f: f.write(ir)
        outs = {}
        for lvl in ("O0", "O2"):
            binp = os.path.join(d, lvl)
            try:
                c = subprocess.run([clang, f"-{lvl}", "-x", "ir", llp, "-o", binp, "-lm"],
                                   capture_output=True, text=True, timeout=timeout)
            except subprocess.TimeoutExpired:
                return ("BUILDFAIL", f"clang -{lvl} timed out")
            if c.returncode != 0:
                # clang rejected verifier-passing IR — itself a finding.
                return ("BUILDFAIL", f"clang -{lvl}: {(c.stderr or '')[-300:]}")
            outs[lvl] = run_bin(binp)
        if outs["O0"] != outs["O2"]:
            return ("DIFF", f"O0={outs['O0']!r} O2={outs['O2']!r}")
        return ("OK", "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--eskiuc", default=os.environ.get("ESKIUC", str(ROOT / "build" / "eskiuc")))
    ap.add_argument("--no-differential", action="store_true",
                    help="skip the O0-vs-O2 runtime differential (auto-skipped if clang is absent)")
    args = ap.parse_args()
    if args.seed is not None: random.seed(args.seed)

    if not os.path.exists(args.eskiuc):
        print(f"error: eskiuc not found at {args.eskiuc}", file=sys.stderr); sys.exit(2)

    clang = None if args.no_differential else find_clang()
    if not args.no_differential and not clang:
        print("note: clang not found — running without the O0/O2 differential oracle")

    seeds = [open(p).read() for p in glob.glob(str(TESTS / "*.esk"))]
    FINDINGS.mkdir(exist_ok=True)
    findings = 0

    for it in range(args.iterations):
        mutated = False
        if random.random() < 0.35 or not seeds:           # generation
            src = random.choice(GENERATORS)()
            # half the time, also mutate the generated program — combines
            # structural diversity with literal/type/line edge-poking.
            if random.random() < 0.5:
                mutated = True
                for _ in range(random.randint(1, 2)):
                    m = random.choice(MUTATORS)(src)
                    if m is not None: src = m
        else:                                              # mutation
            mutated = True
            src = random.choice(seeds)
            for _ in range(random.randint(1, 3)):
                m = random.choice(MUTATORS)(src)
                if m is not None: src = m

        kind, detail = classify(args.eskiuc, src)
        if kind == "OK" and clang and not mutated:
            # un-mutated generated program: UB-free, so an O0/O2 runtime
            # divergence (or IR clang can't build) is the compiler's fault.
            ir = emit_ir(args.eskiuc, src)
            if ir:
                kind, detail = differential(clang, ir)
        if kind != "OK":
            findings += 1
            fn = FINDINGS / f"{kind.lower()}_{it}.esk"
            with open(fn, "w") as f: f.write(src)
            print(f"[{kind}] iter {it} -> {fn}\n    {detail.strip()[:200]}")

    print(f"\n{args.iterations} iterations, {findings} finding(s).")
    sys.exit(1 if findings else 0)


if __name__ == "__main__":
    main()
