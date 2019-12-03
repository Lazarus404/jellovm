// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ENUM_NULLARY_TAGS 256u

jello_enum* jello_enum_new(struct jello_vm* vm, uint32_t type_id, uint32_t tag, uint32_t nfields, const jello_value* fields) {
  size_t payload = (size_t)nfields * sizeof(jello_value);
  size_t total = offsetof(jello_enum, fields) + payload;
  jello_enum* e = (jello_enum*)jello_gc_alloc(vm, total);
  e->h.kind = (uint32_t)JELLO_OBJ_ENUM;
  e->h.type_id = type_id;
  e->tag = tag;
  e->nfields = nfields;
  if(nfields && fields) {
    memcpy(e->fields, fields, payload);
  }
  return e;
}

void vm_enum_nullary_cache_clear(struct jello_vm* vm) {
  if(!vm) return;
  if(vm->enum_nullary_by_type) {
    for(uint32_t i = 0; i < vm->enum_nullary_ntypes; i++) {
      free(vm->enum_nullary_by_type[i]);
    }
    free(vm->enum_nullary_by_type);
  }
  vm->enum_nullary_by_type = NULL;
  vm->enum_nullary_ntypes = 0;
}

void vm_enum_nullary_cache_init(struct jello_vm* vm, uint32_t ntypes) {
  if(!vm) return;
  vm_enum_nullary_cache_clear(vm);
  if(ntypes == 0) return;
  vm->enum_nullary_by_type =
      (enum_nullary_type_cache**)calloc((size_t)ntypes, sizeof(enum_nullary_type_cache*));
  if(!vm->enum_nullary_by_type) abort();
  vm->enum_nullary_ntypes = ntypes;
}

static enum_nullary_type_cache* enum_nullary_type_table(struct jello_vm* vm, uint32_t type_id) {
  if(!vm || !vm->enum_nullary_by_type || type_id >= vm->enum_nullary_ntypes) return NULL;
  enum_nullary_type_cache* t = vm->enum_nullary_by_type[type_id];
  if(t) return t;
  t = (enum_nullary_type_cache*)calloc(1, sizeof(enum_nullary_type_cache));
  if(!t) abort();
  vm->enum_nullary_by_type[type_id] = t;
  return t;
}

jello_enum* jello_enum_nullary_intern(struct jello_vm* vm, uint32_t type_id, uint32_t tag) {
  if(!vm) return NULL;
  if(tag >= ENUM_NULLARY_TAGS) {
    return jello_enum_new(vm, type_id, tag, 0, NULL);
  }
  enum_nullary_type_cache* t = enum_nullary_type_table(vm, type_id);
  if(!t) {
    return jello_enum_new(vm, type_id, tag, 0, NULL);
  }
  if(t->slots[tag]) {
    return t->slots[tag];
  }
  jello_enum* e = jello_enum_new(vm, type_id, tag, 0, NULL);
  t->slots[tag] = e;
  return e;
}
