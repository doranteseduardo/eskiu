#!/usr/bin/env bash
# nullable.sh — verify the checked nullable pointer `?*T`: a `?*T` can't be
# dereferenced without a null-check, `if (x != null)` narrows it, `*T` widens to
# `?*T` but not vice-versa, and a narrowed deref produces the right value.
# (This gates the C++ compiler; the self-host mirror is gated by
# tests/selfhost/nullable_parity.sh.)
set -u
cd "$(dirname "$0")/.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "nullable: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CC="${CC:-cc}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0
compiles() { "$BIN" "$WORK/t.esk" -o "$WORK/t.o" >/dev/null 2>&1; }
check() { if [ "$2" = "$3" ]; then echo "  ok    $1"; else echo "  FAIL  $1 (want $2, got $3)"; fail=1; fi }

# 1. unchecked deref of a ?*T -> compile error
cat > "$WORK/t.esk" <<'EOF'
int main() { int x = 1; ?*int q = &x; return *q; }
EOF
compiles && check "unchecked deref rejected" reject accept || check "unchecked deref rejected" reject reject

# 2. narrowed deref -> compiles and runs to the pointee value
cat > "$WORK/t.esk" <<'EOF'
int main() { int x = 42; ?*int q = &x; if (q != null) { return *q; } return 0; }
EOF
if compiles && $CC "$WORK/t.o" -o "$WORK/t" >/dev/null 2>&1; then
    "$WORK/t"; check "narrowed deref value" 42 "$?"
else check "narrowed deref value" 42 "compile/link-fail"; fi

# 3. ?*T -> *T without a check -> error
cat > "$WORK/t.esk" <<'EOF'
int main() { int x = 1; ?*int q = &x; *int r = q; return 0; }
EOF
compiles && check "nullable->non-null rejected" reject accept || check "nullable->non-null rejected" reject reject

# 4. *T -> ?*T (widen) -> ok
cat > "$WORK/t.esk" <<'EOF'
int main() { int x = 1; *int p = &x; ?*int q = p; if (q != null) { return *q; } return 0; }
EOF
compiles && check "non-null->nullable ok" accept accept || check "non-null->nullable ok" accept reject

echo "----"
[ "$fail" = 0 ] && echo "nullable: OK" || echo "nullable: FAILURES"
exit "$fail"
