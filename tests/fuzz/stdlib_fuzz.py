#!/usr/bin/env python3
"""
stdlib_fuzz.py: mutation fuzzing of the stdlib parsers under AddressSanitizer.

Each target is an Eskiu program in tests/fuzz/stdlib/ (json, base64, url, regex,
hpack, http, multipart, uuid_time) built with `eskiuc --asan`. The driver mutates
a seed corpus (hand-written seeds plus the string literals of the matching
tests/*.esk files), packs the inputs into batch files, and runs the target on
each batch. A finding is any of:

  CRASH     the target died (a signal, an AddressSanitizer report, or a broken
            invariant: the harnesses check round trips such as base64 and url
            encode/decode, hpack encode/decode, and epoch/calendar conversion)
  HANG      the batch exceeded its time limit
  OOM       the target's resident memory passed the cap (tests/fuzz/fuzz_util.py)

The harness writes each input's index to stderr before running it, so the input
that failed is known; it is re-run alone to confirm, and saved to
tests/fuzz/findings/stdlib_<target>_<seed>_<n>.bin.

Usage:
  python3 tests/fuzz/stdlib_fuzz.py                      # CI mode: bounded, fixed seed
  python3 tests/fuzz/stdlib_fuzz.py --seconds 300        # longer: 5 minutes per target
  python3 tests/fuzz/stdlib_fuzz.py --targets json,hpack --inputs 20000 --seed 7
  python3 tests/fuzz/stdlib_fuzz.py --replay FILE.bin --targets json
Exits non-zero if anything was found.
"""

import argparse, glob, os, pathlib, random, re, struct, sys, tempfile, time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FINDINGS = HERE / "findings"
sys.path.insert(0, str(HERE))
from fuzz_util import run_limited

ASAN_ENV = {"ASAN_OPTIONS": "detect_leaks=0:abort_on_error=0:exitcode=86:allocator_may_return_null=0",
            "ESKIU_ROOT": str(ROOT)}


# ── Seeds ──────────────────────────────────────────────────────────────────────

def esk_unescape(body):
    esc = {"n": 10, "t": 9, "r": 13, "f": 12, "v": 11, "0": 0, "\\": 92, '"': 34, "'": 39}
    out, i = bytearray(), 0
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            n = body[i + 1]
            if n == "x":
                m = re.match(r"[0-9a-fA-F]{1,2}", body[i + 2:])
                if m:
                    out.append(int(m.group(0), 16)); i += 2 + len(m.group(0)); continue
            out.append(esc.get(n, ord(n) & 255)); i += 2; continue
        out += c.encode("utf-8", "replace"); i += 1
    return bytes(out)

def literals(pattern):
    """Every string literal in the test files matching `pattern`."""
    seeds = []
    for p in sorted(glob.glob(str(ROOT / "tests" / pattern))):
        src = open(p, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', src):
            s = esk_unescape(m.group(1))
            if s and len(s) < 4096:
                seeds.append(s)
    return seeds

HPACK_RFC = [   # RFC 7541 Appendix C header blocks (C.2.1, C.3.1-3, C.4.1-3, C.5.1, C.6.1)
    "400a637573746f6d2d6b65790d637573746f6d2d686561646572",
    "828684410f7777772e6578616d706c652e636f6d",
    "828684be58086e6f2d6361636865",
    "828785bf400a637573746f6d2d6b65790c637573746f6d2d76616c7565",
    "828684418cf1e3c2e5f23a6ba0ab90f4ff",
    "828684be5886a8eb10649cbf",
    "828785bf408825a849e95ba97d7f8925a849e95bb8e8b4bf",
    "4803333032580770726976617465611d4d6f6e2c203231204f637420323031332032303a31333a323120474d546e1768747470733a2f2f7777772e6578616d706c652e636f6d",
    "488264025885aec3771a4b6196d07abe941054d444a8200595040b8166e082a62d1bff6e919d29ad171863c78f0b97c8e9ae82ae43d3",
    "3fe11f",          # a dynamic table size update
    "ff80808080808080808001",   # an over-long integer
]

def target_seeds(name):
    if name == "json":
        return literals("json*.esk") + [b'{"a":[1,2,{"b":null}],"c":"\\u00e9\\ud83d\\ude00"}',
                                         b"[[[[[[[[[[]]]]]]]]]]", b"-0.5e-10", b'"\\"', b"{}", b"[1,]"]
    if name == "base64":
        return literals("base64*.esk") + [b"TWFu", b"TWE=", b"TQ==", b"", b"====", b"Zm9v\nYmFy"]
    if name == "url":
        return literals("url*.esk") + [b"a=1&b=%41%42&q=hello+world", b"%", b"%zz", b"a&&=&q"]
    if name == "regex":
        return [s.replace(b"\n", b" ") + b"\n" + t for s, t in
                zip(literals("regex*.esk")[::2], literals("regex*.esk")[1::2])] + [
                b"(a|b)*c\nababc", b"^\\d{2,4}$\n12345", b"[a-z]+@[a-z]+\\.com\nx@y.com",
                b"((a*)*)*b\naaaaaaaaaaaaaaaaaaaaaaaa", b"(\n", b"[\n", b"a{1000}\na", b"\\\n",
                "(?i)[\u03c3k]+\\p{Lu}\n\u03a3\u03c2\u212aK\u00c9".encode(), b"(?s:.)\\Q.*\\E(?m)$\na\n.*\n",
                b"(?P<x>\\pL+)|[^\\W\\d]\n\xff\xc3(\xe9"]
    if name == "hpack":
        return [bytes([4, 128]) + bytes.fromhex(h) for h in HPACK_RFC] + [bytes([3, 255]) + bytes.fromhex(h) for h in HPACK_RFC]
    if name == "http":
        return literals("http*.esk") + [
            b"GET /index.html HTTP/1.1\r\nHost: example.org\r\n\r\n",
            b"POST /up HTTP/1.1\r\nContent-Length: 5\r\nContent-Type: text/plain\r\n\r\nhello",
            b"POST / HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n",
            b"GET / HTTP/1.1\nX: y\n\n", b"\r\n\r\n"]
    if name == "multipart":
        body = (b"--XyZ\r\nContent-Disposition: form-data; name=\"a\"\r\n\r\nvalue\r\n"
                b"--XyZ\r\nContent-Disposition: form-data; name=\"file\"; filename=\"f.bin\"\r\n"
                b"Content-Type: application/octet-stream\r\n\r\n\x00\x01\x02\r\n--XyZ--\r\n")
        return [b"multipart/form-data; boundary=XyZ\n" + body,
                b"multipart/form-data; boundary=\"XyZ\"\n" + body,
                b"multipart/form-data; boundary=\n--\r\n", b"boundary=a\n--a"] + literals("multipart*.esk")
    if name == "uuid_time":
        return [bytes(40), bytes([255] * 40), struct.pack("<qqiiiiii", 1, 1700000000, 2024, 2, 29, 23, 59, 60),
                struct.pack("<qq", -1, -62135596800)]
    raise KeyError(name)

DICTS = {
    "json": [b"{", b"}", b"[", b"]", b'"', b":", b",", b"true", b"false", b"null", b"\\u", b"\\ud800",
             b"\\\\", b"1e999", b"-", b"0.", b"\x00", b" "],
    "base64": [b"=", b"==", b"+", b"/", b"\n", b"A", b"\xff"],
    "url": [b"%", b"%2", b"%41", b"+", b"&", b"=", b"a=", b"q=", b"\xff"],
    "regex": [b"(", b")", b"|", b"*", b"+", b"?", b"{", b"}", b"{2,}", b"[", b"]", b"[^", b"\\d", b"\\w",
              b"\\s", b"\\", b"^", b"$", b".", b"\n", b"(?:", b"-", b"(?i)", b"(?s:", b"(?m)", b"(?U)",
              b"(?P<n>", b"\\Q", b"\\E", b"\\pL", b"\\p{Greek}", b"\\P{Lu}", b"[[:alpha:]]", b"\\x{212A}",
              b"\xc3\xa9", b"\xe2\x84\xaa", b"\xf0\x9f\x98\x80", b"\xff", b"\xce"],
    "hpack": [b"\x80", b"\xff", b"\x7f", b"\x40", b"\x00", b"\x20", b"\x3f\xe1\x1f", b"\x10", b"\x0f"],
    "http": [b"\r\n", b"\r\n\r\n", b": ", b"Content-Length: ", b"content-length:", b"0", b"-1",
             b"Host: ", b" ", b"\t", b"\n", b"GET ", b" HTTP/1.1"],
    "multipart": [b"--", b"\r\n", b"boundary=", b"name=\"", b"\"", b";", b"form-data", b"\r\n\r\n"],
    "uuid_time": [b"\xff\xff\xff\xff", b"\x00\x00\x00\x80", b"\xff\xff\xff\x7f"],
}
MAX_LEN = {"http": 12000, "regex": 600, "hpack": 3000}
TARGETS = ["json", "base64", "url", "regex", "hpack", "http", "multipart", "uuid_time"]


# ── Mutation ───────────────────────────────────────────────────────────────────

INTERESTING = [0, 1, 0x7f, 0x80, 0xff, 0x0a, 0x0d, 0x20, 0x22, 0x25, 0x2b, 0x3d, 0x5c]

def mutate(rng, data, corpus, tokens, max_len):
    b = bytearray(data)
    for _ in range(rng.choice([1, 1, 2, 3, 5, 8])):
        op = rng.randrange(9)
        if op == 0 and b:                                   # flip a bit
            i = rng.randrange(len(b)); b[i] ^= 1 << rng.randrange(8)
        elif op == 1 and b:                                 # set an interesting byte
            b[rng.randrange(len(b))] = rng.choice(INTERESTING)
        elif op == 2:                                       # insert random bytes
            i = rng.randrange(len(b) + 1)
            b[i:i] = bytes(rng.randrange(256) for _ in range(rng.randint(1, 4)))
        elif op == 3 and b:                                 # delete a chunk
            i = rng.randrange(len(b)); del b[i:i + rng.randint(1, 16)]
        elif op == 4 and b:                                 # duplicate a chunk
            i = rng.randrange(len(b)); j = min(len(b), i + rng.randint(1, 32))
            k = rng.randrange(len(b) + 1); b[k:k] = b[i:j] * rng.randint(1, 4)
        elif op == 5 and tokens:                            # insert a dictionary token
            i = rng.randrange(len(b) + 1); b[i:i] = rng.choice(tokens)
        elif op == 6 and corpus:                            # splice another seed
            o = rng.choice(corpus)
            i = rng.randrange(len(b) + 1); j = rng.randrange(len(o) + 1)
            b = b[:i] + o[j:]
        elif op == 7 and b:                                 # overwrite with a token
            t = rng.choice(tokens) if tokens else b"\x00"
            i = rng.randrange(len(b)); b[i:i + len(t)] = t
        elif op == 8 and rng.random() < 0.2:                # repeat a token many times
            t = rng.choice(tokens) if tokens else b"a"
            i = rng.randrange(len(b) + 1); b[i:i] = t * rng.randint(16, 400)
    return bytes(b[:max_len])

def pack(inputs):
    return b"".join(struct.pack("<I", len(x)) + x for x in inputs)


# ── Running ────────────────────────────────────────────────────────────────────

def build(eskiuc, name, out):
    src = HERE / "stdlib" / f"{name}_fuzz.esk"
    env = dict(os.environ, **ASAN_ENV)
    status, rc, sout, serr = run_limited([eskiuc, "--asan", str(src), "-o", out], 300, env)
    if status != "OK" or rc != 0:
        print(f"error: building {src.name} failed:\n{(sout + serr)[-2000:]}", file=sys.stderr)
        sys.exit(2)

def run_batch(binp, inputs, work, timeout):
    bp = os.path.join(work, "batch.bin")
    with open(bp, "wb") as f: f.write(pack(inputs))
    status, rc, sout, serr = run_limited([binp, bp], timeout, dict(os.environ, **ASAN_ENV))
    marks = re.findall(r"^#(\d+)$", serr, re.M)
    last = int(marks[-1]) if marks else None
    if status != "OK":
        return (status, last, serr[-3000:])
    if rc != 0 or "AddressSanitizer" in serr or "FUZZ-INVARIANT" in sout or not sout.startswith("ok "):
        detail = (sout + "\n" + serr)
        return ("CRASH", last, detail[-3000:])
    return ("OK", None, "")

def summarize(detail):
    for pat in (r"FUZZ-INVARIANT: .*", r"ERROR: AddressSanitizer: [^\n]*", r"SUMMARY: [^\n]*"):
        m = re.search(pat, detail)
        if m: return m.group(0)[:200]
    return detail.strip().split("\n")[-1][:200] if detail.strip() else "(no output)"

def fuzz_target(args, name, binp, work):
    rng = random.Random(f"{args.seed}:{name}")
    corpus = target_seeds(name)
    tokens = DICTS.get(name, [])
    max_len = MAX_LEN.get(name, 8192)
    found, done, t0 = 0, 0, time.monotonic()
    # the seeds themselves go first
    pending = list(corpus)
    while True:
        if args.seconds:
            if time.monotonic() - t0 >= args.seconds: break
        elif done >= args.inputs:
            break
        batch = pending[:args.batch]
        pending = pending[args.batch:]
        while len(batch) < args.batch:
            batch.append(mutate(rng, rng.choice(corpus), corpus, tokens, max_len))
        status, last, detail = run_batch(binp, batch, work, args.timeout)
        done += len(batch)
        if status == "OK":
            continue
        culprit = batch[last] if last is not None and last < len(batch) else None
        if culprit is None:
            print(f"[{name} {status}] batch failed before any input ran: {summarize(detail)}")
            found += 1; continue
        # confirm alone (a hang or blowup may need the input by itself)
        s2, _, d2 = run_batch(binp, [culprit], work, args.timeout)
        if s2 == "OK":
            s2, d2 = status, detail            # only reproduces with earlier inputs' state
        found += 1
        FINDINGS.mkdir(exist_ok=True)
        out = FINDINGS / f"stdlib_{name}_{args.seed}_{found}.bin"
        with open(out, "wb") as f: f.write(culprit)
        print(f"[{name} {s2}] {summarize(d2)}\n    input ({len(culprit)} bytes) -> {out}\n    {culprit[:120]!r}")
        if found >= args.max_findings:
            break
    dt = time.monotonic() - t0
    print(f"{name}: {done} inputs in {dt:.1f}s, {found} finding(s)")
    return found

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--targets", default=",".join(TARGETS))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--inputs", type=int, default=1500, help="inputs per target (CI mode)")
    ap.add_argument("--seconds", type=float, default=0, help="time budget per target instead of --inputs")
    ap.add_argument("--batch", type=int, default=250)
    ap.add_argument("--timeout", type=float, default=60, help="seconds per batch")
    ap.add_argument("--max-findings", type=int, default=5, help="stop a target after this many")
    ap.add_argument("--eskiuc", default=os.environ.get("ESKIUC", str(ROOT / "build" / "eskiuc")))
    ap.add_argument("--replay", metavar="FILE", help="run one saved input through the (single) target")
    args = ap.parse_args()
    names = [t for t in args.targets.split(",") if t]
    for t in names:
        if t not in TARGETS:
            print(f"error: unknown target {t} (have {', '.join(TARGETS)})", file=sys.stderr); sys.exit(2)
    total = 0
    with tempfile.TemporaryDirectory() as work:
        for name in names:
            binp = os.path.join(work, name)
            build(args.eskiuc, name, binp)
            if args.replay:
                data = open(args.replay, "rb").read()
                status, _, detail = run_batch(binp, [data], work, args.timeout)
                print(f"{name}: {status} {summarize(detail) if status != 'OK' else ''}")
                total += status != "OK"
                continue
            total += fuzz_target(args, name, binp, work)
    print(f"\nstdlib fuzz: {total} finding(s).")
    sys.exit(1 if total else 0)

if __name__ == "__main__":
    main()
