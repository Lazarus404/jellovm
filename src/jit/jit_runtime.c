// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>

#include <jello/internal/ops_decl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jello_jit_layout g_jit_layout;

__attribute__((constructor)) static void jello_jit_layout_init(void) {
  g_jit_layout.exec_ctx_fr = (uint32_t)offsetof(exec_ctx, fr);
  g_jit_layout.call_frame_rf_mem = (uint32_t)offsetof(call_frame, rf) + (uint32_t)offsetof(reg_frame, mem);
  g_jit_layout.exec_ctx_jit_resume_entry = (uint32_t)offsetof(exec_ctx, jit_resume_entry);
  g_jit_layout.exec_ctx_jit_self_resume = (uint32_t)offsetof(exec_ctx, jit_self_resume);
  g_jit_layout.exec_ctx_jit_call_entry = (uint32_t)offsetof(exec_ctx, jit_call_entry);
  g_jit_layout.vm_fuel_limit = (uint32_t)offsetof(jello_vm, fuel_limit);
  g_jit_layout.vm_fuel_remaining = (uint32_t)offsetof(jello_vm, fuel_remaining);
}

const jello_jit_layout* jello_jit_runtime_layout(void) {
  return &g_jit_layout;
}

void* jello_jit_runtime_frame_mem(exec_ctx* ctx) {
  if(!ctx || !ctx->fr) return NULL;
  return ctx->fr->rf.mem;
}

uint32_t jello_jit_runtime_load_u32(exec_ctx* ctx, uint32_t reg) {
  if(!ctx || !ctx->fr) return 0u;
  return vm_load_u32(&ctx->fr->rf, reg);
}

/* Run compiled callee in-place; return 1 if caller should CONTINUE in native. */
static int jit_run_compiled_callee(exec_ctx* ctx, uint32_t caller_depth, uint32_t resume_pc) {
  jello_vm* vm = ctx->vm;
  jello_jit_state* st = (jello_jit_state*)vm->jit_state;
  if(!st || !jello_jit_config_run_enabled()) return 0;
  if(jello_jit_state_nest(st) >= 256u) return 0; /* C-stack guard */
  if(vm->call_frames_len <= caller_depth) return 0;

  call_frame* callee_fr = &((call_frame*)vm->call_frames)[caller_depth];
  if(!callee_fr->f || callee_fr->pc != 0u) return 0;

  uint32_t cfi = (uint32_t)(callee_fr->f - ctx->m->funcs);
  jello_jit_code* ccode = jello_jit_cache_lookup(st, ctx->m, cfi);
  if(!ccode) return 0;

  ctx->frames = (call_frame*)vm->call_frames;
  ctx->fr = callee_fr;
  ctx->f = callee_fr->f;
  callee_fr->jit_entry_done = 1u;
  ctx->jit_resume_entry = NULL;
  jello_jit_state_nest_inc(st);
  jello_jit_run_result rr = jello_jit_runtime_run(ctx, ccode);
  jello_jit_state_nest_dec(st);
  jello_jit_deopt_sync_ctx(ctx);

  if(vm->exc_pending) return 0;
  if(rr == JELLO_JIT_RUN_RETURNED && vm->call_frames_len == 0u) return 0;

  /* Callee returned: back on caller with resume_pc. */
  if(vm->call_frames_len == caller_depth && ctx->fr && ctx->fr->pc == resume_pc) return 1;
  return 0;
}

/* CALL: depth increases → prefer nested JIT run of compiled callee; else YIELD.
 * Non-call slow: same depth and pc==bc_pc+1 → CONTINUE + refresh base.
 * Never fall through to native code that assumes CALL completed in-place. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_slow_op(exec_ctx* ctx, uint32_t bc_pc) {
  if(!ctx || !ctx->fr || !ctx->f) return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  uint32_t depth_before = vm->call_frames_len;
  const jello_insn* ins = &ctx->f->insns[bc_pc];
  jello_op op = (jello_op)ins->op;

  /* Hot CONST_FUN: flyweight cache load without full op_dispatch. */
  if(op == JOP_CONST_FUN) {
    if(ins->imm >= vm->const_fun_cache_len) {
      (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "const_fun out of range");
      return JELLO_JIT_EXIT_TRAP;
    }
    jello_function** cache = (jello_function**)vm->const_fun_cache;
    jello_function* fn = cache[ins->imm];
    if(!fn) {
      uint32_t type_id = ctx->f->reg_types[ins->a];
      fn = jello_function_new(vm, type_id, ins->imm);
      cache[ins->imm] = fn;
    }
    vm_store_ptr(&ctx->fr->rf, ins->a, fn);
    ctx->fr->pc = bc_pc + 1u;
    return JELLO_JIT_EXIT_CONTINUE;
  }

  ctx->fr->pc = bc_pc + 1u;
  op_result r = op_dispatch(ctx, ins);
  jello_jit_deopt_sync_ctx(ctx);
  if(jello_jit_config_dump_enabled()) {
    fprintf(stderr, "JELLO_JIT_SLOW: op=%u bc_pc=%u depth %u->%u r=%d pc=%u\n",
            (unsigned)op, (unsigned)bc_pc, (unsigned)depth_before,
            (unsigned)vm->call_frames_len, (int)r,
            ctx->fr ? (unsigned)ctx->fr->pc : 0u);
  }

  if(r == OP_TRAP || vm->exc_pending) {
    if(ctx->fr) ctx->fr->pc = bc_pc;
    return JELLO_JIT_EXIT_TRAP;
  }
  if(r == OP_RETURN) return JELLO_JIT_EXIT_YIELD;

  if(vm->call_frames_len > depth_before) {
    uint32_t resume_pc = bc_pc + 1u;
    if(depth_before > 0u) {
      call_frame* caller = &((call_frame*)vm->call_frames)[depth_before - 1u];
      caller->pc = resume_pc;
    }
    if(jit_run_compiled_callee(ctx, depth_before, resume_pc)) {
      return JELLO_JIT_EXIT_CONTINUE;
    }
    if(vm->exc_pending) return JELLO_JIT_EXIT_TRAP;
    if(vm->call_frames_len == 0u) return JELLO_JIT_EXIT_RETURNED;
    if(depth_before > 0u && vm->call_frames_len > depth_before) {
      call_frame* caller = &((call_frame*)vm->call_frames)[depth_before - 1u];
      caller->jit_resume_hint = 1u;
    }
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_YIELD;
  }

  /* Tail call: same depth, new function at pc==0 — run nested if compiled. */
  if(vm->call_frames_len == depth_before && ctx->fr && ctx->fr->pc == 0u &&
     (op == JOP_TAILCALL || op == JOP_TAILCALLR)) {
    jello_jit_state* st = (jello_jit_state*)vm->jit_state;
    if(st && jello_jit_config_run_enabled() && jello_jit_state_nest(st) < 256u) {
      uint32_t cfi = (uint32_t)(ctx->f - ctx->m->funcs);
      jello_jit_code* ccode = jello_jit_cache_lookup(st, ctx->m, cfi);
      if(ccode) {
        ctx->fr->jit_entry_done = 1u;
        ctx->jit_resume_entry = NULL;
        jello_jit_state_nest_inc(st);
        jello_jit_run_result rr = jello_jit_runtime_run(ctx, ccode);
        jello_jit_state_nest_dec(st);
        jello_jit_deopt_sync_ctx(ctx);
        if(vm->exc_pending) return JELLO_JIT_EXIT_TRAP;
        if(rr == JELLO_JIT_RUN_RETURNED && vm->call_frames_len == 0u)
          return JELLO_JIT_EXIT_RETURNED;
        return JELLO_JIT_EXIT_YIELD;
      }
    }
    return JELLO_JIT_EXIT_YIELD;
  }

  if(vm->call_frames_len < depth_before) return JELLO_JIT_EXIT_YIELD;
  if(ctx->fr && ctx->fr->pc != bc_pc + 1u) return JELLO_JIT_EXIT_YIELD;

  return JELLO_JIT_EXIT_CONTINUE;
}

jello_jit_exit jello_jit_runtime_ret_status(exec_ctx* ctx, uint32_t ret_reg) {
  if(!ctx || !ctx->f) return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {0};
  ins.op = JOP_RET;
  ins.a = (uint8_t)ret_reg;
  op_result r = op_ret(ctx, &ins);
  jello_jit_deopt_sync_ctx(ctx);
  if(jello_jit_config_dump_enabled()) {
    fprintf(stderr, "JELLO_JIT: ret_status reg=%u r=%d\n", ret_reg, (int)r);
  }
  if(r == OP_TRAP || (ctx->vm && ctx->vm->exc_pending)) return JELLO_JIT_EXIT_TRAP;
  if(r == OP_RETURN) {
    if(!ctx->vm || ctx->vm->call_frames_len == 0u) return JELLO_JIT_EXIT_RETURNED;
    return JELLO_JIT_EXIT_YIELD;
  }
  if(r == OP_CONTINUE) return JELLO_JIT_EXIT_YIELD;
  return JELLO_JIT_EXIT_TRAP;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_call_self(
    exec_ctx* ctx,
    uint32_t first_arg,
    uint32_t nargs,
    uint32_t caller_dst,
    uint32_t resume_pc,
    void* return_addr
) {
  if(!ctx || !ctx->vm || !ctx->m || !ctx->f || !ctx->fr || !return_addr)
    return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;
  if(nargs > 16u || first_arg + nargs > fr->rf.nregs) return JELLO_JIT_EXIT_TRAP;
  if(vm->call_frames_max && vm->call_frames_len >= vm->call_frames_max) {
    vm->trap_code = JELLO_TRAP_STACK_OVERFLOW;
    vm->trap_msg = "stack overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_STACK_OVERFLOW);
    return JELLO_JIT_EXIT_TRAP;
  }
  /* Charge one fuel unit per call (interp charges the CALL insn). Recursive
   * functions often have no backedge FUEL_CHECK. */
  if(vm->fuel_limit) {
    if(vm->fuel_remaining == 0) {
      (void)jello_vm_trap(vm, JELLO_TRAP_FUEL, "instruction limit exceeded");
      return JELLO_JIT_EXIT_TRAP;
    }
    vm->fuel_remaining--;
  }
  fr->pc = resume_pc;
  if(fr->has_pointer_or_dynamic) {
    /* Rare for CALL_SELF (self-rec IR prefers numeric); fall back carefully. */
    vm_call_frames_grow_if_full(vm, &fr);
    ctx->frames = (call_frame*)vm->call_frames;
    ctx->fr = fr;
    uint32_t total = fr->rf.total;
    uint8_t* mem = vm_frame_stack_bump(vm, total);
    call_frame* frames = (call_frame*)vm->call_frames;
    call_frame* nfr = &frames[vm->call_frames_len++];
    nfr->f = f;
    nfr->pc = 0;
    nfr->jit_entry_done = 1u;
    nfr->jit_resume_hint = 0;
    nfr->jit_osr_hint = 0;
    nfr->jit_return_addr = return_addr;
    nfr->caller_dst = caller_dst;
    nfr->exc_base = vm->exc_handlers_len;
    nfr->has_caller = 1u;
    nfr->jdll_ret_capture = 0;
    nfr->has_pointer_or_dynamic = 1u;
    nfr->rf.nregs = fr->rf.nregs;
    nfr->rf.off = fr->rf.off;
    nfr->rf.total = total;
    nfr->rf.off_shared = 1u;
    nfr->rf.mem = mem;
    if(mem && total) memset(mem, 0, (size_t)total);
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[f->reg_types[i]].kind;
      size_t sz = jello_slot_size(k);
      if(sz > 8u) return JELLO_JIT_EXIT_TRAP;
      memcpy(mem + nfr->rf.off[i], fr->rf.mem + fr->rf.off[first_arg + i], sz);
    }
    ctx->fr = nfr;
    ctx->f = f;
    return JELLO_JIT_EXIT_CONTINUE;
  }
  vm_call_frames_grow_if_full(vm, &fr);
  call_frame* nfr =
      vm_push_self_numeric(vm, m, fr, first_arg, nargs, caller_dst, return_addr, 1u);
  ctx->frames = (call_frame*)vm->call_frames;
  ctx->fr = nfr;
  ctx->f = f;
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_call_direct(
    exec_ctx* ctx,
    uint32_t first_arg,
    uint32_t nargs,
    uint32_t caller_dst,
    uint32_t resume_pc,
    void* return_addr,
    uint32_t bytecode_fi,
    uint32_t callee_reg
) {
  if(!ctx || !ctx->vm || !ctx->m || !ctx->f || !ctx->fr || !return_addr)
    return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  if(bytecode_fi >= m->nfuncs) return JELLO_JIT_EXIT_TRAP;
  const jello_bc_function* callee_f = &m->funcs[bytecode_fi];
  call_frame* frames = (call_frame*)vm->call_frames;
  call_frame* fr = ctx->fr;
  if(nargs > 16u || first_arg + nargs > fr->rf.nregs) return JELLO_JIT_EXIT_TRAP;
  const jello_function* funobj = NULL;
  if(callee_reg != 0xFFFFFFFFu) {
    if(callee_reg >= fr->rf.nregs) return JELLO_JIT_EXIT_TRAP;
    jello_value v = vm_load_val(&fr->rf, callee_reg);
    if(!jello_is_ptr(v) || jello_is_null(v)) return JELLO_JIT_EXIT_TRAP;
    funobj = (const jello_function*)jello_as_ptr(v);
    if(!funobj || funobj->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) return JELLO_JIT_EXIT_TRAP;
    uint32_t fi = funobj->func_index;
    if(jello_is_native_builtin(fi) || jello_is_jdll_prim(fi)) return JELLO_JIT_EXIT_TRAP;
    if(fi - JELLO_NATIVE_BUILTIN_COUNT != bytecode_fi) return JELLO_JIT_EXIT_TRAP;
    if(jello_bound_this_is_set(funobj->bound_this)) return JELLO_JIT_EXIT_TRAP;
  }
  if(vm->call_frames_max && vm->call_frames_len >= vm->call_frames_max) {
    vm->trap_code = JELLO_TRAP_STACK_OVERFLOW;
    vm->trap_msg = "stack overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_STACK_OVERFLOW);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm_call_frames_grow_if_full(vm, &fr);
  frames = (call_frame*)vm->call_frames;
  ctx->frames = frames;
  ctx->fr = fr;
  if(vm->fuel_limit) {
    if(vm->fuel_remaining == 0) {
      (void)jello_vm_trap(vm, JELLO_TRAP_FUEL, "instruction limit exceeded");
      return JELLO_JIT_EXIT_TRAP;
    }
    vm->fuel_remaining--;
  }

  fr->pc = resume_pc;
  const frame_layout* cfl = vm_get_frame_layout(vm, m, callee_f);
  if(!cfl) return JELLO_JIT_EXIT_TRAP;
  uint32_t total = cfl->total;
  uint8_t* mem = vm_frame_stack_bump(vm, total);
  call_frame* nfr = &frames[vm->call_frames_len++];
  nfr->f = callee_f;
  nfr->pc = 0;
  nfr->jit_entry_done = 0; /* set below only when entering native body */
  nfr->jit_resume_hint = 0;
  nfr->jit_osr_hint = 0;
  nfr->jit_return_addr = return_addr;
  nfr->caller_dst = caller_dst;
  nfr->exc_base = vm->exc_handlers_len;
  nfr->has_caller = 1u;
  nfr->jdll_ret_capture = 0;
  nfr->has_pointer_or_dynamic = cfl->has_pointer_or_dynamic;
  nfr->rf.nregs = cfl->nregs;
  nfr->rf.off = cfl->off;
  nfr->rf.total = total;
  nfr->rf.off_shared = 1u;
  nfr->rf.mem = mem;
  if(cfl->has_pointer_or_dynamic && mem && total) memset(mem, 0, (size_t)total);
  for(uint32_t i = 0; i < nargs; i++) {
    jello_type_kind k = m->types[callee_f->reg_types[i]].kind;
    size_t sz = jello_slot_size(k);
    if(sz > 8u) return JELLO_JIT_EXIT_TRAP;
    memcpy(mem + nfr->rf.off[i], fr->rf.mem + fr->rf.off[first_arg + i], sz);
  }
  if(funobj && funobj->ncaps) {
    uint32_t cap_start =
        (m->features & (uint32_t)JELLO_BC_FEAT_CAP_START) && callee_f->cap_start < nfr->rf.nregs
            ? callee_f->cap_start
            : nfr->rf.nregs - funobj->ncaps;
    if(cap_start + funobj->ncaps > nfr->rf.nregs || cap_start < nargs) return JELLO_JIT_EXIT_TRAP;
    if(funobj->caps_are_raw) {
      const uint8_t* raw = (const uint8_t*)&funobj->caps[0];
      uint32_t off = 0;
      for(uint32_t i = 0; i < funobj->ncaps; i++) {
        jello_type_kind k = m->types[callee_f->reg_types[cap_start + i]].kind;
        size_t sz = jello_slot_size(k);
        memcpy(mem + nfr->rf.off[cap_start + i], raw + off, sz);
        off += (uint32_t)sz;
      }
    } else {
      for(uint32_t i = 0; i < funobj->ncaps; i++) {
        vm_store_from_boxed(vm, m, callee_f, &nfr->rf, cap_start + i, funobj->caps[i]);
      }
    }
  }
  ctx->frames = frames;
  ctx->fr = nfr;
  ctx->f = callee_f;
  ctx->jit_call_entry = NULL;

  jello_jit_state* st = (jello_jit_state*)vm->jit_state;
  if(st && jello_jit_config_run_enabled()) {
    jello_jit_code* code = jello_jit_cache_lookup(st, m, bytecode_fi);
    if(code && code->body) {
      nfr->jit_entry_done = 1u;
      /* Jump past callee prologue — share the caller's C frame. */
      ctx->jit_call_entry = code->body;
      return JELLO_JIT_EXIT_CONTINUE;
    }
  }
  /* Callee not compiled: interpret + allow on_enter to hot-compile it. */
  if(vm->call_frames_len >= 2u) {
    call_frame* caller = &frames[vm->call_frames_len - 2u];
    caller->jit_resume_hint = 1u;
  }
  return JELLO_JIT_EXIT_YIELD;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_ret_self(exec_ctx* ctx, uint32_t ret_reg) {
  if(!ctx || !ctx->vm || !ctx->m || !ctx->f || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  const jello_bc_function* f = ctx->f;
  call_frame* frames = (call_frame*)vm->call_frames;
  call_frame* fr = ctx->fr;
  if(fr->exc_base > vm->exc_handlers_len) jello_vm_panic();
  vm->exc_handlers_len = fr->exc_base;

  if(!fr->has_caller) {
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ret_reg);
    void* ra = fr->jit_return_addr;
    (void)ra;
    vm_rf_release(vm, &fr->rf);
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
    if(ctx->out) *ctx->out = ret;
    ctx->fr = NULL;
    ctx->frames = NULL;
    ctx->jit_self_resume = NULL;
    return JELLO_JIT_EXIT_RETURNED;
  }

  if(vm->call_frames_len < 2u) jello_vm_panic();
  call_frame* caller = &frames[vm->call_frames_len - 2u];
  void* resume = fr->jit_return_addr;
  uint32_t caller_dst = fr->caller_dst;
  uint32_t ret_tid = f->reg_types[ret_reg];
  if(caller->f == f || caller->f->reg_types[caller_dst] == ret_tid) {
    jello_type_kind k = m->types[ret_tid].kind;
    size_t sz = jello_slot_size(k);
    uint8_t* dst = (uint8_t*)vm_reg_ptr(&caller->rf, caller_dst);
    const uint8_t* src = (const uint8_t*)vm_reg_ptr(&fr->rf, ret_reg);
    if(sz == 4u) *(uint32_t*)dst = *(const uint32_t*)src;
    else if(sz == 8u) *(uint64_t*)dst = *(const uint64_t*)src;
    else memmove(dst, src, sz);
  } else {
    jello_value ret = vm_box_from_typed(vm, m, f, &fr->rf, ret_reg);
    vm_store_from_boxed(vm, m, caller->f, &caller->rf, caller_dst, ret);
  }
  vm_rf_release(vm, &fr->rf);
  vm->call_frames_len--;
  ctx->frames = frames;
  ctx->fr = caller;
  ctx->f = caller->f;
  ctx->jit_self_resume = resume;
  if(!resume) return JELLO_JIT_EXIT_YIELD;
  return JELLO_JIT_EXIT_CONTINUE;
}

void* jello_jit_runtime_take_resume(exec_ctx* ctx) {
  if(!ctx) return NULL;
  void* p = ctx->jit_self_resume;
  ctx->jit_self_resume = NULL;
  return p;
}

int jello_jit_runtime_cmp_i32(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t lhs_reg,
    uint32_t rhs_reg,
    uint32_t cmp_kind,
    uint32_t rhs_imm,
    uint32_t rhs_is_imm
) {
  if(!ctx || !ctx->fr) return 0;
  int32_t lv = (int32_t)vm_load_u32(&ctx->fr->rf, lhs_reg);
  int32_t rv = 0;
  if(rhs_is_imm) {
    rv = (int32_t)rhs_imm;
  } else if(rhs_reg == 255u) {
    rv = 0;
  } else {
    rv = (int32_t)vm_load_u32(&ctx->fr->rf, rhs_reg);
  }
  uint32_t out = 0;
  if(cmp_kind == (uint32_t)JIR_CMP_EQ) {
    out = (uint32_t)((lv == rv) ? 1 : 0);
  } else {
    out = (uint32_t)((lv < rv) ? 1 : 0);
  }
  vm_store_u32(&ctx->fr->rf, dst, out);
  return 0;
}

int jello_jit_runtime_cmp_f64(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t lhs_reg,
    uint32_t rhs_reg,
    uint32_t cmp_kind
) {
  if(!ctx || !ctx->fr) return 0;
  double lv = vm_load_f64(&ctx->fr->rf, lhs_reg);
  double rv = vm_load_f64(&ctx->fr->rf, rhs_reg);
  uint32_t out = 0;
  if(cmp_kind == (uint32_t)JIR_CMP_EQ) {
    out = (uint32_t)((lv == rv) ? 1 : 0);
  } else {
    out = (uint32_t)((lv < rv) ? 1 : 0);
  }
  vm_store_u32(&ctx->fr->rf, dst, out);
  return 0;
}

int jello_jit_runtime_cmp_f32(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t lhs_reg,
    uint32_t rhs_reg,
    uint32_t cmp_kind
) {
  if(!ctx || !ctx->fr) return 0;
  float lv = vm_load_f32(&ctx->fr->rf, lhs_reg);
  float rv = vm_load_f32(&ctx->fr->rf, rhs_reg);
  uint32_t out = 0;
  if(cmp_kind == (uint32_t)JIR_CMP_EQ) {
    out = (uint32_t)((lv == rv) ? 1 : 0);
  } else {
    out = (uint32_t)((lv < rv) ? 1 : 0);
  }
  vm_store_u32(&ctx->fr->rf, dst, out);
  return 0;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_fuel_trap(exec_ctx* ctx) {
  if(!ctx || !ctx->vm) return JELLO_JIT_EXIT_TRAP;
  (void)jello_vm_trap(ctx->vm, JELLO_TRAP_FUEL, "instruction limit exceeded");
  jello_jit_deopt_sync_ctx(ctx);
  return JELLO_JIT_EXIT_TRAP;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_len(exec_ctx* ctx, uint32_t dst, uint32_t bytes_reg) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_bytes* b = (jello_bytes*)vm_load_ptr(&ctx->fr->rf, bytes_reg);
  if(!b) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "bytes_len on null");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm_store_u32(&ctx->fr->rf, dst, b->length);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_eq(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t a_reg,
    uint32_t b_reg
) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {(uint8_t)JOP_BYTES_EQ, (uint8_t)dst, (uint8_t)a_reg, (uint8_t)b_reg, 0};
  op_result r = op_bytes_eq(ctx, &ins);
  if(r == OP_TRAP) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_get_u8(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t bytes_reg,
    uint32_t idx_reg
) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_bytes* b = (jello_bytes*)vm_load_ptr(&ctx->fr->rf, bytes_reg);
  if(!b) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "bytes_get_u8 on null");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  uint32_t idx = vm_load_u32(&ctx->fr->rf, idx_reg);
  if(idx >= b->length) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "bytes_get_u8 index out of bounds");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm_store_u32(&ctx->fr->rf, dst, (uint32_t)b->data[idx]);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_set_u8(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t bytes_reg,
    uint32_t idx_reg
) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_bytes* b = (jello_bytes*)vm_load_ptr(&ctx->fr->rf, bytes_reg);
  if(!b) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "bytes_set_u8 on null");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  uint32_t idx = vm_load_u32(&ctx->fr->rf, idx_reg);
  if(idx >= b->length) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "bytes_set_u8 index out of bounds");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  uint32_t v = vm_load_u32(&ctx->fr->rf, val_reg);
  if(v > 255u) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "bytes_set_u8 value out of range");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  b->data[idx] = (uint8_t)v;
  return JELLO_JIT_EXIT_CONTINUE;
}

typedef op_result (*jello_bytes_rw_op)(exec_ctx* ctx, const jello_insn* ins);

static op_result jit_invoke_bytes_read(exec_ctx* ctx, const jello_insn* ins, uint32_t kind) {
  static const jello_bytes_rw_op ops[8] = {
    op_bytes_read_u16_le,
    op_bytes_read_u16_be,
    op_bytes_read_u32_le,
    op_bytes_read_u32_be,
    op_bytes_read_i32_le,
    op_bytes_read_i32_be,
    op_bytes_read_f32_le,
    op_bytes_read_f32_be,
  };
  if(kind >= 8u) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "bytes_read invalid kind");
    jello_jit_deopt_sync_ctx(ctx);
    return OP_TRAP;
  }
  return ops[kind](ctx, ins);
}

static op_result jit_invoke_bytes_write(exec_ctx* ctx, const jello_insn* ins, uint32_t kind) {
  static const jello_bytes_rw_op ops[8] = {
    op_bytes_write_u16_le,
    op_bytes_write_u16_be,
    op_bytes_write_u32_le,
    op_bytes_write_u32_be,
    op_bytes_write_i32_le,
    op_bytes_write_i32_be,
    op_bytes_write_f32_le,
    op_bytes_write_f32_be,
  };
  if(kind >= 8u) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "bytes_write invalid kind");
    jello_jit_deopt_sync_ctx(ctx);
    return OP_TRAP;
  }
  return ops[kind](ctx, ins);
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_read(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t bytes_reg,
    uint32_t off_reg,
    uint32_t kind
) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {(uint8_t)JOP_BYTES_READ_U16_LE, (uint8_t)dst, (uint8_t)bytes_reg, (uint8_t)off_reg, 0};
  op_result r = jit_invoke_bytes_read(ctx, &ins, kind);
  if(r == OP_TRAP) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_write(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t bytes_reg,
    uint32_t off_reg,
    uint32_t kind
) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {(uint8_t)JOP_BYTES_WRITE_U16_LE, (uint8_t)val_reg, (uint8_t)bytes_reg, (uint8_t)off_reg, 0};
  op_result r = jit_invoke_bytes_write(ctx, &ins, kind);
  if(r == OP_TRAP) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

/* Array element traffic must stay type-generic: every numeric kind (I8/I16/I32/
 * I64/F16/F32/F64) and other boxed slots go through vm_box_from_typed /
 * vm_store_from_boxed. Do not specialize to I32-only fast paths here. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_len(exec_ctx* ctx, uint32_t dst, uint32_t array_reg) {
  if(!ctx || !ctx->fr) return JELLO_JIT_EXIT_TRAP;
  jello_array* a = (jello_array*)vm_load_ptr(&ctx->fr->rf, array_reg);
  if(!a) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "array_len on null");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm_store_u32(&ctx->fr->rf, dst, a->length);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_get(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t array_reg,
    uint32_t idx_reg
) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f) return JELLO_JIT_EXIT_TRAP;
  jello_array* a = (jello_array*)vm_load_ptr(&ctx->fr->rf, array_reg);
  if(!a) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "array_get on null");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  uint32_t idx = vm_load_u32(&ctx->fr->rf, idx_reg);
  if(idx >= a->length) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "array_get index out of bounds");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm_store_from_boxed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, dst, a->data[idx]);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_set(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t array_reg,
    uint32_t idx_reg
) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f || !ctx->vm) return JELLO_JIT_EXIT_TRAP;
  jello_array* a = (jello_array*)vm_load_ptr(&ctx->fr->rf, array_reg);
  if(!a) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "array_set on null");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  uint32_t idx = vm_load_u32(&ctx->fr->rf, idx_reg);
  if(idx >= a->length) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_BOUNDS, "array_set index out of bounds");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  if(!vm_store_num_inplace(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, val_reg, &a->data[idx])) {
    jello_value v = vm_box_from_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, val_reg);
    if(vm_reg_kind(ctx->m, ctx->f, val_reg) == JELLO_T_DYNAMIC) v = vm_clone_numbox(ctx->vm, v);
    a->data[idx] = v;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_get_atom(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f || !ctx->vm) return JELLO_JIT_EXIT_TRAP;
  if(!vm_obj_get_atom_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, dst, obj_reg, atom_id, 0)) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_set_atom(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f || !ctx->vm) return JELLO_JIT_EXIT_TRAP;
  if(!vm_obj_set_atom_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, val_reg, obj_reg, atom_id, 0)) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_insert_atom(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id,
    uint32_t slot_i
) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f || !ctx->vm) return JELLO_JIT_EXIT_TRAP;
  jello_object* o = (jello_object*)vm_load_ptr(&ctx->fr->rf, obj_reg);
  if(!o || slot_i >= o->cap) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, "obj_insert_atom bad obj/slot");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  jello_value v = vm_box_from_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, val_reg);
  if(vm_reg_kind(ctx->m, ctx->f, val_reg) == JELLO_T_DYNAMIC) v = vm_clone_numbox(ctx->vm, v);
  o->keys[slot_i] = atom_id;
  o->states[slot_i] = (uint8_t)JELLO_OBJ_SLOT_OCCUPIED;
  o->vals[slot_i] = v;
  o->len++;
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_object* jello_jit_object_new(jello_vm* vm, uint32_t type_id) {
  return jello_object_new(vm, type_id);
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_new(exec_ctx* ctx, uint32_t dst) {
  if(!ctx || !ctx->fr || !ctx->f || !ctx->vm || dst > 255u) return JELLO_JIT_EXIT_TRAP;
  uint32_t type_id = ctx->f->reg_types[dst];
  jello_object* o = jello_object_new(ctx->vm, type_id);
  if(!o) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm_store_ptr(&ctx->fr->rf, dst, o);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_new(exec_ctx* ctx, uint32_t dst, uint32_t len_reg) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f || dst > 255u || len_reg > 255u)
    return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {0};
  ins.op = (uint8_t)JOP_ARRAY_NEW;
  ins.a = (uint8_t)dst;
  ins.b = (uint8_t)len_reg;
  op_result r = op_array_new(ctx, &ins);
  if(r == OP_TRAP || (ctx->vm && ctx->vm->exc_pending)) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_new(exec_ctx* ctx, uint32_t dst, uint32_t len_reg) {
  if(!ctx || !ctx->fr || !ctx->m || !ctx->f || dst > 255u || len_reg > 255u)
    return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {0};
  ins.op = (uint8_t)JOP_BYTES_NEW;
  ins.a = (uint8_t)dst;
  ins.b = (uint8_t)len_reg;
  op_result r = op_bytes_new(ctx, &ins);
  if(r == OP_TRAP || (ctx->vm && ctx->vm->exc_pending)) {
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_assert(exec_ctx* ctx, uint32_t cond_reg, uint32_t msg_reg, uint32_t has_msg) {
  if(!ctx || !ctx->fr || !ctx->vm || cond_reg > 255u) return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {0};
  ins.op = (uint8_t)JOP_ASSERT;
  ins.a = (uint8_t)cond_reg;
  ins.b = (uint8_t)msg_reg;
  ins.c = (uint8_t)has_msg;
  op_result r = op_assert(ctx, &ins);
  jello_jit_deopt_sync_ctx(ctx);
  if(r == OP_TRAP || ctx->vm->exc_pending) return JELLO_JIT_EXIT_TRAP;
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_const_bytes(exec_ctx* ctx, uint32_t dst, uint32_t idx) {
  if(!ctx || !ctx->fr || !ctx->f || !ctx->vm || !ctx->m || dst > 255u) return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  if(idx >= vm->const_bytes_cache_len || idx >= m->nconst_bytes) return JELLO_JIT_EXIT_TRAP;
  jello_bytes** cache = (jello_bytes**)vm->const_bytes_cache;
  jello_bytes* b = cache[idx];
  if(!b) {
    uint32_t len = m->const_bytes_len[idx];
    uint32_t off = m->const_bytes_off[idx];
    uint32_t type_id = ctx->f->reg_types[dst];
    b = jello_bytes_new(vm, type_id, len);
    if(len > 0) memcpy(b->data, m->const_bytes_data + off, len);
    cache[idx] = b;
  }
  vm_store_ptr(&ctx->fr->rf, dst, b);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_const_fun(exec_ctx* ctx, uint32_t dst, uint32_t func_index) {
  if(!ctx || !ctx->fr || !ctx->f || !ctx->vm || dst > 255u) return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  if(func_index >= vm->const_fun_cache_len) return JELLO_JIT_EXIT_TRAP;
  jello_function** cache = (jello_function**)vm->const_fun_cache;
  jello_function* fn = cache[func_index];
  if(!fn) {
    uint32_t type_id = ctx->f->reg_types[dst];
    fn = jello_function_new(vm, type_id, func_index);
    cache[func_index] = fn;
  }
  vm_store_ptr(&ctx->fr->rf, dst, fn);
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_closure(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t first_cap,
    uint32_t ncaps,
    uint32_t func_index
) {
  if(!ctx || !ctx->fr || dst > 255u || first_cap > 255u || ncaps > 255u)
    return JELLO_JIT_EXIT_TRAP;
  jello_insn ins = {0};
  ins.op = (uint8_t)JOP_CLOSURE;
  ins.a = (uint8_t)dst;
  ins.b = (uint8_t)first_cap;
  ins.c = (uint8_t)ncaps;
  ins.imm = func_index;
  op_result r = op_closure(ctx, &ins);
  jello_jit_deopt_sync_ctx(ctx);
  if(r == OP_TRAP || (ctx->vm && ctx->vm->exc_pending)) return JELLO_JIT_EXIT_TRAP;
  return JELLO_JIT_EXIT_CONTINUE;
}

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_conv(exec_ctx* ctx, uint32_t dst, uint32_t src, uint32_t kind) {
  if(!ctx || !ctx->fr || !ctx->vm || dst > 255u || src > 255u) return JELLO_JIT_EXIT_TRAP;
  reg_frame* rf = &ctx->fr->rf;
  jello_vm* vm = ctx->vm;
  switch((jello_jit_conv)kind) {
    case JIR_CONV_SEXT_I64:
      vm_store_i64(rf, dst, (int64_t)(int32_t)vm_load_u32(rf, src));
      break;
    case JIR_CONV_SEXT_I16: {
      uint32_t x = vm_load_u32(rf, src);
      vm_store_u32(rf, dst, (uint32_t)(int32_t)(int16_t)(int8_t)(x & 0xFFu));
      break;
    }
    case JIR_CONV_TRUNC_I8:
      vm_store_u32_masked(rf, dst, vm_load_u32(rf, src), JELLO_T_I8);
      break;
    case JIR_CONV_TRUNC_I16:
      vm_store_u32_masked(rf, dst, vm_load_u32(rf, src), JELLO_T_I16);
      break;
    case JIR_CONV_I32_FROM_I64:
      vm_store_u32(rf, dst, (uint32_t)(uint64_t)vm_load_i64(rf, src));
      break;
    case JIR_CONV_F64_FROM_I32:
      vm_store_f64(rf, dst, (double)(int32_t)vm_load_u32(rf, src));
      break;
    case JIR_CONV_I32_FROM_F64: {
      uint32_t out_u32 = 0;
      if(!vm_checked_f64_to_i32(vm, vm_load_f64(rf, src), &out_u32)) {
        jello_jit_deopt_sync_ctx(ctx);
        return JELLO_JIT_EXIT_TRAP;
      }
      vm_store_u32(rf, dst, out_u32);
      break;
    }
    case JIR_CONV_F64_FROM_I64:
      vm_store_f64(rf, dst, (double)vm_load_i64(rf, src));
      break;
    case JIR_CONV_I64_FROM_F64: {
      int64_t out_i64 = 0;
      if(!vm_checked_f64_to_i64(vm, vm_load_f64(rf, src), &out_i64)) {
        jello_jit_deopt_sync_ctx(ctx);
        return JELLO_JIT_EXIT_TRAP;
      }
      vm_store_i64(rf, dst, out_i64);
      break;
    }
    case JIR_CONV_F32_FROM_I32:
      vm_store_f32(rf, dst, (float)(int32_t)vm_load_u32(rf, src));
      break;
    case JIR_CONV_I32_FROM_F32: {
      uint32_t out_u32 = 0;
      if(!vm_checked_f64_to_i32(vm, (double)vm_load_f32(rf, src), &out_u32)) {
        jello_jit_deopt_sync_ctx(ctx);
        return JELLO_JIT_EXIT_TRAP;
      }
      vm_store_u32(rf, dst, out_u32);
      break;
    }
    case JIR_CONV_F64_FROM_F32:
      vm_store_f64(rf, dst, (double)vm_load_f32(rf, src));
      break;
    case JIR_CONV_F32_FROM_F64:
      vm_store_f32(rf, dst, (float)vm_load_f64(rf, src));
      break;
    case JIR_CONV_F32_FROM_I64:
      vm_store_f32(rf, dst, (float)vm_load_i64(rf, src));
      break;
    case JIR_CONV_I64_FROM_F32: {
      int64_t out_i64 = 0;
      if(!vm_checked_f64_to_i64(vm, (double)vm_load_f32(rf, src), &out_i64)) {
        jello_jit_deopt_sync_ctx(ctx);
        return JELLO_JIT_EXIT_TRAP;
      }
      vm_store_i64(rf, dst, out_i64);
      break;
    }
    default:
      return JELLO_JIT_EXIT_TRAP;
  }
  return JELLO_JIT_EXIT_CONTINUE;
}

jello_jit_exit jello_jit_runtime_fuel_check(exec_ctx* ctx) {
  if(!ctx || !ctx->vm) return JELLO_JIT_EXIT_TRAP;
  jello_vm* vm = ctx->vm;
  if(!vm->fuel_limit) return JELLO_JIT_EXIT_CONTINUE;
  if(vm->fuel_remaining == 0) {
    (void)jello_vm_trap(vm, JELLO_TRAP_FUEL, "instruction limit exceeded");
    jello_jit_deopt_sync_ctx(ctx);
    return JELLO_JIT_EXIT_TRAP;
  }
  vm->fuel_remaining--;
  return JELLO_JIT_EXIT_CONTINUE;
}

jello_jit_run_result jello_jit_runtime_run(exec_ctx* ctx, const jello_jit_code* code) {
  if(!ctx || !code || !code->entry) return JELLO_JIT_RUN_CONTINUE;
  void* prev_resume = ctx->jit_resume_entry;
  jello_jit_entry_fn entry;
  memcpy(&entry, &code->entry, sizeof(entry));
  jello_jit_exit exit = entry(ctx);
  ctx->jit_resume_entry = prev_resume;
  jello_jit_deopt_sync_ctx(ctx);
  switch(exit) {
    case JELLO_JIT_EXIT_RETURNED:
      return JELLO_JIT_RUN_RETURNED;
    case JELLO_JIT_EXIT_YIELD:
    case JELLO_JIT_EXIT_TRAP:
      return JELLO_JIT_RUN_STEPPED;
    case JELLO_JIT_EXIT_CONTINUE:
    default:
      return JELLO_JIT_RUN_STEPPED;
  }
}
