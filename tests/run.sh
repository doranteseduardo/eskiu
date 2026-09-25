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
#   A run or smoke test may have a C companion, tests/NAME.c: it is compiled with
#   $CC and linked into the test binary (used to check calls across the C ABI).
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
# C++ driver for the unit tests: derive from CC (clang-22 → clang++-22) unless set.
CXX="${CXX:-${CC/clang/clang++}}"
LDFLAGS="-lc++ -lpthread -lm"

# Hardening gate: SANITIZE=asan|ubsan compiles every positive test with the
# matching instrumentation and runs it; a sanitizer abort fails the test.
# ubsan traps (no runtime); asan needs -fsanitize=address at link.
SANITIZE="${SANITIZE:-}"
SAN_FLAG=""
case "$SANITIZE" in
    # detect_leaks=0: the gate targets memory *corruption* (overflow / use-after-
    # free — the alloca-bug class), not leaks. Linux asan turns on LeakSanitizer
    # by default, and the test programs are short-lived and not leak-audited, so
    # leak detection stays off here (auditing the suite for leaks is future work).
    asan)  SAN_FLAG="--asan";  LDFLAGS="$LDFLAGS -fsanitize=address"
           export ASAN_OPTIONS="detect_leaks=0" ;;
    ubsan) SAN_FLAG="--ubsan" ;;
    "")    ;;
    *)     echo "error: SANITIZE must be asan, ubsan, or unset" >&2; exit 2 ;;
esac
[[ -n "$SANITIZE" ]] && echo "(sanitizer: $SANITIZE)"

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

    if ! "$ESKIUC" $SAN_FLAG "$esk" -o "$obj" >"$work/cerr" 2>&1; then
        bad "$name" "compile failed: $(head -1 "$work/cerr")"
        continue
    fi
    companion=""
    if [[ -f "$here/$name.c" ]]; then
        companion="$work/$name.c.o"
        if ! $CC -c "$here/$name.c" -o "$companion" >"$work/lerr" 2>&1; then
            bad "$name" "C companion failed to compile: $(head -1 "$work/lerr")"
            continue
        fi
    fi
    if ! $CC "$obj" $companion $LDFLAGS -o "$bin" >"$work/lerr" 2>&1; then
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
        # Smoke test (non-deterministic output, e.g. threads/timing): exit 0 is enough.
        # (The http2 tests once flaked with a Linux-only SIGILL from an uninitialized
        # fn-pointer; that class was fixed at the root when `alloc<T>` began zero-initializing,
        # so the earlier retry mitigation is gone and a crash now reds the build directly.)
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

# ---- lint tests (-Wall) ----------------------------------------------------
# tests/warnings/NAME.esk must type-check under -Wall and emit exactly the warnings
# listed on its `// EXPECT-WARNING: <substring>` lines (none listed = none allowed),
# so a false-positive lint fails the suite.
echo "Lint tests (-Wall):"
if [[ -d "$here/warnings" ]]; then
    for esk in "$here"/warnings/*.esk; do
        [[ -e "$esk" ]] || continue
        name="warnings/$(basename "$esk" .esk)"
        if ! "$ESKIUC" -Wall "$esk" --test-typechecker >"$work/out" 2>&1; then
            bad "$name" "rejected: $(grep -m1 'error' "$work/out")"; continue
        fi
        got="$(grep -c 'warning:' "$work/out")"
        want_n="$(grep -c 'EXPECT-WARNING:' "$esk")"
        missing=""
        while IFS= read -r w; do
            [[ -n "$w" ]] && ! grep -qF "$w" "$work/out" && missing="$w"
        done < <(grep 'EXPECT-WARNING:' "$esk" | sed 's/.*EXPECT-WARNING:[[:space:]]*//')
        if [[ -n "$missing" ]]; then bad "$name" "missing warning \"$missing\""
        elif [[ "$got" -ne "$want_n" ]]; then bad "$name" "$got warning(s), expected $want_n: $(grep -m1 'warning:' "$work/out")"
        else ok "$name"; fi
    done
fi

# ---- single-file test modes ------------------------------------------------
# --test-lexer/--test-parser/--test-typechecker/--test-codegen must see the same
# program a real build does (same predefined macros), and exit 0 on success. A
# positive test marked `// TEST-MODES` on its first line runs through each mode.
echo "Test modes (same preprocessing as a build):"
for esk in "$here"/*.esk; do
    head -1 "$esk" | grep -q 'TEST-MODES' || continue
    name="$(basename "$esk" .esk)"
    for mode in --test-lexer --test-parser --test-typechecker --test-codegen; do
        if "$ESKIUC" "$esk" $mode >"$work/out" 2>&1; then
            ok "$mode/$name"
        else
            bad "$mode/$name" "exited non-zero: $(grep -m1 error "$work/out")"
        fi
    done
done

# ---- eskiuc run -------------------------------------------------------------
# `run [flags] script [--] args`: a flag's separate value is not the script, a `--`
# after the script is dropped, the program's exit code is propagated, and a
# program killed by a signal exits 128+signal like a shell.
echo "run subcommand:"
args_esk="$here/run_cmd/args.esk"
out="$("$ESKIUC" run -o "$work/unused" "$args_esk" -- a "b c" 2>/dev/null)"; code=$?
if [[ $code -eq 3 && "$out" == $'[a]\n[b c]' ]]; then ok "run/args"; else bad "run/args" "exit $code, output '$out'"; fi
"$ESKIUC" run "$args_esk" crash >/dev/null 2>&1; code=$?
if [[ $code -eq 139 ]]; then ok "run/signal"; else bad "run/signal" "exit $code (expected 139)"; fi

# $CC may carry arguments; a $CC that does not exist warns and falls back.
if CC="$CC -O0" "$ESKIUC" "$args_esk" -o "$work/cc_args" >/dev/null 2>&1 && [[ -x "$work/cc_args" ]]; then
    ok "cli/cc-with-args"
else
    bad "cli/cc-with-args" "CC with arguments did not link"
fi
cc_out="$(CC=no-such-cc-driver "$ESKIUC" "$args_esk" -o "$work/cc_bad" 2>&1)"
if [[ -x "$work/cc_bad" && "$cc_out" == *"warning: \$CC"* ]]; then
    ok "cli/cc-missing"
else
    bad "cli/cc-missing" "no fallback warning: $cc_out"
fi

# -Wall points an unused-parameter warning at the parameter itself.
w_out="$("$ESKIUC" -Wall --test-typechecker "$here/run_cmd/unused_param.esk" 2>&1)"
if [[ "$w_out" == *"unused_param.esk:4:11: warning: unused parameter 'unused_p'"* ]]; then
    ok "cli/unused-param-position"
else
    bad "cli/unused-param-position" "$(printf '%s' "$w_out" | grep -m1 warning)"
fi

# `--help` documents the subcommands and lists only Eskiu's options (the LLVM
# backend's internal flags are hidden).
help_out="$("$ESKIUC" --help 2>&1)"
if [[ "$help_out" == *"eskiuc run"* && "$help_out" == *"eskiuc fmt"* && "$help_out" != *"aarch64-neon-syntax"* ]]; then
    ok "cli/help"
else
    bad "cli/help" "subcommands missing or LLVM options listed"
fi

# ---- go-to-definition -------------------------------------------------------
# tests/lsp/NAME.esk lists `// DEF L:C L2:C2` queries: --definition-at L:C must
# resolve to L2:C2 in the same file (the symbol scope lookup picked, not a
# same-named one elsewhere).
echo "Go-to-definition:"
for esk in "$here"/lsp/*.esk; do
    [[ -e "$esk" ]] || continue
    name="lsp/$(basename "$esk" .esk)"
    while read -r q want; do
        got="$("$ESKIUC" "$esk" --definition-at "$q" 2>&1)"
        if [[ "$got" == "$esk:$want" ]]; then
            ok "$name $q"
        else
            bad "$name $q" "expected $want, got '$got'"
        fi
    done < <(grep '^// DEF ' "$esk" | sed 's#^// DEF ##')
done

# ---- formatter idempotency ------------------------------------------------
# `eskiuc fmt` must be idempotent: formatting an already-formatted file is a
# no-op. Format every positive test into a temp file, then assert `fmt --check`
# reports no further change.
echo "Formatter idempotency:"
for esk in "$here"/*.esk; do
    name="$(basename "$esk" .esk)"
    cp "$esk" "$work/$name.fmt.esk"
    "$ESKIUC" fmt "$work/$name.fmt.esk" >/dev/null 2>&1
    if "$ESKIUC" fmt --check "$work/$name.fmt.esk" >/dev/null 2>&1; then
        ok "fmt/$name"
    else
        bad "fmt/$name" "fmt is not idempotent"
    fi
done

# `eskiuc fmt` output: tests/fmt_cases/NAME.esk must format to NAME.formatted
# byte for byte (line endings included).
echo "Formatter output:"
for esk in "$here"/fmt_cases/*.esk; do
    [[ -e "$esk" ]] || continue
    name="fmt_cases/$(basename "$esk" .esk)"
    cp "$esk" "$work/fmtcase.esk"
    "$ESKIUC" fmt "$work/fmtcase.esk" >/dev/null 2>&1
    if cmp -s "$work/fmtcase.esk" "${esk%.esk}.formatted"; then ok "$name"
    else bad "$name" "formatted output differs from ${name}.formatted"; fi
done

# ---- C++ unit tests -------------------------------------------------------
# The typed `Type` IR (sema/type.{h,cpp}) is a standalone, dependency-free unit;
# its round-trip invariant (parse(s).str()==s) is checked here.
echo "Unit tests:"
rt_src="$here/type_roundtrip/roundtrip_test.cpp"
if [ -f "$rt_src" ]; then
    if "${CXX:-clang++}" -std=c++17 "$rt_src" "$root/sema/type.cpp" -I"$root/sema" \
            -o "$work/type_roundtrip" 2>"$work/rt_cerr"; then
        if "$work/type_roundtrip" >/dev/null 2>&1; then ok "unit/type_roundtrip"
        else bad "unit/type_roundtrip" "round-trip assertions failed"; fi
    else
        bad "unit/type_roundtrip" "compile failed: $(head -1 "$work/rt_cerr")"
    fi
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
