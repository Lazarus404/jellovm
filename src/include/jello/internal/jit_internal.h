// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

// VM ↔ JIT integration hooks (init, try_enter, module unload).
// JIT subsystem internals live in jit_impl.h.
#ifndef JELLO_INTERNAL_JIT_INTERNAL_H
#define JELLO_INTERNAL_JIT_INTERNAL_H

#include <jello/internal/vm_internal.h>

void jello_jit_init(jello_vm* vm);
void jello_jit_shutdown(jello_vm* vm);

void jello_jit_on_enter(exec_ctx* ctx);
void jello_jit_on_backedge(exec_ctx* ctx);
int jello_jit_try_enter(exec_ctx* ctx);
int jello_jit_func_needs_enter(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f);
int jello_jit_func_is_compiled(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f);
void jello_jit_module_unloaded(jello_vm* vm, const jello_bc_module* m);

#endif
