#!/usr/bin/env bash
# Behavioral parity gate for the self-hosted code generator (selfhost/codegen.esk).
#
# LLVM auto-numbers SSA values and constant-folds, so the emitted .ll can't be
# matched byte-for-byte against `--test-codegen`. Instead the oracle is BEHAVIORAL:
# emit .ll → clang → run, and compare the exit code + stdout against the binary the
# C++ `eskiuc` produces from the same source. Same observable behavior = parity.
#
# Usage: tests/selfhost/cg_parity.sh [file.esk ...]
#   no args -> the synthetic corpus under tests/selfhost/cg_inputs/
# Green (exit 0) = identical exit code and stdout for every program.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "cg_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CLANG="${CLANG:-clang}"   # CI installs clang as clang-22; override via $CLANG
command -v "$CLANG" >/dev/null 2>&1 || { echo "cg_parity: $CLANG not found (set CLANG)"; exit 2; }

DRIVER=selfhost/esk_main.esk
CGBIN="$(mktemp -t cg_main.XXXXXX)"
WORK="$(mktemp -d)"
trap 'rm -f "$CGBIN"; rm -rf "$WORK"' EXIT
if ! "$BIN" "$DRIVER" -o "$CGBIN" >/dev/null 2>"$WORK/build.log"; then
    echo "cg_parity: failed to build $DRIVER"; cat "$WORK/build.log"; exit 2
fi

if [ "$#" -gt 0 ]; then files=("$@"); else files=(tests/selfhost/cg_inputs/*.esk); fi

fail=0
total=0
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "MISS  $f"; fail=1; continue; }
    total=$((total + 1))
    base="$(basename "$f" .esk)"

    # Self-hosted: emit .ll, compile with clang, run.
    if ! ESKIU_ROOT="$(pwd)" "$CGBIN" --test-codegen "$f" > "$WORK/$base.ll" 2>"$WORK/$base.emit.err"; then
        echo "FAIL  $base  (self-host codegen errored)"; sed 's/^/      /' "$WORK/$base.emit.err" | head; fail=1; continue
    fi
    if ! "$CLANG" "$WORK/$base.ll" -lc++abi -o "$WORK/$base.self" 2>"$WORK/$base.clang.err"; then
        echo "FAIL  $base  (clang rejected emitted .ll)"; sed 's/^/      /' "$WORK/$base.clang.err" | head; fail=1; continue
    fi
    self_out="$("$WORK/$base.self" 2>/dev/null)"; self_code=$?

    # Reference: the C++ build. Link libc++abi so exception programs (which need
    # the Itanium __cxa_* runtime) link on both sides, matching the self-host clang
    # invocation above.
    if ! "$BIN" "$f" -lc++abi -o "$WORK/$base.cpp" >/dev/null 2>&1; then
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
if [ "$fail" -eq 0 ]; then echo "cg parity: $total/$total programs match"; else echo "cg parity: MISMATCH"; fi
exit "$fail"
