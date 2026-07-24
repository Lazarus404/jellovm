// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello.h>
#include <jello/internal.h>
#include <jello/internal/jit_internal.h>

#include <string.h>
#include <stdlib.h>

/**
 * This is a static-library linker workaround;
 * a deliberately exported symbol so libjellovm.a
 * is less likely to be dropped or treated as
 * empty when something links against it.
 */
int jellovm_dummy_symbol_to_keep_archive(void);
int jellovm_dummy_symbol_to_keep_archive(void) {
	return (int)JELLO_VERSION;
}

jello_vm* jello_vm_create(void) {
  jello_vm* vm = (jello_vm*)calloc(1, sizeof(jello_vm));
  if (!vm) return NULL;
  vm->call_frames_max = 100000u;
  vm->fuel_limit = 0;
  vm->fuel_remaining = 0;
  vm->max_bytes_len = 0;
  vm->max_array_len = 0;
  vm->thread_id = 1u;
  jello_jit_init(vm);
  return vm;
}

void jello_vm_set_jit_enabled(jello_vm* vm, int enabled) {
  if(!vm) return;
  vm->jit_enabled = enabled ? 1u : 0u;
}

int jello_vm_jit_enabled(const jello_vm* vm) {
  if(!vm) return 0;
  return vm->jit_enabled ? 1 : 0;
}

void jello_vm_destroy(jello_vm* vm) {
  if (!vm) return;
  jello_jit_shutdown(vm);
  jello_vm_profile_free(vm);
  jello_gc_shutdown(vm);
  vm_frame_cache_shutdown(vm);
  free(vm->spill);
  vm->spill = NULL;
  vm->spill_len = 0;
  vm->spill_cap = 0;

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
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();

  free(vm->entry_path);
  vm->entry_path = NULL;

  jello_vm_clear_program_args(vm);

  vm->running_module = NULL;

  free(vm);
}

void jello_vm_clear_trap(jello_vm* vm) {
  if (!vm) return;
  vm->trap_code = JELLO_TRAP_NONE;
  vm->trap_msg = NULL;
}

jello_trap_code jello_vm_last_trap_code(const jello_vm* vm) {
  return vm ? vm->trap_code : JELLO_TRAP_NONE;
}

const char* jello_vm_last_trap_msg(const jello_vm* vm) {
  return vm ? vm->trap_msg : NULL;
}

void jello_vm_set_fuel(jello_vm* vm, uint64_t fuel) {
  if(!vm) return;
  vm->fuel_limit = fuel;
  vm->fuel_remaining = fuel;
}

int jello_vm_fuel_charge(jello_vm* vm) {
  if(!vm || !vm->fuel_limit) return 0;
  if(vm->fuel_remaining == 0) {
    (void)jello_vm_trap(vm, JELLO_TRAP_FUEL, "instruction limit exceeded");
    return -1;
  }
  vm->fuel_remaining--;
  return 0;
}

void jello_vm_set_max_bytes_len(jello_vm* vm, uint32_t max_len) {
  if(!vm) return;
  vm->max_bytes_len = max_len;
}

void jello_vm_set_max_array_len(jello_vm* vm, uint32_t max_len) {
  if(!vm) return;
  vm->max_array_len = max_len;
}

void jello_vm_set_entry_path(jello_vm* vm, const char* entry_jlo_path) {
  if(!vm) return;
  free(vm->entry_path);
  vm->entry_path = NULL;
  if(entry_jlo_path && *entry_jlo_path) {
    vm->entry_path = strdup(entry_jlo_path);
  }
}

void jello_vm_clear_program_args(jello_vm* vm) {
  if(!vm) return;
  if(vm->program_argv) {
    for(uint32_t i = 0; i < vm->program_argc; i++) {
      free(vm->program_argv[i]);
    }
    free(vm->program_argv);
  }
  vm->program_argv = NULL;
  vm->program_argc = 0;
}

void jello_vm_set_program_args(jello_vm* vm, int argc, char** argv) {
  if(!vm) return;
  jello_vm_clear_program_args(vm);
  if(argc <= 0 || !argv) return;
  vm->program_argc = (uint32_t)argc;
  vm->program_argv = (char**)calloc((size_t)argc, sizeof(char*));
  if(!vm->program_argv) {
    vm->program_argc = 0;
    return;
  }
  for(int i = 0; i < argc; i++) {
    const char* s = argv[i] ? argv[i] : "";
    vm->program_argv[i] = strdup(s);
    if(!vm->program_argv[i]) {
      jello_vm_clear_program_args(vm);
      return;
    }
  }
}

