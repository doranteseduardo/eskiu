#!/usr/bin/env bash
# cabi_parity.sh — the self-hosted back-end lowers by-value aggregates across the C ABI
# exactly like the C++ `eskiuc`, for every --target the C++ compiler lowers.
#
# For each C-ABI program and target triple this emits IR with BOTH compilers
# (--test-codegen --target T) and compares, by function name, the lowered signatures:
# every `declare` (externs) and every `@__cabi_*` callback thunk. Parameter names and
# the trailing `{` are stripped, so only types and ABI attributes (sret/byval/align/
# alignstack) are compared. Cross targets can't run here, so this is an IR-level check.
#
# Usage: tests/selfhost/cabi_parity.sh   (from repo root or anywhere)
set -u
export LC_ALL=C
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "cabi_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi
DRIVER=selfhost/esk_main.esk
ESKMAIN="$(mktemp -t esk_main.XXXXXX)"
WORK="$(mktemp -d)"
trap 'rm -f "$ESKMAIN"; rm -rf "$WORK"' EXIT

if ! ESKIU_ROOT="$(pwd)" "$BIN" "$DRIVER" -o "$ESKMAIN" >/dev/null 2>"$WORK/build.log"; then
    echo "cabi_parity: failed to build $DRIVER"; cat "$WORK/build.log"; exit 2
fi

TARGETS="arm64-apple-darwin aarch64-unknown-linux-gnu x86_64-unknown-linux-gnu
x86_64-apple-darwin x86_64-pc-windows-msvc x86_64-w64-windows-gnu
armv7-none-linux-gnueabihf armv6k-none-eabihf armv7-none-linux-gnueabi"
INPUTS="tests/c_abi_struct.esk tests/c_abi_callback.esk tests/c_abi_try.esk tests/c_abi_fnptr.esk tests/bitfield_c_layout.esk tests/c_abi_union.esk"

# The lowered signatures in an IR file, one per line, sorted: `NAME<TAB>signature`. The C++
# names a union's storage type `%U.union` and the self-host `%U`; only the spelling differs.
sigs() {
    grep -E '^(declare .*@|define internal .*@__cabi_)' "$1" \
        | sed -E 's/ %[A-Za-z0-9_.]+([,)])/\1/g; s/ *\{$//; s/^define internal /define /; s/(%[A-Za-z0-9_]+)\.union/\1/g' \
        | sed -E "s/^([a-z]+ [^@]*@)([A-Za-z0-9_.]+)(\\(.*)$/\\2$TAB\\1\\2\\3/" \
        | sort
}

TAB="$(printf '\t')"
fail=0; n=0
for src in $INPUTS; do
    for t in $TARGETS; do
        n=$((n + 1))
        name="$(basename "$src" .esk)@$t"
        if ! ESKIU_ROOT="$(pwd)" "$BIN" "$src" --test-codegen --target "$t" >"$WORK/cpp.ll" 2>&1; then
            echo "FAIL  $name  (C++ codegen failed)"; fail=1; continue
        fi
        if ! ESKIU_ROOT="$(pwd)" "$ESKMAIN" "$src" --test-codegen --target "$t" >"$WORK/esk.ll" 2>"$WORK/esk.err"; then
            echo "FAIL  $name  (self-host codegen failed)"; head -3 "$WORK/esk.err"; fail=1; continue
        fi
        sigs "$WORK/cpp.ll" >"$WORK/cpp.sig"
        sigs "$WORK/esk.ll" >"$WORK/esk.sig"
        # A function both compilers declare (or thunk) must have the same lowered
        # signature, and both must emit the same set of thunks and the same EH personality.
        join -t "$TAB" "$WORK/cpp.sig" "$WORK/esk.sig" >"$WORK/both"
        diffs="$(awk -F"$TAB" '$2 != $3 { print $2 "  vs  " $3 }' "$WORK/both")"
        extra="$( (grep -E '@(__cabi_|__gxx_personality)' "$WORK/cpp.sig" | cut -f1; grep -E '@(__cabi_|__gxx_personality)' "$WORK/esk.sig" | cut -f1) | sort | uniq -u)"
        if [ -n "$diffs" ] || [ -n "$extra" ]; then
            echo "FAIL  $name"
            [ -n "$diffs" ] && echo "$diffs" | sed 's/^/  C++ vs self-host: /' | head -8
            [ -n "$extra" ] && echo "$extra" | sed 's/^/  thunk or personality in one compiler only: /' | head -8
            fail=1
        else
            echo "ok    $name  ($(wc -l <"$WORK/both" | tr -d ' ') signatures)"
        fi
    done
done
# Type sizes follow each target's data layout (pointer width, i64/double alignment): the
# `@sz_*` globals of tests/target_sizes.esk fold to the same values in both compilers.
for t in $TARGETS i686-pc-linux-gnu i686-pc-windows-msvc; do
    n=$((n + 1))
    name="target_sizes@$t"
    ESKIU_ROOT="$(pwd)" "$BIN" tests/target_sizes.esk --test-codegen --target "$t" 2>&1 | grep '^@sz_' >"$WORK/cpp.sz"
    ESKIU_ROOT="$(pwd)" "$ESKMAIN" tests/target_sizes.esk --test-codegen --target "$t" 2>&1 | grep '^@sz_' >"$WORK/esk.sz"
    if [ ! -s "$WORK/cpp.sz" ] || ! cmp -s "$WORK/cpp.sz" "$WORK/esk.sz"; then
        echo "FAIL  $name"; diff "$WORK/cpp.sz" "$WORK/esk.sz" | head -8 | sed 's/^/  /'; fail=1
    else
        echo "ok    $name  ($(tr '\n' ' ' <"$WORK/cpp.sz" | sed -E 's/@sz_([a-z]+) = global i32 /\1=/g'))"
    fi
done
echo "----"
if [ "$fail" = 0 ]; then echo "cabi parity: $n program/target pairs match"; else echo "cabi parity: FAILED"; fi
exit "$fail"
