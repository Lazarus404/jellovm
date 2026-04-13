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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

op_result op_closure(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t type_id = f->reg_types[ins->a];
  uint32_t ncaps = (uint32_t)ins->c;

  /* Fast path: all caps non-pointer → store raw bytes, avoid boxing/GC roots. */
  uint8_t all_nonptr = 1;
  uint32_t raw_cap_size = 0;
  for(uint32_t i = 0; i < ncaps && all_nonptr; i++) {
    uint32_t tid = f->reg_types[(uint32_t)ins->b + i];
    jello_type_kind k = m->types[tid].kind;
    if(!vm_type_is_nonptr(k)) all_nonptr = 0;
    else raw_cap_size += (uint32_t)jello_slot_size(k);
  }

  jello_function* clo;
  if(all_nonptr && ncaps > 0) {
    uint8_t raw_buf[256];
    if(raw_cap_size > sizeof(raw_buf)) {
      all_nonptr = 0; /* fall back to boxed */
    } else {
      uint32_t off = 0;
      for(uint32_t i = 0; i < ncaps; i++) {
        uint32_t r = (uint32_t)ins->b + i;
        jello_type_kind k = m->types[f->reg_types[r]].kind;
        size_t sz = jello_slot_size(k);
        memcpy(raw_buf + off, fr->rf.mem + fr->rf.off[r], sz);
        off += (uint32_t)sz;
      }
      clo = jello_closure_new_raw(vm, type_id, ins->imm, ncaps, raw_cap_size, raw_buf);
    }
  }
  if(!all_nonptr || ncaps == 0) {
    jello_value tmp[256];
    for(uint32_t i = 0; i < ncaps; i++) {
      tmp[i] = vm_box_from_typed(vm, m, f, &fr->rf, (uint32_t)ins->b + i);
      jello_gc_push_root(vm, tmp[i]);
    }
    clo = jello_closure_new(vm, type_id, ins->imm, ncaps, tmp);
    jello_gc_pop_roots(vm, ncaps);
  }
  vm_store_ptr(&fr->rf, ins->a, clo);
  if(getenv("JELLO_TRACE_CLOSURE")) {
    uint32_t func_idx = (uint32_t)(f - m->funcs);
    fprintf(stderr, "[JELLO_TRACE] Closure: func=%u dst_reg=%u cap_src_reg=%u ncaps=%u target_func=%u\n",
            (unsigned)func_idx, (unsigned)ins->a, (unsigned)ins->b, (unsigned)ncaps, (unsigned)ins->imm);
  }
  return OP_CONTINUE;
}

op_result op_bind_this(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_function* base = (jello_function*)vm_load_ptr(&fr->rf, ins->b);
  if(!base) jello_vm_panic();
  if(base->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) jello_vm_panic();
  jello_value thisv = vm_box_from_typed(vm, m, f, &fr->rf, ins->c);
  jello_gc_push_root(vm, thisv);
  uint32_t type_id = f->reg_types[ins->a];
  jello_function* bound = jello_function_bind_this(vm, type_id, base, thisv);
  jello_gc_pop_roots(vm, 1);
  vm_store_ptr(&fr->rf, ins->a, bound);
  return OP_CONTINUE;
}

op_result op_call(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t fi = ins->imm;
  if(jello_is_native_builtin(fi)) {
    jello_invoke_native_builtin(ctx, ins, fi, ins->b);
    return OP_CONTINUE;
  }
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) jello_vm_panic();
  const jello_bc_function* cf = &m->funcs[bytecode_idx];
  uint32_t first = ins->b;
  uint32_t na = ins->c;
  if(first + na > fr->rf.nregs) jello_vm_panic();
  uint32_t caller_i = vm->call_frames_len - 1u;
  if(!vm_push_frame(vm, m, cf, f, caller_i, ins->a, first, na, NULL, 1)) return OP_CONTINUE;
  return OP_CONTINUE;
}

op_result op_callr(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(getenv("JELLO_TRACE_CLOSURE")) {
    uint32_t func_idx = (uint32_t)(f - m->funcs);
    fprintf(stderr, "[JELLO_TRACE] CallR: func=%u pc=%u callee_reg=%u val=0x%zx is_ptr=%d is_null=%d cap_start=%u nregs=%u\n",
            (unsigned)func_idx, (unsigned)(fr->pc > 0 ? fr->pc - 1 : 0), (unsigned)ins->b, (size_t)v,
            jello_is_ptr(v) ? 1 : 0, jello_is_null(v) ? 1 : 0,
            (unsigned)f->cap_start, (unsigned)fr->rf.nregs);
  }
  if(!jello_is_ptr(v) || jello_is_null(v)) {
    uint32_t func_idx = (uint32_t)(f - m->funcs);
    uint32_t pc = fr->pc > 0 ? fr->pc - 1 : 0;
    static char buf[128];
    (void)snprintf(buf, sizeof buf, "callr callee not a function (func=%u pc=%u reg=%u val=0x%zx)",
                   (unsigned)func_idx, (unsigned)pc, (unsigned)ins->b, (size_t)v);
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, buf);
    return OP_CONTINUE;
  }
  jello_function* fn = (jello_function*)jello_as_ptr(v);
  if(!fn) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "callr callee is null");
    return OP_CONTINUE;
  }
  if(fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "callr callee not a function");
    return OP_CONTINUE;
  }
  uint32_t fi = fn->func_index;
  if(jello_is_native_builtin(fi)) {
    jello_invoke_native_builtin(ctx, ins, fi, ins->imm);
    return OP_CONTINUE;
  }
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) jello_vm_panic();
  const jello_bc_function* cf = &m->funcs[bytecode_idx];
  uint32_t first = ins->imm;
  uint32_t na = ins->c;
  if(first + na > fr->rf.nregs) jello_vm_panic();
  uint32_t caller_i = vm->call_frames_len - 1u;
  if(!vm_push_frame(vm, m, cf, f, caller_i, ins->a, first, na, fn, 1)) return OP_CONTINUE;
  return OP_CONTINUE;
}

op_result op_tailcall(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;
  call_frame* frames = ctx->frames;

  uint32_t fi = ins->imm;
  if(jello_is_native_builtin(fi)) {
    jello_invoke_native_builtin(ctx, ins, fi, ins->b);
    uint32_t caller_dst = fr->caller_dst;
    uint8_t has_caller = fr->has_caller;
    if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
    vm->exc_handlers_len = fr->exc_base;
    if(!has_caller) {
      jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
      vm_rf_release(vm, &fr->rf);
      vm->call_frames_len--;
      if(ctx->out) *ctx->out = ret;
      return OP_RETURN;
    }
    if(vm->call_frames_len < 2u) jello_vm_panic();
    call_frame* caller = &frames[vm->call_frames_len - 2u];
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    vm_store_from_boxed(m, caller->f, &caller->rf, caller_dst, ret);
    return OP_CONTINUE;
  }
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) jello_vm_panic();
  const jello_bc_function* cf = &m->funcs[bytecode_idx];
  uint32_t first = ins->b;
  uint32_t na = ins->c;
  if(first + na > fr->rf.nregs) jello_vm_panic();
  if(!vm_replace_frame(vm, m, cf, f, first, na, NULL)) return OP_CONTINUE;
  return OP_CONTINUE;
}

op_result op_tailcallr(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_value v = vm_load_val(&fr->rf, ins->b);
  if(!jello_is_ptr(v) || jello_is_null(v)) {
    uint32_t func_idx = (uint32_t)(f - m->funcs);
    uint32_t pc = fr->pc > 0 ? fr->pc - 1 : 0;
    static char buf[128];
    (void)snprintf(buf, sizeof buf, "tailcallr callee not a function (func=%u pc=%u reg=%u)",
                   (unsigned)func_idx, (unsigned)pc, (unsigned)ins->b);
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, buf);
    return OP_CONTINUE;
  }
  jello_function* fn = (jello_function*)jello_as_ptr(v);
  if(fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) {
    (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "tailcallr callee not a function");
    return OP_CONTINUE;
  }
  uint32_t fi = fn->func_index;
  if(jello_is_native_builtin(fi)) {
    jello_invoke_native_builtin(ctx, ins, fi, ins->imm);
    uint32_t caller_dst = fr->caller_dst;
    uint8_t has_caller = fr->has_caller;
    if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
    vm->exc_handlers_len = fr->exc_base;
    if(!has_caller) {
      jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
      vm_rf_release(vm, &fr->rf);
      vm->call_frames_len--;
      if(ctx->out) *ctx->out = ret;
      return OP_RETURN;
    }
    call_frame* frames = (call_frame*)vm->call_frames;
    if(vm->call_frames_len < 2u) jello_vm_panic();
    call_frame* caller = &frames[vm->call_frames_len - 2u];
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    vm_store_from_boxed(m, caller->f, &caller->rf, caller_dst, ret);
    return OP_CONTINUE;
  }
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) jello_vm_panic();
  const jello_bc_function* cf = &m->funcs[bytecode_idx];
  uint32_t first = ins->imm;
  uint32_t na = ins->c;
  if(first + na > fr->rf.nregs) jello_vm_panic();
  if(!vm_replace_frame(vm, m, cf, f, first, na, fn)) return OP_CONTINUE;
  return OP_CONTINUE;
}
