// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/jdll_internal.h>

#include <stdint.h>
#include <stdlib.h>

static void vm_capture_trap_stack(jello_vm* vm, jello_exec_status st) {
  if(st == JELLO_EXEC_OK) return;
  if(vm->stack_trace_buf[0] == 0 && vm->call_frames && vm->call_frames_len > 0u) {
    if(jello_vm_format_stack_trace(vm, vm->stack_trace_buf, sizeof vm->stack_trace_buf) <= 0) {
      vm->stack_trace_buf[0] = 0;
    }
  }
  vm_unwind_all_frames(vm);
}

static jello_exec_status exec_entry(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* entry,
    jello_value* out, jello_value* out_exports, uint32_t entry_module_idx) {
  /* Reset per-run fuel. */
  vm->fuel_remaining = vm->fuel_limit;

  vm->call_frames = NULL;
  vm->call_frames_len = 0;
  vm->call_frames_cap = 0;
  free(vm->const_fun_cache);
  vm->const_fun_cache = NULL;
  vm->const_fun_cache_len = 0;
  vm_enum_nullary_cache_clear(vm);
  if(m && m->nfuncs) {
    size_t cache_len = (size_t)JELLO_NATIVE_BUILTIN_COUNT + (size_t)m->nfuncs;
    vm->const_fun_cache = calloc(cache_len, sizeof(void*));
    if(!vm->const_fun_cache) jello_vm_panic();
    vm->const_fun_cache_len = (uint32_t)cache_len;
  }
  vm_enum_nullary_cache_init(vm, m ? m->ntypes : 0u);
  free(vm->exc_handlers);
  vm->exc_handlers = NULL;
  vm->exc_handlers_len = 0;
  vm->exc_handlers_cap = 0;
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();
  vm_call_frames_reserve(vm);
  if(!vm_push_frame(vm, m, entry, entry, 0, 0, 0, 0, NULL, 0)) return JELLO_EXEC_TRAP;

  exec_ctx ctx = {
    .vm = vm,
    .m = m,
    .out = out,
    .out_exports = out_exports,
    .entry_module_idx = entry_module_idx,
    .min_call_frames = 0,
  };
  return vm_exec_loop(&ctx);
}

jello_exec_status jello_vm_exec_status(jello_vm* vm, const jello_bc_module* m, jello_value* out) {
  return jello_vm_exec_status_exports(vm, m, UINT32_MAX, out, NULL);
}

jello_exec_status jello_vm_exec_status_exports(jello_vm* vm, const jello_bc_module* m,
    uint32_t entry_module_idx, jello_value* out, jello_value* out_exports) {
  if(!vm || !m || !m->funcs || m->entry >= m->nfuncs) jello_vm_panic();
  jello_vm_clear_trap(vm);
  const jello_bc_function* f = &m->funcs[m->entry];
  jello_gc_init(vm);
  vm->running_module = m;
  jello_vm_bind_current(vm);
  jello_exec_status st = exec_entry(vm, m, f, out, out_exports, entry_module_idx);
  vm_capture_trap_stack(vm, st);
  vm->running_module = NULL;
  return st;
}

jello_exec_status jello_vm_exec_status_chunk(jello_vm* vm, const jello_bc_module* m,
    uint32_t entry_func_index, const jello_value* args, uint32_t nargs,
    uint32_t entry_module_idx, jello_value* out, jello_value* out_exports) {
  if(!vm || !m || !m->funcs || entry_func_index >= m->nfuncs) jello_vm_panic();
  if(!args && nargs > 0) jello_vm_panic();
  jello_vm_clear_trap(vm);
  const jello_bc_function* f = &m->funcs[entry_func_index];
  jello_gc_init(vm);
  vm->running_module = m;
  vm->fuel_remaining = vm->fuel_limit;
  vm->call_frames = NULL;
  vm->call_frames_len = 0;
  vm->call_frames_cap = 0;
  free(vm->const_fun_cache);
  vm->const_fun_cache = NULL;
  vm->const_fun_cache_len = 0;
  vm_enum_nullary_cache_clear(vm);
  if(m && m->nfuncs) {
    size_t cache_len = (size_t)JELLO_NATIVE_BUILTIN_COUNT + (size_t)m->nfuncs;
    vm->const_fun_cache = calloc(cache_len, sizeof(void*));
    if(!vm->const_fun_cache) jello_vm_panic();
    vm->const_fun_cache_len = (uint32_t)cache_len;
  }
  vm_enum_nullary_cache_init(vm, m ? m->ntypes : 0u);
  free(vm->exc_handlers);
  vm->exc_handlers = NULL;
  vm->exc_handlers_len = 0;
  vm->exc_handlers_cap = 0;
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();
  vm_call_frames_reserve(vm);
  if(!vm_push_frame_from_values(vm, m, f, args, nargs)) {
    vm->running_module = NULL;
    return JELLO_EXEC_TRAP;
  }
  exec_ctx ctx = {
    .vm = vm,
    .m = m,
    .out = out,
    .out_exports = out_exports,
    .entry_module_idx = entry_module_idx,
    .min_call_frames = 0,
  };
  jello_vm_bind_current(vm);
  jello_exec_status st = vm_exec_loop(&ctx);
  vm_capture_trap_stack(vm, st);
  vm->running_module = NULL;
  return st;
}

jello_exec_status jello_vm_exec_status_closure(jello_vm* vm, const jello_bc_module* m, jello_function* fn,
                                               const jello_value* args, uint32_t nargs, jello_value* out) {
  if(!vm || !m || !fn || !out) jello_vm_panic();
  if(!args && nargs > 0) jello_vm_panic();
  jello_vm_clear_trap(vm);
  jello_gc_init(vm);
  vm->running_module = m;
  vm->fuel_remaining = vm->fuel_limit;
  vm->call_frames = NULL;
  vm->call_frames_len = 0;
  vm->call_frames_cap = 0;
  free(vm->const_fun_cache);
  vm->const_fun_cache = NULL;
  vm->const_fun_cache_len = 0;
  vm_enum_nullary_cache_clear(vm);
  if(m && m->nfuncs) {
    size_t cache_len = (size_t)JELLO_NATIVE_BUILTIN_COUNT + (size_t)m->nfuncs;
    vm->const_fun_cache = calloc(cache_len, sizeof(void*));
    if(!vm->const_fun_cache) jello_vm_panic();
    vm->const_fun_cache_len = (uint32_t)cache_len;
  }
  vm_enum_nullary_cache_init(vm, m ? m->ntypes : 0u);
  free(vm->exc_handlers);
  vm->exc_handlers = NULL;
  vm->exc_handlers_len = 0;
  vm->exc_handlers_cap = 0;
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();
  vm_call_frames_reserve(vm);
  if(!vm_push_frame_closure_from_values(vm, m, fn, args, nargs)) {
    vm->running_module = NULL;
    return JELLO_EXEC_TRAP;
  }
  exec_ctx ctx = {
    .vm = vm,
    .m = m,
    .out = out,
    .out_exports = NULL,
    .entry_module_idx = UINT32_MAX,
    .min_call_frames = 0,
  };
  jello_vm_bind_current(vm);
  jello_exec_status st = vm_exec_loop(&ctx);
  vm_capture_trap_stack(vm, st);
  vm->running_module = NULL;
  return st;
}

jello_value jello_vm_exec(jello_vm* vm, const jello_bc_module* m) {
  jello_value out = jello_make_null();
  jello_exec_status st = jello_vm_exec_status(vm, m, &out);
  if(st != JELLO_EXEC_OK) jello_vm_panic();
  return out;
}
