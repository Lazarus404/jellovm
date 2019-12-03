// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static JELLO_THREAD_LOCAL jello_vm* g_jello_current_vm = NULL;
static atomic_uint g_jello_thread_id = 2;

struct jello_thread {
  pthread_t tid;
  pthread_mutex_t mu;
  pthread_cond_t cv;
  int done;
  int joined;
  jello_exec_status exec_status;
  jello_trap_code trap_code;
  char trap_msg[512];
  jello_value worker_result;
  jello_vm* worker_vm;
  jello_vm* parent_vm;
  const jello_bc_module* module;
  jello_function* closure;
  uint32_t nargs;
  jello_value* args;
  char* entry_path;
  uint64_t fuel_limit;
  uint32_t max_bytes_len;
  uint32_t max_array_len;
  uint32_t thread_id;
};

jello_vm* jello_current_vm(void) {
  return g_jello_current_vm;
}

void jello_vm_bind_current(jello_vm* vm) {
  g_jello_current_vm = vm;
}

uint32_t jello_thread_current_id(void) {
  jello_vm* vm = g_jello_current_vm;
  if(vm && vm->thread_id) return vm->thread_id;
  return 1u;
}

static void thread_apply_parent_limits(jello_vm* child, const jello_vm* parent) {
  if(!child || !parent) return;
  child->fuel_limit = parent->fuel_limit;
  child->max_bytes_len = parent->max_bytes_len;
  child->max_array_len = parent->max_array_len;
  child->thread_id = atomic_fetch_add(&g_jello_thread_id, 1u);
}

static void* thread_worker_main(void* arg) {
  jello_thread* t = (jello_thread*)arg;
  jello_vm* vm = jello_vm_create();
  if(!vm) {
    pthread_mutex_lock(&t->mu);
    t->exec_status = JELLO_EXEC_TRAP;
    t->trap_code = JELLO_TRAP_LIMIT;
    snprintf(t->trap_msg, sizeof t->trap_msg, "thread: failed to create VM");
    t->done = 1;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->mu);
    return NULL;
  }

  thread_apply_parent_limits(vm, t->parent_vm);
  if(t->entry_path) jello_vm_set_entry_path(vm, t->entry_path);
  jello_vm_bind_current(vm);

  jello_value* wargs = NULL;
  if(t->nargs) {
    wargs = (jello_value*)calloc(t->nargs, sizeof(jello_value));
    if(!wargs) {
      jello_vm_destroy(vm);
      jello_vm_bind_current(t->parent_vm);
      pthread_mutex_lock(&t->mu);
      t->exec_status = JELLO_EXEC_TRAP;
      t->trap_code = JELLO_TRAP_LIMIT;
      snprintf(t->trap_msg, sizeof t->trap_msg, "thread: oom");
      t->done = 1;
      pthread_cond_broadcast(&t->cv);
      pthread_mutex_unlock(&t->mu);
      return NULL;
    }
    for(uint32_t i = 0; i < t->nargs; i++) {
      if(!jello_value_share_to_vm(vm, t->args[i], &wargs[i])) {
        free(wargs);
        jello_vm_destroy(vm);
        jello_vm_bind_current(t->parent_vm);
        pthread_mutex_lock(&t->mu);
        t->exec_status = JELLO_EXEC_TRAP;
        t->trap_code = JELLO_TRAP_TYPE_MISMATCH;
        snprintf(t->trap_msg, sizeof t->trap_msg, "thread: arg not copy-safe");
        t->done = 1;
        pthread_cond_broadcast(&t->cv);
        pthread_mutex_unlock(&t->mu);
        return NULL;
      }
    }
  }

  jello_function* worker_fn = jello_closure_share_to_vm(vm, t->closure);
  if(!worker_fn) {
    free(wargs);
    jello_vm_destroy(vm);
    jello_vm_bind_current(t->parent_vm);
    pthread_mutex_lock(&t->mu);
    t->exec_status = JELLO_EXEC_TRAP;
    t->trap_code = JELLO_TRAP_LIMIT;
    snprintf(t->trap_msg, sizeof t->trap_msg, "thread: failed to share closure");
    t->done = 1;
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->mu);
    return NULL;
  }

  jello_value out = jello_make_null();
  jello_exec_status st = jello_vm_exec_status_closure(vm, t->module, worker_fn, wargs, t->nargs, &out);
  free(wargs);

  pthread_mutex_lock(&t->mu);
  t->worker_vm = vm;
  t->worker_result = out;
  t->exec_status = st;
  if(st != JELLO_EXEC_OK) {
    t->trap_code = vm->trap_code;
    const char* msg = vm->trap_msg;
    if(!msg || !msg[0]) msg = "thread execution trapped";
    snprintf(t->trap_msg, sizeof t->trap_msg, "%s", msg);
  }
  t->done = 1;
  pthread_cond_broadcast(&t->cv);
  pthread_mutex_unlock(&t->mu);
  jello_vm_bind_current(t->parent_vm);
  return NULL;
}

static jello_thread* thread_alloc(void) {
  jello_thread* t = (jello_thread*)calloc(1, sizeof(*t));
  if(!t) return NULL;
  if(pthread_mutex_init(&t->mu, NULL) != 0) {
    free(t);
    return NULL;
  }
  if(pthread_cond_init(&t->cv, NULL) != 0) {
    pthread_mutex_destroy(&t->mu);
    free(t);
    return NULL;
  }
  return t;
}

static jello_thread* jello_thread_spawn(jello_vm* parent, const jello_bc_module* m, jello_function* closure,
                                        const jello_value* args, uint32_t nargs) {
  if(!parent || !m || !closure) return NULL;

  jello_thread* t = thread_alloc();
  if(!t) return NULL;

  t->parent_vm = parent;
  t->module = m;
  t->closure = closure;
  t->nargs = nargs;
  t->fuel_limit = parent->fuel_limit;
  t->max_bytes_len = parent->max_bytes_len;
  t->max_array_len = parent->max_array_len;

  if(parent->entry_path) {
    t->entry_path = strdup(parent->entry_path);
    if(!t->entry_path) {
      pthread_mutex_destroy(&t->mu);
      pthread_cond_destroy(&t->cv);
      free(t);
      return NULL;
    }
  }

  if(nargs) {
    t->args = (jello_value*)malloc((size_t)nargs * sizeof(jello_value));
    if(!t->args) {
      free(t->entry_path);
      pthread_mutex_destroy(&t->mu);
      pthread_cond_destroy(&t->cv);
      free(t);
      return NULL;
    }
    memcpy(t->args, args, (size_t)nargs * sizeof(jello_value));
  }

  if(pthread_create(&t->tid, NULL, thread_worker_main, t) != 0) {
    free(t->args);
    free(t->entry_path);
    pthread_mutex_destroy(&t->mu);
    pthread_cond_destroy(&t->cv);
    free(t);
    return NULL;
  }
  return t;
}

jello_thread* jello_thread_spawn_fn(jello_vm* parent, const jello_bc_module* m, jello_function* fn,
                                  const jello_value* args, uint32_t nargs) {
  if(!parent || !m || !fn || fn->h.kind != (uint32_t)JELLO_OBJ_FUNCTION) return NULL;
  if(jello_bound_this_is_set(fn->bound_this)) return NULL;
  if(!jello_function_spawn_caps_ok(fn)) return NULL;
  uint32_t fi = fn->func_index;
  if(jello_is_native_builtin(fi) || jello_is_jdll_prim(fi)) return NULL;
  uint32_t bytecode_idx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  if(bytecode_idx >= m->nfuncs) return NULL;
  if(nargs) {
    for(uint32_t i = 0; i < nargs; i++) {
      if(!jello_value_is_thread_shareable(args[i])) return NULL;
    }
  }
  return jello_thread_spawn(parent, m, fn, args, nargs);
}

static void thread_destroy_worker(jello_thread* t) {
  if(!t || !t->worker_vm) return;
  jello_vm_destroy(t->worker_vm);
  t->worker_vm = NULL;
}

int jello_thread_join(jello_vm* parent, jello_thread* t, jello_value* out) {
  if(!t || !out) return 0;
  out[0] = jello_make_null();

  pthread_mutex_lock(&t->mu);
  while(!t->done) pthread_cond_wait(&t->cv, &t->mu);
  if(t->joined) {
    pthread_mutex_unlock(&t->mu);
    return 0;
  }
  t->joined = 1;
  jello_exec_status st = t->exec_status;
  jello_value worker_out = t->worker_result;
  jello_vm* worker = t->worker_vm;
  char trap_msg[512];
  jello_trap_code trap_code = t->trap_code;
  if(st != JELLO_EXEC_OK) {
    snprintf(trap_msg, sizeof trap_msg, "%s", t->trap_msg);
  }
  pthread_mutex_unlock(&t->mu);

  pthread_join(t->tid, NULL);

  int ok = 0;
  if(st == JELLO_EXEC_OK && worker) {
    if(jello_value_copy_to_vm(parent, worker_out, out)) ok = 1;
    else snprintf(trap_msg, sizeof trap_msg, "thread join: result not copy-safe");
  }

  thread_destroy_worker(t);
  free(t->args);
  free(t->entry_path);
  pthread_mutex_destroy(&t->mu);
  pthread_cond_destroy(&t->cv);
  free(t);

  if(ok) return 1;
  if(parent) {
    (void)jello_vm_trap(parent, trap_code ? trap_code : JELLO_TRAP_TYPE_MISMATCH,
                        trap_msg[0] ? trap_msg : "thread join failed");
  }
  return 0;
}

void jello_thread_detach(jello_thread* t) {
  if(!t) return;
  pthread_detach(t->tid);
  pthread_mutex_lock(&t->mu);
  if(!t->joined) t->joined = 1;
  pthread_mutex_unlock(&t->mu);
  /* Worker cleans up on completion; caller must not join. */
}
