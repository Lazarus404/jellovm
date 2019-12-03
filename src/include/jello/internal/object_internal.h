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

struct jello_object;
void jello_object_free_side_tables(struct jello_object* o);

#endif /* JELLO_INTERNAL_OBJECT_INTERNAL_H */
