// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define JELLO_CHANNEL_DEFAULT_CAP 64u

enum {
  JELLO_CELL_NULL = 0,
  JELLO_CELL_BOOL = 1,
  JELLO_CELL_I32 = 2,
  JELLO_CELL_ATOM = 3,
  JELLO_CELL_I64 = 4,
  JELLO_CELL_F32 = 5,
  JELLO_CELL_F64 = 6,
  JELLO_CELL_F16 = 7,
  JELLO_CELL_BYTES = 8,
};

typedef struct jello_channel_cell {
  uint8_t kind;
  uint8_t pad[3];
  union {
    uint32_t u32;
    uint64_t u64;
    float f32;
    double f64;
    struct {
      uint32_t len;
      uint8_t* data;
    } bytes;
  } u;
} jello_channel_cell;

struct jello_channel {
  pthread_mutex_t mu;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
  uint32_t cap;
  uint32_t head;
  uint32_t tail;
  uint32_t count;
  int closed;
  jello_channel_cell* cells;
};

static void channel_cell_clear(jello_channel_cell* c) {
  if(!c) return;
  if(c->kind == JELLO_CELL_BYTES) {
    free(c->u.bytes.data);
    c->u.bytes.data = NULL;
    c->u.bytes.len = 0;
  }
  c->kind = JELLO_CELL_NULL;
}

static void channel_cell_free(jello_channel_cell* c) {
  channel_cell_clear(c);
}

static int channel_cell_from_value(jello_value v, jello_channel_cell* c) {
  if(!c || !jello_value_is_copy_safe(v)) return 0;
  channel_cell_clear(c);

  if(jello_is_null(v)) {
    c->kind = JELLO_CELL_NULL;
    return 1;
  }
  if(jello_is_bool(v)) {
    c->kind = JELLO_CELL_BOOL;
    c->u.u32 = jello_as_bool(v) ? 1u : 0u;
    return 1;
  }
  if(jello_is_i32(v)) {
    c->kind = JELLO_CELL_I32;
    c->u.u32 = (uint32_t)jello_as_i32(v);
    return 1;
  }
  if(jello_is_atom(v)) {
    c->kind = JELLO_CELL_ATOM;
    c->u.u32 = jello_as_atom(v);
    return 1;
  }
  if(jello_is_box_i64(v)) {
    const jello_box_i64* b = (const jello_box_i64*)jello_as_ptr(v);
    c->kind = JELLO_CELL_I64;
    c->u.u64 = (uint64_t)b->value;
    return 1;
  }
  if(jello_is_box_f32(v)) {
    const jello_box_f32* b = (const jello_box_f32*)jello_as_ptr(v);
    c->kind = JELLO_CELL_F32;
    c->u.f32 = b->value;
    return 1;
  }
  if(jello_is_box_f64(v)) {
    const jello_box_f64* b = (const jello_box_f64*)jello_as_ptr(v);
    c->kind = JELLO_CELL_F64;
    c->u.f64 = b->value;
    return 1;
  }
  if(jello_is_box_f16(v)) {
    const jello_box_f16* b = (const jello_box_f16*)jello_as_ptr(v);
    c->kind = JELLO_CELL_F16;
    c->u.u32 = (uint32_t)b->value;
    return 1;
  }
  if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BYTES) {
    const jello_bytes* b = (const jello_bytes*)jello_as_ptr(v);
    c->kind = JELLO_CELL_BYTES;
    c->u.bytes.len = b->length;
    if(b->length) {
      c->u.bytes.data = (uint8_t*)malloc(b->length);
      if(!c->u.bytes.data) return 0;
      memcpy(c->u.bytes.data, b->data, b->length);
    }
    return 1;
  }
  return 0;
}

static int channel_cell_to_value(jello_vm* vm, const jello_channel_cell* c, jello_value* out) {
  if(!vm || !c || !out) return 0;

  switch(c->kind) {
    case JELLO_CELL_NULL:
      *out = jello_make_null();
      return 1;
    case JELLO_CELL_BOOL:
      *out = jello_make_bool(c->u.u32 != 0);
      return 1;
    case JELLO_CELL_I32:
      *out = jello_make_i32((int32_t)c->u.u32);
      return 1;
    case JELLO_CELL_ATOM:
      *out = jello_make_atom(c->u.u32);
      return 1;
    case JELLO_CELL_I64: {
      jello_box_i64* b = jello_box_i64_new(vm, 0, (int64_t)c->u.u64);
      if(!b) return 0;
      *out = jello_from_ptr(b);
      return 1;
    }
    case JELLO_CELL_F32: {
      jello_box_f32* b = jello_box_f32_new(vm, 0, c->u.f32);
      if(!b) return 0;
      *out = jello_from_ptr(b);
      return 1;
    }
    case JELLO_CELL_F64: {
      jello_box_f64* b = jello_box_f64_new(vm, 0, c->u.f64);
      if(!b) return 0;
      *out = jello_from_ptr(b);
      return 1;
    }
    case JELLO_CELL_F16: {
      jello_box_f16* b = jello_box_f16_new(vm, 0, (uint16_t)c->u.u32);
      if(!b) return 0;
      *out = jello_from_ptr(b);
      return 1;
    }
    case JELLO_CELL_BYTES: {
      jello_bytes* b = jello_bytes_new(vm, 0, c->u.bytes.len);
      if(!b) return 0;
      if(c->u.bytes.len) memcpy(b->data, c->u.bytes.data, c->u.bytes.len);
      *out = jello_from_ptr(b);
      return 1;
    }
    default:
      return 0;
  }
}

#define JELLO_CHANNEL_HANDLE_MAGIC 0x4348464Eu

typedef struct jello_channel_user {
  uint32_t magic;
  uint32_t refs;
  jello_channel* channel;
} jello_channel_user;

static void channel_user_release(jello_channel_user* u) {
  if(!u || u->magic != JELLO_CHANNEL_HANDLE_MAGIC) return;
  if(u->refs == 0) return;
  u->refs--;
  if(u->refs > 0) return;
  if(u->channel) {
    jello_channel_free(u->channel);
    u->channel = NULL;
  }
  u->magic = 0;
  free(u);
}

static void channel_user_finalizer(void* payload) {
  channel_user_release((jello_channel_user*)payload);
}

static jello_channel_user* channel_user_new(jello_channel* ch) {
  jello_channel_user* u = (jello_channel_user*)calloc(1, sizeof(*u));
  if(!u) return NULL;
  u->magic = JELLO_CHANNEL_HANDLE_MAGIC;
  u->refs = 1;
  u->channel = ch;
  return u;
}

static jello_channel_user* channel_user_from_abstract(jello_value v) {
  if(!jello_is_ptr(v) || jello_obj_kind_of(v) != (uint32_t)JELLO_OBJ_ABSTRACT) return NULL;
  jello_abstract* a = (jello_abstract*)jello_as_ptr(v);
  jello_channel_user* u = (jello_channel_user*)a->payload;
  if(!u || u->magic != JELLO_CHANNEL_HANDLE_MAGIC || !u->channel) return NULL;
  return u;
}

jello_channel* jello_channel_from_abstract(jello_value v) {
  jello_channel_user* u = channel_user_from_abstract(v);
  return u ? u->channel : NULL;
}

jello_value jello_channel_abstract_new(jello_vm* vm, jello_channel* ch) {
  if(!vm || !ch) return jello_make_null();
  jello_channel_user* u = channel_user_new(ch);
  if(!u) {
    jello_channel_free(ch);
    return jello_make_null();
  }
  jello_abstract* a = jello_abstract_new_finalized(vm, 0, u, channel_user_finalizer);
  if(!a) {
    channel_user_release(u);
    return jello_make_null();
  }
  return jello_from_ptr(a);
}

int jello_channel_abstract_share(jello_vm* dst, jello_value src, jello_value* out) {
  if(!dst || !out) return 0;
  jello_channel_user* u = channel_user_from_abstract(src);
  if(!u) return 0;
  u->refs++;
  jello_abstract* a = jello_abstract_new_finalized(dst, 0, u, channel_user_finalizer);
  if(!a) {
    channel_user_release(u);
    return 0;
  }
  *out = jello_from_ptr(a);
  return 1;
}

jello_channel* jello_channel_new(uint32_t capacity) {
  if(capacity == 0) capacity = JELLO_CHANNEL_DEFAULT_CAP;
  jello_channel* ch = (jello_channel*)calloc(1, sizeof(*ch));
  if(!ch) return NULL;
  ch->cells = (jello_channel_cell*)calloc(capacity, sizeof(jello_channel_cell));
  if(!ch->cells) {
    free(ch);
    return NULL;
  }
  ch->cap = capacity;
  if(pthread_mutex_init(&ch->mu, NULL) != 0) {
    free(ch->cells);
    free(ch);
    return NULL;
  }
  if(pthread_cond_init(&ch->not_empty, NULL) != 0) {
    pthread_mutex_destroy(&ch->mu);
    free(ch->cells);
    free(ch);
    return NULL;
  }
  if(pthread_cond_init(&ch->not_full, NULL) != 0) {
    pthread_cond_destroy(&ch->not_empty);
    pthread_mutex_destroy(&ch->mu);
    free(ch->cells);
    free(ch);
    return NULL;
  }
  return ch;
}

void jello_channel_free(jello_channel* ch) {
  if(!ch) return;
  for(uint32_t i = 0; i < ch->cap; i++) channel_cell_free(&ch->cells[i]);
  free(ch->cells);
  pthread_cond_destroy(&ch->not_empty);
  pthread_cond_destroy(&ch->not_full);
  pthread_mutex_destroy(&ch->mu);
  free(ch);
}

static jello_channel_cell* channel_slot(jello_channel* ch, uint32_t index) {
  return &ch->cells[index % ch->cap];
}

int jello_channel_send(jello_vm* vm, jello_channel* ch, jello_value msg) {
  if(!ch) return 0;
  if(!jello_value_is_copy_safe(msg)) {
    if(vm) (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "channel send: value not copy-safe");
    return 0;
  }

  jello_channel_cell tmp;
  memset(&tmp, 0, sizeof tmp);
  if(!channel_cell_from_value(msg, &tmp)) {
    if(vm) (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "channel send: marshal failed");
    return 0;
  }

  pthread_mutex_lock(&ch->mu);
  while(!ch->closed && ch->count >= ch->cap) pthread_cond_wait(&ch->not_full, &ch->mu);
  if(ch->closed) {
    pthread_mutex_unlock(&ch->mu);
    channel_cell_free(&tmp);
    return 0;
  }

  jello_channel_cell* slot = channel_slot(ch, ch->tail);
  channel_cell_clear(slot);
  *slot = tmp;
  ch->tail++;
  ch->count++;
  pthread_cond_signal(&ch->not_empty);
  pthread_mutex_unlock(&ch->mu);
  return 1;
}

static int channel_recv_locked(jello_vm* vm, jello_channel* ch, jello_value* out, int block) {
  if(block) {
    while(ch->count == 0 && !ch->closed) pthread_cond_wait(&ch->not_empty, &ch->mu);
  } else if(ch->count == 0) {
    *out = jello_make_null();
    pthread_mutex_unlock(&ch->mu);
    return 1;
  }

  if(ch->count == 0) {
    *out = jello_make_null();
    pthread_mutex_unlock(&ch->mu);
    return 1;
  }

  jello_channel_cell* slot = channel_slot(ch, ch->head);
  jello_channel_cell tmp = *slot;
  slot->kind = JELLO_CELL_NULL;
  ch->head++;
  ch->count--;
  pthread_cond_signal(&ch->not_full);
  pthread_mutex_unlock(&ch->mu);

  if(!channel_cell_to_value(vm, &tmp, out)) {
    channel_cell_free(&tmp);
    if(vm) (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "channel recv: copy failed");
    return 0;
  }
  channel_cell_free(&tmp);
  return 1;
}

int jello_channel_recv(jello_vm* vm, jello_channel* ch, jello_value* out) {
  if(!ch || !out) return 0;
  out[0] = jello_make_null();
  pthread_mutex_lock(&ch->mu);
  return channel_recv_locked(vm, ch, out, 1);
}

int jello_channel_try_recv(jello_vm* vm, jello_channel* ch, jello_value* out) {
  if(!ch || !out) return 0;
  out[0] = jello_make_null();
  pthread_mutex_lock(&ch->mu);
  return channel_recv_locked(vm, ch, out, 0);
}

int jello_channel_close(jello_channel* ch) {
  if(!ch) return 0;
  pthread_mutex_lock(&ch->mu);
  if(ch->closed) {
    pthread_mutex_unlock(&ch->mu);
    return 0;
  }
  ch->closed = 1;
  pthread_cond_broadcast(&ch->not_empty);
  pthread_cond_broadcast(&ch->not_full);
  pthread_mutex_unlock(&ch->mu);
  return 1;
}
