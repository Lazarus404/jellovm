// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

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
  if(vm_reg_kind(m, f, ins->b) == JELLO_T_DYNAMIC) head = vm_clone_numbox(vm, head);
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
  vm_store_from_boxed(vm, m, f, &fr->rf, ins->a, node->head);
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
