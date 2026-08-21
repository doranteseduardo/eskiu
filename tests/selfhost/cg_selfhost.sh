#!/usr/bin/env bash
# Phase D gate: the self-hosted code generator compiles the WHOLE self-hosted compiler.
#
# Two checks, over every selfhost/*.esk module (lexer, preprocessor, parser, sema,
# codegen, the drivers) + the cg_inputs corpus:
#   1. EMIT-VALIDITY  — cg_main (built by the C++ eskiuc) emits .ll that clang accepts.
#   2. BOOTSTRAP FIXPOINT — cg_main.self (cg_main compiled by ITSELF) emits byte-identical
#      .ll to cg_main for every input, including the compiler's own source. Equal output
#      from the C++-built and self-built code generators = a self-consistent fixpoint.
#
# Green (exit 0) = the self-hosted codegen reproduces itself and the whole compiler.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "cg_selfhost: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
CLANG="${CLANG:-clang}"   # CI installs clang as clang-22; override via $CLANG
command -v "$CLANG" >/dev/null 2>&1 || { echo "cg_selfhost: $CLANG not found (set CLANG)"; exit 2; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Build the code generator two ways: by the C++ eskiuc, and by itself.
if ! "$BIN" selfhost/cg_main.esk -o "$WORK/cg_main" 2>"$WORK/b.log"; then
    echo "cg_selfhost: C++ eskiuc could not build cg_main.esk"; cat "$WORK/b.log"; exit 2
fi
if ! "$WORK/cg_main" selfhost/cg_main.esk > "$WORK/cg_main.ll" 2>"$WORK/e.log"; then
    echo "cg_selfhost: cg_main could not emit IR for itself"; cat "$WORK/e.log"; exit 2
fi
if ! "$CLANG" "$WORK/cg_main.ll" -o "$WORK/cg_main.self" 2>"$WORK/c.log"; then
    echo "cg_selfhost: clang rejected self-emitted cg_main.ll"; sed 's/^/      /' "$WORK/c.log" | head; exit 2
fi

MODULES="selfhost/tokens.esk selfhost/ast.esk selfhost/lexer.esk selfhost/preprocessor.esk \
         selfhost/parser.esk selfhost/sema.esk selfhost/codegen.esk selfhost/fmt.esk selfhost/cg_main.esk \
         selfhost/lex_main.esk selfhost/pp_main.esk selfhost/parse_main.esk selfhost/tc_main.esk"
INPUTS="$MODULES tests/selfhost/cg_inputs/*.esk"

fail=0; total=0
for f in $INPUTS; do
    [ -f "$f" ] || continue
    total=$((total + 1))
    base="$(basename "$f")"
    # 1. emit-validity (C++-built codegen → clang -c)
    if ! "$WORK/cg_main" "$f" > "$WORK/a.ll" 2>/dev/null; then echo "FAIL  $base (emit)"; fail=1; continue; fi
    if ! "$CLANG" -c "$WORK/a.ll" -o "$WORK/a.o" 2>"$WORK/cc.log"; then
        echo "FAIL  $base (clang rejected IR)"; sed 's/^/      /' "$WORK/cc.log" | head -3; fail=1; continue
    fi
    # 2. bootstrap fixpoint (self-built codegen emits identical IR)
    "$WORK/cg_main.self" "$f" > "$WORK/b.ll" 2>/dev/null
    if ! diff -q "$WORK/a.ll" "$WORK/b.ll" >/dev/null 2>&1; then
        echo "FAIL  $base (self-codegen IR differs from C++-codegen)"; fail=1; continue
    fi
    echo "ok    $base"
done

echo "----"
if [ "$fail" -eq 0 ]; then echo "cg selfhost: $total/$total modules emit valid IR + reproduce the fixpoint"
else echo "cg selfhost: MISMATCH"; fi
exit "$fail"
