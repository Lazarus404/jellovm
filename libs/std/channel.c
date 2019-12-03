// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

JDLL_DEFINE_KIND(channel);

static jello_channel* channel_from_arg(jdlo_ctx* c, int index) {
  jello_value v = (jello_value)(uintptr_t)jdl_arg_value(c, index);
  return jello_channel_from_abstract(v);
}

void jdll_std_channel_new(jdlo_ctx* c) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  jello_channel* ch = jello_channel_new(0);
  if(!ch) {
    jdl_fail(c, "channel_new: oom");
    return;
  }
  jello_value handle = jello_channel_abstract_new(vm, ch);
  if(jello_is_null(handle)) {
    jdl_fail(c, "channel_new: oom");
    return;
  }
  jdl_return_value(c, (jdl_value)(uintptr_t)handle);
}

void jdll_std_channel_send(jdlo_ctx* c) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  jello_channel* ch = channel_from_arg(c, 0);
  if(!ch) {
    jdl_fail(c, "channel_send: invalid handle");
    return;
  }
  jello_value msg = (jello_value)(uintptr_t)jdl_arg_value(c, 1);
  int ok = jello_channel_send(vm, ch, msg);
  jdl_return_bool(c, ok ? 1 : 0);
}

void jdll_std_channel_recv(jdlo_ctx* c) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  jello_channel* ch = channel_from_arg(c, 0);
  if(!ch) {
    jdl_fail(c, "channel_recv: invalid handle");
    return;
  }
  jello_value out = jello_make_null();
  if(!jello_channel_recv(vm, ch, &out)) {
    const char* msg = vm ? jello_vm_last_trap_msg(vm) : NULL;
    jdl_fail(c, msg ? msg : "channel_recv failed");
    return;
  }
  jdl_return_value(c, (jdl_value)(uintptr_t)out);
}

void jdll_std_channel_try_recv(jdlo_ctx* c) {
  struct jello_vm* vm = jdl_ctx_vm(c);
  jello_channel* ch = channel_from_arg(c, 0);
  if(!ch) {
    jdl_fail(c, "channel_try_recv: invalid handle");
    return;
  }
  jello_value out = jello_make_null();
  if(!jello_channel_try_recv(vm, ch, &out)) {
    const char* msg = vm ? jello_vm_last_trap_msg(vm) : NULL;
    jdl_fail(c, msg ? msg : "channel_try_recv failed");
    return;
  }
  jdl_return_value(c, (jdl_value)(uintptr_t)out);
}

void jdll_std_channel_close(jdlo_ctx* c) {
  jello_channel* ch = channel_from_arg(c, 0);
  if(!ch) {
    jdl_fail(c, "channel_close: invalid handle");
    return;
  }
  jdl_return_bool(c, jello_channel_close(ch) ? 1 : 0);
}
