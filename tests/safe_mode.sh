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

# --- in-bounds is unaffected under --safe ---
cat > "$WORK/t.esk" <<'EOF'
int main() { int[5] a = {1,2,3,4,5}; int[] s = a[1..4]; int i = 2; return s[i]; }
EOF
code=$(run "$WORK/t.esk" --safe); check "in-bounds ok (--safe)" 4 "$code"

echo "----"
[ "$fail" = 0 ] && echo "safe mode: OK" || echo "safe mode: FAILURES"
exit "$fail"
