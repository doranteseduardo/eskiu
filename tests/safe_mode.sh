#!/usr/bin/env bash
# safe_mode.sh — verify `--safe` inserts runtime bounds checks that trap on an
# out-of-range slice / array index, that in-bounds access is unaffected, and that
# the checks are OFF by default (opt-in, zero release cost).
set -u
cd "$(dirname "$0")/.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "safe_mode: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CC="${CC:-cc}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0

# Compile $1 (with optional flag $2), run it, echo the exit code.
run() {
    local src="$1" flag="${2:-}"
    "$BIN" $flag "$WORK/t.esk" -o "$WORK/t.o" >/dev/null 2>&1 || { echo "compile-fail"; return; }
    $CC "$WORK/t.o" -o "$WORK/t" >/dev/null 2>&1 || { echo "link-fail"; return; }
    "$WORK/t" >/dev/null 2>&1; echo "$?"
}
check() { # name expected actual
    if [ "$2" = "$3" ]; then echo "  ok    $1"; else echo "  FAIL  $1 (want $2, got $3)"; fail=1; fi
}

# --- slice OOB (traps under --safe) ---
cat > "$WORK/t.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 9; return s[i]; }
EOF
code=$(run "$WORK/t.esk" --safe); [ "$code" != 0 ] && check "slice OOB traps (--safe)" trap trap || check "slice OOB traps (--safe)" trap "exit $code"

# --- default: the OOB read is not trapped (discard it and return 0 to prove no crash) ---
cat > "$WORK/t.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 9; int x = s[i]; return 0; }
EOF
code=$(run "$WORK/t.esk" "");      check "slice OOB no trap (default)" 0 "$code"

# --- array OOB (dynamic index) ---
cat > "$WORK/t.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int i = 7; return a[i]; }
EOF
code=$(run "$WORK/t.esk" --safe); [ "$code" != 0 ] && check "array OOB traps (--safe)" trap trap || check "array OOB traps (--safe)" trap "exit $code"

# --- a slice of a slice with lo > hi (run-time bounds) traps ---
cat > "$WORK/t.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[0..5]; int lo = 3; int hi = 1; int[] t = s[lo..hi]; return (int)t.len; }
EOF
code=$(run "$WORK/t.esk" --safe); [ "$code" != 0 ] && check "slice lo > hi traps (--safe)" trap trap || check "slice lo > hi traps (--safe)" trap "exit $code"

# --- in-bounds is unaffected under --safe ---
cat > "$WORK/t.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 2; return s[i]; }
EOF
code=$(run "$WORK/t.esk" --safe); check "in-bounds ok (--safe)" 4 "$code"

# --- signed overflow and null dereference trap under --safe ---
trap_case() { # name source: traps under --safe
    printf '%s\n' "$2" > "$WORK/t.esk"
    code=$(run "$WORK/t.esk" --safe); [ "$code" != 0 ] && check "$1 traps (--safe)" trap trap || check "$1 traps (--safe)" trap "exit $code"
}
trap_case "int + overflow" 'int main() { int a = 2147483647; int b = a; b = b - 2147483646; return a + b; }'
trap_case "int * overflow" 'int main() { int a = 65536; int b = a; return a * b; }'
trap_case "int64 - overflow" 'int main() { int64 a = -9223372036854775807; int64 b = 2; return (int)(a - b); }'
trap_case "null deref" 'int main() { *int p = null; return *p; }'
trap_case "null member" 'struct P { int x; }
int main() { *P p = null; return p.x; }'
trap_case "null index" 'int main() { *int p = null; int i = 3; return p[i]; }'
printf '%s\n' 'int main() { int a = 2147483647; int b = a + 1; return b == -2147483647 - 1 ? 0 : 1; }' > "$WORK/t.esk"
code=$(run "$WORK/t.esk" "");      check "signed overflow wraps (default)" 0 "$code"
printf '%s\n' 'int main() { uint32 u = 4294967295; u = u + (uint32)1; int8 c = 127; c = c + (int8)1; int x = 5; *int p = &x; return u == (uint32)0 && c == (int8)-128 && *p == 5 ? 0 : 1; }' > "$WORK/t.esk"
code=$(run "$WORK/t.esk" --safe); check "unsigned, narrow and non-null pass (--safe)" 0 "$code"

echo "----"
[ "$fail" = 0 ] && echo "safe mode: OK" || echo "safe mode: FAILURES"
exit "$fail"
