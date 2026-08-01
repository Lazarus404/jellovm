// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/jdll_internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void jello_vm_panic(void) {
  abort();
}

static const char* trap_source_basename(const char* path) {
  if(!path) return NULL;
  const char* slash = strrchr(path, '/');
  if(!slash) slash = strrchr(path, '\\');
  return slash ? slash + 1 : path;
}

static int trap_msg_has_location(const char* msg) {
  return msg && strstr(msg, ".jello:") != NULL;
}

static void format_trap_loc(char* buf, size_t cap, const char* file, uint32_t loc) {
  uint16_t line = (uint16_t)(loc & 0xFFFFu);
  uint16_t col = (uint16_t)(loc >> 16);
  if(col > 0u) {
    snprintf(buf, cap, "%s:%u:%u: ", file, (unsigned)line, (unsigned)col);
  } else {
    snprintf(buf, cap, "%s:%u: ", file, (unsigned)line);
  }
}

static void maybe_prefix_trap_location(jello_vm* vm, const char* msg) {
  if(!vm || trap_msg_has_location(msg)) return;
  if(!vm->running_module || !vm->call_frames || vm->call_frames_len == 0u) return;
  const jello_bc_module* mod = vm->running_module;
  if(!(mod->features & (uint32_t)JELLO_BC_FEAT_LINE_TABLE) || mod->nsource_files == 0u) return;
  call_frame* fr = &((call_frame*)vm->call_frames)[vm->call_frames_len - 1u];
  const jello_bc_function* f = fr->f;
  if(!f || !f->lines || f->nlines != f->ninsns || fr->pc == 0u) return;
  uint32_t pc = fr->pc - 1u;
  if(pc >= f->nlines) return;
  uint32_t loc = f->lines[pc];
  uint16_t line = (uint16_t)(loc & 0xFFFFu);
  if(line == 0u) return;
  if(f->source_file >= mod->nsource_files || !mod->source_files[f->source_file]) return;
  const char* file = trap_source_basename(mod->source_files[f->source_file]);
  char prefix[512];
  format_trap_loc(prefix, sizeof prefix, file, loc);
  snprintf(vm->trap_msg_buf, sizeof vm->trap_msg_buf, "%s%s", prefix, msg);
  vm->trap_msg = vm->trap_msg_buf;
}

jello_exec_status jello_vm_trap(jello_vm* vm, jello_trap_code code, const char* msg) {
  if(vm) {
    vm->trap_code = code;
    if(msg) {
      snprintf(vm->trap_msg_buf, sizeof vm->trap_msg_buf, "%s", msg);
      vm->trap_msg = vm->trap_msg_buf;
      maybe_prefix_trap_location(vm, msg);
    } else {
      vm->trap_msg_buf[0] = 0;
      vm->trap_msg = NULL;
    }
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)code);
  }
  return JELLO_EXEC_TRAP;
}

void vm_exc_push(jello_vm* vm, uint32_t frame_index, uint32_t catch_pc, uint32_t dst_reg, uint8_t trap_only) {
  exc_handler* hs = (exc_handler*)vm->exc_handlers;
  if(vm->exc_handlers_len == vm->exc_handlers_cap) {
    uint32_t ncap = vm->exc_handlers_cap ? (vm->exc_handlers_cap * 2u) : 16u;
    exc_handler* nh = (exc_handler*)realloc(hs, sizeof(exc_handler) * (size_t)ncap);
    if(!nh) jello_vm_panic();
    hs = nh;
    vm->exc_handlers = nh;
    vm->exc_handlers_cap = ncap;
  }
  exc_handler* h = &hs[vm->exc_handlers_len++];
  h->frame_index = frame_index;
  h->catch_pc = catch_pc;
  h->dst_reg = dst_reg;
  h->trap_only = trap_only ? 1u : 0u;
}

int vm_exc_pop(jello_vm* vm, exc_handler* out) {
  if(vm->exc_handlers_len == 0) return 0;
  exc_handler* hs = (exc_handler*)vm->exc_handlers;
  *out = hs[--vm->exc_handlers_len];
  return 1;
}

void vm_exc_pop_for_frame(jello_vm* vm, uint32_t frame_index) {
  exc_handler* hs = (exc_handler*)vm->exc_handlers;
  while(vm->exc_handlers_len > 0 && hs[vm->exc_handlers_len - 1u].frame_index == frame_index) {
    vm->exc_handlers_len--;
  }
}

void vm_unwind_all_frames(jello_vm* vm) {
  if(!vm || !vm->call_frames) return;
  call_frame* frames = (call_frame*)vm->call_frames;
  /* Release in LIFO order (top first) for frame stack compatibility. */
  for(uint32_t i = vm->call_frames_len; i > 0; i--) {
    vm_rf_release(vm, &frames[i - 1u].rf);
  }
  free(vm->call_frames);
  vm->call_frames = NULL;
  vm->call_frames_len = 0;
  vm->call_frames_cap = 0;
}

static void vm_capture_uncaught_stack(jello_vm* vm) {
  vm->stack_trace_buf[0] = 0;
  if(jello_vm_format_stack_trace(vm, vm->stack_trace_buf, sizeof vm->stack_trace_buf) <= 0) {
    vm->stack_trace_buf[0] = 0;
  }
}

static const char* trap_kind_name(jello_trap_code code) {
  switch(code) {
    case JELLO_TRAP_TYPE_MISMATCH: return "TypeMismatch";
    case JELLO_TRAP_BOUNDS: return "Bounds";
    case JELLO_TRAP_NULL_DEREF: return "NullDeref";
    case JELLO_TRAP_THROWN: return "Thrown";
    case JELLO_TRAP_STACK_OVERFLOW: return "StackOverflow";
    case JELLO_TRAP_FUEL: return "Fuel";
    case JELLO_TRAP_LIMIT: return "Limit";
    default: return "Trap";
  }
}

static int catch_payload_is_user_throw(jello_vm* vm) {
  if(vm->trap_code != JELLO_TRAP_THROWN) return 0;
  if(!jello_is_i32(vm->exc_payload)) return 1;
  int32_t v = jello_as_i32(vm->exc_payload);
  return v != (int32_t)JELLO_TRAP_THROWN;
}

static uint32_t module_object_type_id(const jello_bc_module* m) {
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_OBJECT) return i;
  }
  return 0u;
}

static uint32_t module_bytes_type_id(const jello_bc_module* m) {
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) return i;
  }
  return 0u;
}

uint32_t vm_module_atom_id_or_default(const jello_bc_module* m, const char* name, uint32_t def) {
  if(!m || !m->atoms) return def;
  for(uint32_t i = 0; i < m->natoms; i++) {
    if(m->atoms[i] && strcmp(m->atoms[i], name) == 0) return i;
  }
  return def;
}

/* Build the catch payload for VM traps (not user throw).
 * Contract: object with Bytes "kind", optional Bytes "message", I32 "code".
 * Atom slots are resolved from the running module table (see JELLO_ATOM_*). */
static jello_value vm_wrap_catch_payload(jello_vm* vm) {
  if(catch_payload_is_user_throw(vm)) return vm->exc_payload;
  const jello_bc_module* mod = vm->running_module;
  if(!mod) return vm->exc_payload;
  uint32_t obj_tid = module_object_type_id(mod);
  jello_object* o = jello_object_new(vm, obj_tid);
  const char* kind = trap_kind_name(vm->trap_code);
  if(vm->trap_code == JELLO_TRAP_THROWN &&
     jello_is_i32(vm->exc_payload) &&
     jello_as_i32(vm->exc_payload) == (int32_t)JELLO_TRAP_THROWN) {
    kind = "Assert";
  }
  uint32_t kind_atom = vm_module_atom_id_or_default(mod, "kind", JELLO_ATOM_KIND);
  uint32_t msg_atom = vm_module_atom_id_or_default(mod, "message", JELLO_ATOM_MESSAGE);
  uint32_t code_atom = vm_module_atom_id_or_default(mod, "code", JELLO_ATOM_CODE);
  uint32_t bytes_tid = module_bytes_type_id(mod);
  jello_bytes* kb = jello_bytes_new(vm, bytes_tid, (uint32_t)strlen(kind));
  if(kb) memcpy(kb->data, kind, strlen(kind));
  jello_object_set(o, kind_atom, jello_from_ptr(kb));
  if(vm->trap_msg) {
    size_t mlen = strlen(vm->trap_msg);
    jello_bytes* mb = jello_bytes_new(vm, bytes_tid, (uint32_t)mlen);
    if(mb && mlen) memcpy(mb->data, vm->trap_msg, mlen);
    jello_object_set(o, msg_atom, jello_from_ptr(mb));
  }
  jello_object_set(o, code_atom, jello_make_i32((int32_t)vm->trap_code));
  return jello_from_ptr(o);
}

int vm_exc_dispatch(jello_vm* vm, jello_value* out) {
  if(!vm->exc_pending) return 0;
  if(vm->exc_handlers_len == 0) {
    vm_capture_uncaught_stack(vm);
    vm_unwind_all_frames(vm);
    free(vm->exc_handlers);
    vm->exc_handlers = NULL;
    vm->exc_handlers_len = 0;
    vm->exc_handlers_cap = 0;
    return 1;
  }
  exc_handler h;
  // Pop handlers until we find one that is willing to catch this trap.
  for(;;) {
    if(vm->exc_handlers_len == 0) {
      vm_capture_uncaught_stack(vm);
      vm_unwind_all_frames(vm);
      free(vm->exc_handlers);
      vm->exc_handlers = NULL;
      vm->exc_handlers_len = 0;
      vm->exc_handlers_cap = 0;
      return 1;
    }
    if(!vm_exc_pop(vm, &h)) jello_vm_panic();
    if(vm->trap_code == JELLO_TRAP_THROWN && h.trap_only) {
      // Trap-only handler: ignore thrown exceptions/asserts.
      continue;
    }
    break;
  }
  while(vm->call_frames_len - 1u > h.frame_index) {
    uint32_t idx = vm->call_frames_len - 1u;
    call_frame* frames = (call_frame*)vm->call_frames;
    vm_exc_pop_for_frame(vm, idx);
    vm_rf_release(vm, &frames[idx].rf);
    vm->call_frames_len--;
  }
  if(vm->call_frames_len == 0) jello_vm_panic();
  call_frame* frames = (call_frame*)vm->call_frames;
  call_frame* target = &frames[h.frame_index];
  target->pc = h.catch_pc;
  jello_value payload = vm_wrap_catch_payload(vm);
  vm_store_val(&target->rf, h.dst_reg, payload);
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();
  jello_vm_clear_trap(vm);
  return 0;
}
