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

op_result op_list_nil(exec_ctx* ctx, const jello_insn* ins) {
  vm_store_ptr(&ctx->fr->rf, ins->a, NULL);
  return OP_CONTINUE;
}

op_result op_list_cons(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value head = vm_box_from_typed(vm, m, f, &fr->rf, ins->b);
  jello_gc_push_root(vm, head);
  jello_list* tail = (jello_list*)vm_load_ptr(&fr->rf, ins->c);
  uint32_t type_id = f->reg_types[ins->a];
  jello_list* node = jello_list_cons(vm, type_id, head, tail);
  jello_gc_pop_roots(vm, 1);
  vm_store_ptr(&fr->rf, ins->a, node);
  return OP_CONTINUE;
}

op_result op_list_head(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_list* node = (jello_list*)vm_load_ptr(&fr->rf, ins->b);
  if(!node) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "list_head on nil");
    return OP_CONTINUE;
  }
  vm_store_from_boxed(m, f, &fr->rf, ins->a, node->head);
  return OP_CONTINUE;
}

op_result op_list_tail(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_list* node = (jello_list*)vm_load_ptr(&fr->rf, ins->b);
  if(!node) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "list_tail on nil");
    return OP_CONTINUE;
  }
  vm_store_ptr(&fr->rf, ins->a, node->tail);
  return OP_CONTINUE;
}

op_result op_list_is_nil(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  void* p = vm_load_ptr(&fr->rf, ins->b);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)(p == NULL));
  return OP_CONTINUE;
}
