// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

#include <stdlib.h>

JDLL_DEFINE_KIND(thread);

typedef struct jdll_thread_handle {
  jello_thread* thread;
} jdll_thread_handle;

static void jdll_thread_handle_fin(void* payload) {
  jdll_thread_handle* h = (jdll_thread_handle*)payload;
  if(!h) return;
  if(h->thread) {
    jello_thread_detach(h->thread);
    h->thread = NULL;
  }
  free(h);
}

static jdll_thread_handle* thread_handle_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_thread_handle*)a->payload;
}

void jdll_std_thread_spawn(jdlo_ctx* c) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  const jello_bc_module* m = jdl_ctx_module(c);
  if(!vm || !m) {
    jdl_fail(c, "thread_spawn: no active VM context");
    return;
  }

  jello_function* fn = jdl_arg_fun(c, 0);
  if(!fn) {
    jdl_fail(c, "thread_spawn: expected function");
    return;
  }

  jello_value args[2];
  args[0] = (jello_value)(uintptr_t)jdl_arg_value(c, 1);
  args[1] = (jello_value)(uintptr_t)jdl_arg_value(c, 2);

  jello_thread* t = jello_thread_spawn_fn(vm, m, fn, args, 2);
  if(!t) {
    jdl_fail(c, "thread_spawn: failed (captures, native fn, or bad args)");
    return;
  }

  jdll_thread_handle* h = (jdll_thread_handle*)malloc(sizeof(*h));
  if(!h) {
    jello_value dummy = jello_make_null();
    (void)jello_thread_join(vm, t, &dummy);
    jdl_fail(c, "thread_spawn: oom");
    return;
  }
  h->thread = t;
  jdl_return_abstract(c, h, jdll_thread_handle_fin);
}

void jdll_std_thread_join(jdlo_ctx* c) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  if(!vm) {
    jdl_fail(c, "thread_join: no active VM context");
    return;
  }

  jdll_thread_handle* h = thread_handle_from_arg(c, 0);
  if(!h || !h->thread) {
    jdl_fail(c, "thread_join: invalid handle");
    return;
  }

  jello_thread* t = h->thread;
  h->thread = NULL;

  jello_value out = jello_make_null();
  if(!jello_thread_join(vm, t, &out)) {
    const char* msg = jello_vm_last_trap_msg(vm);
    jdl_fail(c, msg ? msg : "thread_join failed");
    return;
  }
  jdl_return_value(c, (jdl_value)(uintptr_t)out);
}

void jdll_std_thread_id(jdlo_ctx* c) {
  jdl_return_i32(c, (int32_t)jello_thread_current_id());
}
