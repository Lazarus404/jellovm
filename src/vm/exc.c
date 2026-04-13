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

#include <stdlib.h>

void jello_vm_panic(void) {
  abort();
}

jello_exec_status jello_vm_trap(jello_vm* vm, jello_trap_code code, const char* msg) {
  if(vm) {
    vm->trap_code = code;
    vm->trap_msg = msg;
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

int vm_exc_dispatch(jello_vm* vm, jello_value* out) {
  if(!vm->exc_pending) return 0;
  if(vm->exc_handlers_len == 0) {
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
    if(frames[idx].exc_base > vm->exc_handlers_len) jello_vm_panic();
    vm->exc_handlers_len = frames[idx].exc_base;
    vm_rf_release(vm, &frames[idx].rf);
    vm->call_frames_len--;
  }
  if(vm->call_frames_len == 0) jello_vm_panic();
  call_frame* frames = (call_frame*)vm->call_frames;
  call_frame* target = &frames[h.frame_index];
  target->pc = h.catch_pc;
  vm_store_val(&target->rf, h.dst_reg, vm->exc_payload);
  vm->exc_pending = 0;
  vm->exc_payload = jello_make_null();
  jello_vm_clear_trap(vm);
  return 0;
}
