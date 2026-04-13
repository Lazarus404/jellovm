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

typedef struct jello_gc_hdr {
  struct jello_gc_hdr* next;
  size_t total_size;
  uint8_t marked;
  uint8_t _pad[7];
} jello_gc_hdr;

static void destroy_object(void* p);

static jello_gc_hdr* hdr_of(void* obj) {
  return (jello_gc_hdr*)((uint8_t*)obj - sizeof(jello_gc_hdr));
}

static void* obj_of(jello_gc_hdr* h) {
  return (void*)((uint8_t*)h + sizeof(jello_gc_hdr));
}

static void gc_panic(void) {
  abort();
}

void jello_gc_init(struct jello_vm* vm) {
  if(!vm) return;
  if(vm->gc_next_collect == 0) vm->gc_next_collect = 1024u * 1024u; // 1MB default
}

void jello_gc_shutdown(struct jello_vm* vm) {
  if(!vm) return;
  /* Clear roots that may point into the heap; otherwise the next run's GC
   * could trace dangling pointers (use-after-free). */
  vm->spill_len = 0;
  /* Invalidate frame cache (frame_layouts_mod may point to a freed module). */
  vm_frame_cache_shutdown(vm);
  // Free everything on the GC heap (best-effort).
  jello_gc_hdr* h = (jello_gc_hdr*)vm->gc_objects;
  while(h) {
    jello_gc_hdr* next = h->next;
    destroy_object(obj_of(h));
    free(h);
    h = next;
  }
  vm->gc_objects = NULL;
  vm->gc_bytes_live = 0;

  free(vm->gc_roots);
  vm->gc_roots = NULL;
  vm->gc_roots_len = 0;
  vm->gc_roots_cap = 0;
}

void jello_gc_push_root(struct jello_vm* vm, jello_value v) {
  if(!vm) gc_panic();
  if(vm->gc_roots_len == vm->gc_roots_cap) {
    uint32_t ncap = vm->gc_roots_cap ? (vm->gc_roots_cap * 2u) : 32u;
    jello_value* nv = (jello_value*)realloc(vm->gc_roots, sizeof(jello_value) * (size_t)ncap);
    if(!nv) gc_panic();
    vm->gc_roots = nv;
    vm->gc_roots_cap = ncap;
  }
  vm->gc_roots[vm->gc_roots_len++] = v;
}

void jello_gc_pop_roots(struct jello_vm* vm, uint32_t n) {
  if(!vm) gc_panic();
  if(n > vm->gc_roots_len) gc_panic();
  vm->gc_roots_len -= n;
}

// --- mark ---------------------------------------------------------------------

typedef struct mark_stack {
  void** items;
  uint32_t len;
  uint32_t cap;
} mark_stack;

static void ms_push(mark_stack* ms, void* p) {
  if(ms->len == ms->cap) {
    uint32_t ncap = ms->cap ? (ms->cap * 2u) : 64u;
    void** ni = (void**)realloc(ms->items, sizeof(void*) * (size_t)ncap);
    if(!ni) gc_panic();
    ms->items = ni;
    ms->cap = ncap;
  }
  ms->items[ms->len++] = p;
}

static void* ms_pop(mark_stack* ms) {
  if(ms->len == 0) return NULL;
  return ms->items[--ms->len];
}

static void mark_ptr(mark_stack* ms, void* p) {
  if(!p) return;
  jello_gc_hdr* h = hdr_of(p);
  if(h->marked) return;
  h->marked = 1;
  ms_push(ms, p);
}

static void mark_val(mark_stack* ms, jello_value v) {
  if(jello_is_ptr(v)) {
    mark_ptr(ms, jello_as_ptr(v));
  }
}

static void trace_object(mark_stack* ms, void* p) {
  const jello_obj_header* oh = (const jello_obj_header*)p;
  switch((jello_obj_kind)oh->kind) {
    case JELLO_OBJ_BYTES:
    case JELLO_OBJ_BOX_I64:
    case JELLO_OBJ_BOX_F64:
    case JELLO_OBJ_BOX_F32:
    case JELLO_OBJ_BOX_F16:
      return;
    case JELLO_OBJ_LIST: {
      const jello_list* n = (const jello_list*)p;
      mark_val(ms, n->head);
      mark_ptr(ms, n->tail);
      return;
    }
    case JELLO_OBJ_ARRAY: {
      const jello_array* a = (const jello_array*)p;
      for(uint32_t i = 0; i < a->length; i++) mark_val(ms, a->data[i]);
      return;
    }
    case JELLO_OBJ_OBJECT: {
      const jello_object* o = (const jello_object*)p;
      mark_ptr(ms, (void*)o->proto);
      for(uint32_t i = 0; i < o->cap; i++) {
        if(o->states[i] == 1) mark_val(ms, o->vals[i]);
      }
      return;
    }
    case JELLO_OBJ_FUNCTION: {
      const jello_function* f = (const jello_function*)p;
      mark_val(ms, f->bound_this);
      if(!f->caps_are_raw) {
        for(uint32_t i = 0; i < f->ncaps; i++) mark_val(ms, f->caps[i]);
      }
      return;
    }
    case JELLO_OBJ_ABSTRACT:
      // Opaque by default.
      return;
    default:
      return;
  }
}

// --- sweep --------------------------------------------------------------------

static void destroy_object(void* p) {
  const jello_obj_header* oh = (const jello_obj_header*)p;
  switch((jello_obj_kind)oh->kind) {
    case JELLO_OBJ_ARRAY: {
      jello_array* a = (jello_array*)p;
      free(a->data);
      a->data = NULL;
      a->length = 0;
      return;
    }
    case JELLO_OBJ_OBJECT: {
      jello_object* o = (jello_object*)p;
      free(o->keys);
      free(o->vals);
      free(o->states);
      o->keys = NULL;
      o->vals = NULL;
      o->states = NULL;
      return;
    }
    case JELLO_OBJ_FUNCTION: {
      // Single-allocation closure (captures are inline).
      return;
    }
    case JELLO_OBJ_ABSTRACT: {
      jello_abstract* a = (jello_abstract*)p;
      if(a->finalizer) a->finalizer(a->payload);
      a->payload = NULL;
      a->finalizer = NULL;
      return;
    }
    default:
      return;
  }
}

// Root scanning helpers.
static void scan_typed_frames(struct jello_vm* vm, mark_stack* ms) {
  const jello_bc_module* m = vm->running_module;
  if(!m) return;
  const call_frame* frames = (const call_frame*)vm->call_frames;
  for(uint32_t fi = 0; fi < vm->call_frames_len; fi++) {
    const jello_bc_function* f = frames[fi].f;
    const reg_frame* rf = &frames[fi].rf;
    for(uint32_t r = 0; r < rf->nregs; r++) {
      jello_type_kind k = m->types[f->reg_types[r]].kind;
      if(k == JELLO_T_DYNAMIC) {
        jello_value v;
        memcpy(&v, rf->mem + rf->off[r], sizeof(v));
        mark_val(ms, v);
      } else {
        switch(k) {
          case JELLO_T_BYTES:
          case JELLO_T_FUNCTION:
          case JELLO_T_LIST:
          case JELLO_T_ARRAY:
          case JELLO_T_OBJECT:
          case JELLO_T_ABSTRACT: {
            void* p = NULL;
            memcpy(&p, rf->mem + rf->off[r], sizeof(void*));
            mark_ptr(ms, p);
            break;
          }
          default:
            break;
        }
      }
    }
  }
}

void jello_gc_collect(struct jello_vm* vm) {
  if(!vm) gc_panic();

  vm->gc_collections++;

  mark_stack ms = {0};

  // Roots: spill
  for(uint32_t i = 0; i < vm->spill_len; i++) mark_val(&ms, vm->spill[i]);
  // Roots: temp roots
  for(uint32_t i = 0; i < vm->gc_roots_len; i++) mark_val(&ms, vm->gc_roots[i]);
  // Roots: cached const functions (allocated on GC heap)
  if(vm->const_fun_cache && vm->const_fun_cache_len) {
    void** fs = (void**)vm->const_fun_cache;
    for(uint32_t i = 0; i < vm->const_fun_cache_len; i++) {
      mark_ptr(&ms, fs[i]);
    }
  }
  // Roots: typed call frames
  scan_typed_frames(vm, &ms);

  // Mark loop
  for(;;) {
    void* p = ms_pop(&ms);
    if(!p) break;
    trace_object(&ms, p);
  }
  free(ms.items);

  // Sweep
  jello_gc_hdr** prev = (jello_gc_hdr**)&vm->gc_objects;
  jello_gc_hdr* h = (jello_gc_hdr*)vm->gc_objects;
  size_t live_bytes = 0;
  while(h) {
    jello_gc_hdr* next = h->next;
    if(!h->marked) {
      void* obj = obj_of(h);
      destroy_object(obj);
      vm->gc_freed_objects++;
      *prev = next;
      free(h);
    } else {
      h->marked = 0;
      live_bytes += h->total_size;
      prev = &h->next;
    }
    h = next;
  }

  vm->gc_bytes_live = live_bytes;
  // Next threshold: grow by ~2x with a floor.
  vm->gc_next_collect = (live_bytes < 256u * 1024u) ? (512u * 1024u) : (live_bytes * 2u);
}

void* jello_gc_alloc(struct jello_vm* vm, size_t size) {
  if(!vm) gc_panic();

  if(vm->gc_next_collect && vm->gc_bytes_live >= vm->gc_next_collect) {
    jello_gc_collect(vm);
  }

  size_t total = sizeof(jello_gc_hdr) + size;
  jello_gc_hdr* h = (jello_gc_hdr*)malloc(total);
  if(!h) gc_panic();
  h->next = (jello_gc_hdr*)vm->gc_objects;
  h->total_size = total;
  h->marked = 0;
  vm->gc_objects = h;
  vm->gc_bytes_live += total;
  return obj_of(h);
}

