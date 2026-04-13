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

op_result op_obj_get_atom(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "obj_get_atom on null");
    return OP_CONTINUE;
  }
  uint32_t atom_id = ins->imm;

  if(!m->proto_enabled) {
    jello_value v = jello_object_get(o, atom_id);
    vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
    return OP_CONTINUE;
  }
  if(atom_id == JELLO_ATOM___PROTO__) {
    jello_value v = jello_object_get(o, atom_id);
    vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
    return OP_CONTINUE;
  }
  jello_object* cur = o;
  jello_value v = jello_make_null();
  for(uint32_t steps = 0;; steps++) {
    if(jello_object_has(cur, atom_id)) {
      v = jello_object_get(cur, atom_id);
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
  vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
  return OP_CONTINUE;
}

op_result op_obj_set_atom(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_object* o = (jello_object*)vm_load_ptr(&fr->rf, ins->b);
  if(!o) jello_vm_panic();
  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  uint32_t atom_id = ins->imm;
  jello_object_set(o, atom_id, v);
  if(atom_id == JELLO_ATOM___PROTO__) {
    obj_update_proto_cache(o, v, m);
  }
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
    vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
    return OP_CONTINUE;
  }
  if(atom_id == JELLO_ATOM___PROTO__) {
    jello_value v = jello_object_get(o, atom_id);
    vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
    return OP_CONTINUE;
  }
  jello_object* cur = o;
  jello_value v = jello_make_null();
  for(uint32_t steps = 0;; steps++) {
    if(jello_object_has(cur, atom_id)) {
      v = jello_object_get(cur, atom_id);
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
  vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
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
  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  jello_object_set(o, atom_id, v);
  if(atom_id == JELLO_ATOM___PROTO__) {
    obj_update_proto_cache(o, v, m);
  }
  return OP_CONTINUE;
}
