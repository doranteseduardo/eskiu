#!/usr/bin/env bash
# Parity gate for the self-hosted type checker (selfhost/sema.esk) vs the C++ one.
#
# Sema has no byte-exact dump like the lexer/parser; the contract is the VERDICT.
# So, mirroring tests/run.sh's negative tests:
#   * Positive corpus (tests/*.esk): the self-hosted `tc_main` must reach the same
#     verdict as `eskiuc --test-typechecker` (all currently type-check clean → both
#     exit 0). This guards against the checker FALSELY rejecting valid code.
#   * Negative corpus (tests/errors/*.esk): for the error classes this slice
#     implements (HANDLED below), `tc_main` must reject (exit != 0) AND emit the
#     file's `EXPECT-ERROR:` substring. Error classes not yet implemented are listed
#     as skipped — they come online as later slices add checks.
#
# Green (exit 0) = verdict parity on every positive file + every handled negative.

set -u
cd "$(dirname "$0")/../.." || exit 2

BIN="${ESKIUC:-}"
if [ -z "$BIN" ]; then
    if [ -x build/eskiuc ]; then BIN=build/eskiuc
    elif command -v eskiuc >/dev/null 2>&1; then BIN=eskiuc
    else echo "tc_parity: cannot find eskiuc (set ESKIUC or build it)"; exit 2; fi
fi

# Error classes (files under tests/errors/) that the current sema slice catches.
# Grows per slice; the rest are reported as skipped.
HANDLED="undefined_var arg_count undefined_type await_outside_async async_no_await switch_dup_case switch_dup_folded switch_dup_const unknown_intrinsic undefined_field match_duplicate match_nonexhaustive
         dup_enum_member_located bitfield_located alias_unknown_type missing_return_located uninit_located undefined_fn_located struct_redef_located int64_literal_range enum_value_overflow enum_value_too_big switch_case_range switch_case_range_int shift_count_range shift_count_negative index_oob_expr index_oob_enum index_oob_const struct_init_mixed array_size_negative array_size_zero compare_slice compare_array compare_iface compare_closure not_slice logical_struct slice_len_assign method_value_self method_wrong_self iface_ptr_to_ptr iface_from_nullable method_on_nullable iface_const_receiver nullable_cond_assign nullable_ptr_arith const_param_assign const_array_slice dangling_element dangling_field dangling_slice generic_redefinition generic_nongeneric_clash generic_proto_clash struct_value_cycle_array call_shadowed_fn method_ptr_ptr
         const_reassign const_no_init const_field const_ptr_write const_ptr_drop
         trait_unsatisfied trait_primitive_unsat question_bad_return escaping_param
         missing_return missing_return_if
         main_void redefinition defer_return defer_break div_by_zero incdec_nonlvalue
         labeled_break_unknown labeled_break_defer
         const_addr_of init_incompatible init_void float_to_int literal_out_of_range
         compare_incompatible compare_struct ternary_incompatible fn_return_mismatch
         index_oob array_overflow array_2d_oob array_2d_init_overflow
         dangling_local uninitialized pp_error unterminated_char
         incdec_float question_type_args c_array_global c_array_local
         struct_init_field_type struct_init_too_many struct_init_dup_field struct_init_lit_range
         struct_init_generic_field sizeof_unknown
         struct_value_cycle field_unknown_type param_unknown_type template_arg_count template_unknown_arg
         iface_return_mismatch iface_arity_mismatch arg_type_mismatch
         missing_return_labeled_break missing_return_dowhile_break missing_return_switch_break
         nullable_reassign nullable_shadow nullable_loop_reassign
         const_method_call const_ptr_method_call const_addr_arg const_struct_addr must_use_method
         fn_value_arg_count fn_value_arg_type fn_field_arg_count fn_chained_arg_count iface_call_arg_count
         generic_arg_count generic_explicit_arg_count generic_type_arg_count generic_arg_conflict generic_uninferable generic_infer_shape
         match_dup_value alias_cycle void_var void_param void_field bitfield_float bitfield_too_wide
         operator_redefinition
         dup_local dup_param_local dup_global dup_param dup_fn_global dup_struct_fn dup_struct dup_field dup_enum
         dup_enum_member dup_default dup_method_fn dup_field_method proto_conflict
         slice_bounds_order slice_bounds_oob compound_lit_range ternary_lit_range
         assign_rvalue assign_ternary addr_of_call addr_of_literal addr_of_bitfield index_scalar
         index_struct cast_struct cast_ptr_float case_nonconst break_outside continue_in_switch
         await_in_lambda unknown_sig_type unknown_var_type
         init_string_from_int init_ptr_from_int assign_float_to_int float_expr_to_int match_non_enum
         lambda_sig_mismatch question_non_result undefined_template_fn
         return_in_void call_non_fn member_of_int shift_by_float string_plus_string
         pp_include pp_unknown_directive pp_stray_endif pp_stray_else pp_missing_endif
         pp_if_bad_expr pp_stringify pp_if_zero_div
         pp_if_shift_range pp_if_int_too_large pp_if_multichar
         pp_macro_too_few pp_macro_too_many pp_macro_unterminated
         import_lex_error import_directory import_empty
         int_literal_too_large hex_literal_too_large float_exponent_no_digits
         float_const_cast_range
         unexpected_char empty_char unterminated_comment unterminated_string
         octal_bad_digit octal_bad_digit2 hex_no_digits number_suffix number_underscore
         parse_error parse_error_located parse_missing_operand parse_return_no_semi parse_import_no_semi
         parse_catch_colon parse_array_field parse_postdec_literal parse_leading_dot
         import_missing import_missing_std operator_as_name
         pragma_link_malformed pragma_pack_invalid pragma_pack_push_invalid method_arg_type method_arg_count method_free_fn_arg_type iface_call_arg_type generic_inst_method_arg
         generic_inst_operator generic_inst_nested generic_struct_method_inst extern_fnptr_closure
         range_bound_float range_bound_ptr
         generic_dot_call_arg_type generic_dot_call_arg_count generic_dot_call_const generic_dot_call_in_generic
         call_global_nonfn unknown_global_type deref_non_pointer unary_struct string_arith struct_arith
         return_no_value assign_struct_to_int switch_case_location
         nullable_to_ptr_arg nullable_to_ptr_return nullable_to_ptr_assign operator_void_result hex_literal_range hex_literal_range_wide literal_over_64_bits struct_lit_other_struct array_assign_size array_arg_size slice_to_ptr slice_elem_mismatch fn_value_null async_result_as_int await_result_mismatch iface_to_iface iface_null iface_method_ret_mismatch iface_from_int_ptr generic_explicit_arg_type generic_ret_mismatch const_ptr_ternary void_ptr_deref_value
         lambda_return_mismatch lambda_void_returns_value lambda_undefined_var lambda_const_capture_assign lambda_dangling_local lambda_dup_param lambda_question_non_result cond_fn_value cond_string cond_struct_while cond_enum_payload ternary_cond_struct fn_value_arith fn_value_deref bnot_float operator_unary_as_binary operator_index_assign operator_operand_order incdec_struct compare_union compare_enum_payload array_plus enum_payload_arith union_unknown_member
         match_payload_arity match_unknown_variant match_other_enum_variant match_binding_type match_arm_undefined_var match_dup_default variant_ctor_arity variant_ctor_type switch_dup_enum_value switch_on_struct catch_undefined_var
         array_lit_elem_type array_lit_elem_float index_negative_literal index_by_float index_by_string alias_array_index_oob method_on_int method_on_generic_prim member_of_array forin_over_int
         const_ptr_member_write const_ptr_index_write const_self_write const_ptr_nested_field_write const_ptr_deref_method const_ptr_through_generic
         constraint_inferred_unsat constraint_inferred_prim constraint_multi_one_missing constraint_struct_param constraint_method_signature constraint_unknown_iface sizeof_generic_uninstantiated alias_unknown_target alias_generic_not_imported alias_cycle_generic
         lambda_reconciled_return nullable_non_pointer await_non_future ptr_arith_to_int string_arith_to_int ternary_void_arms"
HANDLED="$(echo $HANDLED)"   # collapse the multi-line list to single spaces for matching

DRIVER=selfhost/esk_main.esk
TCBIN="$(mktemp -t tc_main.XXXXXX)"
trap 'rm -f "$TCBIN"' EXIT
if ! "$BIN" "$DRIVER" -o "$TCBIN" >/dev/null 2>/tmp/tcbuild.$$; then
    echo "tc_parity: failed to build $DRIVER"; cat /tmp/tcbuild.$$; rm -f /tmp/tcbuild.$$; exit 2
fi
rm -f /tmp/tcbuild.$$

fail=0

# ── positives: verdict must match the C++ oracle ──
pos=0
echo "Positive corpus (verdict must match --test-typechecker):"
for f in tests/*.esk; do
    [ -f "$f" ] || continue
    cref=0; "$BIN" --test-typechecker "$f" >/dev/null 2>&1 || cref=1
    cgot=0; ESKIU_ROOT="$(pwd)" "$TCBIN" --test-typechecker "$f" >/dev/null 2>&1 || cgot=1
    if [ "$cref" -eq "$cgot" ]; then
        pos=$((pos + 1))
    else
        echo "  FAIL  ${f#tests/}  (C++ verdict=$cref, self-host=$cgot)"
        fail=1
    fi
done
echo "  ok: $pos/$(ls tests/*.esk | wc -l | tr -d ' ') positive files agree"

# ── negatives: handled error classes must be rejected with the right message ──
echo "Negative corpus (handled error classes):"
for esk in tests/errors/*.esk; do
    [ -e "$esk" ] || continue
    base="$(basename "$esk" .esk)"
    # Not sema's job — caught upstream by the (already self-hosted) lexer / parser /
    # preprocessor, not the type checker.
    # These reject with the right VERDICT but the self-host lexer/parser wording still
    # differs from C++ (a diagnostic-text consistency item, not a behavioral one).
    UPSTREAM=" "
    case " $HANDLED " in
        *" $base "*) ;;
        *) case "$UPSTREAM" in
               *" $base "*) echo "  skip  errors/$base  (upstream: lexer/parser/pp)" ;;
               *) echo "  skip  errors/$base  (sema — not yet implemented)" ;;
           esac
           continue ;;
    esac
    want="$(grep -m1 'EXPECT-ERROR:' "$esk" | sed 's/.*EXPECT-ERROR:[[:space:]]*//')"
    out="$(ESKIU_ROOT="$(pwd)" "$TCBIN" --test-typechecker "$esk" 2>&1)"; code=$?
    if [ "$code" -eq 0 ]; then
        echo "  FAIL  errors/$base  (accepted code that should be rejected)"; fail=1
    elif [ -n "$want" ] && ! printf '%s' "$out" | grep -qF "$want"; then
        echo "  FAIL  errors/$base  (rejected, but message missing \"$want\")"; fail=1
    else
        echo "  ok    errors/$base"
    fi
done

echo "----"
if [ "$fail" -eq 0 ]; then echo "tc parity: OK"; else echo "tc parity: MISMATCH"; fi
exit "$fail"
