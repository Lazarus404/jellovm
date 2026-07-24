// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/jit_internal.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static size_t align_up(size_t x, size_t a) {
  return (x + (a - 1u)) & ~(a - 1u);
}

/* Direct self-CALL (ConstFun) passes funobj=NULL, so closure captures are not
 * re-installed. Copy capture slots from the caller frame on recursive entry. */
static void vm_inherit_self_capture_slots(const jello_bc_module* m,
                                          const jello_bc_function* f,
                                          reg_frame* dst_rf,
                                          const reg_frame* src_rf,
                                          uint32_t nargs) {
  if(!(m->features & (uint32_t)JELLO_BC_FEAT_CAP_START)) return;
  if(f->cap_start == 0u || f->cap_start >= f->nregs) return;
  for(uint32_t r = f->cap_start; r < f->nregs; r++) {
    if(r < nargs) continue;
    jello_type_kind k = m->types[f->reg_types[r]].kind;
    size_t sz = jello_slot_size(k);
    memmove(dst_rf->mem + dst_rf->off[r], src_rf->mem + src_rf->off[r], sz);
  }
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

void vm_frame_stack_grow(jello_vm* vm, uint32_t need) {
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
  return vm_frame_stack_bump(vm, size);
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
  if(vm->frame_layouts_mod) jello_jit_module_unloaded(vm, vm->frame_layouts_mod);
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
    size_t sz = jello_slot_size(k);
    total = align_up(total, sz);
    fl->off[i] = (uint32_t)total;
    total += sz;
    if(k == JELLO_T_DYNAMIC || k == JELLO_T_BYTES || k == JELLO_T_LIST ||
       k == JELLO_T_ARRAY || k == JELLO_T_OBJECT || k == JELLO_T_FUNCTION || k == JELLO_T_ABSTRACT ||
       k == JELLO_T_ENUM)
      has_any = 1;
  }
  fl->total = (uint32_t)total;
  fl->has_pointer_or_dynamic = has_any;
  /* jit_ineligible: only TRY (unsupported). CALL/CALLR compile via slow path;
   * self-rec still gets jit_self_rec for eager entry compile. Jmp-loop TCO
   * (return if … else { self(…) }) has no self CALL in bytecode — detect via
   * JmpIf + backward Jmp instead. */
  fl->jit_ineligible = 0u;
  fl->jit_self_rec = 0u;
  {
    uint8_t heap_regs = 0u;
    for(uint32_t r = 0; r < fl->nregs; r++) {
      jello_type_kind k = vm_reg_kind(m, f, r);
      if(k == JELLO_T_DYNAMIC || k == JELLO_T_BYTES || k == JELLO_T_LIST || k == JELLO_T_ARRAY ||
         k == JELLO_T_OBJECT || k == JELLO_T_ABSTRACT || k == JELLO_T_ENUM)
        heap_regs = 1u;
    }
    uint8_t saw_self = 0u, saw_try = 0u, saw_non_self_call = 0u;
    uint8_t saw_jmp_if = 0u, saw_back_jmp = 0u;
    for(uint32_t i = 0; i < f->ninsns; i++) {
      jello_op op = (jello_op)f->insns[i].op;
      if(op == JOP_TRY) {
        saw_try = 1u;
        break;
      }
      if(op == JOP_JMP_IF) saw_jmp_if = 1u;
      if(op == JOP_JMP) {
        int32_t d = (int32_t)f->insns[i].imm;
        if(d < 0) saw_back_jmp = 1u;
      }
      if(op == JOP_TAILCALL || op == JOP_TAILCALLR) {
        saw_non_self_call = 1u;
        continue;
      }
      if(op == JOP_CALL) {
        uint32_t fi = f->insns[i].imm;
        if(jello_is_native_builtin(fi)) {
          saw_non_self_call = 1u;
          continue;
        }
        uint32_t bidx = fi - JELLO_NATIVE_BUILTIN_COUNT;
        if(bidx < m->nfuncs && &m->funcs[bidx] == f && f->insns[i].c <= 16u && !heap_regs)
          saw_self = 1u;
        else
          saw_non_self_call = 1u;
      } else if(op == JOP_CALLR) {
        uint32_t callee_reg = f->insns[i].b;
        uint32_t na = f->insns[i].c;
        int is_self = 0;
        for(int32_t j = (int32_t)i - 1; j >= 0; j--) {
          const jello_insn* w = &f->insns[j];
          if(w->a != callee_reg) continue;
          if((jello_op)w->op == JOP_CONST_FUN) {
            uint32_t fi = w->imm;
            if(!jello_is_native_builtin(fi)) {
              uint32_t bidx = fi - JELLO_NATIVE_BUILTIN_COUNT;
              if(bidx < m->nfuncs && &m->funcs[bidx] == f) is_self = 1;
            }
          }
          break;
        }
        if(is_self && na <= 16u && !heap_regs)
          saw_self = 1u;
        else
          saw_non_self_call = 1u;
      }
    }
    if(saw_try) fl->jit_ineligible = 1u;
    else if((saw_self && !saw_non_self_call) ||
            (saw_jmp_if && saw_back_jmp && !saw_non_self_call && !heap_regs))
      fl->jit_self_rec = 1u;
  }
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

void vm_call_frames_reserve(jello_vm* vm) {
  if(!vm || vm->call_frames_cap >= VM_CALL_FRAMES_INITIAL) return;
  call_frame* nf = (call_frame*)calloc((size_t)VM_CALL_FRAMES_INITIAL, sizeof(call_frame));
  if(!nf) jello_vm_panic();
  free(vm->call_frames);
  vm->call_frames = nf;
  vm->call_frames_cap = VM_CALL_FRAMES_INITIAL;
  vm->call_frames_len = 0;
}

void vm_call_frames_grow_if_full(jello_vm* vm, call_frame** fr_inout) {
  if(!vm || !fr_inout || !*fr_inout) return;
  if(vm->call_frames_len != vm->call_frames_cap) return;
  call_frame* frames = (call_frame*)vm->call_frames;
  uint32_t ncap = vm->call_frames_cap ? (vm->call_frames_cap * 2u) : 16u;
  call_frame* nf = (call_frame*)realloc(frames, sizeof(call_frame) * (size_t)ncap);
  if(!nf) jello_vm_panic();
  vm->call_frames = nf;
  vm->call_frames_cap = ncap;
  *fr_inout = &nf[vm->call_frames_len - 1u];
}

call_frame* vm_push_self_numeric(jello_vm* vm, const jello_bc_module* m, call_frame* fr,
                                 uint32_t first_arg, uint32_t nargs, uint32_t caller_dst,
                                 void* jit_return_addr, uint8_t jit_entry_done) {
  call_frame* frames = (call_frame*)vm->call_frames;
  const jello_bc_function* f = fr->f;
  uint32_t total = fr->rf.total;
  /* Bump may rebase fr->rf.mem; copy after so src/dst never overlap. */
  uint8_t* mem = vm_frame_stack_bump(vm, total);
  call_frame* nfr = &frames[vm->call_frames_len++];
  nfr->f = f;
  nfr->pc = 0;
  nfr->jit_entry_done = jit_entry_done;
  nfr->jit_resume_hint = 0;
  nfr->jit_osr_hint = 0;
  nfr->jit_return_addr = jit_return_addr;
  nfr->caller_dst = caller_dst;
  nfr->exc_base = vm->exc_handlers_len;
  nfr->has_caller = 1u;
  nfr->jdll_ret_capture = 0;
  nfr->has_pointer_or_dynamic = 0u;
  nfr->rf.nregs = fr->rf.nregs;
  nfr->rf.off = fr->rf.off;
  nfr->rf.total = total;
  nfr->rf.off_shared = 1u;
  nfr->rf.mem = mem;
  if(nargs == 1u && jello_slot_size(m->types[f->reg_types[0]].kind) == 4u) {
    *(uint32_t*)(mem + nfr->rf.off[0]) =
        *(const uint32_t*)(fr->rf.mem + fr->rf.off[first_arg]);
  } else {
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[f->reg_types[i]].kind;
      size_t sz = jello_slot_size(k);
      uint8_t* dstp = mem + nfr->rf.off[i];
      const uint8_t* srcp = fr->rf.mem + fr->rf.off[first_arg + i];
      if(sz == 4u) *(uint32_t*)dstp = *(const uint32_t*)srcp;
      else if(sz == 8u) *(uint64_t*)dstp = *(const uint64_t*)srcp;
      else memmove(dstp, srcp, sz);
    }
  }
  return nfr;
}

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
  if(funobj && (funobj->ncaps != 0u || jello_bound_this_is_set(funobj->bound_this))) return 0u;
  for(uint32_t i = 0; i < nargs; i++) {
    uint32_t src_tid = caller_f->reg_types[first_arg + i];
    uint32_t dst_tid = callee_f->reg_types[dst_base + i];
    if(!vm_arg_types_compatible(m, src_tid, dst_tid)) return 0u;
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

  /* Self-recursive push: reuse caller's layout pointers; skip layout lookup.
   * Pointer/Dynamic frames fall through (need memset + Dynamic tagged null). */
  if(has_caller && caller_f == callee_f && !funobj && nargs <= VM_FAST_ARG_NARGS_MAX_PUSH) {
    if(caller_frame_index >= vm->call_frames_len) jello_vm_panic();
    call_frame* caller = &frames[caller_frame_index];
    if(!caller->has_pointer_or_dynamic) {
      uint32_t total = caller->rf.total;
      uint8_t* mem = vm_frame_stack_alloc(vm, total);
      call_frame* fr = &frames[vm->call_frames_len++];
      fr->f = callee_f;
      fr->pc = 0;
      fr->jit_entry_done = caller->jit_entry_done; /* already 1 if jit_ineligible */
      fr->jit_resume_hint = 0;
      fr->jit_osr_hint = 0;
      fr->jit_return_addr = NULL;
      fr->caller_dst = caller_dst;
      fr->exc_base = vm->exc_handlers_len;
      fr->has_caller = 1u;
      fr->jdll_ret_capture = 0;
      fr->has_pointer_or_dynamic = 0u;
      fr->rf.nregs = caller->rf.nregs;
      fr->rf.off = caller->rf.off;
      fr->rf.total = total;
      fr->rf.off_shared = 1u;
      fr->rf.mem = mem;
#ifndef NDEBUG
      if(first_arg + nargs > caller->rf.nregs) jello_vm_panic();
#endif
      /* Same function ⇒ same arg types; copy by callee slot size. */
      if(nargs == 1u && jello_slot_size(m->types[callee_f->reg_types[0]].kind) == 4u) {
        *(uint32_t*)(mem + fr->rf.off[0]) =
            *(const uint32_t*)(caller->rf.mem + caller->rf.off[first_arg]);
      } else {
        for(uint32_t i = 0; i < nargs; i++) {
          jello_type_kind k = m->types[callee_f->reg_types[i]].kind;
          size_t sz = jello_slot_size(k);
          uint8_t* dstp = mem + fr->rf.off[i];
          const uint8_t* srcp = caller->rf.mem + caller->rf.off[first_arg + i];
          if(sz == 4u) *(uint32_t*)dstp = *(const uint32_t*)srcp;
          else if(sz == 8u) *(uint64_t*)dstp = *(const uint64_t*)srcp;
          else memmove(dstp, srcp, sz);
        }
      }
      return 1;
    }
  }

  call_frame* fr = &frames[vm->call_frames_len++];
  fr->f = callee_f;
  fr->pc = 0;
  fr->jit_resume_hint = 0;
  fr->jit_osr_hint = 0;
  fr->jit_return_addr = NULL;
  fr->caller_dst = caller_dst;
  fr->exc_base = vm->exc_handlers_len;
  fr->has_caller = has_caller;
  fr->jdll_ret_capture = 0;
  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  /* Skip exec_loop pc==0 JIT probe for functions the JIT will never compile. */
  fr->jit_entry_done = fl->jit_ineligible ? 1u : 0u;
  fr->has_pointer_or_dynamic = fl->has_pointer_or_dynamic;
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
    uint8_t fast_args = 0u;
    if(nargs <= VM_FAST_ARG_NARGS_MAX_PUSH && !funobj) {
      fast_args = vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 0u, NULL, 0u);
    } else if(nargs <= VM_FAST_ARG_NARGS_MAX_PUSH) {
      fast_args = vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 0u, funobj, 0u);
    }
    if(fast_args) {
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
      if(caller_f == callee_f && !funobj) {
        vm_inherit_self_capture_slots(m, callee_f, &fr->rf, &caller->rf, nargs);
      }
      return 1;
    }
    vm_init_args_and_caps(vm, m, callee_f, &fr->rf, caller_f, &caller->rf, first_arg, nargs, funobj);
    if(caller_f == callee_f && !funobj) {
      vm_inherit_self_capture_slots(m, callee_f, &fr->rf, &caller->rf, nargs);
    }
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
  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  uint32_t old_total = fr->rf.total;

  uint8_t use_typed_copy = 0u;
  if(nargs <= VM_FAST_ARG_NARGS_MAX_TAIL && !funobj) {
    if(caller_f == callee_f) use_typed_copy = 1u;
    else
      use_typed_copy = vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 0u, NULL, 0u);
  } else if(nargs <= VM_FAST_ARG_NARGS_MAX_TAIL) {
    use_typed_copy = vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 0u, funobj, 0u);
  }

  uint32_t dst_base = 0u;
  uint8_t use_bound_this_only = 0u;
  if(!use_typed_copy && nargs <= VM_FAST_ARG_NARGS_MAX_TAIL && funobj &&
     jello_bound_this_is_set(funobj->bound_this) && funobj->ncaps == 0u &&
     callee_f->nregs >= 1u + nargs) {
    use_bound_this_only = vm_can_use_fast_arg_copy(m, caller_f, callee_f, first_arg, nargs, 1u, NULL, 0u);
    if(use_bound_this_only) dst_base = 1u;
  }

  if(use_typed_copy || use_bound_this_only) {
    uint8_t raw_tmp[32 * 8];
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[callee_f->reg_types[dst_base + i]].kind;
      size_t sz = jello_slot_size(k);
      memcpy(raw_tmp + i * 8, fr->rf.mem + fr->rf.off[first_arg + i], sz);
    }

    if(fl->total <= old_total) {
      if(fl->total < old_total) {
        uint32_t delta = old_total - fl->total;
        fr->rf.mem += delta;
        vm->frame_stack_top -= delta;
      }
      fr->f = callee_f;
      fr->pc = 0;
      fr->jit_entry_done = fl->jit_ineligible ? 1u : 0u;
      fr->has_pointer_or_dynamic = fl->has_pointer_or_dynamic;
      fr->jit_resume_hint = 0;
      fr->jit_osr_hint = 0;
      fr->jit_return_addr = NULL;
      fr->caller_dst = caller_dst;
      fr->exc_base = exc_base;
      fr->has_caller = has_caller;
      fr->rf.nregs = fl->nregs;
      fr->rf.off = fl->off;
      fr->rf.total = fl->total;
      fr->rf.off_shared = 1u;

      if(fr->rf.mem && fl->total && fl->has_pointer_or_dynamic) {
        memset(fr->rf.mem, 0, (size_t)fl->total);
        for(uint32_t r = 0; r < fl->nregs; r++) {
          if(m->types[callee_f->reg_types[r]].kind == JELLO_T_DYNAMIC) {
            jello_value v = jello_make_null();
            memcpy(fr->rf.mem + fl->off[r], &v, sizeof(v));
          }
        }
      }

      if(use_bound_this_only) {
        vm_store_from_boxed(vm, m, callee_f, &fr->rf, 0, funobj->bound_this);
      }
      for(uint32_t i = 0; i < nargs; i++) {
        jello_type_kind k = m->types[callee_f->reg_types[dst_base + i]].kind;
        size_t sz = jello_slot_size(k);
        memcpy(fr->rf.mem + fr->rf.off[dst_base + i], raw_tmp + i * 8, sz);
      }
      return 1;
    }

    vm_rf_release(vm, &fr->rf);

    fr->f = callee_f;
    fr->pc = 0;
    fr->jit_entry_done = fl->jit_ineligible ? 1u : 0u;
    fr->has_pointer_or_dynamic = fl->has_pointer_or_dynamic;
    fr->jit_resume_hint = 0;
    fr->jit_osr_hint = 0;
    fr->jit_return_addr = NULL;
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

    if(use_bound_this_only) {
      vm_store_from_boxed(vm, m, callee_f, &fr->rf, 0, funobj->bound_this);
    }
    for(uint32_t i = 0; i < nargs; i++) {
      jello_type_kind k = m->types[callee_f->reg_types[dst_base + i]].kind;
      size_t sz = jello_slot_size(k);
      memcpy(fr->rf.mem + fr->rf.off[dst_base + i], raw_tmp + i * 8, sz);
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

  fr->f = callee_f;
  fr->pc = 0;
  fr->jit_entry_done = fl->jit_ineligible ? 1u : 0u;
  fr->has_pointer_or_dynamic = fl->has_pointer_or_dynamic;
  fr->jit_resume_hint = 0;
  fr->jit_osr_hint = 0;
  fr->jit_return_addr = NULL;
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
  if(funobj && jello_bound_this_is_set(funobj->bound_this)) {
    vm_store_from_boxed(vm, m, callee_f, &fr->rf, 0, funobj->bound_this);
    arg_base = 1u;
  }
  for(uint32_t i = 0; i < nargs; i++) {
    vm_store_from_boxed(vm, m, callee_f, &fr->rf, arg_base + i, tmp[i]);
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
        vm_store_from_boxed(vm, m, callee_f, &fr->rf, cap_start + i, funobj->caps[i]);
      }
    }
  }
  return 1;
}

/* Phase 4: push a frame with args from jello_value* (for chunk exec with pre-bound imports).
 * Stores args[0..nargs-1] into callee regs 0..nargs-1. */
static void vm_init_closure_args_from_values(jello_vm* vm,
                                             const jello_bc_module* m,
                                             const jello_bc_function* callee_f,
                                             reg_frame* callee_rf,
                                             const jello_function* funobj,
                                             const jello_value* args,
                                             uint32_t nargs);

static int vm_push_frame_entry(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* callee_f,
                               const jello_function* funobj, const jello_value* args, uint32_t nargs) {
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
  fr->jit_resume_hint = 0;
  fr->jit_osr_hint = 0;
  fr->jit_return_addr = NULL;
  fr->caller_dst = 0;
  fr->exc_base = vm->exc_handlers_len;
  fr->has_caller = 0;
  fr->jdll_ret_capture = 0;

  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  fr->jit_entry_done = fl->jit_ineligible ? 1u : 0u;
  fr->has_pointer_or_dynamic = fl->has_pointer_or_dynamic;
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

  if(funobj) {
    vm_init_closure_args_from_values(vm, m, callee_f, &fr->rf, funobj, args, nargs);
  } else {
    for(uint32_t i = 0; i < nargs; i++) {
      vm_store_from_boxed(vm, m, callee_f, &fr->rf, i, args[i]);
    }
  }
  return 1;
}

int vm_push_frame_from_values(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* callee_f,
                              const jello_value* args, uint32_t nargs) {
  return vm_push_frame_entry(vm, m, callee_f, NULL, args, nargs);
}

int vm_push_frame_closure_from_values(jello_vm* vm, const jello_bc_module* m, jello_function* fn,
                                      const jello_value* args, uint32_t nargs) {
  if(!fn || fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) return 0;
  uint32_t fi = fn->func_index;
  if(jello_is_native_builtin(fi) || jello_is_jdll_prim(fi)) return 0;
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) return 0;
  return vm_push_frame_entry(vm, m, &m->funcs[bytecode_idx], fn, args, nargs);
}

static void vm_init_closure_args_from_values(jello_vm* vm,
                                             const jello_bc_module* m,
                                             const jello_bc_function* callee_f,
                                             reg_frame* callee_rf,
                                             const jello_function* funobj,
                                             const jello_value* args,
                                             uint32_t nargs) {
  uint32_t arg_base = 0;
  if(funobj && jello_bound_this_is_set(funobj->bound_this)) {
    if(callee_rf->nregs < 1) jello_vm_panic();
    vm_store_from_boxed(vm, m, callee_f, callee_rf, 0, funobj->bound_this);
    arg_base = 1;
  }
  if(arg_base + nargs > callee_rf->nregs) jello_vm_panic();
  for(uint32_t i = 0; i < nargs; i++) {
    vm_store_from_boxed(vm, m, callee_f, callee_rf, arg_base + i, args[i]);
  }
  if(funobj && funobj->ncaps) {
    if(funobj->ncaps > callee_rf->nregs) jello_vm_panic();
    uint32_t cap_start = (m->features & (uint32_t)JELLO_BC_FEAT_CAP_START) && callee_f->cap_start < callee_rf->nregs
      ? callee_f->cap_start
      : callee_rf->nregs - funobj->ncaps;
    if(cap_start < arg_base + nargs) jello_vm_panic();
    if(funobj->caps_are_raw) {
      const uint8_t* raw = (const uint8_t*)&funobj->caps[0];
      uint32_t off = 0;
      for(uint32_t i = 0; i < funobj->ncaps; i++) {
        jello_type_kind k = m->types[callee_f->reg_types[cap_start + i]].kind;
        size_t sz = jello_slot_size(k);
        memcpy(callee_rf->mem + callee_rf->off[cap_start + i], raw + off, sz);
        off += (uint32_t)sz;
      }
    } else {
      for(uint32_t i = 0; i < funobj->ncaps; i++) {
        vm_store_from_boxed(vm, m, callee_f, callee_rf, cap_start + i, funobj->caps[i]);
      }
    }
  }
}

int vm_push_frame_jello_call(jello_vm* vm, const jello_bc_module* m, jello_function* fn,
                             jello_value bound_this, const jello_value* args, uint32_t nargs,
                             uint8_t jdll_ret_capture) {
  if(!fn || fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) return 0;
  uint32_t fi = fn->func_index;
  if(jello_is_native_builtin(fi) || jello_is_jdll_prim(fi)) return 0;
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) return 0;
  const jello_bc_function* callee_f = &m->funcs[bytecode_idx];

  jello_function* call_fn = fn;
  uint8_t bound_tmp = 0;
  if(jello_bound_this_is_set(bound_this)) {
    call_fn = jello_function_bind_this(vm, fn->h.type_id, fn, bound_this);
    if(!call_fn) return 0;
    bound_tmp = 1;
  }

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

  call_frame* fr = &frames[vm->call_frames_len++];
  fr->f = callee_f;
  fr->pc = 0;
  fr->jit_resume_hint = 0;
  fr->jit_osr_hint = 0;
  fr->jit_return_addr = NULL;
  fr->caller_dst = 0;
  fr->exc_base = vm->exc_handlers_len;
  fr->has_caller = 1;
  fr->jdll_ret_capture = jdll_ret_capture ? 1u : 0u;

  const frame_layout* fl = vm_get_frame_layout(vm, m, callee_f);
  fr->jit_entry_done = fl->jit_ineligible ? 1u : 0u;
  fr->has_pointer_or_dynamic = fl->has_pointer_or_dynamic;
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

  if(bound_tmp) {
    jello_gc_push_root(vm, jello_from_ptr(call_fn));
  }
  vm_init_closure_args_from_values(vm, m, callee_f, &fr->rf, call_fn, args, nargs);
  if(bound_tmp) {
    jello_gc_pop_roots(vm, 1);
  }
  return 1;
}
