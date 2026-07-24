// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

// Object open-addressing hash table internals.
#ifndef JELLO_INTERNAL_OBJECT_INTERNAL_H
#define JELLO_INTERNAL_OBJECT_INTERNAL_H

#include <stdint.h>

enum {
  JELLO_OBJ_SLOT_EMPTY    = 0,
  JELLO_OBJ_SLOT_OCCUPIED = 1,
  JELLO_OBJ_SLOT_TOMB     = 2,
};

#define JELLO_OBJECT_INIT_CAP    8u
#define JELLO_OBJECT_LOAD_NUM   10u
#define JELLO_OBJECT_LOAD_DEN    7u
#define JELLO_OBJ_IC_NONE       UINT32_MAX

struct jello_object;
void jello_object_free_side_tables(struct jello_object* o);
void jello_object_invalidate_ic(struct jello_object* o);
void jello_object_init_empty(struct jello_object* o, uint32_t type_id);
void jello_object_reinit_from_freelist(struct jello_object* o, uint32_t type_id);
void* jello_gc_alloc_recycled(struct jello_vm* vm, size_t size, int* recycled);
size_t jello_object_empty_alloc_bytes(void);
int jello_object_is_freelist_recyclable(const struct jello_object* o);

#endif /* JELLO_INTERNAL_OBJECT_INTERNAL_H */
