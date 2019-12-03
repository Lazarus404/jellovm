// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/jdll.h>
#include <jello/internal/jdll_internal.h>

#include <string.h>

jello_value jdl_arg_value(jdlo_ctx* c, int index) {
  if(!c || c->failed || index < 0 || (uint32_t)index >= c->nargs) return jello_make_null();
  reg_frame* rf = jdl_caller_rf(c);
  if(!rf) return jello_make_null();
  exec_ctx* x = c->xctx;
  return vm_box_from_typed(x->vm, x->m, c->caller_f, rf, c->first_arg_reg + (uint32_t)index);
}

int jdl_is_fun(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  return jello_is_ptr(jv) && jello_obj_kind_of(jv) == (uint32_t)JELLO_OBJ_FUNCTION;
}

jello_function* jdl_as_fun(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  if(!jdl_is_fun(v)) return NULL;
  return (jello_function*)jello_as_ptr(jv);
}

jello_function* jdl_arg_fun(jdlo_ctx* c, int index) {
  return jdl_as_fun(jdl_arg_value(c, index));
}

void jdl_gc_root(jdlo_ctx* c, jdl_value v) {
  if(!c || c->failed) return;
  jello_gc_push_root(c->xctx->vm, (jello_value)(uintptr_t)v);
}

void jdl_gc_release(jdlo_ctx* c, uint32_t count) {
  if(!c || count == 0) return;
  jello_gc_pop_roots(c->xctx->vm, count);
}

void jello_jdll_pin_plugin_api(void) {
  /* LTO can drop these when only referenced from dlopen'd .jdll plugins. */
#pragma GCC diagnostic push
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#pragma GCC diagnostic ignored "-Wpedantic"
  volatile void* keep = NULL;
  void* p = NULL;
#define JELLO_PIN_FN(fn) memcpy(&p, &(fn), sizeof(p)), keep = p
  JELLO_PIN_FN(jdl_arg_value);
  JELLO_PIN_FN(jdl_call_ex);
  JELLO_PIN_FN(jdl_is_fun);
  JELLO_PIN_FN(jdl_as_fun);
  JELLO_PIN_FN(jdl_arg_fun);
  JELLO_PIN_FN(jdl_gc_root);
  JELLO_PIN_FN(jdl_gc_release);
  JELLO_PIN_FN(jdl_return_value);
  JELLO_PIN_FN(jdl_ctx_vm);
  JELLO_PIN_FN(jdl_ctx_module);
  JELLO_PIN_FN(jello_thread_spawn_fn);
  JELLO_PIN_FN(jello_thread_join);
  JELLO_PIN_FN(jello_thread_current_id);
  JELLO_PIN_FN(jello_channel_new);
  JELLO_PIN_FN(jello_channel_free);
  JELLO_PIN_FN(jello_channel_send);
  JELLO_PIN_FN(jello_channel_recv);
  JELLO_PIN_FN(jello_channel_try_recv);
  JELLO_PIN_FN(jello_channel_close);
  JELLO_PIN_FN(jello_channel_from_abstract);
  JELLO_PIN_FN(jello_channel_abstract_new);
  JELLO_PIN_FN(jello_channel_abstract_share);
#undef JELLO_PIN_FN
#pragma GCC diagnostic pop
  (void)keep;
}

int jello_jdll_call_ex(exec_ctx* ctx, jello_function* fn, jello_value bound_this,
                       const jello_value* args, uint32_t nargs, jello_value* out, jello_value* exc_out) {
  if(!ctx || !ctx->vm || !ctx->m || !fn || !out) return 0;

  jello_vm* vm = ctx->vm;
  uint32_t min_frames = vm->call_frames_len;
  uint32_t saved_min = ctx->min_call_frames;

  vm->jdll_call_out = out;
  if(!vm_push_frame_jello_call(vm, ctx->m, fn, bound_this, args, nargs, 1)) {
    vm->jdll_call_out = NULL;
    return 0;
  }

  ctx->min_call_frames = min_frames;
  jello_exec_status st = vm_exec_loop(ctx);
  ctx->min_call_frames = saved_min;
  vm->jdll_call_out = NULL;

  if(vm->exc_pending || st != JELLO_EXEC_OK) {
    if(exc_out) *exc_out = vm->exc_payload;
    return 0;
  }
  return 1;
}

int jdl_call_ex(jdlo_ctx* c, jdl_value this_obj, jdl_value fun, jdl_value* args, int nargs,
                jdl_value* out, jdl_value* exc_out) {
  if(!c || c->failed || !out) return 0;
  if(nargs < 0) return 0;
  jello_function* fn = jdl_as_fun(fun);
  if(!fn) {
    jdl_fail(c, "jdl_call_ex: callee is not a function");
    return 0;
  }
  jello_value bound = (jello_value)(uintptr_t)this_obj;
  jello_value ret = jello_make_null();
  jello_value exc = jello_make_null();
  jello_value* jargs = (jello_value*)args;
  if(!jello_jdll_call_ex(c->xctx, fn, bound, jargs, (uint32_t)nargs, &ret, exc_out ? &exc : NULL)) {
    if(exc_out) *exc_out = (jdl_value)(uintptr_t)exc;
    jdl_fail(c, "jdl_call_ex failed");
    return 0;
  }
  *out = (jdl_value)(uintptr_t)ret;
  return 1;
}
