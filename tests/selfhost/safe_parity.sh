#!/usr/bin/env bash
# safe_parity.sh — behavioral parity for the `--safe` bounds-check mirror.
#
# The self-hosted back-end (selfhost/codegen.esk) must insert the same runtime
# slice/array bounds checks as the C++ `eskiuc` under `--safe`: an out-of-range
# index traps, an in-bounds one does not, and without `--safe` there is no check.
# For each program this builds a native binary with BOTH compilers and asserts the
# exit codes agree (a trap shows up as the same non-zero signal exit on both).
#
# Usage: tests/selfhost/safe_parity.sh   (from repo root or anywhere)
set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "safe_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CC="${CC:-cc}"
DRIVER=selfhost/esk_main.esk
ESKMAIN="$(mktemp -t esk_main.XXXXXX)"
WORK="$(mktemp -d)"
trap 'rm -f "$ESKMAIN"; rm -rf "$WORK"' EXIT

# Build the self-hosted compiler with the C++ seed.
if ! ESKIU_ROOT="$(pwd)" "$BIN" "$DRIVER" -o "$ESKMAIN" >/dev/null 2>"$WORK/build.log"; then
    echo "safe_parity: failed to build $DRIVER"; cat "$WORK/build.log"; exit 2
fi

fail=0
# The C++ eskiuc emits an object (link it with $CC); the self-hosted esk_main's `-o`
# invokes clang itself and writes a native binary. Compile $src with flag $flag, run the
# result, echo the exit code. $self=1 selects the self-hosted (already-linked) path.
run() { # comp src flag self
    local comp="$1" src="$2" flag="${3:-}" self="${4:-0}"
    if [ "$self" = 1 ]; then
        ESKIU_ROOT="$(pwd)" "$comp" $flag "$src" -o "$WORK/t" >/dev/null 2>&1 || { echo "compile-fail"; return; }
    else
        ESKIU_ROOT="$(pwd)" "$comp" $flag "$src" -o "$WORK/t.o" >/dev/null 2>&1 || { echo "compile-fail"; return; }
        $CC "$WORK/t.o" -o "$WORK/t" >/dev/null 2>&1 || { echo "link-fail"; return; }
    fi
    "$WORK/t" >/dev/null 2>&1; echo "$?"
}
# Assert both compilers give the same exit code for $src under flag $flag.
parity() { # name src flag
    local name="$1" src="$2" flag="${3:-}"
    local cpp self
    cpp="$(run "$BIN" "$src" "$flag" 0)"
    self="$(run "$ESKMAIN" "$src" "$flag" 1)"
    if [ "$cpp" = "$self" ]; then echo "  ok    $name  (exit $cpp)"; else
        echo "  FAIL  $name  (cpp=$cpp self=$self)"; fail=1; fi
}

cat > "$WORK/slice_oob.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 9; return s[i]; }
EOF
cat > "$WORK/array_oob.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int i = 7; return a[i]; }
EOF
cat > "$WORK/inbounds.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 2; return s[i]; }
EOF
cat > "$WORK/default_noop.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 9; int x = s[i]; return 0; }
EOF

parity "slice OOB traps (--safe)"     "$WORK/slice_oob.esk"    --safe
parity "array OOB traps (--safe)"     "$WORK/array_oob.esk"    --safe
parity "in-bounds ok (--safe)"        "$WORK/inbounds.esk"     --safe
parity "no check without --safe"      "$WORK/default_noop.esk" ""

echo "----"
[ "$fail" = 0 ] && echo "safe parity: OK" || echo "safe parity: MISMATCH"
exit "$fail"
