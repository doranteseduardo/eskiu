#!/usr/bin/env bash
# ir_golden.sh — structural LLVM IR assertions for critical ABI & codegen invariants
#
# Complements behavioral testing by checking that emitted LLVM IR contains the exact
# structural attributes required by the target C ABI and LLVM verification:
#   - `sret` attributes on large aggregate returns
#   - `signext` / `zeroext` on narrow integer parameters and return types
#   - `personality` function and `landingpad` on exception blocks
#   - `@__cabi_*` callback thunks for C function pointers
#
# Usage: ./tests/ir_golden.sh [path-to-eskiuc]

set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"

ESKIUC="${1:-${ESKIUC:-$repo/build/eskiuc}}"
if [[ ! -x "$ESKIUC" ]]; then
    echo "error: compiler not found at $ESKIUC (build it first: cmake --build build)" >&2
    exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

pass=0
fail=0
failed_names=()

ok()  { printf '  \033[32mPASS\033[0m  %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  \033[31mFAIL\033[0m  %s — %s\n' "$1" "$2"; fail=$((fail+1)); failed_names+=("$1"); }

echo "Structural LLVM IR assertions:"

# 1. sret on large aggregate return across targets
for target in "arm64-apple-darwin" "x86_64-unknown-linux-gnu" "x86_64-apple-darwin"; do
    ir="$work/struct_$target.ll"
    if "$ESKIUC" "$here/c_abi_struct.esk" --test-codegen --target "$target" > "$ir" 2>&1; then
        if grep -q "sret(%Big)" "$ir" && grep -q "call void @c_mk(ptr sret(%Big)" "$ir"; then
            ok "ir/sret ($target)"
        else
            bad "ir/sret ($target)" "missing sret attribute on %Big return"
        fi
    else
        bad "ir/sret ($target)" "codegen failed"
    fi
done

# 2. signext and zeroext on narrow integer ABI boundaries
ir="$work/narrow.ll"
if "$ESKIUC" "$here/c_abi_narrow.esk" --test-codegen > "$ir" 2>&1; then
    if grep -q "declare i64 @c_s8(i8 signext)" "$ir" && \
       grep -q "declare i64 @c_u8(i8 zeroext)" "$ir" && \
       grep -q "declare signext i8 @c_ret_s8(i32)" "$ir" && \
       grep -q "declare zeroext i16 @c_ret_u16(i32)" "$ir"; then
        ok "ir/signext_zeroext"
    else
        bad "ir/signext_zeroext" "missing signext/zeroext parameter or return attributes"
    fi
else
    bad "ir/signext_zeroext" "codegen failed"
fi

# 3. Exception landingpad and personality function
ir="$work/try.ll"
if "$ESKIUC" "$here/c_abi_try.esk" --test-codegen > "$ir" 2>&1; then
    if grep -q "personality ptr @__gxx_personality_v0" "$ir" && \
       grep -q "landingpad" "$ir" && \
       grep -q "__gxx_personality_v0" "$ir"; then
        ok "ir/eh_personality"
    else
        bad "ir/eh_personality" "missing __gxx_personality_v0 or landingpad in IR"
    fi
else
    bad "ir/eh_personality" "codegen failed"
fi

# 4. C ABI callback thunks
ir="$work/callback.ll"
if "$ESKIUC" "$here/c_abi_callback.esk" --test-codegen > "$ir" 2>&1; then
    if grep -q "@__cabi_" "$ir"; then
        ok "ir/cabi_callback_thunks"
    else
        bad "ir/cabi_callback_thunks" "missing @__cabi_ thunk in IR"
    fi
else
    bad "ir/cabi_callback_thunks" "codegen failed"
fi

echo ""
echo "Summary: $pass passed, $fail failed."
if [[ $fail -gt 0 ]]; then
    printf 'Failed tests: %s\n' "${failed_names[*]}" >&2
    exit 1
fi
exit 0
