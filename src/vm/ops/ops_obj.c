// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

#include <string.h>

op_result op_obj_new(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t type_id = f->reg_types[ins->a];
  jello_object* o = jello_object_new(vm, type_id);
  vm_store_ptr(&fr->rf, ins->a, o);
  return OP_CONTINUE;
}

op_result op_obj_has_atom(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_has_atom on null");
    return OP_CONTINUE;
  }
  int has = jello_object_has(o, ins->imm);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)(has != 0));
  return OP_CONTINUE;
}

static void obj_update_proto_cache(jello_object* o, jello_value v, const jello_bc_module* m) {
  if(!m->proto_enabled) return;
  if(jello_is_ptr(v)) {
    void* p = jello_as_ptr(v);
    if(p) {
      jello_obj_header* h = (jello_obj_header*)p;
      if(h->kind == (uint32_t)JELLO_OBJ_OBJECT) {
        o->proto = (jello_object*)p;
      } else {
        o->proto = NULL;
      }
    } else {
      o->proto = NULL;
    }
  } else {
    o->proto = NULL;
  }
}

int vm_obj_get_atom_typed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f,
                          reg_frame* rf, uint32_t dst, uint32_t obj_reg, uint32_t atom_id,
                          uint32_t slot_hint) {
  jello_object* o = (jello_object*)vm_load_ptr(rf, obj_reg);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_get_atom on null");
    return 0;
  }
  /* No chain walk when proto disabled, reading __proto__, or object has no proto. */
  if(!m->proto_enabled || atom_id == JELLO_ATOM___PROTO__ || !o->proto) {
    jello_value v = jello_object_get_slot(o, slot_hint, atom_id);
    switch(vm_reg_kind(m, f, dst)) {
      case JELLO_T_I8:
      case JELLO_T_I16:
      case JELLO_T_I32:
        if(jello_is_i32(v)) {
          vm_store_u32(rf, dst, (uint32_t)jello_as_i32(v));
          return 1;
        }
        break;
      case JELLO_T_F64:
        if(jello_is_box_f64(v)) {
          vm_store_f64(rf, dst, jello_as_box_f64(v));
          return 1;
        }
        break;
      case JELLO_T_F32:
        if(jello_is_box_f32(v)) {
          vm_store_f32(rf, dst, jello_as_box_f32(v));
          return 1;
        }
        break;
      case JELLO_T_I64:
        if(jello_is_box_i64(v)) {
          vm_store_i64(rf, dst, jello_as_box_i64(v));
          return 1;
        }
        break;
      default:
        break;
    }
    vm_store_from_boxed(vm, m, f, rf, dst, v);
    return 1;
  }
  jello_object* cur = o;
  jello_value v = jello_make_null();
  for(uint32_t steps = 0;; steps++) {
    jello_value* slot = jello_object_slot(cur, atom_id);
    if(slot) {
      v = *slot;
      break;
    }
    jello_object* p = cur->proto;
    if(!p) break;
    cur = p;
    if(steps > 1024u) {
      (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "prototype chain too deep");
      return 0;
    }
  }
  vm_store_from_boxed(vm, m, f, rf, dst, v);
  return 1;
}

int vm_obj_set_atom_typed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f,
                          reg_frame* rf, uint32_t val_reg, uint32_t obj_reg, uint32_t atom_id,
                          uint32_t slot_hint) {
  jello_object* o = (jello_object*)vm_load_ptr(rf, obj_reg);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_set_atom on null");
    return 0;
  }
  int existed = 0;
  jello_value* slot = jello_object_set_slot(o, slot_hint, atom_id, &existed);
  if(!slot) return 0;
  if(existed && atom_id != JELLO_ATOM___PROTO__ &&
     vm_store_num_inplace(vm, m, f, rf, val_reg, slot))
    return 1;
  jello_value v = vm_box_from_typed(vm, m, f, rf, val_reg);
  if(vm_reg_kind(m, f, val_reg) == JELLO_T_DYNAMIC) v = vm_clone_numbox(vm, v);
  *slot = v;
  if(atom_id == JELLO_ATOM___PROTO__) obj_update_proto_cache(o, v, m);
  return 1;
}

op_result op_obj_get_atom(exec_ctx* ctx, const jello_insn* ins) {
  (void)vm_obj_get_atom_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, ins->a, ins->b, ins->imm, ins->c);
  return OP_CONTINUE;
}

op_result op_obj_set_atom(exec_ctx* ctx, const jello_insn* ins) {
  (void)vm_obj_set_atom_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, ins->a, ins->b, ins->imm, ins->c);
  return OP_CONTINUE;
}

op_result op_obj_get(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) jello_vm_panic();
  uint32_t atom_id = vm_load_u32(&fr->rf, ins->c);

  if(atom_id >= m->natoms) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "obj_get atom id out of range");
    return OP_CONTINUE;
  }
  if(!m->proto_enabled) {
    jello_value v = jello_object_get(o, atom_id);
    vm_store_from_boxed(vm, m, f, &fr->rf, ins->a, v);
    return OP_CONTINUE;
  }
  if(atom_id == JELLO_ATOM___PROTO__) {
    jello_value v = jello_object_get(o, atom_id);
    vm_store_from_boxed(vm, m, f, &fr->rf, ins->a, v);
    return OP_CONTINUE;
  }
  jello_object* cur = o;
  jello_value v = jello_make_null();
  for(uint32_t steps = 0;; steps++) {
    jello_value* slot = jello_object_slot(cur, atom_id);
    if(slot) {
      v = *slot;
      break;
    }
    jello_object* p = cur->proto;
    if(!p) break;
    cur = p;
    if(steps > 1024u) {
      (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "prototype chain too deep");
      return OP_CONTINUE;
    }
  }
  vm_store_from_boxed(vm, m, f, &fr->rf, ins->a, v);
  return OP_CONTINUE;
}

op_result op_obj_set(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) jello_vm_panic();
  uint32_t atom_id = vm_load_u32(&fr->rf, ins->c);

  if(atom_id >= m->natoms) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "obj_set atom id out of range");
    return OP_CONTINUE;
  }
  int existed = 0;
  jello_value* slot = jello_object_upsert(o, atom_id, &existed);
  if(!slot) jello_vm_panic();
  if(existed && atom_id != JELLO_ATOM___PROTO__ &&
     vm_store_num_inplace(vm, m, f, &fr->rf, ins->a, slot))
    return OP_CONTINUE;
  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  if(vm_reg_kind(m, f, ins->a) == JELLO_T_DYNAMIC) v = vm_clone_numbox(vm, v);
  *slot = v;
  if(atom_id == JELLO_ATOM___PROTO__) obj_update_proto_cache(o, v, m);
  return OP_CONTINUE;
}

op_result op_obj_has(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_has on null");
    return OP_CONTINUE;
  }
  uint32_t atom_id = vm_load_u32(&fr->rf, ins->c);
  if(atom_id >= m->natoms) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "obj_has atom id out of range");
    return OP_CONTINUE;
  }
  int has = jello_object_has(o, atom_id);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)(has != 0));
  return OP_CONTINUE;
}

op_result op_obj_remove(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_remove on null");
    return OP_CONTINUE;
  }
  uint32_t atom_id = vm_load_u32(&fr->rf, ins->c);
  if(atom_id >= m->natoms) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "obj_remove atom id out of range");
    return OP_CONTINUE;
  }
  int removed = jello_object_remove(o, atom_id);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)(removed != 0));
  return OP_CONTINUE;
}

op_result op_obj_clear(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_clear on null");
    return OP_CONTINUE;
  }
  jello_object_clear(o);
  return OP_CONTINUE;
}

op_result op_obj_copy(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_object* src = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!src) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_copy on null");
    return OP_CONTINUE;
  }
  jello_object* dst = jello_object_copy(vm, src);
  vm_store_ptr(&fr->rf, ins->a, dst);
  return OP_CONTINUE;
}

op_result op_obj_keys(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_keys on null");
    return OP_CONTINUE;
  }
  uint32_t list_type_id = f->reg_types[ins->a];
  jello_list* tail = NULL;
  for(uint32_t i = 0; i < o->cap; i++) {
    if(o->states[i] == JELLO_OBJ_SLOT_OCCUPIED) {
      jello_value head = jello_make_atom(o->keys[i]);
      tail = jello_list_cons(vm, list_type_id, head, tail);
    }
  }
  vm_store_ptr(&fr->rf, ins->a, tail);
  return OP_CONTINUE;
}
