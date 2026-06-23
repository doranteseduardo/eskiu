#!/usr/bin/env bash
# Parity gate for the self-hosted type checker (selfhost/sema.esk) vs the C++ one.
#
# Sema has no byte-exact dump like the lexer/parser; the contract is the VERDICT.
# So, mirroring tests/run.sh's negative tests:
#   * Positive corpus (tests/*.esk): the self-hosted `tc_main` must reach the same
#     verdict as `eskiuc --test-typechecker` (all currently type-check clean → both
#     exit 0). This guards against the checker FALSELY rejecting valid code.
#   * Negative corpus (tests/errors/*.esk): for the error classes this slice
#     implements (HANDLED below), `tc_main` must reject (exit != 0) AND emit the
#     file's `EXPECT-ERROR:` substring. Error classes not yet implemented are listed
#     as skipped — they come online as later slices add checks.
#
# Green (exit 0) = verdict parity on every positive file + every handled negative.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "tc_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi

# Error classes (files under tests/errors/) that the current sema slice catches.
# Grows per slice; the rest are reported as skipped.
HANDLED="undefined_var arg_count undefined_type"

DRIVER=selfhost/tc_main.esk
TCBIN="$(mktemp -t tc_main.XXXXXX)"
trap 'rm -f "$TCBIN"' EXIT
if ! "$BIN" "$DRIVER" -o "$TCBIN" >/dev/null 2>/tmp/tcbuild.$$; then
    echo "tc_parity: failed to build $DRIVER"; cat /tmp/tcbuild.$$; rm -f /tmp/tcbuild.$$; exit 2
fi
rm -f /tmp/tcbuild.$$

fail=0

# ── positives: verdict must match the C++ oracle ──
pos=0
echo "Positive corpus (verdict must match --test-typechecker):"
for f in tests/*.esk; do
    [ -f "$f" ] || continue
    "$BIN" --test-typechecker "$f" >/dev/null 2>&1; cref=0; [ $? -ne 0 ] && cref=1
    "$TCBIN" "$f" >/dev/null 2>&1; cgot=0; [ $? -ne 0 ] && cgot=1
    if [ "$cref" -eq "$cgot" ]; then
        pos=$((pos + 1))
    else
        echo "  FAIL  ${f#tests/}  (C++ verdict=$cref, self-host=$cgot)"
        fail=1
    fi
done
echo "  ok: $pos/$(ls tests/*.esk | wc -l | tr -d ' ') positive files agree"

# ── negatives: handled error classes must be rejected with the right message ──
echo "Negative corpus (handled error classes):"
for esk in tests/errors/*.esk; do
    [ -e "$esk" ] || continue
    base="$(basename "$esk" .esk)"
    case " $HANDLED " in
        *" $base "*) ;;
        *) echo "  skip  errors/$base  (not yet implemented)"; continue ;;
    esac
    want="$(grep -m1 'EXPECT-ERROR:' "$esk" | sed 's/.*EXPECT-ERROR:[[:space:]]*//')"
    out="$("$TCBIN" "$esk" 2>&1)"; code=$?
    if [ "$code" -eq 0 ]; then
        echo "  FAIL  errors/$base  (accepted code that should be rejected)"; fail=1
    elif [ -n "$want" ] && ! printf '%s' "$out" | grep -qF "$want"; then
        echo "  FAIL  errors/$base  (rejected, but message missing \"$want\")"; fail=1
    else
        echo "  ok    errors/$base"
    fi
done

echo "----"
if [ "$fail" -eq 0 ]; then echo "tc parity: OK"; else echo "tc parity: MISMATCH"; fi
exit "$fail"
