// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

#include <string.h>

static int array_elem_kind(
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t arr_reg,
    jello_type_kind* out
) {
  if(vm_reg_kind(m, f, arr_reg) != JELLO_T_ARRAY) return 0;
  jello_type_id arr_tid = f->reg_types[arr_reg];
  if(arr_tid >= m->ntypes) return 0;
  const jello_type_entry* te = &m->types[arr_tid];
  if(te->kind != JELLO_T_ARRAY) return 0;
  uint32_t elem_tid = te->as.unary.elem;
  if(elem_tid >= m->ntypes) return 0;
  *out = m->types[elem_tid].kind;
  return 1;
}

op_result op_array_new(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t len = vm_load_u32(&fr->rf, ins->b);
  if(vm->max_array_len && len > vm->max_array_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "array_new length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_array* a = jello_array_new(vm, type_id, len);
  if(a && a->data && len) {
    // Typed arrays store boxed values. For Array<I32> (and other i32-ish element types),
    // default to 0 rather than null so typed reads are well-defined.
    if(type_id < m->ntypes) {
      const jello_type_entry* te = &m->types[type_id];
      if(te->kind == JELLO_T_ARRAY) {
        uint32_t elem_tid = te->as.unary.elem;
        if(elem_tid < m->ntypes) {
          jello_type_kind ek = m->types[elem_tid].kind;
          if(ek == JELLO_T_I8 || ek == JELLO_T_I16 || ek == JELLO_T_I32) {
            jello_value z = jello_make_i32(0);
            for(uint32_t i = 0; i < len; i++) a->data[i] = z;
          }
        }
      }
    }
  }
  vm_store_ptr(&fr->rf, ins->a, a);
  return OP_CONTINUE;
}

op_result op_array_len(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_array* a = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!a) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_len on null");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, a->length);
  return OP_CONTINUE;
}

op_result op_array_get(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_array* a = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!a) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_get on null");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= a->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "array_get index out of bounds");
    return OP_CONTINUE;
  }
  jello_type_kind elem_k;
  if(vm_reg_kind(m, f, ins->a) == JELLO_T_F64 && array_elem_kind(m, f, ins->b, &elem_k) &&
     elem_k == JELLO_T_F64) {
    jello_value v = a->data[idx];
    if(jello_is_box_f64(v)) {
      vm_store_f64(&fr->rf, ins->a, jello_as_box_f64(v));
      return OP_CONTINUE;
    }
  }
  vm_store_from_boxed(vm, m, f, &fr->rf, ins->a, a->data[idx]);
  return OP_CONTINUE;
}

op_result op_array_set(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_array* a = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!a) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_set on null");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= a->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "array_set index out of bounds");
    return OP_CONTINUE;
  }
  jello_type_kind elem_k;
  if(vm_reg_kind(m, f, ins->a) == JELLO_T_F64 && array_elem_kind(m, f, ins->b, &elem_k) &&
     elem_k == JELLO_T_F64) {
    jello_value* slot = &a->data[idx];
    if(jello_is_box_f64(*slot)) {
      ((jello_box_f64*)jello_as_ptr(*slot))->value = vm_load_f64(&fr->rf, ins->a);
      return OP_CONTINUE;
    }
  }
  if(vm_store_num_inplace(vm, m, f, &fr->rf, ins->a, &a->data[idx]))
    return OP_CONTINUE;
  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  if(vm_reg_kind(m, f, ins->a) == JELLO_T_DYNAMIC) v = vm_clone_numbox(vm, v);
  a->data[idx] = v;
  return OP_CONTINUE;
}

op_result op_array_concat2(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_array* x = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!x) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_concat2 on null lhs");
    return OP_CONTINUE;
  }
  jello_array* y = (jello_array*)vm_load_ptr(&fr->rf, ins->c);
  if(!y) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_concat2 on null rhs");
    return OP_CONTINUE;
  }
  uint64_t total64 = (uint64_t)x->length + (uint64_t)y->length;
  if(total64 > 0xffffffffu) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "array_concat2 length overflow");
    return OP_CONTINUE;
  }
  uint32_t total = (uint32_t)total64;
  if(vm->max_array_len && total > vm->max_array_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "array_concat2 length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  if(x->h.type_id != type_id || y->h.type_id != type_id) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "array_concat2 type mismatch");
    return OP_CONTINUE;
  }
  jello_array* out = jello_array_new(vm, type_id, total);
  jello_gc_push_root(vm, jello_from_ptr(out));
  for(uint32_t i = 0; i < x->length; i++) out->data[i] = vm_clone_numbox(vm, x->data[i]);
  for(uint32_t i = 0; i < y->length; i++)
    out->data[x->length + i] = vm_clone_numbox(vm, y->data[i]);
  vm_store_ptr(&fr->rf, ins->a, out);
  jello_gc_pop_roots(vm, 1);
  return OP_CONTINUE;
}

op_result op_array_resize(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  call_frame* fr = ctx->fr;

  jello_array* a = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!a) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_resize on null");
    return OP_CONTINUE;
  }
  uint32_t new_len = vm_load_u32(&fr->rf, ins->c);
  if(!jello_array_resize(vm, m, a, new_len)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "array_resize failed");
  }
  return OP_CONTINUE;
}
