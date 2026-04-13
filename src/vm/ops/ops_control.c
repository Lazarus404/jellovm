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
#include <jello/internal/ops_decl.h>

#include <stdlib.h>
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

op_result op_jmp(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  const jello_bc_function* f = ctx->f;

  int32_t d = (int32_t)ins->imm;
  int32_t npc = (int32_t)fr->pc + d;
  if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
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
  uint32_t cond = vm_load_u32(&fr->rf, ins->a);
  if(cond != 0) return OP_CONTINUE;

  vm->trap_code = JELLO_TRAP_THROWN;
  vm->trap_msg = "assertion failed";
  vm->exc_pending = 1;
  vm->exc_payload = jello_make_i32(-1);
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
