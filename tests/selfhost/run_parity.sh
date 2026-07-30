#!/usr/bin/env bash
# `run` parity gate for the self-hosted compiler driver (selfhost/esk_main.esk).
#
# Exercises the whole `eskiuc run script.esk [args...]` path: compile to a temp exe,
# execute it forwarding [args...], propagate its exit code, delete it. Oracle is the
# C++ `eskiuc run` on the same programs: identical exit code + stdout for every one.
#
# Usage: tests/selfhost/run_parity.sh [file.esk ...]
#   no args -> the corpus under tests/selfhost/driver_inputs/
# Green (exit 0) = the self-host `run` matches the C++ `run` everywhere.

set -u
cd "$(dirname "$0")/../.." || exit 2
ROOT="$(pwd)"

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "run_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CLANG="${CLANG:-clang}"   # the self-host driver shells out to $CLANG (CI installs it as clang-22)
export CLANG
command -v "$CLANG" >/dev/null 2>&1 || { echo "run_parity: $CLANG not found (set CLANG)"; exit 2; }

# Build the self-hosted driver with the C++ compiler.
ESKMAIN="$(mktemp -t esk_main.XXXXXX)"
WORK="$(mktemp -d)"
trap 'rm -f "$ESKMAIN"; rm -rf "$WORK"' EXIT
if ! "$BIN" selfhost/esk_main.esk -o "$ESKMAIN" >/dev/null 2>"$WORK/build.log"; then
    echo "run_parity: failed to build selfhost/esk_main.esk"; cat "$WORK/build.log"; exit 2
fi

if [ "$#" -gt 0 ]; then files=("$@"); else files=(tests/selfhost/driver_inputs/*.esk); fi

fail=0
total=0
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "MISS  $f"; fail=1; continue; }
    base="$(basename "$f" .esk)"

    # Reference: the C++ `run`. Skip programs the C++ compiler cannot build.
    cpp_out="$(ESKIU_ROOT="$ROOT" "$BIN" run "$f" 2>/dev/null)"; cpp_code=$?
    if ! ESKIU_ROOT="$ROOT" "$BIN" run "$f" >/dev/null 2>"$WORK/$base.cpp.err"; then
        if grep -qi 'error' "$WORK/$base.cpp.err"; then
            echo "skip  $base  (C++ run could not build it)"; continue
        fi
    fi
    total=$((total + 1))

    # Self-hosted `run`: compile to a temp exe, execute, propagate exit + stdout.
    self_out="$(ESKIU_ROOT="$ROOT" "$ESKMAIN" run "$f" 2>/dev/null)"; self_code=$?

    if [ "$self_code" = "$cpp_code" ] && [ "$self_out" = "$cpp_out" ]; then
        echo "ok    $base  (exit $self_code)"
    else
        echo "FAIL  $base  (self exit=$self_code out=$self_out | cpp exit=$cpp_code out=$cpp_out)"; fail=1
    fi
done

echo "----"
if [ "$fail" -eq 0 ]; then echo "run parity: $total/$total programs match"; else echo "run parity: MISMATCH"; fi
exit "$fail"
