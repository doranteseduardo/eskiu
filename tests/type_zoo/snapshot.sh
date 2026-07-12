#!/usr/bin/env bash
#
# Golden-IR oracle for the typed-Type migration (v0.2.3 Stage 0.5).
#
# The migration of normalizeType / getTypeFromString / substType / unifyTypeParam
# onto the structured `Type` IR is meant to be BEHAVIOR-PRESERVING. This harness
# makes that falsifiable: it snapshots the LLVM IR emitted for a curated corpus
# of type-rich programs (the "type zoo"). The IR is the downstream product of
# getTypeFromString (type lowering) and normalizeType (struct:/template/ADT
# resolution + instantiation side effects), so any change in those functions
# that alters behavior shows up as an IR diff here.
#
#   snapshot.sh capture   # write goldens from the current eskiuc
#   snapshot.sh check     # rebuild eskiuc yourself first, then diff vs goldens
#
# Workflow per migration commit: `capture` on the pre-change build, make the
# change, rebuild, then `check` — a clean check proves no behavior change.
#
# Host-specific header lines (ModuleID/source_filename/target datalayout/triple)
# are stripped, so goldens are stable across the actual IR body.

set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
ESKIUC="${ESKIUC:-$root/build/eskiuc}"
golden="$here/golden"

# Curated type zoo: programs that densely exercise the type grammar —
# templates, nested templates, ADT/generic enums, interfaces/traits (incl.
# primitive-satisfaction), pointers + const, fn-types/closures, sret, variadics.
corpus=(
  list_struct map_generic nested_template template_inference_composite
  template_inference templates_result template_struct_literal
  enum_generic enum_adt either_stdlib enums
  interfaces structs_methods traits_ok traits_multi traits_primitive
  pointers pointer_const arrays_md ternary_ir
  closures fn_pointer generic_closure closure_coerce
  sret variadic
  async_basic async_channel
)

# Emit the canonical IR for one test: extract the body between the ==== banners,
# drop host-specific header lines.
emit_ir() {
  "$ESKIUC" --test-codegen "$root/tests/$1.esk" 2>/dev/null \
    | awk '/^====/{n++; next} n==1{print}' \
    | grep -vE '^; ModuleID|^source_filename|^target datalayout|^target triple'
}

mode="${1:-check}"
case "$mode" in
  capture)
    mkdir -p "$golden"
    for t in "${corpus[@]}"; do
      emit_ir "$t" > "$golden/$t.ll"
      if [ ! -s "$golden/$t.ll" ]; then echo "WARN: empty IR for $t (compile failed?)"; fi
    done
    echo "captured ${#corpus[@]} golden IR snapshots -> $golden"
    ;;
  check)
    if [ ! -d "$golden" ]; then echo "no goldens; run: $0 capture"; exit 2; fi
    fail=0
    tmp="$(mktemp)"
    for t in "${corpus[@]}"; do
      emit_ir "$t" > "$tmp"          # same producer as capture → no newline artifact
      if ! diff -q "$tmp" "$golden/$t.ll" >/dev/null 2>&1; then
        echo "IR CHANGED: $t"
        diff "$tmp" "$golden/$t.ll" | head -20
        fail=1
      fi
    done
    rm -f "$tmp"
    if [ "$fail" = 0 ]; then echo "golden IR: ${#corpus[@]} snapshots match"; else echo "golden IR: MISMATCH"; fi
    exit $fail
    ;;
  *) echo "usage: $0 capture|check"; exit 2 ;;
esac
