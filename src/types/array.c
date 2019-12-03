// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <stdlib.h>
#include <string.h>

jello_array* jello_array_new(struct jello_vm* vm, uint32_t type_id, uint32_t length) {
  jello_array* a = (jello_array*)jello_gc_alloc(vm, sizeof(jello_array));
  a->h.kind = (uint32_t)JELLO_OBJ_ARRAY;
  a->h.type_id = type_id;
  a->length = length;
  if(length == 0) {
    a->data = NULL;
  } else {
    a->data = (jello_value*)malloc(sizeof(jello_value) * (size_t)length);
    if(!a->data) abort();
    // Initialize to null (boxed).
    for(uint32_t i = 0; i < length; i++) a->data[i] = jello_make_null();
  }
  return a;
}

static jello_value jello_array_default_elem(struct jello_vm* vm,
                                            const jello_bc_module* m,
                                            uint32_t type_id) {
  (void)vm;
  if(type_id < m->ntypes) {
    const jello_type_entry* te = &m->types[type_id];
    if(te->kind == JELLO_T_ARRAY) {
      uint32_t elem_tid = te->as.unary.elem;
      if(elem_tid < m->ntypes) {
        switch(m->types[elem_tid].kind) {
          case JELLO_T_I8:
          case JELLO_T_I16:
          case JELLO_T_I32:
          case JELLO_T_I64:
            return jello_make_i32(0);
          case JELLO_T_F16:
          case JELLO_T_F32:
          case JELLO_T_F64:
            return jello_make_null(); /* floats default null until boxed read path */
          case JELLO_T_BOOL:
            return jello_make_bool(0);
          default:
            break;
        }
      }
    }
  }
  return jello_make_null();
}

int jello_array_resize(struct jello_vm* vm,
                       const jello_bc_module* m,
                       jello_array* a,
                       uint32_t new_len) {
  if(!a) return 0;
  if(vm->max_array_len && new_len > vm->max_array_len) return 0;
  if(new_len == a->length) return 1;
  if(new_len == 0) {
    free(a->data);
    a->data = NULL;
    a->length = 0;
    return 1;
  }
  jello_value* nd = (jello_value*)realloc(a->data, (size_t)new_len * sizeof(jello_value));
  if(!nd) return 0;
  if(new_len > a->length) {
    jello_value dv = jello_array_default_elem(vm, m, a->h.type_id);
    for(uint32_t i = a->length; i < new_len; i++) nd[i] = dv;
  }
  a->data = nd;
  a->length = new_len;
  return 1;
}

