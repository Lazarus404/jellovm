// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_internal.h>

void jello_jit_init(jello_vm* vm) {
  if(vm) vm->jit_enabled = 0u;
}

void jello_jit_shutdown(jello_vm* vm) {
  (void)vm;
}

void jello_jit_on_enter(exec_ctx* ctx) {
  (void)ctx;
}

void jello_jit_on_backedge(exec_ctx* ctx) {
  (void)ctx;
}

int jello_jit_func_needs_enter(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f) {
  (void)vm;
  (void)m;
  (void)f;
  return 0;
}

int jello_jit_func_is_compiled(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f) {
  (void)vm;
  (void)m;
  (void)f;
  return 0;
}

int jello_jit_try_enter(exec_ctx* ctx) {
  (void)ctx;
  return 0;
}

void jello_jit_module_unloaded(jello_vm* vm, const jello_bc_module* m) {
  (void)vm;
  (void)m;
}
