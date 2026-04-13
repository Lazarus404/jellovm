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

op_result op_bytes_new(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t len = vm_load_u32(&fr->rf, ins->b);
  if(vm->max_bytes_len && len > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "bytes_new length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* b = jello_bytes_new(vm, type_id, len);
  vm_store_ptr(&fr->rf, ins->a, b);
  return OP_CONTINUE;
}

op_result op_bytes_len(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_bytes* b = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!b) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_len on null");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, b->length);
  return OP_CONTINUE;
}

op_result op_bytes_get_u8(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_bytes* b = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!b) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_get_u8 on null");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= b->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_get_u8 index out of bounds");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, (uint32_t)b->data[idx]);
  return OP_CONTINUE;
}

op_result op_bytes_set_u8(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_bytes* b = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!b) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_set_u8 on null");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= b->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_set_u8 index out of bounds");
    return OP_CONTINUE;
  }
  uint32_t v = vm_load_u32(&fr->rf, ins->a);
  if(v > 255u) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_set_u8 value out of range");
    return OP_CONTINUE;
  }
  b->data[idx] = (uint8_t)v;
  return OP_CONTINUE;
}

op_result op_bytes_concat2(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_bytes* x = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!x) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat2 on null lhs");
    return OP_CONTINUE;
  }
  jello_bytes* y = (jello_bytes*)vm_load_ptr(&fr->rf, ins->c);
  if(!y) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat2 on null rhs");
    return OP_CONTINUE;
  }
  uint64_t total64 = (uint64_t)x->length + (uint64_t)y->length;
  if(total64 > 0xffffffffu) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_concat2 length overflow");
    return OP_CONTINUE;
  }
  uint32_t total = (uint32_t)total64;
  if(vm->max_bytes_len && total > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "bytes_concat2 length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* outb = jello_bytes_new(vm, type_id, total);
  jello_gc_push_root(vm, jello_from_ptr(outb));
  if(x->length) memcpy(outb->data, x->data, x->length);
  if(y->length) memcpy(outb->data + x->length, y->data, y->length);
  vm_store_ptr(&fr->rf, ins->a, outb);
  jello_gc_pop_roots(vm, 1);
  return OP_CONTINUE;
}

op_result op_bytes_concat_many(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_array* parts = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!parts) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat_many on null");
    return OP_CONTINUE;
  }

  uint64_t total64 = 0;
  for(uint32_t i = 0; i < parts->length; i++) {
    jello_value pv = parts->data[i];
    if(jello_is_null(pv)) {
      (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat_many element is null");
      return OP_CONTINUE;
    }
    if(!jello_is_ptr(pv) || jello_obj_kind_of(pv) != (uint32_t)JELLO_OBJ_BYTES) {
      (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "bytes_concat_many element not bytes");
      return OP_CONTINUE;
    }
    jello_bytes* b = (jello_bytes*)jello_as_ptr(pv);
    total64 += (uint64_t)b->length;
    if(total64 > 0xffffffffu) {
      (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_concat_many length overflow");
      return OP_CONTINUE;
    }
  }

  uint32_t total = (uint32_t)total64;
  if(vm->max_bytes_len && total > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "bytes_concat_many length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* outb = jello_bytes_new(vm, type_id, total);
  jello_gc_push_root(vm, jello_from_ptr(outb));
  uint32_t w = 0;
  for(uint32_t i = 0; i < parts->length; i++) {
    jello_bytes* b = (jello_bytes*)jello_as_ptr(parts->data[i]);
    if(b->length) {
      memcpy(outb->data + w, b->data, b->length);
      w += b->length;
    }
  }
  vm_store_ptr(&fr->rf, ins->a, outb);
  jello_gc_pop_roots(vm, 1);
  return OP_CONTINUE;
}
