// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <stdlib.h>
#include <string.h>

static uint32_t hash_u32(uint32_t x) {
  // A simple integer hash (xorshift/mix).
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static void ensure_cap(jello_object* o);

/* Table layout after header (or in a side malloc): keys, pad, vals, states. */
static size_t object_table_bytes(uint32_t cap) {
  size_t keys = (size_t)cap * sizeof(uint32_t);
  size_t align = sizeof(jello_value);
  size_t pad = (align - (keys % align)) % align;
  size_t vals = (size_t)cap * sizeof(jello_value);
  size_t states = (size_t)cap;
  return keys + pad + vals + states;
}

static void object_bind_table(jello_object* o, uint8_t* base, uint32_t cap) {
  size_t keys = (size_t)cap * sizeof(uint32_t);
  size_t align = sizeof(jello_value);
  size_t pad = (align - (keys % align)) % align;
  o->keys = (uint32_t*)base;
  o->vals = (jello_value*)(base + keys + pad);
  o->states = base + keys + pad + (size_t)cap * sizeof(jello_value);
  o->cap = cap;
}

static uint8_t* object_inline_base(jello_object* o) {
  return (uint8_t*)o + sizeof(jello_object);
}

static int object_uses_inline_table(const jello_object* o) {
  return o && o->keys == (uint32_t*)object_inline_base((jello_object*)o);
}

static uint32_t find_slot(jello_object* o, uint32_t atom_id, int* found) {
  /* Empty table: no occupied/tomb slots — place at the natural hash index. */
  if(o->len == 0u) {
    *found = 0;
    return hash_u32(atom_id) & (o->cap - 1u);
  }
  uint32_t mask = o->cap - 1u;
  uint32_t h = hash_u32(atom_id);
  uint32_t i = h & mask;
  uint32_t first_tomb = UINT32_MAX;

  for(;;) {
    uint8_t st = o->states[i];
    if(st == JELLO_OBJ_SLOT_EMPTY) {
      *found = 0;
      return (first_tomb != UINT32_MAX) ? first_tomb : i;
    }
    if(st == JELLO_OBJ_SLOT_OCCUPIED && o->keys[i] == atom_id) {
      *found = 1;
      return i;
    }
    if(st == JELLO_OBJ_SLOT_TOMB && first_tomb == UINT32_MAX) first_tomb = i;
    i = (i + 1u) & mask;
  }
}

/* Lookup-only: no insert index. Tiny inline tables use a linear scan. */
static uint32_t find_occupied_slot(jello_object* o, uint32_t atom_id, int* found) {
  if(o->len == 0u) {
    *found = 0;
    return 0;
  }
  if(o->len <= 4u && o->cap <= JELLO_OBJECT_INIT_CAP) {
    for(uint32_t i = 0; i < o->cap; i++) {
      if(o->states[i] == JELLO_OBJ_SLOT_OCCUPIED && o->keys[i] == atom_id) {
        *found = 1;
        return i;
      }
    }
    *found = 0;
    return 0;
  }
  uint32_t mask = o->cap - 1u;
  uint32_t i = hash_u32(atom_id) & mask;
  for(;;) {
    uint8_t st = o->states[i];
    if(st == JELLO_OBJ_SLOT_EMPTY) {
      *found = 0;
      return 0;
    }
    if(st == JELLO_OBJ_SLOT_OCCUPIED && o->keys[i] == atom_id) {
      *found = 1;
      return i;
    }
    i = (i + 1u) & mask;
  }
}

jello_object* jello_object_new(struct jello_vm* vm, uint32_t type_id) {
  uint32_t cap = JELLO_OBJECT_INIT_CAP;
  size_t total = sizeof(jello_object) + object_table_bytes(cap);
  jello_object* o = (jello_object*)jello_gc_alloc(vm, total);
  o->h.kind = (uint32_t)JELLO_OBJ_OBJECT;
  o->h.type_id = type_id;
  o->proto = NULL;
  o->len = 0;
  object_bind_table(o, object_inline_base(o), cap);
  memset(o->states, 0, cap);
  return o;
}

void jello_object_free_side_tables(jello_object* o) {
  if(!o || object_uses_inline_table(o)) return;
  free(o->keys); /* keys is the malloc base for the whole table block */
  o->keys = NULL;
  o->vals = NULL;
  o->states = NULL;
}

int jello_object_has(jello_object* o, uint32_t atom_id) {
  int found = 0;
  (void)find_occupied_slot(o, atom_id, &found);
  return found;
}

jello_value jello_object_get(jello_object* o, uint32_t atom_id) {
  int found = 0;
  uint32_t i = find_occupied_slot(o, atom_id, &found);
  if(!found) return jello_make_null();
  return o->vals[i];
}

jello_value* jello_object_slot(jello_object* o, uint32_t atom_id) {
  if(!o) return NULL;
  int found = 0;
  uint32_t i = find_occupied_slot(o, atom_id, &found);
  if(!found) return NULL;
  return &o->vals[i];
}

jello_value* jello_object_upsert(jello_object* o, uint32_t atom_id, int* out_existed) {
  if(!o) return NULL;
  ensure_cap(o);
  int found = 0;
  uint32_t i = find_slot(o, atom_id, &found);
  if(!found) {
    o->keys[i] = atom_id;
    o->states[i] = JELLO_OBJ_SLOT_OCCUPIED;
    o->vals[i] = jello_make_null();
    o->len++;
  }
  if(out_existed) *out_existed = found;
  return &o->vals[i];
}

void jello_object_set(jello_object* o, uint32_t atom_id, jello_value v) {
  jello_value* slot = jello_object_upsert(o, atom_id, NULL);
  if(slot) *slot = v;
}

int jello_object_remove(jello_object* o, uint32_t atom_id) {
  int found = 0;
  uint32_t i = find_slot(o, atom_id, &found);
  if(!found) return 0;
  o->states[i] = JELLO_OBJ_SLOT_TOMB;
  o->len--;
  return 1;
}

void jello_object_clear(jello_object* o) {
  memset(o->states, 0, o->cap);
  o->len = 0;
}

jello_object* jello_object_copy(struct jello_vm* vm, jello_object* src) {
  jello_object* dst = jello_object_new(vm, src->h.type_id);
  for(uint32_t i = 0; i < src->cap; i++) {
    if(src->states[i] == JELLO_OBJ_SLOT_OCCUPIED) {
      /* Clone numeric boxes so in-place field updates stay object-local. */
      jello_object_set(dst, src->keys[i], vm_clone_numbox(vm, src->vals[i]));
    }
  }
  dst->proto = src->proto;
  return dst;
}

static void rehash(jello_object* o, uint32_t new_cap) {
  uint32_t* old_keys = o->keys;
  jello_value* old_vals = o->vals;
  uint8_t* old_states = o->states;
  uint32_t old_cap = o->cap;
  uint8_t old_inline = (uint8_t)object_uses_inline_table(o);
  uint32_t old_len = o->len;

  uint8_t* block = (uint8_t*)malloc(object_table_bytes(new_cap));
  if(!block) abort();
  object_bind_table(o, block, new_cap);
  memset(o->states, 0, new_cap);
  o->len = 0;

  for(uint32_t i = 0; i < old_cap; i++) {
    if(old_states[i] != JELLO_OBJ_SLOT_OCCUPIED) continue;
    int found = 0;
    uint32_t slot = find_slot(o, old_keys[i], &found);
    o->keys[slot] = old_keys[i];
    o->states[slot] = JELLO_OBJ_SLOT_OCCUPIED;
    o->vals[slot] = old_vals[i];
    o->len++;
  }
  o->len = old_len;

  if(!old_inline) free(old_keys);
}

static void ensure_cap(jello_object* o) {
  // Grow when load factor would exceed ~0.7
  if((o->len + 1u) * JELLO_OBJECT_LOAD_NUM < o->cap * JELLO_OBJECT_LOAD_DEN) return;
  rehash(o, o->cap * 2u);
}
