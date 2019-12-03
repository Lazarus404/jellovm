// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <string.h>

jello_bytes* jello_bytes_new(struct jello_vm* vm, uint32_t type_id, uint32_t length) {
  size_t total = sizeof(jello_bytes) + (size_t)length;
  jello_bytes* b = (jello_bytes*)jello_gc_alloc(vm, total);
  b->h.kind = (uint32_t)JELLO_OBJ_BYTES;
  b->h.type_id = type_id;
  b->length = length;
  if(length) memset(b->data, 0, length);
  return b;
}

