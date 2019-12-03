// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

jello_abstract* jello_abstract_new(struct jello_vm* vm, uint32_t type_id, void* payload) {
  return jello_abstract_new_finalized(vm, type_id, payload, NULL);
}

jello_abstract* jello_abstract_new_finalized(struct jello_vm* vm, uint32_t type_id, void* payload, jello_abstract_finalizer finalizer) {
  jello_abstract* a = (jello_abstract*)jello_gc_alloc(vm, sizeof(jello_abstract));
  a->h.kind = (uint32_t)JELLO_OBJ_ABSTRACT;
  a->h.type_id = type_id;
  a->payload = payload;
  a->finalizer = finalizer;
  return a;
}

