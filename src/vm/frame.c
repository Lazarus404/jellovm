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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static size_t align_up(size_t x, size_t a) {
  return (x + (a - 1u)) & ~(a - 1u);
}

static void vm_free_frame_layouts(jello_vm* vm) {
  if(!vm) return;
  frame_layout* ls = (frame_layout*)vm->frame_layouts;
  for(uint32_t i = 0; i < vm->frame_layouts_len; i++) {
    free(ls[i].off);
    ls[i].off = NULL;
    ls[i].nregs = 0;
    ls[i].total = 0;
  }
  free(vm->frame_layouts);
  vm->frame_layouts = NULL;
  vm->frame_layouts_len = 0;
  vm->frame_layouts_mod = NULL;
}

static uint32_t rf_bucket_for_size(uint32_t size) {
  if(size == 0) return 0;
  uint32_t bucket = size >> 2u; /* 4-byte granularity */
  return bucket >= RF_POOL_BUCKETS ? RF_POOL_BUCKETS - 1u : bucket;
}

static void vm_free_rf_pool(jello_vm* vm) {
  if(!vm) return;
  for(uint32_t i = 0; i < RF_POOL_BUCKETS; i++) {
    rf_mem_block* b = (rf_mem_block*)vm->rf_free_by_size[i];
    while(b) {
      rf_mem_block* next = b->next;
      free(b);
      b = next;
    }
    vm->rf_free_by_size[i] = NULL;
  }
}

#define FRAME_STACK_INITIAL (64u * 1024u) /* 64KB */

static void vm_ensure_frame_stack(jello_vm* vm, uint32_t need) {
  if(vm->frame_stack_top + need <= vm->frame_stack_cap) return;
  uint8_t* old_stack = vm->frame_stack;
  uint32_t old_cap = vm->frame_stack_cap;
  /* 4x growth reduces realloc+rebase frequency (was 2x). */
  uint32_t new_cap = old_cap ? (old_cap * 4u) : FRAME_STACK_INITIAL;
  while(new_cap < vm->frame_stack_top + need) new_cap *= 4u;
  uint8_t* p = (uint8_t*)realloc(vm->frame_stack, (size_t)new_cap);
  if(!p) jello_vm_panic();
  vm->frame_stack = p;
  vm->frame_stack_cap = new_cap;
  /* If realloc moved the block, rebase all call frame rf.mem pointers. */
  if(p != old_stack && old_stack && vm->call_frames) {
    call_frame* frames = (call_frame*)vm->call_frames;
    uint32_t n = vm->call_frames_len;
    for(uint32_t i = 0; i < n; i++) {
      reg_frame* rf = &frames[i].rf;
      if(rf->mem) {
        ptrdiff_t off = (ptrdiff_t)(rf->mem - old_stack);
        rf->mem = p + off;
      }
    }
  }
}

static uint8_t* vm_frame_stack_alloc(jello_vm* vm, uint32_t size) {
  if(size == 0) return NULL;
  vm_ensure_frame_stack(vm, size);
  uint8_t* mem = vm->frame_stack + vm->frame_stack_top;
  vm->frame_stack_top += size;
  return mem;
}

static void vm_free_frame_stack(jello_vm* vm) {
  if(!vm) return;
  free(vm->frame_stack);
  vm->frame_stack = NULL;
  vm->frame_stack_top = 0;
  vm->frame_stack_cap = 0;
}

void vm_frame_cache_shutdown(jello_vm* vm) {
  vm_free_frame_layouts(vm);
  vm_free_rf_pool(vm);
  vm_free_frame_stack(vm);
}

/* REPL: invalidate frame layouts so frame_layouts_mod does not point at freed module. */
void jello_vm_invalidate_frame_cache(jello_vm* vm) {
  if(!vm) return;
  vm_free_frame_layouts(vm);
}

static void vm_prepare_frame_layouts(jello_vm* vm, const jello_bc_module* m) {
  if(!vm || !m) return;
  if(vm->frame_layouts_mod == m) return;
  vm_free_frame_layouts(vm);
  vm->frame_layouts_len = m->nfuncs;
  vm->frame_layouts = calloc((size_t)m->nfuncs, sizeof(frame_layout));
  if(!vm->frame_layouts) jello_vm_panic();
  vm->frame_layouts_mod = m;
}

void vm_rf_release(jello_vm* vm, reg_frame* rf) {
  if(!rf) return;
  if(rf->mem) {
    /* Frame stack: LIFO pop (no malloc/free). */
    if(vm->frame_stack && rf->mem >= vm->frame_stack &&
       rf->mem < vm->frame_stack + vm->frame_stack_cap) {
      vm->frame_stack_top -= rf->total;
    } else {
      rf_mem_block* b = ((rf_mem_block*)rf->mem) - 1;
      uint32_t bucket = rf_bucket_for_size(b->size);
      b->next = (rf_mem_block*)vm->rf_free_by_size[bucket];
      vm->rf_free_by_size[bucket] = b;
    }
  }
  if(rf->off && !rf->off_shared) {
    free((void*)rf->off);
  }
  rf->mem = NULL;
  rf->off = NULL;
  rf->nregs = 0;
  rf->total = 0;
  rf->off_shared = 0;
}

const frame_layout* vm_get_frame_layout(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f) {
  if(!vm || !m || !f) jello_vm_panic();
  vm_prepare_frame_layouts(vm, m);
  if(vm->frame_layouts_len != m->nfuncs) jello_vm_panic();

  ptrdiff_t idx = f - m->funcs;
  if(idx < 0 || (uint32_t)idx >= m->nfuncs) jello_vm_panic();
  frame_layout* fl = &((frame_layout*)vm->frame_layouts)[(uint32_t)idx];
  if(fl->off) return fl;

  fl->nregs = f->nregs;
  fl->off = (uint32_t*)malloc(sizeof(uint32_t) * (size_t)fl->nregs);
  if(!fl->off) jello_vm_panic();

  size_t total = 0;
  uint8_t has_any = 0;
  for(uint32_t i = 0; i < fl->nregs; i++) {
    jello_type_kind k = vm_reg_kind(m, f, i);
    size_t a = jello_slot_align(k);
    size_t s = jello_slot_size(k);
    total = align_up(total, a);
    fl->off[i] = (uint32_t)total;
    total += s;
    if(k == JELLO_T_DYNAMIC || k == JELLO_T_BYTES || k == JELLO_T_LIST ||
       k == JELLO_T_ARRAY || k == JELLO_T_OBJECT || k == JELLO_T_FUNCTION || k == JELLO_T_ABSTRACT)
      has_any = 1;
  }
  fl->total = (uint32_t)total;
  fl->has_pointer_or_dynamic = has_any;
  return fl;
}

/* Returns 1 if type is non-pointer (no GC root needed when copying). */
uint8_t vm_type_is_nonptr(jello_type_kind k) {
  return (k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32 || k == JELLO_T_I64 ||
          k == JELLO_T_F16 || k == JELLO_T_F32 || k == JELLO_T_F64 ||
          k == JELLO_T_BOOL || k == JELLO_T_ATOM);
}

#define VM_FAST_ARG_NARGS_MAX_PUSH  16u
#define VM_FAST_ARG_NARGS_MAX_TAIL 32u

/* Unified predicate for both vm_push_frame and vm_replace_frame.
 * Returns 1 if we can use fast arg copy (no funobj, or plain function).
 * dst_base: callee reg index for first arg (0 normally, 1 when bound_this occupies reg 0).
 * require_nonptr: 1 for tail-call raw copy (no boxing), 0 for push (vm_copy_arg_strict handles ptrs).
 * Caller must check nargs is within limit before calling. */
static uint8_t vm_can_use_fast_arg_copy(const jello_bc_module* m,
                                        const jello_bc_function* caller_f,
                                        const jello_bc_function* callee_f,
                                        uint32_t first_arg, uint32_t nargs,
                                        uint32_t dst_base,
                                        const jello_function* funobj,
                                        uint8_t require_nonptr) {
  if(nargs == 0u) return 0u;
  if(funobj && (funobj->ncaps != 0u || !jello_is_null(funobj->bound_this))) return 0u;
  for(uint32_t i = 0; i < nargs; i++) {
    uint32_t src_tid = caller_f->reg_types[first_arg + i];
    uint32_t dst_tid = callee_f->reg_types[dst_base + i];
    if(src_tid != dst_tid) return 0u;
    if(require_nonptr) {
      jello_type_kind k = m->types[dst_tid].kind;
      if(!vm_type_is_nonptr(k)) return 0u;
    }
  }
  return 1u;
}

int vm_push_frame(jello_vm* vm,
                  const jello_bc_module* m,
                  const jello_bc_function* callee_f,
                  const jello_bc_function* caller_f,
                  uint32_t caller_frame_index,
                  uint32_t caller_dst,
                  uint32_t first_arg,
                  uint32_t nargs,
                  const jello_function* funobj,
                  uint8_t has_caller) {
  if(vm->call_frames_max && vm->call_frames_len >= vm->call_frames_max) {
    vm->trap_code = JELLO_TRAP_STACK_OVERFLOW;
    vm->trap_msg = "stack overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_STACK_OVERFLOW);
    return 0;
  }
  call_frame* frames = (call_frame*)vm->call_frames;
  if(vm->call_frames_len == vm->call_frames_cap) {
    uint32_t ncap = vm->call_frames_cap ? (vm->call_frames_cap * 2u) : 16u;
    call_frame* nf = (call_frame*)realloc(frames, sizeof(call_frame) * (size_t)ncap);
    if(!nf) jello_vm_panic();
    frames = nf;
    vm->call_frames = nf;
    vm->call_frames_cap = ncap;
  }

  /* Reset frame stack at start of each execution (first frame). */
  if(vm->call_frames_len == 0) vm->frame_stack_top = 0;

  call_frame* fr = &frames[vm->call_frames_len++];
  fr->f = callee_f;
  fr->pc = 0;
  fr->caller_dst = caller_dst;
  fr->exc_base = vm->exc_handlers_len;
  fr->has_caller = has_caller;
  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  fr->rf.nregs = fl->nregs;
  fr->rf.off = fl->off;
  fr->rf.total = fl->total;
  fr->rf.off_shared = 1u;
  /* Use contiguous frame stack (no per-call malloc) for hot path. */
  fr->rf.mem = vm_frame_stack_alloc(vm, fl->total);
  /* Init frame: all-numeric functions skip entirely (compiler ensures def-before-use).
   * Functions with pointer/Dynamic slots: memset to zero (null/zero ptr), then set
   * Dynamic slots to tagged null. Single memset + minimal loop is faster than
   * per-register type lookup loop. */
  if(fr->rf.mem && fl->total && fl->has_pointer_or_dynamic) {
    memset(fr->rf.mem, 0, (size_t)fl->total);
    for(uint32_t r = 0; r < fl->nregs; r++) {
      if(m->types[callee_f->reg_types[r]].kind == JELLO_T_DYNAMIC) {
        jello_value v = jello_make_null();
        memcpy(fr->rf.mem + fl->off[r], &v, sizeof(v));
      }
    }
  }
  if(has_caller) {
    if(caller_frame_index >= vm->call_frames_len - 1u) jello_vm_panic();
    call_frame* caller = &frames[caller_frame_index];
    if(nargs <= VM_FAST_ARG_NARGS_MAX_PUSH &&
       vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 0u, funobj, 0u)) {
#ifndef NDEBUG
      if(first_arg + nargs > caller->rf.nregs) jello_vm_panic();
#endif
      for(uint32_t i = 0; i < nargs; i++) {
        uint32_t src = first_arg + i;
        uint32_t dst = i;
        jello_type_kind k = m->types[callee_f->reg_types[dst]].kind;
        size_t sz = jello_slot_size(k);
        uint8_t* dstp = fr->rf.mem + fr->rf.off[dst];
        const uint8_t* srcp = caller->rf.mem + caller->rf.off[src];
        if(sz == 4u) *(uint32_t*)dstp = *(const uint32_t*)srcp;
        else if(sz == 8u) *(uint64_t*)dstp = *(const uint64_t*)srcp;
        else memmove(dstp, srcp, sz);
      }
      return 1;
    }
    vm_init_args_and_caps(vm, m, callee_f, &fr->rf, caller_f, &caller->rf, first_arg, nargs, funobj);
  }
  return 1;
}

/* Tail call: replace current frame instead of pushing. Preserves caller_dst and exc_base
 * so the callee returns to our caller. Frame stack is LIFO, so we must copy args to temp,
 * release current frame, then alloc and set up the new callee.
 *
 * Fast path: when no funobj (or funobj has no bound_this/caps) and all args are non-pointer
 * types with matching caller/callee types, use raw memcpy instead of box/unbox. */
int vm_replace_frame(jello_vm* vm,
                     const jello_bc_module* m,
                     const jello_bc_function* callee_f,
                     const jello_bc_function* caller_f,
                     uint32_t first_arg,
                     uint32_t nargs,
                     const jello_function* funobj) {
  call_frame* frames = (call_frame*)vm->call_frames;
  if(vm->call_frames_len < 1u) jello_vm_panic();
  call_frame* fr = &frames[vm->call_frames_len - 1u];

  uint32_t caller_dst = fr->caller_dst;
  uint32_t exc_base = fr->exc_base;
  uint8_t has_caller = fr->has_caller;

  /* Fast path 1: no funobj (or plain function), all args non-pointer, types match. */
  uint8_t use_raw_copy = (nargs <= VM_FAST_ARG_NARGS_MAX_TAIL &&
                          vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 0u, funobj, 1u));

  /* Fast path 2: bound_this only (ncaps=0), all args non-pointer, types match. */
  uint8_t use_bound_this_fast = 0u;
  if(!use_raw_copy && nargs <= VM_FAST_ARG_NARGS_MAX_TAIL && funobj &&
     !jello_is_null(funobj->bound_this) && funobj->ncaps == 0u &&
     callee_f->nregs >= 1u + nargs) {
    use_bound_this_fast = vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 1u, NULL, 1u);
  }

  if(use_raw_copy) {
    /* Raw copy path: no boxing, no GC roots. Copy to temp, release, alloc, copy back. */
    uint8_t raw_tmp[32 * 8];
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[callee_f->reg_types[i]].kind;
      size_t sz = jello_slot_size(k);
      memcpy(raw_tmp + i * 8, fr->rf.mem + fr->rf.off[first_arg + i], sz);
    }

    vm_rf_release(vm, &fr->rf);

    const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
    fr->f = callee_f;
    fr->pc = 0;
    fr->caller_dst = caller_dst;
    fr->exc_base = exc_base;
    fr->has_caller = has_caller;
    fr->rf.nregs = fl->nregs;
    fr->rf.off = fl->off;
    fr->rf.total = fl->total;
    fr->rf.off_shared = 1u;
    fr->rf.mem = vm_frame_stack_alloc(vm, fl->total);

    if(fr->rf.mem && fl->total && fl->has_pointer_or_dynamic) {
      memset(fr->rf.mem, 0, (size_t)fl->total);
      for(uint32_t r = 0; r < fl->nregs; r++) {
        if(m->types[callee_f->reg_types[r]].kind == JELLO_T_DYNAMIC) {
          jello_value v = jello_make_null();
          memcpy(fr->rf.mem + fl->off[r], &v, sizeof(v));
        }
      }
    }

    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[callee_f->reg_types[i]].kind;
      size_t sz = jello_slot_size(k);
      memcpy(fr->rf.mem + fr->rf.off[i], raw_tmp + i * 8, sz);
    }
    return 1;
  }

  if(use_bound_this_fast) {
    /* Bound_this only: box just bound_this, raw-copy args. */
    uint8_t raw_tmp[32 * 8];
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[callee_f->reg_types[i]].kind;
      size_t sz = jello_slot_size(k);
      memcpy(raw_tmp + i * 8, fr->rf.mem + fr->rf.off[first_arg + i], sz);
    }

    vm_rf_release(vm, &fr->rf);

    const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
    fr->f = callee_f;
    fr->pc = 0;
    fr->caller_dst = caller_dst;
    fr->exc_base = exc_base;
    fr->has_caller = has_caller;
    fr->rf.nregs = fl->nregs;
    fr->rf.off = fl->off;
    fr->rf.total = fl->total;
    fr->rf.off_shared = 1u;
    fr->rf.mem = vm_frame_stack_alloc(vm, fl->total);

    if(fr->rf.mem && fl->total && fl->has_pointer_or_dynamic) {
      memset(fr->rf.mem, 0, (size_t)fl->total);
      for(uint32_t r = 0; r < fl->nregs; r++) {
        if(m->types[callee_f->reg_types[r]].kind == JELLO_T_DYNAMIC) {
          jello_value v = jello_make_null();
          memcpy(fr->rf.mem + fl->off[r], &v, sizeof(v));
        }
      }
    }

    vm_store_from_boxed(m, callee_f, &fr->rf, 0, funobj->bound_this);
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[callee_f->reg_types[1 + i]].kind;
      size_t sz = jello_slot_size(k);
      memcpy(fr->rf.mem + fr->rf.off[1 + i], raw_tmp + i * 8, sz);
    }
    return 1;
  }

  /* Box args to temp before releasing (frame stack is LIFO). */
  jello_value tmp[32];
  if(nargs > 32u) jello_vm_panic();
  for(uint32_t i = 0; i < nargs; i++) {
    tmp[i] = vm_box_from_typed(vm, m, caller_f, &fr->rf, first_arg + i);
    jello_gc_push_root(vm, tmp[i]);
  }

  vm_rf_release(vm, &fr->rf);

  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  fr->f = callee_f;
  fr->pc = 0;
  fr->caller_dst = caller_dst;
  fr->exc_base = exc_base;
  fr->has_caller = has_caller;
  fr->rf.nregs = fl->nregs;
  fr->rf.off = fl->off;
  fr->rf.total = fl->total;
  fr->rf.off_shared = 1u;
  fr->rf.mem = vm_frame_stack_alloc(vm, fl->total);

  if(fr->rf.mem && fl->total && fl->has_pointer_or_dynamic) {
    memset(fr->rf.mem, 0, (size_t)fl->total);
    for(uint32_t r = 0; r < fl->nregs; r++) {
      if(m->types[callee_f->reg_types[r]].kind == JELLO_T_DYNAMIC) {
        jello_value v = jello_make_null();
        memcpy(fr->rf.mem + fl->off[r], &v, sizeof(v));
      }
    }
  }

  jello_gc_pop_roots(vm, nargs);

  uint32_t arg_base = 0u;
  if(funobj && !jello_is_null(funobj->bound_this)) {
    vm_store_from_boxed(m, callee_f, &fr->rf, 0, funobj->bound_this);
    arg_base = 1u;
  }
  for(uint32_t i = 0; i < nargs; i++) {
    vm_store_from_boxed(m, callee_f, &fr->rf, arg_base + i, tmp[i]);
  }
  if(funobj && funobj->ncaps) {
    uint32_t cap_start = (m->features & (uint32_t)JELLO_BC_FEAT_CAP_START) && callee_f->cap_start < fr->rf.nregs
      ? callee_f->cap_start
      : fr->rf.nregs - funobj->ncaps;
    if(funobj->caps_are_raw) {
      const uint8_t* raw = (const uint8_t*)&funobj->caps[0];
      uint32_t off = 0;
      for(uint32_t i = 0; i < funobj->ncaps; i++) {
        jello_type_kind k = m->types[callee_f->reg_types[cap_start + i]].kind;
        size_t sz = jello_slot_size(k);
        memcpy(fr->rf.mem + fr->rf.off[cap_start + i], raw + off, sz);
        off += (uint32_t)sz;
      }
    } else {
      for(uint32_t i = 0; i < funobj->ncaps; i++) {
        vm_store_from_boxed(m, callee_f, &fr->rf, cap_start + i, funobj->caps[i]);
      }
    }
  }
  return 1;
}

/* Phase 4: push a frame with args from jello_value* (for chunk exec with pre-bound imports).
 * Stores args[0..nargs-1] into callee regs 0..nargs-1. */
int vm_push_frame_from_values(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* callee_f,
                              const jello_value* args, uint32_t nargs) {
  if(vm->call_frames_max && vm->call_frames_len >= vm->call_frames_max) {
    vm->trap_code = JELLO_TRAP_STACK_OVERFLOW;
    vm->trap_msg = "stack overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_STACK_OVERFLOW);
    return 0;
  }
  if(nargs > callee_f->nregs) jello_vm_panic();

  call_frame* frames = (call_frame*)vm->call_frames;
  if(vm->call_frames_len == vm->call_frames_cap) {
    uint32_t ncap = vm->call_frames_cap ? (vm->call_frames_cap * 2u) : 16u;
    call_frame* nf = (call_frame*)realloc(frames, sizeof(call_frame) * (size_t)ncap);
    if(!nf) jello_vm_panic();
    frames = nf;
    vm->call_frames = nf;
    vm->call_frames_cap = ncap;
  }

  if(vm->call_frames_len == 0) vm->frame_stack_top = 0;

  call_frame* fr = &frames[vm->call_frames_len++];
  fr->f = callee_f;
  fr->pc = 0;
  fr->caller_dst = 0;
  fr->exc_base = vm->exc_handlers_len;
  fr->has_caller = 0;

  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  fr->rf.nregs = fl->nregs;
  fr->rf.off = fl->off;
  fr->rf.total = fl->total;
  fr->rf.off_shared = 1u;
  fr->rf.mem = vm_frame_stack_alloc(vm, fl->total);

  if(fr->rf.mem && fl->total && fl->has_pointer_or_dynamic) {
    memset(fr->rf.mem, 0, (size_t)fl->total);
    for(uint32_t r = 0; r < fl->nregs; r++) {
      if(m->types[callee_f->reg_types[r]].kind == JELLO_T_DYNAMIC) {
        jello_value v = jello_make_null();
        memcpy(fr->rf.mem + fl->off[r], &v, sizeof(v));
      }
    }
  }

  for(uint32_t i = 0; i < nargs; i++) {
    vm_store_from_boxed(m, callee_f, &fr->rf, i, args[i]);
  }
  return 1;
}
