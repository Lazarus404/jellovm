// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <string.h>

int jello_value_is_copy_safe(jello_value v) {
  if(jello_is_null(v) || jello_is_bool(v) || jello_is_i32(v) || jello_is_atom(v)) return 1;
  if(jello_is_box_i64(v) || jello_is_box_f32(v) || jello_is_box_f64(v) || jello_is_box_f16(v)) return 1;
  if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BYTES) return 1;
  return 0;
}

int jello_value_copy_to_vm(jello_vm* dst, jello_value src, jello_value* out) {
  if(!dst || !out) return 0;
  if(!jello_value_is_copy_safe(src)) return 0;

  if(jello_is_null(src) || jello_is_bool(src) || jello_is_i32(src) || jello_is_atom(src)) {
    *out = src;
    return 1;
  }
  if(jello_is_box_i64(src)) {
    const jello_box_i64* s = (const jello_box_i64*)jello_as_ptr(src);
    jello_box_i64* b = jello_box_i64_new(dst, s->h.type_id, s->value);
    if(!b) return 0;
    *out = jello_from_ptr(b);
    return 1;
  }
  if(jello_is_box_f32(src)) {
    const jello_box_f32* s = (const jello_box_f32*)jello_as_ptr(src);
    jello_box_f32* b = jello_box_f32_new(dst, s->h.type_id, s->value);
    if(!b) return 0;
    *out = jello_from_ptr(b);
    return 1;
  }
  if(jello_is_box_f64(src)) {
    const jello_box_f64* s = (const jello_box_f64*)jello_as_ptr(src);
    jello_box_f64* b = jello_box_f64_new(dst, s->h.type_id, s->value);
    if(!b) return 0;
    *out = jello_from_ptr(b);
    return 1;
  }
  if(jello_is_box_f16(src)) {
    const jello_box_f16* s = (const jello_box_f16*)jello_as_ptr(src);
    jello_box_f16* b = jello_box_f16_new(dst, s->h.type_id, s->value);
    if(!b) return 0;
    *out = jello_from_ptr(b);
    return 1;
  }
  if(jello_is_ptr(src) && jello_obj_kind_of(src) == (uint32_t)JELLO_OBJ_BYTES) {
    const jello_bytes* s = (const jello_bytes*)jello_as_ptr(src);
    jello_bytes* b = jello_bytes_new(dst, s->h.type_id, s->length);
    if(!b) return 0;
    if(s->length) memcpy(b->data, s->data, s->length);
    *out = jello_from_ptr(b);
    return 1;
  }
  return 0;
}

int jello_value_is_thread_shareable(jello_value v) {
  return jello_value_is_copy_safe(v) || jello_channel_from_abstract(v) != NULL;
}

int jello_value_is_thread_cap_safe(jello_value v) {
  if(jello_value_is_thread_shareable(v)) return 1;
  if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_FUNCTION) {
    jello_function* f = (jello_function*)jello_as_ptr(v);
    return f->ncaps == 0u && !jello_bound_this_is_set(f->bound_this);
  }
  return 0;
}

static int jello_function_spawn_caps_ok_impl(jello_function* fn) {
  if(!fn) return 0;
  for(uint32_t i = 0; i < fn->ncaps; i++) {
    if(!jello_value_is_thread_cap_safe(fn->caps[i])) return 0;
  }
  return 1;
}

int jello_function_spawn_caps_ok(jello_function* fn) {
  return jello_function_spawn_caps_ok_impl(fn);
}

static jello_function* jello_function_share_to_vm(jello_vm* dst, jello_function* src) {
  if(!dst || !src || jello_bound_this_is_set(src->bound_this)) return NULL;
  if(src->caps_are_raw) {
    return jello_closure_new_raw(dst, src->h.type_id, src->func_index, src->ncaps, src->raw_cap_size,
                                 (const uint8_t*)&src->caps[0]);
  }
  if(src->ncaps == 0) {
    return jello_function_new(dst, src->h.type_id, src->func_index);
  }
  jello_value* caps = (jello_value*)calloc(src->ncaps, sizeof(jello_value));
  if(!caps) return NULL;
  for(uint32_t i = 0; i < src->ncaps; i++) {
    if(jello_is_ptr(src->caps[i]) && jello_obj_kind_of(src->caps[i]) == (uint32_t)JELLO_OBJ_FUNCTION) {
      jello_function* shared = jello_function_share_to_vm(dst, (jello_function*)jello_as_ptr(src->caps[i]));
      if(!shared) {
        free(caps);
        return NULL;
      }
      caps[i] = jello_from_ptr(shared);
    } else if(!jello_value_share_to_vm(dst, src->caps[i], &caps[i])) {
      free(caps);
      return NULL;
    }
  }
  jello_function* fn = jello_closure_new(dst, src->h.type_id, src->func_index, src->ncaps, caps);
  free(caps);
  return fn;
}

int jello_value_share_to_vm(jello_vm* dst, jello_value src, jello_value* out) {
  if(!dst || !out) return 0;
  if(jello_channel_from_abstract(src)) return jello_channel_abstract_share(dst, src, out);
  if(jello_is_ptr(src) && jello_obj_kind_of(src) == (uint32_t)JELLO_OBJ_FUNCTION) {
    jello_function* shared = jello_function_share_to_vm(dst, (jello_function*)jello_as_ptr(src));
    if(!shared) return 0;
    *out = jello_from_ptr(shared);
    return 1;
  }
  return jello_value_copy_to_vm(dst, src, out);
}

jello_function* jello_closure_share_to_vm(jello_vm* dst, jello_function* src) {
  return jello_function_share_to_vm(dst, src);
}
