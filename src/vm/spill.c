// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <stdlib.h>

void vm_spill_push(jello_vm* vm, jello_value v) {
  if(vm->spill_len == vm->spill_cap) {
    uint32_t new_cap = vm->spill_cap ? vm->spill_cap * 2u : 16u;
    jello_value* nv = (jello_value*)realloc(vm->spill, (size_t)new_cap * sizeof(jello_value));
    if(!nv) jello_vm_panic();
    vm->spill = nv;
    vm->spill_cap = new_cap;
  }
  vm->spill[vm->spill_len++] = v;
}

jello_value vm_spill_pop(jello_vm* vm) {
  if(vm->spill_len == 0) jello_vm_panic();
  return vm->spill[--vm->spill_len];
}
