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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
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

#include <jello.h>
#include <jello/internal.h>

#include <string.h>
#include <stdlib.h>

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
  return vm;
}

void jello_vm_destroy(jello_vm* vm) {
  if (!vm) return;
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

  free(vm->exc_handlers);
  vm->exc_handlers = NULL;
  vm->exc_handlers_len = 0;
  vm->exc_handlers_cap = 0;
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();

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

void jello_vm_set_max_bytes_len(jello_vm* vm, uint32_t max_len) {
  if(!vm) return;
  vm->max_bytes_len = max_len;
}

void jello_vm_set_max_array_len(jello_vm* vm, uint32_t max_len) {
  if(!vm) return;
  vm->max_array_len = max_len;
}

