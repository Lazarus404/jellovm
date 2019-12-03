// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

jello_list* jello_list_cons(struct jello_vm* vm, uint32_t type_id, jello_value head, jello_list* tail) {
  jello_list* n = (jello_list*)jello_gc_alloc(vm, sizeof(jello_list));
  n->h.kind = (uint32_t)JELLO_OBJ_LIST;
  n->h.type_id = type_id;
  n->head = head;
  n->tail = tail;
  return n;
}

