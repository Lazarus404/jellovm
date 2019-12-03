// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>

#include <jello/internal/vm_internal.h>

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#  include <sys/mman.h>
#  include <unistd.h>
#endif

typedef struct jello_jit_cache_entry {
  const jello_bc_module* mod;
  uint32_t func_idx;
  jello_jit_code code;
  struct jello_jit_cache_entry* next;
} jello_jit_cache_entry;

typedef struct jello_jit_hot_table {
  const jello_bc_module* mod;
  uint32_t* counts;
  uint32_t nfuncs;
} jello_jit_hot_table;

struct jello_jit_state {
  jello_jit_cache_entry* cache;
  jello_jit_hot_table* hot;
  uint32_t nhot;
  uint32_t nest; /* nested jello_jit_runtime_run depth (callee chaining) */
};

static void* jit_map_code(const uint8_t* code, size_t size, size_t* out_map_size) {
  if(!code || size == 0) return NULL;
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  size_t ps = (size_t)si.dwPageSize;
  if(ps == 0) return NULL;
  size_t total = (size + ps - 1u) & ~(ps - 1u);
  void* mem = VirtualAlloc(NULL, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if(!mem) return NULL;
  memcpy(mem, code, size);
  DWORD old_prot = 0;
  if(!VirtualProtect(mem, total, PAGE_EXECUTE_READ, &old_prot)) {
    VirtualFree(mem, 0, MEM_RELEASE);
    return NULL;
  }
  FlushInstructionCache(GetCurrentProcess(), mem, total);
  if(out_map_size) *out_map_size = total;
  return mem;
#elif defined(__unix__) || defined(__APPLE__)
  long ps = sysconf(_SC_PAGESIZE);
  if(ps <= 0) return NULL;
  size_t total = (size + (size_t)ps - 1u) & ~((size_t)ps - 1u);
  void* mem = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if(mem == MAP_FAILED) return NULL;
  memcpy(mem, code, size);
  if(mprotect(mem, total, PROT_READ | PROT_EXEC) != 0) {
    munmap(mem, total);
    return NULL;
  }
  if(out_map_size) *out_map_size = total;
  return mem;
#else
  (void)out_map_size;
  return NULL;
#endif
}

static void jit_unmap(void* mem, size_t size) {
  if(!mem) return;
#if defined(_WIN32)
  (void)size;
  VirtualFree(mem, 0, MEM_RELEASE);
#elif defined(__unix__) || defined(__APPLE__)
  long ps = sysconf(_SC_PAGESIZE);
  if(ps <= 0) return;
  size_t total = (size + (size_t)ps - 1u) & ~((size_t)ps - 1u);
  munmap(mem, total);
#else
  (void)size;
#endif
}

jello_jit_state* jello_jit_state_create(void) {
  return (jello_jit_state*)calloc(1, sizeof(jello_jit_state));
}

uint32_t jello_jit_state_nest(const jello_jit_state* st) {
  return st ? st->nest : 0u;
}

void jello_jit_state_nest_inc(jello_jit_state* st) {
  if(st) st->nest++;
}

void jello_jit_state_nest_dec(jello_jit_state* st) {
  if(st && st->nest) st->nest--;
}

void jello_jit_state_destroy(jello_jit_state* st) {
  if(!st) return;
  jello_jit_cache_entry* e = st->cache;
  while(e) {
    jello_jit_cache_entry* next = e->next;
    jit_unmap(e->code.base ? e->code.base : e->code.entry, e->code.map_size ? e->code.map_size : e->code.size);
    free(e->code.bc_pc_map);
    free(e);
    e = next;
  }
  for(uint32_t i = 0; i < st->nhot; i++) {
    free(st->hot[i].counts);
  }
  free(st->hot);
  free(st);
}

jello_jit_code* jello_jit_cache_lookup(jello_jit_state* st, const jello_bc_module* m, uint32_t func_idx) {
  if(!st || !m) return NULL;
  for(jello_jit_cache_entry* e = st->cache; e; e = e->next) {
    if(e->mod == m && e->func_idx == func_idx) return &e->code;
  }
  return NULL;
}

jello_jit_code* jello_jit_cache_insert(
    jello_jit_state* st,
    const jello_bc_module* m,
    uint32_t func_idx,
    const uint8_t* code,
    size_t size,
    size_t entry_off,
    size_t body_off,
    const uint32_t* bc_pc_map,
    uint32_t nbc_pc_map
) {
  if(!st || !m || !code || size == 0 || entry_off >= size || body_off >= size) return NULL;
  size_t map_size = 0;
  void* mem = jit_map_code(code, size, &map_size);
  if(!mem) return NULL;
  jello_jit_cache_entry* e = (jello_jit_cache_entry*)calloc(1, sizeof(jello_jit_cache_entry));
  if(!e) {
    jit_unmap(mem, map_size);
    return NULL;
  }
  e->mod = m;
  e->func_idx = func_idx;
  e->code.base = (uint8_t*)mem;
  e->code.entry = (uint8_t*)mem + entry_off;
  e->code.body = (uint8_t*)mem + body_off;
  e->code.size = size;
  e->code.map_size = map_size;
  e->code.func_idx = func_idx;
  if(bc_pc_map && nbc_pc_map) {
    e->code.bc_pc_map = (uint32_t*)malloc((size_t)nbc_pc_map * sizeof(uint32_t));
    if(!e->code.bc_pc_map) {
      jit_unmap(mem, map_size);
      free(e);
      return NULL;
    }
    memcpy(e->code.bc_pc_map, bc_pc_map, (size_t)nbc_pc_map * sizeof(uint32_t));
    e->code.nbc_pc_map = nbc_pc_map;
  }
  e->next = st->cache;
  st->cache = e;
  return &e->code;
}

void jello_jit_cache_drop_module(jello_jit_state* st, const jello_bc_module* m) {
  if(!st || !m) return;
  jello_jit_cache_entry** prev = &st->cache;
  for(jello_jit_cache_entry* e = st->cache; e; ) {
    if(e->mod == m) {
      *prev = e->next;
      jit_unmap(e->code.base ? e->code.base : e->code.entry, e->code.map_size ? e->code.map_size : e->code.size);
      jello_jit_cache_entry* dead = e;
      e = e->next;
      free(dead->code.bc_pc_map);
      free(dead);
      continue;
    }
    prev = &e->next;
    e = e->next;
  }
  for(uint32_t i = 0; i < st->nhot; i++) {
    if(st->hot[i].mod == m) {
      free(st->hot[i].counts);
      st->hot[i].counts = NULL;
      st->hot[i].mod = NULL;
      st->hot[i].nfuncs = 0;
    }
  }
}

static jello_jit_hot_table* hot_table_for(jello_jit_state* st, const jello_bc_module* m) {
  for(uint32_t i = 0; i < st->nhot; i++) {
    if(st->hot[i].mod == m) return &st->hot[i];
  }
  jello_jit_hot_table* nh = (jello_jit_hot_table*)realloc(st->hot, (size_t)(st->nhot + 1u) * sizeof(jello_jit_hot_table));
  if(!nh) return NULL;
  st->hot = nh;
  jello_jit_hot_table* t = &st->hot[st->nhot++];
  memset(t, 0, sizeof(*t));
  t->mod = m;
  t->nfuncs = m->nfuncs;
  t->counts = (uint32_t*)calloc((size_t)m->nfuncs, sizeof(uint32_t));
  if(!t->counts) return NULL;
  return t;
}

uint32_t* jello_jit_hot_counter(jello_jit_state* st, const jello_bc_module* m, uint32_t func_idx) {
  if(!st || !m || func_idx >= m->nfuncs) return NULL;
  jello_jit_hot_table* t = hot_table_for(st, m);
  if(!t || !t->counts) return NULL;
  return &t->counts[func_idx];
}

int jello_jit_hot_is_rejected(jello_jit_state* st, const jello_bc_module* m, uint32_t func_idx) {
  uint32_t* hot = jello_jit_hot_counter(st, m, func_idx);
  return hot && *hot == JELLO_JIT_HOT_REJECTED;
}

void jello_jit_hot_mark_rejected(jello_jit_state* st, const jello_bc_module* m, uint32_t func_idx) {
  uint32_t* hot = jello_jit_hot_counter(st, m, func_idx);
  if(hot) *hot = JELLO_JIT_HOT_REJECTED;
}

int jello_jit_func_needs_enter(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f) {
  if(!jello_jit_config_enabled(vm) || !vm || !m || !f || !vm->jit_state) return 0;
  uint32_t fi = (uint32_t)(f - m->funcs);
  jello_jit_state* st = (jello_jit_state*)vm->jit_state;
  if(jello_jit_cache_lookup(st, m, fi)) return 0;
  if(jello_jit_hot_is_rejected(st, m, fi)) return 0;
  return 1;
}

int jello_jit_func_is_compiled(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f) {
  if(!jello_jit_config_enabled(vm) || !vm || !m || !f || !vm->jit_state) return 0;
  uint32_t fi = (uint32_t)(f - m->funcs);
  return jello_jit_cache_lookup((jello_jit_state*)vm->jit_state, m, fi) != NULL;
}
