#!/usr/bin/env bash
# Driver parity gate for the self-hosted compiler driver (selfhost/esk_main.esk).
#
# Unlike cg_parity (which compares raw codegen), this exercises the WHOLE driver:
# `esk_main <file.esk> -o <out>` parses, type-checks, codegens, writes a temp .ll, and
# invokes clang to link a native binary — the same end-to-end path the C++ `eskiuc`
# takes. Oracle is behavioral: run both binaries and compare exit code + stdout.
#
# Usage: tests/selfhost/driver_parity.sh [file.esk ...]
#   no args -> the corpus under tests/selfhost/driver_inputs/
# Green (exit 0) = identical exit code and stdout for every program.

set -u
cd "$(dirname "$0")/../.." || exit 2
ROOT="$(pwd)"

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "driver_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CLANG="${CLANG:-clang}"   # the driver shells out to $CLANG (CI installs it as clang-22)
export CLANG
command -v "$CLANG" >/dev/null 2>&1 || { echo "driver_parity: $CLANG not found (set CLANG)"; exit 2; }

# Build the self-hosted driver with the C++ compiler.
ESKMAIN="$(mktemp -t esk_main.XXXXXX)"
WORK="$(mktemp -d)"
trap 'rm -f "$ESKMAIN"; rm -rf "$WORK"' EXIT
if ! "$BIN" selfhost/esk_main.esk -o "$ESKMAIN" >/dev/null 2>"$WORK/build.log"; then
    echo "driver_parity: failed to build selfhost/esk_main.esk"; cat "$WORK/build.log"; exit 2
fi

if [ "$#" -gt 0 ]; then files=("$@"); else files=(tests/selfhost/driver_inputs/*.esk); fi

fail=0
total=0
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "MISS  $f"; fail=1; continue; }
    total=$((total + 1))
    base="$(basename "$f" .esk)"

    # Self-hosted driver: parse → sema → codegen → clang, all in the Eskiu binary.
    if ! ESKIU_ROOT="$ROOT" "$ESKMAIN" "$f" -o "$WORK/$base.self" 2>"$WORK/$base.self.err"; then
        echo "FAIL  $base  (self-host driver errored)"; sed 's/^/      /' "$WORK/$base.self.err" | grep -v 'overriding the module' | head; fail=1; continue
    fi
    self_out="$("$WORK/$base.self" 2>/dev/null)"; self_code=$?

    # Reference: the C++ driver.
    if ! "$BIN" "$f" -o "$WORK/$base.cpp" >/dev/null 2>&1; then
        echo "skip  $base  (C++ eskiuc could not build it)"; total=$((total - 1)); continue
    fi
    cpp_out="$("$WORK/$base.cpp" 2>/dev/null)"; cpp_code=$?

    if [ "$self_code" = "$cpp_code" ] && [ "$self_out" = "$cpp_out" ]; then
        echo "ok    $base  (exit $self_code)"
    else
        echo "FAIL  $base  (self exit=$self_code out=$self_out | cpp exit=$cpp_code out=$cpp_out)"; fail=1
    fi
done

echo "----"
if [ "$fail" -eq 0 ]; then echo "driver parity: $total/$total programs match"; else echo "driver parity: MISMATCH"; fi
exit "$fail"
