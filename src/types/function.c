// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <string.h>

jello_function* jello_function_new(struct jello_vm* vm, uint32_t type_id, uint32_t func_index) {
  jello_function* fn = (jello_function*)jello_gc_alloc(vm, sizeof(jello_function));
  fn->h.kind = (uint32_t)JELLO_OBJ_FUNCTION;
  fn->h.type_id = type_id;
  fn->func_index = func_index;
  fn->ncaps = 0;
  fn->bound_this = jello_make_null();
  fn->caps_are_raw = 0;
  fn->raw_cap_size = 0;
  return fn;
}

jello_function* jello_closure_new(struct jello_vm* vm, uint32_t type_id, uint32_t func_index, uint32_t ncaps, const jello_value* caps) {
  jello_function* fn = (jello_function*)jello_gc_alloc(vm, sizeof(jello_function) + sizeof(jello_value) * (size_t)ncaps);
  fn->h.kind = (uint32_t)JELLO_OBJ_FUNCTION;
  fn->h.type_id = type_id;
  fn->func_index = func_index;
  fn->ncaps = ncaps;
  fn->bound_this = jello_make_null();
  fn->caps_are_raw = 0;
  fn->raw_cap_size = 0;
  if(ncaps) memcpy(fn->caps, caps, sizeof(jello_value) * (size_t)ncaps);
  return fn;
}

jello_function* jello_closure_new_raw(struct jello_vm* vm, uint32_t type_id, uint32_t func_index, uint32_t ncaps, uint32_t raw_cap_size, const uint8_t* raw_caps) {
  jello_function* fn = (jello_function*)jello_gc_alloc(vm, sizeof(jello_function) + (size_t)raw_cap_size);
  fn->h.kind = (uint32_t)JELLO_OBJ_FUNCTION;
  fn->h.type_id = type_id;
  fn->func_index = func_index;
  fn->ncaps = ncaps;
  fn->bound_this = jello_make_null();
  fn->caps_are_raw = 1;
  fn->raw_cap_size = raw_cap_size;
  if(raw_cap_size && raw_caps) memcpy((uint8_t*)&fn->caps[0], raw_caps, (size_t)raw_cap_size);
  return fn;
}

jello_function* jello_function_bind_this(struct jello_vm* vm, uint32_t type_id, const jello_function* f, jello_value bound_this) {
  if(!f) return NULL;
  jello_function* fn;
  if(f->caps_are_raw) {
    fn = (jello_function*)jello_gc_alloc(vm, sizeof(jello_function) + (size_t)f->raw_cap_size);
    fn->h.kind = (uint32_t)JELLO_OBJ_FUNCTION;
    fn->h.type_id = type_id;
    fn->func_index = f->func_index;
    fn->ncaps = f->ncaps;
    fn->bound_this = bound_this;
    fn->caps_are_raw = 1;
    fn->raw_cap_size = f->raw_cap_size;
    memcpy((uint8_t*)&fn->caps[0], (const uint8_t*)&f->caps[0], (size_t)f->raw_cap_size);
  } else {
    fn = (jello_function*)jello_gc_alloc(vm, sizeof(jello_function) + sizeof(jello_value) * (size_t)f->ncaps);
    fn->h.kind = (uint32_t)JELLO_OBJ_FUNCTION;
    fn->h.type_id = type_id;
    fn->func_index = f->func_index;
    fn->ncaps = f->ncaps;
    fn->bound_this = bound_this;
    fn->caps_are_raw = 0;
    fn->raw_cap_size = 0;
    if(fn->ncaps) memcpy(fn->caps, f->caps, sizeof(jello_value) * (size_t)fn->ncaps);
  }
  return fn;
}

