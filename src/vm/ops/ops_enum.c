// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

#if defined(_WIN32) || defined(__MINGW32__)
#  include <malloc.h>
#  ifndef alloca
#    define alloca _alloca
#  endif
#else
#  include <alloca.h>
#endif
#include <stddef.h>

op_result op_enum_new(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t type_id = f->reg_types[ins->a];
  uint32_t tag = ins->imm;
  uint32_t nfields = (uint32_t)ins->c;
  jello_enum* e;
  if(nfields == 0) {
    e = jello_enum_nullary_intern(vm, type_id, tag);
  } else {
    jello_value* fields = (jello_value*)alloca((size_t)nfields * sizeof(jello_value));
    for(uint32_t i = 0; i < nfields; i++) {
      uint32_t r = (uint32_t)ins->b + i;
      if(r >= f->nregs) jello_vm_panic();
      fields[i] = vm_box_from_typed(vm, m, f, &fr->rf, r);
    }
    e = jello_enum_new(vm, type_id, tag, nfields, fields);
  }
  vm_store_ptr(&fr->rf, ins->a, e);
  return OP_CONTINUE;
}

op_result op_enum_tag(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_enum* e = (jello_enum*)vm_load_ptr(&fr->rf, ins->b);
  if(!e) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "enum_tag on null");
    return OP_CONTINUE;
  }
  if(jello_obj_kind_of(jello_from_ptr(e)) != JELLO_OBJ_ENUM) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "enum_tag on non-enum");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, e->tag);
  return OP_CONTINUE;
}

op_result op_enum_get(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_enum* e = (jello_enum*)vm_load_ptr(&fr->rf, ins->b);
  if(!e) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "enum_get on null");
    return OP_CONTINUE;
  }
  if(jello_obj_kind_of(jello_from_ptr(e)) != JELLO_OBJ_ENUM) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "enum_get on non-enum");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= e->nfields) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "enum_get index out of bounds");
    return OP_CONTINUE;
  }
  vm_store_from_boxed(vm, m, f, &fr->rf, ins->a, e->fields[idx]);
  return OP_CONTINUE;
}
