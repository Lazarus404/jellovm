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

op_result op_to_dyn(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->b);
  vm_store_val(&fr->rf, ins->a, v);
  return OP_CONTINUE;
}

op_result op_from_dyn_i32(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(!jello_is_i32(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_i32 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, (uint32_t)jello_as_i32(v));
  return OP_CONTINUE;
}

static uint32_t from_dyn_int_value(jello_vm* vm, jello_value v, int* ok) {
  if(jello_is_i32(v)) {
    *ok = 1;
    return (uint32_t)jello_as_i32(v);
  }
  if(jello_is_box_i64(v)) {
    *ok = 1;
    return (uint32_t)jello_as_box_i64(v);
  }
  *ok = 0;
  return 0;
}

op_result op_from_dyn_i8(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  int ok;
  uint32_t val = from_dyn_int_value(vm, v, &ok);
  if(!ok) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_i8 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_u32_masked(&fr->rf, ins->a, val, JELLO_T_I8);
  return OP_CONTINUE;
}

op_result op_from_dyn_i16(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  int ok;
  uint32_t val = from_dyn_int_value(vm, v, &ok);
  if(!ok) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_i16 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_u32_masked(&fr->rf, ins->a, val, JELLO_T_I16);
  return OP_CONTINUE;
}

op_result op_from_dyn_i64(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(jello_is_i32(v)) {
    vm_store_i64(&fr->rf, ins->a, (int64_t)jello_as_i32(v));
    return OP_CONTINUE;
  }
  if(!jello_is_box_i64(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_i64 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_i64(&fr->rf, ins->a, jello_as_box_i64(v));
  return OP_CONTINUE;
}

op_result op_from_dyn_f64(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(jello_is_i32(v)) {
    vm_store_f64(&fr->rf, ins->a, (double)jello_as_i32(v));
    return OP_CONTINUE;
  }
  if(jello_is_null(v)) {
    vm_store_f64(&fr->rf, ins->a, 0.0);
    return OP_CONTINUE;
  }
  if(!jello_is_box_f64(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_f64 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_f64(&fr->rf, ins->a, jello_as_box_f64(v));
  return OP_CONTINUE;
}

op_result op_from_dyn_f32(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(jello_is_i32(v)) {
    vm_store_f32(&fr->rf, ins->a, (float)jello_as_i32(v));
    return OP_CONTINUE;
  }
  if(!jello_is_box_f32(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_f32 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_f32(&fr->rf, ins->a, jello_as_box_f32(v));
  return OP_CONTINUE;
}

op_result op_from_dyn_f16(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(!jello_is_box_f16(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_f16 type mismatch");
    return OP_CONTINUE;
  }
  vm_store_f16_bits(&fr->rf, ins->a, jello_as_box_f16(v));
  return OP_CONTINUE;
}

op_result op_from_dyn_bool(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(!jello_is_bool(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_bool type mismatch");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, (uint32_t)jello_as_bool(v));
  return OP_CONTINUE;
}

op_result op_from_dyn_atom(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(!jello_is_atom(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_atom type mismatch");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, jello_as_atom(v));
  return OP_CONTINUE;
}

op_result op_from_dyn_ptr(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  jello_type_kind dk = vm_reg_kind(m, f, ins->a);
  uint32_t want = vm_expected_obj_kind_for_typed_ptr(dk);
  if(want == 0) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_ptr dst not pointer-kind");
    return OP_CONTINUE;
  }
  if(jello_is_null(v)) {
    vm_store_ptr(&fr->rf, ins->a, NULL);
    return OP_CONTINUE;
  }
  if(!jello_is_ptr(v)) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_ptr type mismatch");
    return OP_CONTINUE;
  }
  if(jello_obj_kind_of(v) != want) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "from_dyn_ptr kind mismatch");
    return OP_CONTINUE;
  }
  vm_store_ptr(&fr->rf, ins->a, jello_as_ptr(v));
  return OP_CONTINUE;
}

op_result op_spill_push(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value v = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
  vm_spill_push(vm, v);
  return OP_CONTINUE;
}

op_result op_spill_pop(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value v = vm_spill_pop(vm);
  vm_store_from_boxed(m, f, &fr->rf, ins->a, v);
  return OP_CONTINUE;
}

op_result op_physeq(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value vb = vm_box_from_typed(vm, m, f, &fr->rf, ins->b);
  jello_value vc = vm_box_from_typed(vm, m, f, &fr->rf, ins->c);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)(vb == vc));
  return OP_CONTINUE;
}

op_result op_kindof(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  vm_store_u32(&fr->rf, ins->a, vm_kindof_dynamic(v));
  return OP_CONTINUE;
}
