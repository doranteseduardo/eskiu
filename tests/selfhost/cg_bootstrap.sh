#!/usr/bin/env bash
# Three-stage self-hosting bootstrap — the canonical "the compiler builds itself"
# proof, over the unified driver selfhost/esk_main.esk (pp → parse → sema → codegen).
#
#   stage1 (cc0) : the C++ eskiuc compiles esk_main.esk         (built by the old toolchain)
#   stage2 (cc1) : cc0 compiles esk_main.esk → IR → clang        (the FIRST self-built compiler)
#   stage3 (cc2) : cc1 compiles esk_main.esk → IR → clang        (built by the self-built one)
#
# A compiler is a fixpoint when stage2 and stage3 agree: cc1 and cc2 must emit
# byte-identical IR for the compiler's own source. (We compare emitted IR, not the linked
# binaries — Mach-O embeds an LC_UUID + ad-hoc code signature that differ even for
# byte-identical input, an incidental linker artifact, not a compiler property.)
# stage1-vs-stage2 equality is the separate emit-fixpoint that cg_selfhost.sh checks; here
# we go one generation further and also confirm the self-built compiler still WORKS.
#
# Green (exit 0) = cc1 ≡ cc2 (identical IR for the compiler AND for a sample) and cc2
# compiles a runnable sample program correctly.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "cg_bootstrap: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CLANG="${CLANG:-clang}"   # CI installs clang as clang-22; override via $CLANG
command -v "$CLANG" >/dev/null 2>&1 || { echo "cg_bootstrap: $CLANG not found (set CLANG)"; exit 2; }

DRIVER=selfhost/esk_main.esk
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# stage1: the old (C++) toolchain builds the self-hosted compiler.
if ! "$BIN" "$DRIVER" -o "$WORK/cc0" >/dev/null 2>"$WORK/log"; then
    echo "cg_bootstrap: stage1 — C++ eskiuc could not build $DRIVER"; cat "$WORK/log"; exit 2
fi

# stage2: cc0 compiles the compiler → cc1.
if ! "$WORK/cc0" "$DRIVER" > "$WORK/ir1.ll" 2>"$WORK/log"; then
    echo "cg_bootstrap: stage2 — cc0 could not compile $DRIVER"; cat "$WORK/log"; exit 1
fi
if ! "$CLANG" "$WORK/ir1.ll" -o "$WORK/cc1" 2>"$WORK/log"; then
    echo "cg_bootstrap: stage2 — clang rejected cc0's IR"; sed 's/^/      /' "$WORK/log" | head; exit 1
fi

# stage3: cc1 (the self-built compiler) compiles the compiler → cc2.
if ! "$WORK/cc1" "$DRIVER" > "$WORK/ir2.ll" 2>"$WORK/log"; then
    echo "cg_bootstrap: stage3 — cc1 (self-built) could not compile $DRIVER"; cat "$WORK/log"; exit 1
fi
if ! "$CLANG" "$WORK/ir2.ll" -o "$WORK/cc2" 2>"$WORK/log"; then
    echo "cg_bootstrap: stage3 — clang rejected cc1's IR"; sed 's/^/      /' "$WORK/log" | head; exit 1
fi

fail=0

# Fixpoint 1: cc1 and cc2 emit identical IR for the compiler's own source.
if diff -q "$WORK/ir1.ll" "$WORK/ir2.ll" >/dev/null 2>&1; then
    echo "ok    stage2 ≡ stage3 IR (self-built compiler reproduces its own output)"
else
    echo "FAIL  stage2 ≠ stage3 IR — not a fixpoint"; diff "$WORK/ir1.ll" "$WORK/ir2.ll" | head; fail=1
fi

# Fixpoint 2: cc1 and cc2 emit byte-identical IR for a sample program — the two
# self-built generations are the same compiler, not just self-consistent on one input.
printf 'int add(int a, int b) { return a + b; }\nint main() { return add(40, 2); }\n' > "$WORK/sample.esk"
"$WORK/cc1" "$WORK/sample.esk" > "$WORK/s1.ll" 2>/dev/null
"$WORK/cc2" "$WORK/sample.esk" > "$WORK/s2.ll" 2>/dev/null
if diff -q "$WORK/s1.ll" "$WORK/s2.ll" >/dev/null 2>&1 && [ -s "$WORK/s1.ll" ]; then
    echo "ok    cc1 and cc2 emit identical IR for a sample program"
else
    echo "FAIL  cc1 and cc2 diverge on a sample program"; fail=1
fi

# Functional: the self-built compiler still produces a correct, runnable program.
if "$CLANG" "$WORK/s2.ll" -o "$WORK/sample" 2>/dev/null; then
    "$WORK/sample"; code=$?
    if [ "$code" = "42" ]; then echo "ok    cc2 compiles a runnable sample (exit $code)"
    else echo "FAIL  cc2-compiled sample exited $code, expected 42"; fail=1; fi
else
    echo "FAIL  clang rejected cc2's IR for the sample"; fail=1
fi

echo "----"
if [ "$fail" -eq 0 ]; then echo "cg bootstrap: 3-stage self-host fixpoint reached (cc1 ≡ cc2)"
else echo "cg bootstrap: FAILED"; fi
exit "$fail"
