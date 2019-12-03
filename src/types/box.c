// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

jello_box_i64* jello_box_i64_new(struct jello_vm* vm, uint32_t type_id, int64_t v) {
  jello_box_i64* b = (jello_box_i64*)jello_gc_alloc(vm, sizeof(jello_box_i64));
  b->h.kind = (uint32_t)JELLO_OBJ_BOX_I64;
  b->h.type_id = type_id;
  b->value = v;
  return b;
}

jello_box_f64* jello_box_f64_new(struct jello_vm* vm, uint32_t type_id, double v) {
  jello_box_f64* b = (jello_box_f64*)jello_gc_alloc(vm, sizeof(jello_box_f64));
  b->h.kind = (uint32_t)JELLO_OBJ_BOX_F64;
  b->h.type_id = type_id;
  b->value = v;
  return b;
}

jello_box_f32* jello_box_f32_new(struct jello_vm* vm, uint32_t type_id, float v) {
  jello_box_f32* b = (jello_box_f32*)jello_gc_alloc(vm, sizeof(jello_box_f32));
  b->h.kind = (uint32_t)JELLO_OBJ_BOX_F32;
  b->h.type_id = type_id;
  b->value = v;
  return b;
}

jello_box_f16* jello_box_f16_new(struct jello_vm* vm, uint32_t type_id, uint16_t bits) {
  jello_box_f16* b = (jello_box_f16*)jello_gc_alloc(vm, sizeof(jello_box_f16));
  b->h.kind = (uint32_t)JELLO_OBJ_BOX_F16;
  b->h.type_id = type_id;
  b->value = bits;
  return b;
}

