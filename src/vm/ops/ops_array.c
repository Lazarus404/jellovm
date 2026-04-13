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
  vm_store_from_boxed(m, f, &fr->rf, ins->a, a->data[idx]);
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
  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  a->data[idx] = v;
  return OP_CONTINUE;
}
