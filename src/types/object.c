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

jello_object* jello_object_new(struct jello_vm* vm, uint32_t type_id) {
  jello_object* o = (jello_object*)jello_gc_alloc(vm, sizeof(jello_object));
  o->h.kind = (uint32_t)JELLO_OBJ_OBJECT;
  o->h.type_id = type_id;
  o->proto = NULL;
  o->cap = 8;
  o->len = 0;
  o->keys = (uint32_t*)malloc(sizeof(uint32_t) * o->cap);
  o->vals = (jello_value*)malloc(sizeof(jello_value) * o->cap);
  o->states = (uint8_t*)malloc(o->cap);
  if(!o->keys || !o->vals || !o->states) abort();
  memset(o->states, 0, o->cap);
  return o;
}

static uint32_t find_slot(jello_object* o, uint32_t atom_id, int* found) {
  uint32_t mask = o->cap - 1u;
  uint32_t h = hash_u32(atom_id);
  uint32_t i = h & mask;
  uint32_t first_tomb = UINT32_MAX;

  for(;;) {
    uint8_t st = o->states[i];
    if(st == 0) {
      *found = 0;
      return (first_tomb != UINT32_MAX) ? first_tomb : i;
    }
    if(st == 1 && o->keys[i] == atom_id) {
      *found = 1;
      return i;
    }
    if(st == 2 && first_tomb == UINT32_MAX) first_tomb = i;
    i = (i + 1u) & mask;
  }
}

int jello_object_has(jello_object* o, uint32_t atom_id) {
  int found = 0;
  (void)find_slot(o, atom_id, &found);
  return found;
}

jello_value jello_object_get(jello_object* o, uint32_t atom_id) {
  int found = 0;
  uint32_t i = find_slot(o, atom_id, &found);
  if(!found) return jello_make_null();
  return o->vals[i];
}

void jello_object_set(jello_object* o, uint32_t atom_id, jello_value v) {
  ensure_cap(o);
  int found = 0;
  uint32_t i = find_slot(o, atom_id, &found);
  if(!found) {
    o->keys[i] = atom_id;
    o->states[i] = 1;
    o->len++;
  }
  o->vals[i] = v;
}

static void rehash(jello_object* o, uint32_t new_cap) {
  uint32_t* old_keys = o->keys;
  jello_value* old_vals = o->vals;
  uint8_t* old_states = o->states;
  uint32_t old_cap = o->cap;

  o->cap = new_cap;
  o->keys = (uint32_t*)malloc(sizeof(uint32_t) * o->cap);
  o->vals = (jello_value*)malloc(sizeof(jello_value) * o->cap);
  o->states = (uint8_t*)malloc(o->cap);
  if(!o->keys || !o->vals || !o->states) abort();
  memset(o->states, 0, o->cap);

  uint32_t old_len = o->len;
  o->len = 0;
  for(uint32_t i = 0; i < old_cap; i++) {
    if(old_states[i] == 1) {
      jello_object_set(o, old_keys[i], old_vals[i]);
    }
  }
  o->len = old_len;

  free(old_keys);
  free(old_vals);
  free(old_states);
}

static void ensure_cap(jello_object* o) {
  // Grow when load factor would exceed ~0.7
  if((o->len + 1u) * 10u < o->cap * 7u) return;
  rehash(o, o->cap * 2u);
}

