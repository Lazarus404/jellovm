// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#ifndef JELLO_JDLL_INTERNAL_H
#define JELLO_JDLL_INTERNAL_H

#include <jello/internal.h>
#include <stdint.h>

#define JELLO_FUNC_INDEX_JDLL_PRIM 0xFFFFFFFEu

typedef struct jdlo_ctx {
  exec_ctx* xctx;
  uint32_t dst_reg;
  uint32_t first_arg_reg;
  uint32_t nargs;
  const jello_bc_function* caller_f;
  uint32_t caller_frame_idx;
  uint8_t ret_kind;
  uint8_t failed;
  const char* fail_msg;
} jdlo_ctx;

static inline call_frame* jdl_caller_frame(jdlo_ctx* c) {
  if(!c || !c->xctx || !c->xctx->vm) return NULL;
  jello_vm* vm = c->xctx->vm;
  call_frame* frames = (call_frame*)vm->call_frames;
  if(!frames || c->caller_frame_idx >= vm->call_frames_len) return NULL;
  return &frames[c->caller_frame_idx];
}

static inline reg_frame* jdl_caller_rf(jdlo_ctx* c) {
  call_frame* fr = jdl_caller_frame(c);
  return fr ? &fr->rf : NULL;
}

typedef void (*jdll_export_fn)(jdlo_ctx* c);

#define JDLL_ABI_VARARGS_ARITY 255u
#define JDLL_ABI_MIXED_VARARGS_ARITY 254u

typedef struct jdll_prim_caps {
  jdll_export_fn fn;
  uint8_t ret_kind;
  uint8_t arity;
  uint8_t arg_kinds[8];
  const char* library_id;
  const char* export_name;
  const char* export_sym;
} jdll_prim_caps;

int jello_vm_format_stack_trace(const jello_vm* vm, char* buf, size_t cap);
void jello_vm_print_stack_trace(const jello_vm* vm, FILE* out);
void jello_jdll_set_active_prim(jello_vm* vm, const jdll_prim_caps* caps);
void jello_jdll_clear_active_prim(jello_vm* vm);

int jello_jdll_fill_exports(exec_ctx* ctx, uint32_t exports_reg, uint32_t key_reg);
int jello_jdll_invoke_prim(exec_ctx* ctx, const jello_insn* ins, const jello_function* fn, uint32_t first_arg_reg);

char* jello_jdll_resolve_library(const char* entry_path, const char* library_id);
/** Discovery anchor for JDLL search: `JELLO_ENTRY_PATH` when set, else `argv_entry`. */
const char* jello_discovery_entry_path(const char* argv_entry);
int jello_jdll_preflight_module(const jello_bc_module* m, const char* entry_path);

int jello_jdll_call_ex(exec_ctx* ctx, jello_function* fn, jello_value bound_this,
                       const jello_value* args, uint32_t nargs, jello_value* out, jello_value* exc_out);

void jello_jdll_pin_plugin_api(void);

#endif
