/**
 * Copyright 2017 - Jahred Love
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <jello/internal.h>

// Hot path helpers (inlined handlers use memmove).
#include <string.h>

#include <jello/internal/ops_decl.h>

static op_result op_panic(exec_ctx* ctx, const jello_insn* ins) {
  (void)ctx;
  (void)ins;
  jello_vm_panic();
  return OP_CONTINUE;
}

op_result op_dispatch(exec_ctx* ctx, const jello_insn* ins) {
#if defined(JELLOVM_USE_COMPUTED_GOTO) && (defined(__GNUC__) || defined(__clang__))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wgnu-label-as-value"
  /* Computed goto: often 10-20% faster than switch on hot dispatch. */
  static const void* const op_table[128] = {
    [JOP_NOP] = &&L_NOP, [JOP_RET] = &&L_RET, [JOP_MOV] = &&L_MOV, [JOP_CALL] = &&L_CALL,
    [JOP_CALLR] = &&L_CALLR, [JOP_TAILCALL] = &&L_TAILCALL, [JOP_TAILCALLR] = &&L_TAILCALLR, [JOP_CONST_I32] = &&L_CONST_I32, [JOP_CONST_BOOL] = &&L_CONST_BOOL,
    [JOP_CONST_ATOM] = &&L_CONST_ATOM, [JOP_CONST_FUN] = &&L_CONST_FUN, [JOP_CONST_NULL] = &&L_CONST_NULL,
    [JOP_CONST_F32] = &&L_CONST_F32, [JOP_CONST_F16] = &&L_CONST_F16, [JOP_CONST_I64] = &&L_CONST_I64, [JOP_CONST_F64] = &&L_CONST_F64,
    [JOP_CONST_I8_IMM] = &&L_CONST_I8_IMM,
    [JOP_CONST_BYTES] = &&L_CONST_BYTES, [JOP_JMP] = &&L_JMP, [JOP_JMP_IF] = &&L_JMP_IF,
    [JOP_ASSERT] = &&L_ASSERT,
    [JOP_CLOSURE] = &&L_CLOSURE, [JOP_BIND_THIS] = &&L_BIND_THIS, [JOP_ADD_I32] = &&L_ADD_I32,
    [JOP_ADD_I32_IMM] = &&L_ADD_I32_IMM, [JOP_SUB_I32] = &&L_SUB_I32, [JOP_SUB_I32_IMM] = &&L_SUB_I32_IMM,
    [JOP_MUL_I32] = &&L_MUL_I32, [JOP_MUL_I32_IMM] = &&L_MUL_I32_IMM, [JOP_ADD_F32] = &&L_ADD_F32,
    [JOP_SUB_F32] = &&L_SUB_F32, [JOP_MUL_F32] = &&L_MUL_F32,
    [JOP_ADD_F16] = &&L_ADD_F16, [JOP_SUB_F16] = &&L_SUB_F16, [JOP_MUL_F16] = &&L_MUL_F16,
    [JOP_ADD_F64] = &&L_ADD_F64,
    [JOP_SUB_F64] = &&L_SUB_F64, [JOP_MUL_F64] = &&L_MUL_F64, [JOP_ADD_I64] = &&L_ADD_I64,
    [JOP_SUB_I64] = &&L_SUB_I64, [JOP_MUL_I64] = &&L_MUL_I64,
    [JOP_MOD_I32] = &&L_MOD_I32, [JOP_MOD_I64] = &&L_MOD_I64,
    [JOP_SHL_I32] = &&L_SHL_I32, [JOP_SHL_I64] = &&L_SHL_I64,
    [JOP_SHR_I32] = &&L_SHR_I32, [JOP_SHR_I64] = &&L_SHR_I64,
    [JOP_SEXT_I64] = &&L_SEXT_I64, [JOP_SEXT_I16] = &&L_SEXT_I16,
    [JOP_TRUNC_I8] = &&L_TRUNC_I8, [JOP_TRUNC_I16] = &&L_TRUNC_I16,
    [JOP_DIV_F32] = &&L_DIV_F32, [JOP_DIV_F64] = &&L_DIV_F64,
    [JOP_I32_FROM_I64] = &&L_I32_FROM_I64, [JOP_F64_FROM_I32] = &&L_F64_FROM_I32,
    [JOP_I32_FROM_F64] = &&L_I32_FROM_F64, [JOP_F64_FROM_I64] = &&L_F64_FROM_I64,
    [JOP_I64_FROM_F64] = &&L_I64_FROM_F64, [JOP_F32_FROM_I32] = &&L_F32_FROM_I32,
    [JOP_I32_FROM_F32] = &&L_I32_FROM_F32, [JOP_F64_FROM_F32] = &&L_F64_FROM_F32,     [JOP_F32_FROM_F64] = &&L_F32_FROM_F64,
    [JOP_F16_FROM_F32] = &&L_F16_FROM_F32, [JOP_F32_FROM_F16] = &&L_F32_FROM_F16,
    [JOP_F16_FROM_I32] = &&L_F16_FROM_I32, [JOP_I32_FROM_F16] = &&L_I32_FROM_F16,
    [JOP_F32_FROM_I64] = &&L_F32_FROM_I64, [JOP_I64_FROM_F32] = &&L_I64_FROM_F32,
    [JOP_EQ_I32] = &&L_EQ_I32, [JOP_EQ_I32_IMM] = &&L_EQ_I32_IMM, [JOP_LT_I32_IMM] = &&L_LT_I32_IMM,
    [JOP_EQ_F32] = &&L_EQ_F32,
    [JOP_EQ_F64] = &&L_EQ_F64, [JOP_EQ_I64] = &&L_EQ_I64, [JOP_LT_I32] = &&L_LT_I32,
    [JOP_LT_I64] = &&L_LT_I64, [JOP_LT_F32] = &&L_LT_F32, [JOP_LT_F64] = &&L_LT_F64,
    [JOP_TO_DYN] = &&L_TO_DYN, [JOP_FROM_DYN_I32] = &&L_FROM_DYN_I32, [JOP_FROM_DYN_I64] = &&L_FROM_DYN_I64,
    [JOP_FROM_DYN_F64] = &&L_FROM_DYN_F64, [JOP_FROM_DYN_F16] = &&L_FROM_DYN_F16,
    [JOP_FROM_DYN_F32] = &&L_FROM_DYN_F32, [JOP_FROM_DYN_I8] = &&L_FROM_DYN_I8, [JOP_FROM_DYN_I16] = &&L_FROM_DYN_I16,
    [JOP_FROM_DYN_BOOL] = &&L_FROM_DYN_BOOL, [JOP_FROM_DYN_ATOM] = &&L_FROM_DYN_ATOM, [JOP_FROM_DYN_PTR] = &&L_FROM_DYN_PTR,
    [JOP_SPILL_PUSH] = &&L_SPILL_PUSH, [JOP_SPILL_POP] = &&L_SPILL_POP, [JOP_NEG_I32] = &&L_NEG_I32,
    [JOP_NEG_I64] = &&L_NEG_I64, [JOP_NEG_F32] = &&L_NEG_F32, [JOP_NEG_F64] = &&L_NEG_F64,
    [JOP_NOT_BOOL] = &&L_NOT_BOOL, [JOP_TRY] = &&L_TRY, [JOP_ENDTRY] = &&L_ENDTRY, [JOP_THROW] = &&L_THROW,
    [JOP_PHYSEQ] = &&L_PHYSEQ, [JOP_KINDOF] = &&L_KINDOF, [JOP_SWITCH_KIND] = &&L_SWITCH_KIND,
    [JOP_CASE_KIND] = &&L_CASE_KIND, [JOP_LIST_NIL] = &&L_LIST_NIL, [JOP_LIST_CONS] = &&L_LIST_CONS,
    [JOP_LIST_HEAD] = &&L_LIST_HEAD, [JOP_LIST_TAIL] = &&L_LIST_TAIL, [JOP_LIST_IS_NIL] = &&L_LIST_IS_NIL,
    [JOP_ARRAY_NEW] = &&L_ARRAY_NEW, [JOP_ARRAY_LEN] = &&L_ARRAY_LEN, [JOP_ARRAY_GET] = &&L_ARRAY_GET,
    [JOP_ARRAY_SET] = &&L_ARRAY_SET, [JOP_BYTES_NEW] = &&L_BYTES_NEW, [JOP_BYTES_LEN] = &&L_BYTES_LEN,
    [JOP_BYTES_GET_U8] = &&L_BYTES_GET_U8, [JOP_BYTES_SET_U8] = &&L_BYTES_SET_U8,
    [JOP_BYTES_CONCAT2] = &&L_BYTES_CONCAT2, [JOP_BYTES_CONCAT_MANY] = &&L_BYTES_CONCAT_MANY,
    [JOP_OBJ_NEW] = &&L_OBJ_NEW, [JOP_OBJ_HAS_ATOM] = &&L_OBJ_HAS_ATOM, [JOP_OBJ_GET_ATOM] = &&L_OBJ_GET_ATOM,
    [JOP_OBJ_SET_ATOM] = &&L_OBJ_SET_ATOM, [JOP_OBJ_GET] = &&L_OBJ_GET, [JOP_OBJ_SET] = &&L_OBJ_SET,
  };
  unsigned op = (unsigned)ins->op;
  if(op >= 128) goto L_PANIC;
  goto *op_table[op];
L_NOP: return op_nop(ctx, ins);
L_RET: {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;
  call_frame* frames = ctx->frames;

  uint32_t caller_dst = fr->caller_dst;
  uint8_t has_caller = fr->has_caller;
  if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
  vm->exc_handlers_len = fr->exc_base;

  if(!has_caller) {
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    free(vm->call_frames);
    vm->call_frames = NULL;
    vm->call_frames_len = 0;
    vm->call_frames_cap = 0;
    free(vm->const_fun_cache);
    vm->const_fun_cache = NULL;
    vm->const_fun_cache_len = 0;
    free(vm->exc_handlers);
    vm->exc_handlers = NULL;
    vm->exc_handlers_len = 0;
    vm->exc_handlers_cap = 0;
    if(ctx->out) *ctx->out = ret;
    return OP_RETURN;
  }

  if(vm->call_frames_len < 2u) jello_vm_panic();
  call_frame* caller = &frames[vm->call_frames_len - 2u];

  // Fast path: exact type match -> raw copy (avoid boxing/unboxing).
  uint32_t ret_tid = f->reg_types[ins->a];
  if(caller->f->reg_types[caller_dst] == ret_tid) {
    jello_type_kind k = m->types[ret_tid].kind;
    size_t sz = jello_slot_size(k);
    uint8_t* dst = (uint8_t*)vm_reg_ptr(&caller->rf, caller_dst);
    const uint8_t* src = (const uint8_t*)vm_reg_ptr(&fr->rf, ins->a);
    if(sz == 4u) *(uint32_t*)dst = *(const uint32_t*)src;
    else if(sz == 8u) *(uint64_t*)dst = *(const uint64_t*)src;
    else memmove(dst, src, sz);
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    return OP_CONTINUE;
  }

  jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  vm_rf_release(vm, &fr->rf);
  vm->call_frames_len--;
  vm_store_from_boxed(m, caller->f, &caller->rf, caller_dst, ret);
  return OP_CONTINUE;
}
L_MOV: {
  const jello_bc_module* m = ctx->m;
  call_frame* fr = ctx->fr;
  const jello_bc_function* f = ctx->f;
  uint32_t a = ins->a, b = ins->b;
  jello_type_kind k = m->types[f->reg_types[a]].kind;
  size_t sz = jello_slot_size(k);
  uint8_t* dst = (uint8_t*)vm_reg_ptr(&fr->rf, a);
  const uint8_t* src = (const uint8_t*)vm_reg_ptr(&fr->rf, b);
  if(sz == 4u) *(uint32_t*)dst = *(const uint32_t*)src;
  else if(sz == 8u) *(uint64_t*)dst = *(const uint64_t*)src;
  else memmove(dst, src, sz);
  return OP_CONTINUE;
}
L_CALL: return op_call(ctx, ins);
L_CALLR: return op_callr(ctx, ins);
L_TAILCALL: return op_tailcall(ctx, ins);
L_TAILCALLR: return op_tailcallr(ctx, ins);
L_CONST_I32: {
  vm_store_u32(&ctx->fr->rf, ins->a, ins->imm);
  return OP_CONTINUE;
}
L_CONST_BOOL: {
  vm_store_u32(&ctx->fr->rf, ins->a, (uint32_t)(ins->c & 1u));
  return OP_CONTINUE;
}
L_CONST_ATOM: return op_const_atom(ctx, ins);
L_CONST_FUN: {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;
  if(ins->imm >= vm->const_fun_cache_len) jello_vm_panic();
  jello_function** cache = (jello_function**)vm->const_fun_cache;
  jello_function* fn = cache[ins->imm];
  if(!fn) {
    uint32_t type_id = f->reg_types[ins->a];
    fn = jello_function_new(vm, type_id, ins->imm);
    cache[ins->imm] = fn;
  }
  vm_store_ptr(&fr->rf, ins->a, fn);
  return OP_CONTINUE;
}
L_CONST_NULL: return op_const_null(ctx, ins);
L_CONST_F32: return op_const_f32(ctx, ins);
L_CONST_F16: return op_const_f16(ctx, ins);
L_CONST_I8_IMM: return op_const_i8_imm(ctx, ins);
L_CONST_I64: return op_const_i64(ctx, ins);
L_CONST_F64: return op_const_f64(ctx, ins);
L_CONST_BYTES: return op_const_bytes(ctx, ins);
L_JMP: {
  call_frame* fr = ctx->fr;
  const jello_bc_function* f = ctx->f;
  int32_t d = (int32_t)ins->imm;
  int32_t npc = (int32_t)fr->pc + d;
  if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
  fr->pc = (uint32_t)npc;
  return OP_CONTINUE;
}
L_JMP_IF: {
  call_frame* fr = ctx->fr;
  const jello_bc_function* f = ctx->f;
  uint32_t cond = vm_load_u32(&fr->rf, ins->a);
  if(cond != 0) {
    int32_t d = (int32_t)ins->imm;
    int32_t npc = (int32_t)fr->pc + d;
    if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
    fr->pc = (uint32_t)npc;
  }
  return OP_CONTINUE;
}
L_ASSERT: return op_assert(ctx, ins);
L_CLOSURE: return op_closure(ctx, ins);
L_BIND_THIS: return op_bind_this(ctx, ins);
L_ADD_I32: {
  call_frame* fr = ctx->fr;
  jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a);
  uint32_t a = vm_load_u32(&fr->rf, ins->b);
  uint32_t b = vm_load_u32(&fr->rf, ins->c);
  vm_store_u32_masked(&fr->rf, ins->a, a + b, k);
  return OP_CONTINUE;
}
L_ADD_I32_IMM: return op_add_i32_imm(ctx, ins);
L_SUB_I32: {
  call_frame* fr = ctx->fr;
  jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a);
  uint32_t a = vm_load_u32(&fr->rf, ins->b);
  uint32_t b = vm_load_u32(&fr->rf, ins->c);
  vm_store_u32_masked(&fr->rf, ins->a, a - b, k);
  return OP_CONTINUE;
}
L_SUB_I32_IMM: return op_sub_i32_imm(ctx, ins);
L_MUL_I32: return op_mul_i32(ctx, ins);
L_MUL_I32_IMM: return op_mul_i32_imm(ctx, ins);
L_ADD_F32: return op_add_f32(ctx, ins);
L_SUB_F32: return op_sub_f32(ctx, ins);
L_MUL_F32: return op_mul_f32(ctx, ins);
L_ADD_F16: return op_add_f16(ctx, ins);
L_SUB_F16: return op_sub_f16(ctx, ins);
L_MUL_F16: return op_mul_f16(ctx, ins);
L_ADD_F64: return op_add_f64(ctx, ins);
L_SUB_F64: return op_sub_f64(ctx, ins);
L_MUL_F64: return op_mul_f64(ctx, ins);
L_ADD_I64: return op_add_i64(ctx, ins);
L_SUB_I64: return op_sub_i64(ctx, ins);
L_MUL_I64: return op_mul_i64(ctx, ins);
    L_MOD_I32: return op_mod_i32(ctx, ins);
    L_MOD_I64: return op_mod_i64(ctx, ins);
    L_SHL_I32: return op_shl_i32(ctx, ins);
    L_SHL_I64: return op_shl_i64(ctx, ins);
    L_SHR_I32: return op_shr_i32(ctx, ins);
    L_SHR_I64: return op_shr_i64(ctx, ins);
    L_SEXT_I64: return op_sext_i64(ctx, ins);
L_SEXT_I16: return op_sext_i16(ctx, ins);
L_TRUNC_I8: return op_trunc_i8(ctx, ins);
L_TRUNC_I16: return op_trunc_i16(ctx, ins);
L_DIV_F32: return op_div_f32(ctx, ins);
L_DIV_F64: return op_div_f64(ctx, ins);
L_I32_FROM_I64: return op_i32_from_i64(ctx, ins);
L_F64_FROM_I32: return op_f64_from_i32(ctx, ins);
L_I32_FROM_F64: return op_i32_from_f64(ctx, ins);
L_F64_FROM_I64: return op_f64_from_i64(ctx, ins);
L_I64_FROM_F64: return op_i64_from_f64(ctx, ins);
L_F32_FROM_I32: return op_f32_from_i32(ctx, ins);
L_I32_FROM_F32: return op_i32_from_f32(ctx, ins);
L_F64_FROM_F32: return op_f64_from_f32(ctx, ins);
L_F32_FROM_F64: return op_f32_from_f64(ctx, ins);
L_F16_FROM_F32: return op_f16_from_f32(ctx, ins);
L_F32_FROM_F16: return op_f32_from_f16(ctx, ins);
L_F16_FROM_I32: return op_f16_from_i32(ctx, ins);
L_I32_FROM_F16: return op_i32_from_f16(ctx, ins);
L_F32_FROM_I64: return op_f32_from_i64(ctx, ins);
L_I64_FROM_F32: return op_i64_from_f32(ctx, ins);
L_EQ_I32: {
  call_frame* fr = ctx->fr;
  int32_t a = vm_load_i32ish(&fr->rf, ins->b);
  int32_t b = vm_load_i32ish(&fr->rf, ins->c);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)((a == b) ? 1 : 0));
  return OP_CONTINUE;
}
L_EQ_I32_IMM: return op_eq_i32_imm(ctx, ins);
L_LT_I32_IMM: return op_lt_i32_imm(ctx, ins);
L_EQ_F32: return op_eq_f32(ctx, ins);
L_EQ_F64: return op_eq_f64(ctx, ins);
L_EQ_I64: return op_eq_i64(ctx, ins);
L_LT_I32: {
  call_frame* fr = ctx->fr;
  int32_t a = vm_load_i32ish(&fr->rf, ins->b);
  int32_t b = vm_load_i32ish(&fr->rf, ins->c);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)((a < b) ? 1 : 0));
  return OP_CONTINUE;
}
L_LT_I64: return op_lt_i64(ctx, ins);
L_LT_F32: return op_lt_f32(ctx, ins);
L_LT_F64: return op_lt_f64(ctx, ins);
L_TO_DYN: return op_to_dyn(ctx, ins);
L_FROM_DYN_I64: return op_from_dyn_i64(ctx, ins);
L_FROM_DYN_I32: return op_from_dyn_i32(ctx, ins);
L_FROM_DYN_I16: return op_from_dyn_i16(ctx, ins);
L_FROM_DYN_I8: return op_from_dyn_i8(ctx, ins);
L_FROM_DYN_F64: return op_from_dyn_f64(ctx, ins);
L_FROM_DYN_F32: return op_from_dyn_f32(ctx, ins);
L_FROM_DYN_F16: return op_from_dyn_f16(ctx, ins);
L_FROM_DYN_BOOL: return op_from_dyn_bool(ctx, ins);
L_FROM_DYN_ATOM: return op_from_dyn_atom(ctx, ins);
L_FROM_DYN_PTR: return op_from_dyn_ptr(ctx, ins);
L_SPILL_PUSH: return op_spill_push(ctx, ins);
L_SPILL_POP: return op_spill_pop(ctx, ins);
L_NEG_I32: return op_neg_i32(ctx, ins);
L_NEG_I64: return op_neg_i64(ctx, ins);
L_NEG_F32: return op_neg_f32(ctx, ins);
L_NEG_F64: return op_neg_f64(ctx, ins);
L_NOT_BOOL: return op_not_bool(ctx, ins);
L_TRY: return op_try(ctx, ins);
L_ENDTRY: return op_endtry(ctx, ins);
L_THROW: return op_throw(ctx, ins);
L_PHYSEQ: return op_physeq(ctx, ins);
L_KINDOF: return op_kindof(ctx, ins);
L_SWITCH_KIND: return op_switch_kind(ctx, ins);
L_CASE_KIND: return op_case_kind(ctx, ins);
L_LIST_NIL: return op_list_nil(ctx, ins);
L_LIST_CONS: return op_list_cons(ctx, ins);
L_LIST_HEAD: return op_list_head(ctx, ins);
L_LIST_TAIL: return op_list_tail(ctx, ins);
L_LIST_IS_NIL: return op_list_is_nil(ctx, ins);
L_ARRAY_NEW: return op_array_new(ctx, ins);
L_ARRAY_LEN: return op_array_len(ctx, ins);
L_ARRAY_GET: return op_array_get(ctx, ins);
L_ARRAY_SET: return op_array_set(ctx, ins);
L_BYTES_NEW: return op_bytes_new(ctx, ins);
L_BYTES_LEN: return op_bytes_len(ctx, ins);
L_BYTES_GET_U8: return op_bytes_get_u8(ctx, ins);
L_BYTES_SET_U8: return op_bytes_set_u8(ctx, ins);
L_BYTES_CONCAT2: return op_bytes_concat2(ctx, ins);
L_BYTES_CONCAT_MANY: return op_bytes_concat_many(ctx, ins);
L_OBJ_NEW: return op_obj_new(ctx, ins);
L_OBJ_HAS_ATOM: return op_obj_has_atom(ctx, ins);
L_OBJ_GET_ATOM: return op_obj_get_atom(ctx, ins);
L_OBJ_SET_ATOM: return op_obj_set_atom(ctx, ins);
L_OBJ_GET: return op_obj_get(ctx, ins);
L_OBJ_SET: return op_obj_set(ctx, ins);
L_PANIC: return op_panic(ctx, ins);
#pragma GCC diagnostic pop
#else
  switch((jello_op)ins->op) {
    case JOP_NOP: return op_nop(ctx, ins);
    case JOP_RET: return op_ret(ctx, ins);
    case JOP_MOV: return op_mov(ctx, ins);
    case JOP_CALL: return op_call(ctx, ins);
    case JOP_CALLR: return op_callr(ctx, ins);
    case JOP_TAILCALL: return op_tailcall(ctx, ins);
    case JOP_TAILCALLR: return op_tailcallr(ctx, ins);
    case JOP_CONST_I32: return op_const_i32(ctx, ins);
    case JOP_CONST_BOOL: return op_const_bool(ctx, ins);
    case JOP_CONST_ATOM: return op_const_atom(ctx, ins);
    case JOP_CONST_FUN: return op_const_fun(ctx, ins);
    case JOP_CONST_NULL: return op_const_null(ctx, ins);
    case JOP_CONST_F32: return op_const_f32(ctx, ins);
    case JOP_CONST_F16: return op_const_f16(ctx, ins);
    case JOP_CONST_I8_IMM: return op_const_i8_imm(ctx, ins);
    case JOP_CONST_I64: return op_const_i64(ctx, ins);
    case JOP_CONST_F64: return op_const_f64(ctx, ins);
    case JOP_CONST_BYTES: return op_const_bytes(ctx, ins);
    case JOP_JMP: return op_jmp(ctx, ins);
    case JOP_JMP_IF: return op_jmp_if(ctx, ins);
    case JOP_ASSERT: return op_assert(ctx, ins);
    case JOP_CLOSURE: return op_closure(ctx, ins);
    case JOP_BIND_THIS: return op_bind_this(ctx, ins);
    case JOP_ADD_I32: return op_add_i32(ctx, ins);
    case JOP_ADD_I32_IMM: return op_add_i32_imm(ctx, ins);
    case JOP_SUB_I32: return op_sub_i32(ctx, ins);
    case JOP_SUB_I32_IMM: return op_sub_i32_imm(ctx, ins);
    case JOP_MUL_I32: return op_mul_i32(ctx, ins);
    case JOP_MUL_I32_IMM: return op_mul_i32_imm(ctx, ins);
    case JOP_ADD_F32: return op_add_f32(ctx, ins);
    case JOP_SUB_F32: return op_sub_f32(ctx, ins);
    case JOP_MUL_F32: return op_mul_f32(ctx, ins);
    case JOP_ADD_F16: return op_add_f16(ctx, ins);
    case JOP_SUB_F16: return op_sub_f16(ctx, ins);
    case JOP_MUL_F16: return op_mul_f16(ctx, ins);
    case JOP_ADD_F64: return op_add_f64(ctx, ins);
    case JOP_SUB_F64: return op_sub_f64(ctx, ins);
    case JOP_MUL_F64: return op_mul_f64(ctx, ins);
    case JOP_ADD_I64: return op_add_i64(ctx, ins);
    case JOP_SUB_I64: return op_sub_i64(ctx, ins);
    case JOP_MUL_I64: return op_mul_i64(ctx, ins);
    case JOP_MOD_I32: return op_mod_i32(ctx, ins);
    case JOP_MOD_I64: return op_mod_i64(ctx, ins);
    case JOP_SHL_I32: return op_shl_i32(ctx, ins);
    case JOP_SHL_I64: return op_shl_i64(ctx, ins);
    case JOP_SHR_I32: return op_shr_i32(ctx, ins);
    case JOP_SHR_I64: return op_shr_i64(ctx, ins);
    case JOP_DIV_F32: return op_div_f32(ctx, ins);
    case JOP_DIV_F64: return op_div_f64(ctx, ins);
    case JOP_SEXT_I64: return op_sext_i64(ctx, ins);
    case JOP_SEXT_I16: return op_sext_i16(ctx, ins);
    case JOP_TRUNC_I8: return op_trunc_i8(ctx, ins);
    case JOP_TRUNC_I16: return op_trunc_i16(ctx, ins);
    case JOP_I32_FROM_I64: return op_i32_from_i64(ctx, ins);
    case JOP_F64_FROM_I32: return op_f64_from_i32(ctx, ins);
    case JOP_I32_FROM_F64: return op_i32_from_f64(ctx, ins);
    case JOP_F64_FROM_I64: return op_f64_from_i64(ctx, ins);
    case JOP_I64_FROM_F64: return op_i64_from_f64(ctx, ins);
    case JOP_F32_FROM_I32: return op_f32_from_i32(ctx, ins);
    case JOP_I32_FROM_F32: return op_i32_from_f32(ctx, ins);
    case JOP_F64_FROM_F32: return op_f64_from_f32(ctx, ins);
    case JOP_F32_FROM_F64: return op_f32_from_f64(ctx, ins);
    case JOP_F16_FROM_F32: return op_f16_from_f32(ctx, ins);
    case JOP_F32_FROM_F16: return op_f32_from_f16(ctx, ins);
    case JOP_F16_FROM_I32: return op_f16_from_i32(ctx, ins);
    case JOP_I32_FROM_F16: return op_i32_from_f16(ctx, ins);
    case JOP_F32_FROM_I64: return op_f32_from_i64(ctx, ins);
    case JOP_I64_FROM_F32: return op_i64_from_f32(ctx, ins);
    case JOP_EQ_I32: return op_eq_i32(ctx, ins);
    case JOP_EQ_I32_IMM: return op_eq_i32_imm(ctx, ins);
    case JOP_LT_I32_IMM: return op_lt_i32_imm(ctx, ins);
    case JOP_EQ_F32: return op_eq_f32(ctx, ins);
    case JOP_EQ_F64: return op_eq_f64(ctx, ins);
    case JOP_EQ_I64: return op_eq_i64(ctx, ins);
    case JOP_LT_I32: return op_lt_i32(ctx, ins);
    case JOP_LT_I64: return op_lt_i64(ctx, ins);
    case JOP_LT_F32: return op_lt_f32(ctx, ins);
    case JOP_LT_F64: return op_lt_f64(ctx, ins);
    case JOP_TO_DYN: return op_to_dyn(ctx, ins);
    case JOP_FROM_DYN_I8: return op_from_dyn_i8(ctx, ins);
    case JOP_FROM_DYN_I16: return op_from_dyn_i16(ctx, ins);
    case JOP_FROM_DYN_I32: return op_from_dyn_i32(ctx, ins);
    case JOP_FROM_DYN_I64: return op_from_dyn_i64(ctx, ins);
    case JOP_FROM_DYN_F16: return op_from_dyn_f16(ctx, ins);
    case JOP_FROM_DYN_F32: return op_from_dyn_f32(ctx, ins);
    case JOP_FROM_DYN_F64: return op_from_dyn_f64(ctx, ins);
    case JOP_FROM_DYN_BOOL: return op_from_dyn_bool(ctx, ins);
    case JOP_FROM_DYN_ATOM: return op_from_dyn_atom(ctx, ins);
    case JOP_FROM_DYN_PTR: return op_from_dyn_ptr(ctx, ins);
    case JOP_SPILL_PUSH: return op_spill_push(ctx, ins);
    case JOP_SPILL_POP: return op_spill_pop(ctx, ins);
    case JOP_NEG_I32: return op_neg_i32(ctx, ins);
    case JOP_NEG_I64: return op_neg_i64(ctx, ins);
    case JOP_NEG_F32: return op_neg_f32(ctx, ins);
    case JOP_NEG_F64: return op_neg_f64(ctx, ins);
    case JOP_NOT_BOOL: return op_not_bool(ctx, ins);
    case JOP_TRY: return op_try(ctx, ins);
    case JOP_ENDTRY: return op_endtry(ctx, ins);
    case JOP_THROW: return op_throw(ctx, ins);
    case JOP_PHYSEQ: return op_physeq(ctx, ins);
    case JOP_KINDOF: return op_kindof(ctx, ins);
    case JOP_SWITCH_KIND: return op_switch_kind(ctx, ins);
    case JOP_CASE_KIND: return op_case_kind(ctx, ins);
    case JOP_LIST_NIL: return op_list_nil(ctx, ins);
    case JOP_LIST_CONS: return op_list_cons(ctx, ins);
    case JOP_LIST_HEAD: return op_list_head(ctx, ins);
    case JOP_LIST_TAIL: return op_list_tail(ctx, ins);
    case JOP_LIST_IS_NIL: return op_list_is_nil(ctx, ins);
    case JOP_ARRAY_NEW: return op_array_new(ctx, ins);
    case JOP_ARRAY_LEN: return op_array_len(ctx, ins);
    case JOP_ARRAY_GET: return op_array_get(ctx, ins);
    case JOP_ARRAY_SET: return op_array_set(ctx, ins);
    case JOP_BYTES_NEW: return op_bytes_new(ctx, ins);
    case JOP_BYTES_LEN: return op_bytes_len(ctx, ins);
    case JOP_BYTES_GET_U8: return op_bytes_get_u8(ctx, ins);
    case JOP_BYTES_SET_U8: return op_bytes_set_u8(ctx, ins);
    case JOP_BYTES_CONCAT2: return op_bytes_concat2(ctx, ins);
    case JOP_BYTES_CONCAT_MANY: return op_bytes_concat_many(ctx, ins);
    case JOP_OBJ_NEW: return op_obj_new(ctx, ins);
    case JOP_OBJ_HAS_ATOM: return op_obj_has_atom(ctx, ins);
    case JOP_OBJ_GET_ATOM: return op_obj_get_atom(ctx, ins);
    case JOP_OBJ_SET_ATOM: return op_obj_set_atom(ctx, ins);
    case JOP_OBJ_GET: return op_obj_get(ctx, ins);
    case JOP_OBJ_SET: return op_obj_set(ctx, ins);
    default: return op_panic(ctx, ins);
  }
#endif
}
