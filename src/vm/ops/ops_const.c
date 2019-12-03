// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

#include <string.h>

op_result op_mov(exec_ctx* ctx, const jello_insn* ins) {
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

op_result op_const_i8_imm(exec_ctx* ctx, const jello_insn* ins) {
  int8_t v = (int8_t)(uint8_t)ins->c;
  vm_store_u32(&ctx->fr->rf, ins->a, (uint32_t)(int32_t)v);
  return OP_CONTINUE;
}

/*
 * op_const_i16 would duplicate op_const_i32 with no encoding
 * or performance benefit, since every i16 value already fits
 * in ins->imm and every i16 register is already a 32-bit slot.
 */

op_result op_const_i32(exec_ctx* ctx, const jello_insn* ins) {
  vm_store_u32(&ctx->fr->rf, ins->a, ins->imm);
  return OP_CONTINUE;
}

op_result op_const_i64(exec_ctx* ctx, const jello_insn* ins) {
  const jello_bc_module* m = ctx->m;
  vm_store_i64(&ctx->fr->rf, ins->a, m->const_i64[ins->imm]);
  return OP_CONTINUE;
}

op_result op_const_f16(exec_ctx* ctx, const jello_insn* ins) {
  uint16_t bits = (uint16_t)(ins->imm & 0xFFFFu);
  vm_store_f16_bits(&ctx->fr->rf, ins->a, bits);
  return OP_CONTINUE;
}

op_result op_const_bool(exec_ctx* ctx, const jello_insn* ins) {
  vm_store_u32(&ctx->fr->rf, ins->a, (uint32_t)(ins->c & 1u));
  return OP_CONTINUE;
}

op_result op_const_atom(exec_ctx* ctx, const jello_insn* ins) {
  vm_store_u32(&ctx->fr->rf, ins->a, ins->imm);
  return OP_CONTINUE;
}

op_result op_const_null(exec_ctx* ctx, const jello_insn* ins) {
  vm_store_val(&ctx->fr->rf, ins->a, jello_make_null());
  return OP_CONTINUE;
}

op_result op_const_f32(exec_ctx* ctx, const jello_insn* ins) {
  uint32_t bits = ins->imm;
  float fv;
  memcpy(&fv, &bits, sizeof(fv));
  vm_store_f32(&ctx->fr->rf, ins->a, fv);
  return OP_CONTINUE;
}

op_result op_const_f64(exec_ctx* ctx, const jello_insn* ins) {
  const jello_bc_module* m = ctx->m;
  vm_store_f64(&ctx->fr->rf, ins->a, m->const_f64[ins->imm]);
  return OP_CONTINUE;
}

op_result op_const_bytes(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  const uint32_t idx = ins->imm;
  uint32_t len = 0;
  uint32_t off = 0;
  if(idx < m->nconst_bytes) {
    len = m->const_bytes_len[idx];
    off = m->const_bytes_off[idx];
  } else {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "const_bytes out of range");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* b = jello_bytes_new(vm, type_id, len);
  if(len > 0) memcpy(b->data, m->const_bytes_data + off, len);
  vm_store_ptr(&fr->rf, ins->a, b);
  return OP_CONTINUE;
}

op_result op_const_fun(exec_ctx* ctx, const jello_insn* ins) {
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
