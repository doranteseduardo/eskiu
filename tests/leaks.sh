#!/usr/bin/env bash
# leaks.sh — run targeted memory-sensitive tests under AddressSanitizer with leak detection
#
# Unlike tests/run.sh (which sets detect_leaks=0 to avoid false noise from short-lived,
# un-audited test programs), this suite validates memory-critical language constructs
# where leaks must NEVER occur:
#   - async coroutine frames and closure environments
#   - cancelled futures and deferred cleanups
#   - channels and event loop task lifecycles
#   - defer and exception unwind stacks
#   - explicit allocators and free
#
# Usage: ./tests/leaks.sh [path-to-eskiuc]

set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"

ESKIUC="${1:-${ESKIUC:-$repo/build/eskiuc}}"
if [[ ! -x "$ESKIUC" ]]; then
    echo "error: compiler not found at $ESKIUC (build it first: cmake --build build)" >&2
    exit 2
fi

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
failed_names=()

ok()  { printf '  \033[32mPASS\033[0m  %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  \033[31mFAIL\033[0m  %s — %s\n' "$1" "$2"; fail=$((fail+1)); failed_names+=("$1"); }

TESTS=(
    "async_closure_env.esk"
    "async_cancel.esk"
    "async_cancel_await_cleanup.esk"
    "async_finally_await.esk"
    "async_channel.esk"
    "async_try_cancel.esk"
    "defer.esk"
    "defer_unwind.esk"
    "alloc.esk"
    "alloc_with.esk"
    "alloc_with_method.esk"
)

echo "Running leak-checked suite (ASAN detect_leaks=1):"
for test_file in "${TESTS[@]}"; do
    esk="$here/$test_file"
    if [[ ! -f "$esk" ]]; then
        bad "$test_file" "file not found"
        continue
    fi
    name="$(basename "$test_file" .esk)"
    bin="$work/$name"

    # Compile with AddressSanitizer
    if ! "$ESKIUC" "$esk" -o "$bin" --asan >"$work/$name.compile.log" 2>&1; then
        bad "$name" "compilation with --asan failed"
        continue
    fi

    # Run and capture output
    if ! "$bin" >"$work/$name.out" 2>"$work/$name.err"; then
        leak_msg="$(grep -E "LeakSanitizer|AddressSanitizer" "$work/$name.err" | head -n 1 || true)"
        if [[ -n "$leak_msg" ]]; then
            bad "$name" "leak detected: $leak_msg"
        else
            bad "$name" "execution failed with non-zero exit code"
        fi
        continue
    fi

    # Check for expected output if .expected exists
    exp="$here/$name.expected"
    if [[ -f "$exp" ]]; then
        if diff -u "$exp" "$work/$name.out" >/dev/null 2>&1; then
            ok "$name"
        else
            bad "$name" "output mismatch"
        fi
    else
        ok "$name"
    fi
done

echo ""
echo "Summary: $pass passed, $fail failed."
if [[ $fail -gt 0 ]]; then
    printf 'Failed tests: %s\n' "${failed_names[*]}" >&2
    exit 1
fi
exit 0
