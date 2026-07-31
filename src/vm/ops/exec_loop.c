// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>
#include <jello/internal/jit_internal.h>
#include <jello/internal/jdll_internal.h>

#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#  define JELLO_LIKELY(x) __builtin_expect(!!(x), 1)
#  define JELLO_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define JELLO_LIKELY(x) (x)
#  define JELLO_UNLIKELY(x) (x)
#endif

jello_exec_status vm_exec_loop(exec_ctx* ctx) {
  if(!ctx || !ctx->vm || !ctx->m) jello_vm_panic();
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  jello_value* out = ctx->out;

  for(;;) {
  CHECK_EXC:
    if(ctx->min_call_frames && vm->call_frames_len <= ctx->min_call_frames) {
      return JELLO_EXEC_OK;
    }
    if(vm->exc_pending) {
      if(vm_exc_dispatch(vm, out)) return JELLO_EXEC_TRAP;
    }
    if(vm->call_frames_len == 0) {
      free(vm->call_frames);
      vm->call_frames = NULL;
      vm->call_frames_cap = 0;
      free(vm->exc_handlers);
      vm->exc_handlers = NULL;
      vm->exc_handlers_cap = 0;
      if(out) *out = jello_make_null();
      return JELLO_EXEC_OK;
    }

    call_frame* frames = (call_frame*)vm->call_frames;
    call_frame* fr = &frames[vm->call_frames_len - 1u];
    const jello_bc_function* f = fr->f;
#ifndef NDEBUG
    if(fr->pc >= f->ninsns) jello_vm_panic();
#endif

    if(JELLO_UNLIKELY(fr->jit_resume_hint | fr->jit_osr_hint |
                      ((fr->pc == 0) & !fr->jit_entry_done))) {
      int try_jit = 0;
      if(fr->pc == 0 && !fr->jit_entry_done) {
        fr->jit_entry_done = 1u;
        ctx->f = f;
        ctx->fr = fr;
        ctx->frames = frames;
        jello_jit_on_enter(ctx);
        if(jello_jit_func_is_compiled(vm, m, f)) try_jit = 1;
      } else {
        try_jit = 1;
      }
      if(try_jit) {
        ctx->f = f;
        ctx->fr = fr;
        ctx->frames = frames;
        int jit_rc = jello_jit_try_enter(ctx);
        if(jit_rc == 1) return JELLO_EXEC_OK;
        if(jit_rc == 2) goto CHECK_EXC;
      }
    }

    const jello_insn* ins = &f->insns[fr->pc++];
    ctx->f = f;
    ctx->fr = fr;
    ctx->frames = frames;
    reg_frame* rf = &fr->rf;

    if(JELLO_UNLIKELY(vm->profile_enabled && vm->op_counts)) vm->op_counts[ins->op]++;

#if defined(JELLOVM_REFERENCE_INTERP)
    // Reference mode: execute everything via the canonical dispatcher.
    // This is slower, but minimizes duplicated semantics and is useful for conformance testing.
    op_result r = op_dispatch(ctx, ins);
    if(r == OP_RETURN) return JELLO_EXEC_OK;
    if(r == OP_TRAP) goto CHECK_EXC;
#else
    // Hot opcodes are handled inline here to avoid paying an extra `op_dispatch()` call
    // per instruction. Everything else falls back to `op_dispatch()`.
    switch((jello_op)ins->op) {
      /* Control / misc */
      case JOP_RET: {
        uint32_t caller_dst = fr->caller_dst;
        uint8_t has_caller = fr->has_caller;
        if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
        vm->exc_handlers_len = fr->exc_base;

        if(!has_caller) {
          jello_value ret = vm_box_from_typed(vm, m, f, rf, ins->a);
          vm_rf_release(vm, rf);
          vm->call_frames_len--;
          free(vm->call_frames);
          vm->call_frames = NULL;
          vm->call_frames_len = 0;
          vm->call_frames_cap = 0;
          free(vm->const_fun_cache);
          vm->const_fun_cache = NULL;
          vm->const_fun_cache_len = 0;
          free(vm->const_bytes_cache);
          vm->const_bytes_cache = NULL;
          vm->const_bytes_cache_len = 0;
          vm_enum_nullary_cache_clear(vm);
          free(vm->exc_handlers);
          vm->exc_handlers = NULL;
          vm->exc_handlers_len = 0;
          vm->exc_handlers_cap = 0;
          if(out) *out = ret;
          return JELLO_EXEC_OK;
        }

  if(vm->call_frames_len < 2u) jello_vm_panic();
  call_frame* caller = &frames[vm->call_frames_len - 2u];

  if(fr->jdll_ret_capture && vm->jdll_call_out) {
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ins->a);
    *vm->jdll_call_out = ret;
    vm->jdll_call_out = NULL;
    vm_rf_release(vm, &fr->rf);
    vm->call_frames_len--;
    break;
  }

  /* Same-function return: typed locals always match (compiler-checked). */
        uint32_t ret_tid = f->reg_types[ins->a];
        if(JELLO_LIKELY(caller->f == f || caller->f->reg_types[caller_dst] == ret_tid)) {
          jello_type_kind k = m->types[ret_tid].kind;
          size_t sz = jello_slot_size(k);
          uint8_t* dst = (uint8_t*)vm_reg_ptr(&caller->rf, caller_dst);
          const uint8_t* src = (const uint8_t*)vm_reg_ptr(rf, ins->a);
          if(sz == 4u) *(uint32_t*)dst = *(const uint32_t*)src;
          else if(sz == 8u) *(uint64_t*)dst = *(const uint64_t*)src;
          else memmove(dst, src, sz);
          vm_rf_release(vm, rf);
          vm->call_frames_len--;
          break;
        }

        jello_value ret = vm_box_from_typed(vm, m, f, rf, ins->a);
        vm_rf_release(vm, rf);
        vm->call_frames_len--;
        vm_store_from_boxed(vm, m, caller->f, &caller->rf, caller_dst, ret);
        break;
      }
      case JOP_JMP: {
        int32_t d = (int32_t)ins->imm;
        int32_t npc = (int32_t)fr->pc + d;
        if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
        if(d < 0 && jello_vm_fuel_charge(vm) != 0) goto CHECK_EXC;
        fr->pc = (uint32_t)npc;
        if(d < 0) {
          ctx->f = f;
          ctx->fr = fr;
          ctx->frames = frames;
          jello_jit_on_backedge(ctx);
        }
        break;
      }
      case JOP_JMP_IF: {
        uint32_t cond = vm_load_u32(rf, ins->a);
        if(cond != 0) {
          int32_t d = (int32_t)ins->imm;
          int32_t npc = (int32_t)fr->pc + d;
          if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
          fr->pc = (uint32_t)npc;
        }
        break;
      }
      case JOP_MOV: {
        uint32_t a = ins->a, b = ins->b;
        jello_type_kind k = m->types[f->reg_types[a]].kind;
        size_t sz = jello_slot_size(k);
        uint8_t* dst = (uint8_t*)vm_reg_ptr(rf, a);
        const uint8_t* src = (const uint8_t*)vm_reg_ptr(rf, b);
        if(sz == 4u) *(uint32_t*)dst = *(const uint32_t*)src;
        else if(sz == 8u) *(uint64_t*)dst = *(const uint64_t*)src;
        else memmove(dst, src, sz);
        break;
      }
      case JOP_ASSERT: {
        op_result r = op_assert(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      /* Calls / closures */
      case JOP_CALL: {
        if(jello_vm_fuel_charge(vm) != 0) goto CHECK_EXC;
        uint32_t fi = ins->imm;
        if(jello_is_native_builtin(fi)) {
          jello_invoke_native_builtin(ctx, ins, fi, ins->b);
        } else {
          uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
          if(bytecode_idx >= m->nfuncs) jello_vm_panic();
          const jello_bc_function* cf = &m->funcs[bytecode_idx];
          uint32_t first = ins->b;
          uint32_t na = ins->c;
          if(first + na > fr->rf.nregs) jello_vm_panic();
          /* Numeric self-call: shared push (interp + JIT). */
          if(JELLO_LIKELY(cf == f && !fr->has_pointer_or_dynamic && na <= 16u &&
                          (!vm->call_frames_max || vm->call_frames_len < vm->call_frames_max))) {
            vm_call_frames_grow_if_full(vm, &fr);
            frames = (call_frame*)vm->call_frames;
            (void)vm_push_self_numeric(vm, m, fr, first, na, ins->a, NULL, fr->jit_entry_done);
          } else {
            uint32_t caller_i = vm->call_frames_len - 1u;
            if(!vm_push_frame(vm, m, cf, f, caller_i, ins->a, first, na, NULL, 1)) goto CHECK_EXC;
          }
        }
        break;
      }
      case JOP_CALLR: {
        if(jello_vm_fuel_charge(vm) != 0) goto CHECK_EXC;
        jello_value v = vm_load_val(rf, ins->b);
        if(!jello_is_ptr(v) || jello_is_null(v)) {
          (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "callr callee not a function");
          goto CHECK_EXC;
        }
        jello_function* fn = (jello_function*)jello_as_ptr(v);
        if(!fn || fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) {
          (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "callr callee not a function");
          goto CHECK_EXC;
        }
        uint32_t fi = fn->func_index;
        if(jello_is_jdll_prim(fi)) {
          if(!jello_jdll_invoke_prim(ctx, ins, fn, ins->imm)) goto CHECK_EXC;
        } else if(jello_is_native_builtin(fi)) {
          jello_invoke_native_builtin(ctx, ins, fi, ins->imm);
        } else {
          uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
          if(bytecode_idx >= m->nfuncs) jello_vm_panic();
          const jello_bc_function* cf = &m->funcs[bytecode_idx];
          uint32_t first = ins->imm;
          uint32_t na = ins->c;
          if(first + na > fr->rf.nregs) jello_vm_panic();
          uint32_t caller_i = vm->call_frames_len - 1u;
          if(!vm_push_frame(vm, m, cf, f, caller_i, ins->a, first, na, fn, 1)) goto CHECK_EXC;
        }
        break;
      }
      case JOP_TAILCALL: {
        uint32_t fi = ins->imm;
        if(jello_is_native_builtin(fi)) {
          jello_invoke_native_builtin(ctx, ins, fi, ins->b);
          uint32_t caller_dst = fr->caller_dst;
          uint8_t has_caller = fr->has_caller;
          if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
          vm->exc_handlers_len = fr->exc_base;
          if(!has_caller) {
            jello_value ret = vm_box_from_typed(vm, m, f, rf, ins->a);
            vm_rf_release(vm, rf);
            vm->call_frames_len--;
            if(out) *out = ret;
            return JELLO_EXEC_OK;
          }
          if(vm->call_frames_len < 2u) jello_vm_panic();
          call_frame* caller = &frames[vm->call_frames_len - 2u];
          jello_value ret = vm_box_from_typed(vm, m, f, rf, ins->a);
          vm_rf_release(vm, rf);
          vm->call_frames_len--;
          vm_store_from_boxed(vm, m, caller->f, &caller->rf, caller_dst, ret);
        } else {
          uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
          if(bytecode_idx >= m->nfuncs) jello_vm_panic();
          const jello_bc_function* cf = &m->funcs[bytecode_idx];
          uint32_t first = ins->b;
          uint32_t na = ins->c;
          if(first + na > fr->rf.nregs) jello_vm_panic();
          if(!vm_replace_frame(vm, m, cf, f, first, na, NULL)) goto CHECK_EXC;
        }
        break;
      }
      case JOP_TAILCALLR: {
        jello_value v = vm_load_val(rf, ins->b);
        if(!jello_is_ptr(v) || jello_is_null(v)) {
          (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "tailcallr callee not a function");
          goto CHECK_EXC;
        }
        jello_function* fn = (jello_function*)jello_as_ptr(v);
        if(!fn || fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) {
          (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "tailcallr callee not a function");
          goto CHECK_EXC;
        }
        uint32_t fi = fn->func_index;
        if(jello_is_native_builtin(fi)) {
          jello_invoke_native_builtin(ctx, ins, fi, ins->imm);
          uint32_t caller_dst = fr->caller_dst;
          uint8_t has_caller = fr->has_caller;
          if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
          vm->exc_handlers_len = fr->exc_base;
          if(!has_caller) {
            jello_value ret = vm_box_from_typed(vm, m, f, rf, ins->a);
            vm_rf_release(vm, rf);
            vm->call_frames_len--;
            if(out) *out = ret;
            return JELLO_EXEC_OK;
          }
          if(vm->call_frames_len < 2u) jello_vm_panic();
          call_frame* caller = &frames[vm->call_frames_len - 2u];
          jello_value ret = vm_box_from_typed(vm, m, f, rf, ins->a);
          vm_rf_release(vm, rf);
          vm->call_frames_len--;
          vm_store_from_boxed(vm, m, caller->f, &caller->rf, caller_dst, ret);
        } else {
          uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
          if(bytecode_idx >= m->nfuncs) jello_vm_panic();
          const jello_bc_function* cf = &m->funcs[bytecode_idx];
          uint32_t first = ins->imm;
          uint32_t na = ins->c;
          if(first + na > fr->rf.nregs) jello_vm_panic();
          if(!vm_replace_frame(vm, m, cf, f, first, na, fn)) goto CHECK_EXC;
        }
        break;
      }
      case JOP_CONST_FUN: {
        if(ins->imm >= vm->const_fun_cache_len) jello_vm_panic();
        jello_function** cache = (jello_function**)vm->const_fun_cache;
        jello_function* fn = cache[ins->imm];
        if(!fn) {
          uint32_t type_id = f->reg_types[ins->a];
          fn = jello_function_new(vm, type_id, ins->imm);
          cache[ins->imm] = fn;
        }
        vm_store_ptr(rf, ins->a, fn);
        break;
      }
      /* Typed constants */
      case JOP_CONST_I32: {
        vm_store_u32(rf, ins->a, ins->imm);
        break;
      }
      case JOP_CONST_I8_IMM: {
        int8_t v = (int8_t)(uint8_t)ins->c;
        vm_store_u32(rf, ins->a, (uint32_t)(int32_t)v);
        break;
      }
      case JOP_CONST_BOOL: {
        vm_store_u32(rf, ins->a, (uint32_t)(ins->c & 1u));
        break;
      }
      case JOP_CONST_NULL: {
        vm_store_val(rf, ins->a, jello_make_null());
        break;
      }
      case JOP_CONST_F64: {
        vm_store_f64(rf, ins->a, m->const_f64[ins->imm]);
        break;
      }
      case JOP_CONST_BYTES: {
        op_result r = op_const_bytes(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      /* Bytes helpers */
      case JOP_BYTES_CONCAT2: {
        op_result r = op_bytes_concat2(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_CONCAT_MANY: {
        op_result r = op_bytes_concat_many(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_BITAND2: {
        op_result r = op_bytes_bitand2(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_BITOR2: {
        op_result r = op_bytes_bitor2(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_BITXOR2: {
        op_result r = op_bytes_bitxor2(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      /* I32 arithmetic */
      case JOP_ADD_I32: {
        jello_type_kind k = m->types[f->reg_types[ins->a]].kind;
        uint32_t a = vm_load_u32(rf, ins->b);
        uint32_t b = vm_load_u32(rf, ins->c);
        uint32_t v = a + b;
        if(k == JELLO_T_I32) {
          vm_store_u32(rf, ins->a, v);
        } else {
          vm_store_u32_masked(rf, ins->a, v, k);
        }
        break;
      }
      case JOP_SUB_I32: {
        jello_type_kind k = m->types[f->reg_types[ins->a]].kind;
        uint32_t a = vm_load_u32(rf, ins->b);
        uint32_t b = vm_load_u32(rf, ins->c);
        uint32_t v = a - b;
        if(k == JELLO_T_I32) {
          vm_store_u32(rf, ins->a, v);
        } else {
          vm_store_u32_masked(rf, ins->a, v, k);
        }
        break;
      }
      case JOP_MUL_I32: {
        jello_type_kind k = m->types[f->reg_types[ins->a]].kind;
        uint32_t a = vm_load_u32(rf, ins->b);
        uint32_t b = vm_load_u32(rf, ins->c);
        uint32_t v = a * b;
        if(k == JELLO_T_I32) {
          vm_store_u32(rf, ins->a, v);
        } else {
          vm_store_u32_masked(rf, ins->a, v, k);
        }
        break;
      }
      case JOP_ADD_I32_IMM: {
        jello_type_kind k = m->types[f->reg_types[ins->a]].kind;
        uint32_t a = vm_load_u32(rf, ins->b);
        int32_t imm = (int32_t)(int8_t)ins->c;
        uint32_t v = (uint32_t)((int32_t)a + imm);
        if(k == JELLO_T_I32) {
          vm_store_u32(rf, ins->a, v);
        } else {
          vm_store_u32_masked(rf, ins->a, v, k);
        }
        break;
      }
      case JOP_SUB_I32_IMM: {
        jello_type_kind k = m->types[f->reg_types[ins->a]].kind;
        uint32_t a = vm_load_u32(rf, ins->b);
        int32_t imm = (int32_t)(int8_t)ins->c;
        uint32_t v = (uint32_t)((int32_t)a - imm);
        if(k == JELLO_T_I32) {
          vm_store_u32(rf, ins->a, v);
        } else {
          vm_store_u32_masked(rf, ins->a, v, k);
        }
        break;
      }
      case JOP_MUL_I32_IMM: {
        jello_type_kind k = m->types[f->reg_types[ins->a]].kind;
        uint32_t a = vm_load_u32(rf, ins->b);
        int32_t imm = (int32_t)(int8_t)ins->c;
        uint32_t v = a * (uint32_t)imm;
        if(k == JELLO_T_I32) {
          vm_store_u32(rf, ins->a, v);
        } else {
          vm_store_u32_masked(rf, ins->a, v, k);
        }
        break;
      }
      /* Float arithmetic */
      case JOP_ADD_F64: {
        double a = vm_load_f64(rf, ins->b);
        double b = vm_load_f64(rf, ins->c);
        vm_store_f64(rf, ins->a, a + b);
        break;
      }
      case JOP_SUB_F64: {
        double a = vm_load_f64(rf, ins->b);
        double b = vm_load_f64(rf, ins->c);
        vm_store_f64(rf, ins->a, a - b);
        break;
      }
      case JOP_MUL_F64: {
        double a = vm_load_f64(rf, ins->b);
        double b = vm_load_f64(rf, ins->c);
        vm_store_f64(rf, ins->a, a * b);
        break;
      }
      case JOP_DIV_F64: {
        double a = vm_load_f64(rf, ins->b);
        double b = vm_load_f64(rf, ins->c);
        vm_store_f64(rf, ins->a, a / b);
        break;
      }
      /* Unary */
      case JOP_NOT_BOOL: {
        uint32_t x = vm_load_u32(rf, ins->b);
        vm_store_u32(rf, ins->a, (uint32_t)(x == 0));
        break;
      }
      /* Comparisons */
      case JOP_EQ_I32: {
        int32_t a = (int32_t)vm_load_u32(rf, ins->b);
        int32_t b = (int32_t)vm_load_u32(rf, ins->c);
        vm_store_u32(rf, ins->a, (uint32_t)((a == b) ? 1 : 0));
        break;
      }
      case JOP_LT_I32: {
        int32_t a = (int32_t)vm_load_u32(rf, ins->b);
        int32_t b = (int32_t)vm_load_u32(rf, ins->c);
        vm_store_u32(rf, ins->a, (uint32_t)((a < b) ? 1 : 0));
        break;
      }
      case JOP_EQ_I32_IMM: {
        int32_t a = (int32_t)vm_load_u32(rf, ins->b);
        int32_t imm = (int32_t)(int8_t)ins->c;
        vm_store_u32(rf, ins->a, (uint32_t)((a == imm) ? 1 : 0));
        break;
      }
      case JOP_LT_I32_IMM: {
        int32_t a = (int32_t)vm_load_u32(rf, ins->b);
        int32_t imm = (int32_t)(int8_t)ins->c;
        vm_store_u32(rf, ins->a, (uint32_t)((a < imm) ? 1 : 0));
        break;
      }
      case JOP_EQ_F64: {
        double a = vm_load_f64(rf, ins->b);
        double b = vm_load_f64(rf, ins->c);
        vm_store_u32(rf, ins->a, (uint32_t)((a == b) ? 1 : 0));
        break;
      }
      case JOP_LT_F64: {
        double a = vm_load_f64(rf, ins->b);
        double b = vm_load_f64(rf, ins->c);
        vm_store_u32(rf, ins->a, (uint32_t)((a < b) ? 1 : 0));
        break;
      }
      /* Containers */
      case JOP_ARRAY_NEW: {
        op_result r = op_array_new(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_ARRAY_LEN: {
        jello_array* a = (jello_array*)vm_load_ptr(rf, ins->b);
        if(!a) {
          (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_len on null");
          break;
        }
        vm_store_u32(rf, ins->a, a->length);
        break;
      }
      case JOP_ARRAY_GET: {
        jello_array* a = (jello_array*)vm_load_ptr(rf, ins->b);
        if(!a) {
          (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_get on null");
          break;
        }
        uint32_t idx = vm_load_u32(rf, ins->c);
        if(idx >= a->length) {
          (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "array_get index out of bounds");
          break;
        }
        {
          jello_type_id arr_tid = f->reg_types[ins->b];
          int f64_fast = vm_reg_kind(m, f, ins->a) == JELLO_T_F64 && arr_tid < m->ntypes &&
                         m->types[arr_tid].kind == JELLO_T_ARRAY;
          if(f64_fast) {
            uint32_t elem_tid = m->types[arr_tid].as.unary.elem;
            f64_fast = elem_tid < m->ntypes && m->types[elem_tid].kind == JELLO_T_F64;
          }
          if(f64_fast) {
            jello_value v = a->data[idx];
            if(jello_is_box_f64(v)) {
              vm_store_f64(rf, ins->a, jello_as_box_f64(v));
              break;
            }
          }
        }
        vm_store_from_boxed(vm, m, f, rf, ins->a, a->data[idx]);
        break;
      }
      case JOP_ARRAY_SET: {
        jello_array* a = (jello_array*)vm_load_ptr(rf, ins->b);
        if(!a) {
          (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "array_set on null");
          break;
        }
        uint32_t idx = vm_load_u32(rf, ins->c);
        if(idx >= a->length) {
          (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "array_set index out of bounds");
          break;
        }
        {
          jello_type_id arr_tid = f->reg_types[ins->b];
          int f64_fast = vm_reg_kind(m, f, ins->a) == JELLO_T_F64 && arr_tid < m->ntypes &&
                         m->types[arr_tid].kind == JELLO_T_ARRAY;
          if(f64_fast) {
            uint32_t elem_tid = m->types[arr_tid].as.unary.elem;
            f64_fast = elem_tid < m->ntypes && m->types[elem_tid].kind == JELLO_T_F64;
          }
          if(f64_fast) {
            jello_value* slot = &a->data[idx];
            if(jello_is_box_f64(*slot)) {
              ((jello_box_f64*)jello_as_ptr(*slot))->value = vm_load_f64(rf, ins->a);
              break;
            }
          }
        }
        if(!vm_store_num_inplace(vm, m, f, rf, ins->a, &a->data[idx])) {
          jello_value v = vm_box_from_typed(vm, m, f, rf, ins->a);
          if(vm_reg_kind(m, f, ins->a) == JELLO_T_DYNAMIC) v = vm_clone_numbox(vm, v);
          a->data[idx] = v;
        }
        break;
      }
      case JOP_BYTES_NEW: {
        op_result r = op_bytes_new(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_LEN: {
        op_result r = op_bytes_len(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_GET_U8: {
        op_result r = op_bytes_get_u8(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_SET_U8: {
        op_result r = op_bytes_set_u8(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      case JOP_OBJ_NEW: {
        uint32_t type_id = f->reg_types[ins->a];
        jello_object* o = jello_object_new(vm, type_id);
        vm_store_ptr(rf, ins->a, o);
        break;
      }
      case JOP_OBJ_GET_ATOM: {
        if(!vm_obj_get_atom_typed(vm, m, f, rf, ins->a, ins->b, ins->imm, ins->c)) goto CHECK_EXC;
        break;
      }
      case JOP_OBJ_SET_ATOM: {
        if(!vm_obj_set_atom_typed(vm, m, f, rf, ins->a, ins->b, ins->imm, ins->c)) goto CHECK_EXC;
        break;
      }
      case JOP_BYTES_EQ: {
        op_result r = op_bytes_eq(ctx, ins);
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
      default: {
        op_result r = op_dispatch(ctx, ins);
        if(r == OP_RETURN) return JELLO_EXEC_OK;
        if(r == OP_TRAP) goto CHECK_EXC;
        break;
      }
    }
#endif
  }
}

