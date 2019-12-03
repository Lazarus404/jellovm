// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int vm_arg_types_compatible(const jello_bc_module* m, uint32_t src_tid, uint32_t dst_tid) {
  if(src_tid == dst_tid) return 1;
  if(src_tid >= m->ntypes || dst_tid >= m->ntypes) return 0;
  return m->types[src_tid].kind == m->types[dst_tid].kind;
}

void vm_copy_arg_strict(jello_vm* vm,
                        const jello_bc_module* m,
                        const jello_bc_function* caller_f, reg_frame* caller_rf, uint32_t src,
                        const jello_bc_function* callee_f, reg_frame* callee_rf, uint32_t dst) {
  uint32_t src_tid = caller_f->reg_types[src];
  uint32_t dst_tid = callee_f->reg_types[dst];
  if(src_tid >= m->ntypes || dst_tid >= m->ntypes) jello_vm_panic();

  jello_type_kind sk = m->types[src_tid].kind;
  jello_type_kind dk = m->types[dst_tid].kind;

  if(src_tid == dst_tid || sk == dk) {
    jello_type_kind k = dk;

    uint8_t* dstp = callee_rf->mem + callee_rf->off[dst];
    const uint8_t* srcp = caller_rf->mem + caller_rf->off[src];

    if(k == JELLO_T_BOOL || k == JELLO_T_ATOM || k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32) {
      *(uint32_t*)dstp = *(const uint32_t*)srcp;
      return;
    }

    size_t sz = jello_slot_size(k);
    memmove(dstp, srcp, sz);
    return;
  }

  if(sk == JELLO_T_DYNAMIC) {
    jello_value v = vm_load_val(caller_rf, src);
    vm_store_from_boxed(vm, m, callee_f, callee_rf, dst, v);
    return;
  }

  if(dk == JELLO_T_DYNAMIC) {
    jello_value v = vm_box_from_typed(vm, m, caller_f, caller_rf, src);
    vm_store_val(callee_rf, dst, v);
    return;
  }

  (void)vm;
  jello_vm_panic();
}

void vm_init_args_and_caps(jello_vm* vm, const jello_bc_module* m,
                           const jello_bc_function* callee_f, reg_frame* callee_rf,
                           const jello_bc_function* caller_f, reg_frame* caller_rf,
                           uint32_t first_arg, uint32_t nargs,
                           const jello_function* funobj) {
  uint32_t arg_base = 0;
  if(funobj && jello_bound_this_is_set(funobj->bound_this)) {
    if(callee_rf->nregs < 1) jello_vm_panic();
    vm_store_from_boxed(vm, m, callee_f, callee_rf, 0, funobj->bound_this);
    arg_base = 1;
  }

  if(arg_base + nargs > callee_rf->nregs) jello_vm_panic();
  for(uint32_t i = 0; i < nargs; i++) {
    vm_copy_arg_strict(vm, m, caller_f, caller_rf, first_arg + i, callee_f, callee_rf, arg_base + i);
  }

  if(funobj && funobj->ncaps) {
    if(funobj->ncaps > callee_rf->nregs) jello_vm_panic();
    uint32_t cap_start = (m->features & (uint32_t)JELLO_BC_FEAT_CAP_START) && callee_f->cap_start < callee_rf->nregs
      ? callee_f->cap_start
      : callee_rf->nregs - funobj->ncaps;
    if(getenv("JELLO_TRACE_CLOSURE")) {
      uint32_t callee_idx = (uint32_t)(callee_f - m->funcs);
      fprintf(stderr, "[JELLO_TRACE] vm_init_args_and_caps: callee_func=%u ncaps=%u cap_start=%u nregs=%u feat=%u callee_f->cap_start=%u\n",
              (unsigned)callee_idx, (unsigned)funobj->ncaps, (unsigned)cap_start,
              (unsigned)callee_rf->nregs, (unsigned)((m->features & (uint32_t)JELLO_BC_FEAT_CAP_START) ? 1 : 0),
              (unsigned)callee_f->cap_start);
    }
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
