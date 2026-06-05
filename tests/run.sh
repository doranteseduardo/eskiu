#!/usr/bin/env bash
#
# Eskiu test runner.
#
# Three kinds of tests, all driven off the files in this directory:
#
#   1. run    tests/NAME.esk + tests/NAME.expected
#             Compile -> link -> execute, then require stdout to match
#             NAME.expected exactly. A regression that changes output FAILS.
#
#   2. smoke  tests/NAME.esk with NO NAME.expected
#             Compile -> link -> execute, require exit code 0 only.
#             Used for tests whose output is non-deterministic (threads) or
#             which only need to prove they build and run.
#
#   3. error  tests/errors/NAME.esk
#             Run --test-typechecker; require a NON-zero exit AND that the
#             diagnostics contain the substring after "EXPECT-ERROR:" on the
#             file's first line. Proves the compiler REJECTS bad code.
#
# Usage:  tests/run.sh            (from repo root or anywhere)
#         ESKIUC=/path/eskiuc tests/run.sh
#
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
ESKIUC="${ESKIUC:-$root/build/eskiuc}"
CC="${CC:-clang}"
LDFLAGS="-lc++ -lpthread"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
failed_names=()

if [[ ! -x "$ESKIUC" ]]; then
    echo "error: compiler not found at $ESKIUC (build it: cmake --build build)" >&2
    exit 2
fi

ok()   { printf '  \033[32mPASS\033[0m  %s\n' "$1"; pass=$((pass+1)); }
bad()  { printf '  \033[31mFAIL\033[0m  %s — %s\n' "$1" "$2"; fail=$((fail+1)); failed_names+=("$1"); }

# ---- positive tests (run + smoke) -----------------------------------------
echo "Positive tests:"
for esk in "$here"/*.esk; do
    name="$(basename "$esk" .esk)"
    obj="$work/$name.o"
    bin="$work/$name"

    if ! "$ESKIUC" "$esk" -o "$obj" >"$work/cerr" 2>&1; then
        bad "$name" "compile failed: $(head -1 "$work/cerr")"
        continue
    fi
    if ! $CC "$obj" $LDFLAGS -o "$bin" >"$work/lerr" 2>&1; then
        bad "$name" "link failed: $(head -1 "$work/lerr")"
        continue
    fi
    "$bin" >"$work/out" 2>&1
    code=$?

    expected="$here/$name.expected"
    if [[ -f "$expected" ]]; then
        if [[ $code -ne 0 ]]; then
            bad "$name" "exited $code (expected 0)"
        elif diff -u "$expected" "$work/out" >"$work/diff" 2>&1; then
            ok "$name"
        else
            bad "$name" "output mismatch"
            sed 's/^/        /' "$work/diff"
        fi
    else
        # smoke test: exit 0 is enough
        if [[ $code -eq 0 ]]; then
            ok "$name (smoke)"
        else
            bad "$name" "exited $code (smoke test expects 0)"
        fi
    fi
done

# ---- negative tests (must be rejected) ------------------------------------
echo "Negative tests (must fail to type-check):"
if [[ -d "$here/errors" ]]; then
    for esk in "$here"/errors/*.esk; do
        [[ -e "$esk" ]] || continue
        name="errors/$(basename "$esk" .esk)"
        want="$(grep -m1 'EXPECT-ERROR:' "$esk" | sed 's/.*EXPECT-ERROR:[[:space:]]*//')"

        "$ESKIUC" "$esk" --test-typechecker >"$work/out" 2>&1
        code=$?
        if [[ $code -eq 0 ]]; then
            bad "$name" "compiler ACCEPTED code that should be rejected"
        elif [[ -n "$want" ]] && ! grep -qF "$want" "$work/out"; then
            bad "$name" "rejected, but message missing expected text: \"$want\""
        else
            ok "$name"
        fi
    done
fi

# ---- summary --------------------------------------------------------------
echo
echo "------------------------------------------------------------"
printf 'Results: \033[32m%d passed\033[0m, \033[31m%d failed\033[0m\n' "$pass" "$fail"
if [[ $fail -gt 0 ]]; then
    printf 'Failures: %s\n' "${failed_names[*]}"
    exit 1
fi
exit 0
