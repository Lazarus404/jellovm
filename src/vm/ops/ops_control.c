// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

op_result op_nop(exec_ctx* ctx, const jello_insn* ins) {
  (void)ctx;
  (void)ins;
  return OP_CONTINUE;
}

op_result op_ret(exec_ctx* ctx, const jello_insn* ins) {
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
    if(ctx->out_exports && ctx->entry_module_idx != UINT32_MAX &&
       ctx->entry_module_idx < f->nregs) {
      *ctx->out_exports = vm_box_from_typed(vm, m, f, &fr->rf, ctx->entry_module_idx);
    }
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    free(vm->call_frames);
    vm->call_frames = NULL;
    vm->call_frames_len = 0;
    vm->call_frames_cap = 0;
    free(vm->const_fun_cache);
    vm->const_fun_cache = NULL;
    vm->const_fun_cache_len = 0;
    free(vm->const_bytes_cache);
    vm->const_bytes_cache = NULL;
    vm->const_bytes_cache_len = 0;
    vm_enum_nullary_cache_clear(vm);
    free(vm->exc_handlers);
    vm->exc_handlers = NULL;
    vm->exc_handlers_len = 0;
    vm->exc_handlers_cap = 0;
    if(ctx->out) *ctx->out = ret;
    return OP_RETURN;
  }

  if(vm->call_frames_len < 2u) jello_vm_panic();
  call_frame* caller = &frames[vm->call_frames_len - 2u];

  if(fr->jdll_ret_capture && vm->jdll_call_out) {
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
    *vm->jdll_call_out = ret;
    vm->jdll_call_out = NULL;
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    return OP_CONTINUE;
  }

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
  vm_store_from_boxed(vm, m, caller->f, &caller->rf, caller_dst, ret);
  return OP_CONTINUE;
}

op_result op_jmp(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  const jello_bc_function* f = ctx->f;
  jello_vm* vm = ctx->vm;

  int32_t d = (int32_t)ins->imm;
  int32_t npc = (int32_t)fr->pc + d;
  if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
  if(d < 0 && jello_vm_fuel_charge(vm) != 0) return OP_TRAP;
  fr->pc = (uint32_t)npc;
  return OP_CONTINUE;
}

op_result op_jmp_if(exec_ctx* ctx, const jello_insn* ins) {
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

op_result op_assert(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  uint32_t cond = vm_load_u32(&fr->rf, ins->a);
  if(cond != 0) return OP_CONTINUE;

  vm->trap_code = JELLO_TRAP_THROWN;
  vm->exc_pending = 1;
  vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_THROWN);

  if(ins->c == 1u) {
    jello_value msgv = vm_box_from_typed(vm, m, f, &fr->rf, ins->b);
    if(jello_is_ptr(msgv) && jello_obj_kind_of(msgv) == (uint32_t)JELLO_OBJ_BYTES) {
      jello_bytes* mb = (jello_bytes*)jello_as_ptr(msgv);
      if(mb && mb->length > 0u) {
        uint32_t n = mb->length;
        if(n > 160u) n = 160u;
        snprintf(vm->trap_msg_buf, sizeof vm->trap_msg_buf, "%.*sassertion failed",
                 (int)n, (const char*)mb->data);
        vm->trap_msg = vm->trap_msg_buf;
        return OP_TRAP;
      }
    }
  }

  vm->trap_msg = "assertion failed";
  return OP_TRAP;
}

op_result op_try(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  const jello_bc_function* f = ctx->f;

  uint32_t frame_index = vm->call_frames_len - 1u;
  int32_t d = (int32_t)ins->imm;
  int32_t catch_pc = (int32_t)fr->pc + d;
  if(catch_pc < 0 || catch_pc > (int32_t)f->ninsns) jello_vm_panic();
  vm_exc_push(vm, frame_index, (uint32_t)catch_pc, (uint32_t)ins->a, (uint8_t)ins->b);
  return OP_CONTINUE;
}

op_result op_endtry(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  (void)ins;

  uint32_t frame_index = vm->call_frames_len - 1u;
  exc_handler top;
  if(!vm_exc_pop(vm, &top)) jello_vm_panic();
  if(top.frame_index != frame_index) jello_vm_panic();
  return OP_CONTINUE;
}

op_result op_throw(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value payload = vm_load_val(&fr->rf, ins->a);
  vm->trap_code = JELLO_TRAP_THROWN;
  vm->trap_msg = "unhandled throw";
  vm->exc_pending = 1;
  vm->exc_payload = payload;
  return OP_TRAP;
}
