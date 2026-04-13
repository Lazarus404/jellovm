/**
 * Copyright 2017 - Jahred Love
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <jello/internal.h>

#include <string.h>

jello_function* jello_function_new(struct jello_vm* vm, uint32_t type_id, uint32_t func_index) {
  jello_function* fn = (jello_function*)jello_gc_alloc(vm, sizeof(jello_function));
  fn->h.kind = (uint32_t)JELLO_OBJ_FUNCTION;
  fn->h.type_id = type_id;
  fn->func_index = func_index;
  fn->ncaps = 0;
  fn->bound_this = jello_make_null();
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

