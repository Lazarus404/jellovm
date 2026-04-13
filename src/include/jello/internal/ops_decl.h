/**
 * Shared forward declarations for opcode handlers (see dispatch.c).
 * Included by ops_*.c so -Wmissing-prototypes is satisfied before definitions.
 *
 * VM-internal: use #include <jello/internal/ops_decl.h> in VM sources only.
 */
#ifndef JELLO_OPS_DECL_H
#define JELLO_OPS_DECL_H

#include <jello/internal.h>

op_result op_nop(exec_ctx* ctx, const jello_insn* ins);
op_result op_ret(exec_ctx* ctx, const jello_insn* ins);
op_result op_jmp(exec_ctx* ctx, const jello_insn* ins);
op_result op_jmp_if(exec_ctx* ctx, const jello_insn* ins);
op_result op_assert(exec_ctx* ctx, const jello_insn* ins);
op_result op_try(exec_ctx* ctx, const jello_insn* ins);
op_result op_endtry(exec_ctx* ctx, const jello_insn* ins);
op_result op_throw(exec_ctx* ctx, const jello_insn* ins);

op_result op_mov(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_i8_imm(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_bool(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_atom(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_null(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_bytes(exec_ctx* ctx, const jello_insn* ins);
op_result op_const_fun(exec_ctx* ctx, const jello_insn* ins);

op_result op_call(exec_ctx* ctx, const jello_insn* ins);
op_result op_callr(exec_ctx* ctx, const jello_insn* ins);
op_result op_tailcall(exec_ctx* ctx, const jello_insn* ins);
op_result op_tailcallr(exec_ctx* ctx, const jello_insn* ins);
op_result op_closure(exec_ctx* ctx, const jello_insn* ins);
op_result op_bind_this(exec_ctx* ctx, const jello_insn* ins);

op_result op_neg_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_neg_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_neg_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_neg_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_not_bool(exec_ctx* ctx, const jello_insn* ins);
op_result op_add_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_add_i32_imm(exec_ctx* ctx, const jello_insn* ins);
op_result op_sub_i32_imm(exec_ctx* ctx, const jello_insn* ins);
op_result op_mul_i32_imm(exec_ctx* ctx, const jello_insn* ins);
op_result op_add_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_add_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_sub_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_sub_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_sub_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_mul_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_mul_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_mul_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_add_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_sub_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_mul_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_div_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_div_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_add_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_sub_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_mul_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_mod_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_mod_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_shl_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_shl_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_shr_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_shr_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_eq_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_eq_i32_imm(exec_ctx* ctx, const jello_insn* ins);
op_result op_lt_i32_imm(exec_ctx* ctx, const jello_insn* ins);
op_result op_eq_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_eq_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_eq_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_lt_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_lt_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_lt_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_lt_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_sext_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_sext_i16(exec_ctx* ctx, const jello_insn* ins);
op_result op_trunc_i8(exec_ctx* ctx, const jello_insn* ins);
op_result op_trunc_i16(exec_ctx* ctx, const jello_insn* ins);
op_result op_i32_from_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_f64_from_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_i32_from_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_f64_from_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_i64_from_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_f32_from_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_i32_from_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_f64_from_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_f32_from_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_f16_from_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_f32_from_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_f16_from_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_i32_from_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_f32_from_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_i64_from_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_switch_kind(exec_ctx* ctx, const jello_insn* ins);
op_result op_case_kind(exec_ctx* ctx, const jello_insn* ins);

op_result op_to_dyn(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_i8(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_i16(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_i32(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_i64(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_f16(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_f32(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_f64(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_bool(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_atom(exec_ctx* ctx, const jello_insn* ins);
op_result op_from_dyn_ptr(exec_ctx* ctx, const jello_insn* ins);
op_result op_spill_push(exec_ctx* ctx, const jello_insn* ins);
op_result op_spill_pop(exec_ctx* ctx, const jello_insn* ins);
op_result op_physeq(exec_ctx* ctx, const jello_insn* ins);
op_result op_kindof(exec_ctx* ctx, const jello_insn* ins);

op_result op_list_nil(exec_ctx* ctx, const jello_insn* ins);
op_result op_list_cons(exec_ctx* ctx, const jello_insn* ins);
op_result op_list_head(exec_ctx* ctx, const jello_insn* ins);
op_result op_list_tail(exec_ctx* ctx, const jello_insn* ins);
op_result op_list_is_nil(exec_ctx* ctx, const jello_insn* ins);

op_result op_array_new(exec_ctx* ctx, const jello_insn* ins);
op_result op_array_len(exec_ctx* ctx, const jello_insn* ins);
op_result op_array_get(exec_ctx* ctx, const jello_insn* ins);
op_result op_array_set(exec_ctx* ctx, const jello_insn* ins);

op_result op_bytes_new(exec_ctx* ctx, const jello_insn* ins);
op_result op_bytes_len(exec_ctx* ctx, const jello_insn* ins);
op_result op_bytes_get_u8(exec_ctx* ctx, const jello_insn* ins);
op_result op_bytes_set_u8(exec_ctx* ctx, const jello_insn* ins);
op_result op_bytes_concat2(exec_ctx* ctx, const jello_insn* ins);
op_result op_bytes_concat_many(exec_ctx* ctx, const jello_insn* ins);

op_result op_obj_new(exec_ctx* ctx, const jello_insn* ins);
op_result op_obj_has_atom(exec_ctx* ctx, const jello_insn* ins);
op_result op_obj_get_atom(exec_ctx* ctx, const jello_insn* ins);
op_result op_obj_set_atom(exec_ctx* ctx, const jello_insn* ins);
op_result op_obj_get(exec_ctx* ctx, const jello_insn* ins);
op_result op_obj_set(exec_ctx* ctx, const jello_insn* ins);

#endif /* JELLO_OPS_DECL_H */
