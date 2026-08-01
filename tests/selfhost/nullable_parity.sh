#!/usr/bin/env bash
# nullable_parity.sh — behavioral parity for the checked nullable pointer `?*T`.
#
# The self-hosted front-end (selfhost/parser.esk + sema.esk) must enforce the same
# nullable rules as the C++ `eskiuc`: an unchecked deref of a `?*T` is rejected,
# `if (x != null)` narrows it so the guarded deref is allowed, `?*T` -> `*T` without a
# check is rejected, and `*T` -> `?*T` widening is allowed. For each program this checks
# that both compilers agree on the accept/reject verdict; the narrowed case also links
# and runs, and must produce the same value on both.
set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "nullable_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CC="${CC:-cc}"
DRIVER=selfhost/esk_main.esk
ESKMAIN="$(mktemp -t esk_main.XXXXXX)"
WORK="$(mktemp -d)"
trap 'rm -f "$ESKMAIN"; rm -rf "$WORK"' EXIT

if ! ESKIU_ROOT="$(pwd)" "$BIN" "$DRIVER" -o "$ESKMAIN" >/dev/null 2>"$WORK/build.log"; then
    echo "nullable_parity: failed to build $DRIVER"; cat "$WORK/build.log"; exit 2
fi

fail=0
# Type-check $src with $comp; echo accept | reject.
verdict() { # comp src
    if ESKIU_ROOT="$(pwd)" "$1" --test-typechecker "$2" >/dev/null 2>&1; then echo accept; else echo reject; fi
}
# Assert both compilers reach the same accept/reject verdict on $src.
parity() { # name src
    local cpp self
    cpp="$(verdict "$BIN" "$2")"; self="$(verdict "$ESKMAIN" "$2")"
    if [ "$cpp" = "$self" ]; then echo "  ok    $1  ($cpp)"; else
        echo "  FAIL  $1  (cpp=$cpp self=$self)"; fail=1; fi
}

cat > "$WORK/deref.esk"   <<'EOF'
int main() { int x = 1; ?*int q = &x; return *q; }
EOF
cat > "$WORK/narrow.esk"  <<'EOF'
int main() { int x = 42; ?*int q = &x; if (q != null) { return *q; } return 0; }
EOF
cat > "$WORK/tononnull.esk" <<'EOF'
int main() { int x = 1; ?*int q = &x; *int r = q; return 0; }
EOF
cat > "$WORK/widen.esk"   <<'EOF'
int main() { int x = 1; *int p = &x; ?*int q = p; if (q != null) { return *q; } return 0; }
EOF

parity "unchecked deref rejected"    "$WORK/deref.esk"
parity "narrowed deref accepted"     "$WORK/narrow.esk"
parity "nullable->non-null rejected" "$WORK/tononnull.esk"
parity "non-null->nullable accepted" "$WORK/widen.esk"

# The narrowed program must not only type-check on both, it must compile and run to the
# same pointee value (42) through each compiler's full pipeline.
cpp_o="$WORK/c.o"; self_bin="$WORK/s"
"$BIN" "$WORK/narrow.esk" -o "$cpp_o" >/dev/null 2>&1 && $CC "$cpp_o" -o "$WORK/c" >/dev/null 2>&1
"$WORK/c" >/dev/null 2>&1; cpp_code=$?
ESKIU_ROOT="$(pwd)" "$ESKMAIN" "$WORK/narrow.esk" -o "$self_bin" >/dev/null 2>&1
"$self_bin" >/dev/null 2>&1; self_code=$?
if [ "$cpp_code" = "$self_code" ] && [ "$cpp_code" = 42 ]; then
    echo "  ok    narrowed deref value  (exit 42)"
else echo "  FAIL  narrowed deref value  (cpp=$cpp_code self=$self_code, want 42)"; fail=1; fi

echo "----"
[ "$fail" = 0 ] && echo "nullable parity: OK" || echo "nullable parity: MISMATCH"
exit "$fail"
