// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

// Internal VM runtime structs shared across translation units.
// Not part of the public embedder API.
#ifndef JELLO_INTERNAL_VM_INTERNAL_H
#define JELLO_INTERNAL_VM_INTERNAL_H

#include <stdint.h>

#include <jello.h>

/* Opaque in public API; full definition here for VM implementation. */
struct jello_bc_module {
  uint32_t version;
  uint32_t features;
  uint32_t ntypes;
  const jello_type_entry* types;
  uint32_t nsigs;
  const jello_fun_sig* sigs;
  uint32_t natoms;
  const char* atoms_data;
  const char* const* atoms;
  uint8_t proto_enabled; /* 1 if __proto__ atom exists and enables prototype chain */
  uint32_t nconst_i64;
  const int64_t* const_i64;
  uint32_t nconst_f64;
  const double* const_f64;
  uint32_t nconst_bytes;
  const uint32_t* const_bytes_len;
  const uint32_t* const_bytes_off;
  const uint8_t* const_bytes_data;
  uint32_t nfuncs;
  const jello_bc_function* funcs;
  uint32_t entry;
  uint32_t nsource_files;
  const char* const* source_files;
};

typedef struct enum_nullary_type_cache {
  jello_enum* slots[256];
} enum_nullary_type_cache;

struct jello_vm {
  jello_value* spill;
  uint32_t spill_len;
  uint32_t spill_cap;

  /* Safety limits */
  uint64_t fuel_limit;
  uint64_t fuel_remaining;
  uint32_t max_bytes_len;
  uint32_t max_array_len;

  void* gc_objects;
  size_t gc_bytes_live;
  size_t gc_next_collect;
  /* Recycled numeric boxes / small inline objects. Linked via gc_hdr->next; not on gc_objects. */
  void* gc_box_free;
  uint32_t gc_box_free_len;

  jello_value* gc_roots;
  uint32_t gc_roots_len;
  uint32_t gc_roots_cap;

  uint64_t gc_collections;
  uint64_t gc_freed_objects;
  uint64_t gc_freelist_hits;
  uint64_t gc_freelist_misses;

  void* call_frames;
  uint32_t call_frames_len;
  uint32_t call_frames_cap;
  uint32_t call_frames_max;

  void* const_fun_cache;
  uint32_t const_fun_cache_len;

  /* Flyweight cache for nullary enum values (cleared with const_fun_cache). */
  enum_nullary_type_cache** enum_nullary_by_type;
  uint32_t enum_nullary_ntypes;

  /* Frame layout cache + register frame memory pool (performance-critical for calls). */
  void* frame_layouts; /* frame_layout[] (len = m->nfuncs) for running_module */
  uint32_t frame_layouts_len;
  const jello_bc_module* frame_layouts_mod;
#define RF_POOL_BUCKETS 64u
  void* rf_free_by_size[64]; /* rf_mem_block* per size bucket for O(1) alloc */

  /* Contiguous frame stack: avoids per-call malloc for hot path. */
  uint8_t* frame_stack;
  uint32_t frame_stack_top;
  uint32_t frame_stack_cap;

  const jello_bc_module* running_module;

  void* exc_handlers;
  uint32_t exc_handlers_len;
  uint32_t exc_handlers_cap;
  uint8_t exc_pending;
  jello_value exc_payload;

  jello_trap_code trap_code;
  const char* trap_msg;
  char trap_msg_buf[512];
  char stack_trace_buf[1024];

  char* entry_path;

  /* Program command-line args (after module path); owned copies. */
  uint32_t program_argc;
  char** program_argv;

  uint32_t thread_id;

  const char* jdll_active_library;
  const char* jdll_active_export;
  const char* jdll_active_sym;
  void* jdll_active_fn;

  /** When set, a frame with `jdll_ret_capture` stores its return value here. */
  jello_value* jdll_call_out;

  /* Partial JIT (optional build). */
  void* jit_state;
  uint8_t jit_enabled;
};

typedef struct reg_frame {
  uint8_t* mem;
  const uint32_t* off; /* nregs offsets */
  uint32_t nregs;
  uint32_t total;      /* total bytes in mem */
  uint8_t off_shared;  /* 1 if `off` points into cached layout */
} reg_frame;

typedef struct frame_layout {
  uint32_t nregs;
  uint32_t total;
  uint32_t* off;
  uint8_t has_pointer_or_dynamic; /* 1 if any reg needs init (Dynamic or pointer type) */
  uint8_t jit_ineligible;         /* 1: never compile; skip pc==0 JIT probe */
  uint8_t jit_self_rec;           /* 1: only self-CALL (numeric) — entry-compile candidate */
} frame_layout;

typedef struct rf_mem_block {
  struct rf_mem_block* next;
  uint32_t size;
} rf_mem_block;

typedef struct call_frame {
  const jello_bc_function* f;
  reg_frame rf;
  uint32_t pc;
  uint32_t caller_dst; // destination register in caller
  uint32_t exc_base;   // exc_handlers_len at frame entry (O(1) cleanup)
  uint8_t has_caller;
  uint8_t jdll_ret_capture; /* 1: RET captures into vm->jdll_call_out */
  uint8_t has_pointer_or_dynamic; /* cached from frame_layout for self-call fast path */
  uint8_t jit_entry_done;   /* 1: JIT entry attempted at pc==0 for this frame */
  uint8_t jit_resume_hint;  /* 1: try mid-function JIT resume (e.g. after CALL yield) */
  uint8_t jit_osr_hint;     /* 1: try mid-function JIT entry after backedge compile */
  void* jit_return_addr;    /* native resume after this frame returns (JIT self-call) */
} call_frame;

typedef struct exc_handler {
  uint32_t frame_index;
  uint32_t catch_pc;
  uint32_t dst_reg;
  uint8_t trap_only; /* 1: traps only (do not catch THROWN), 0: catch everything */
} exc_handler;

/* --- exec context (for opcode handlers) --- */
typedef struct exec_ctx {
  jello_vm* vm;
  const jello_bc_module* m;
  const jello_bc_function* f;
  call_frame* fr;
  call_frame* frames;
  jello_value* out;
  jello_value* out_exports;   /* REPL: capture entry module's exports when top-level returns */
  uint32_t entry_module_idx; /* which reg holds exports (UINT32_MAX = don't capture) */
  uint32_t min_call_frames;  /* nested JDLL callback: stop when len <= this (0 = run to completion) */
  void* jit_resume_entry;    /* JIT: non-NULL → prologue branches here (mid-function resume) */
  void* jit_self_resume;     /* JIT: ret_self leaves native return address here for br */
  void* jit_call_entry;      /* JIT: call_direct leaves callee native entry here for br */
} exec_ctx;

typedef enum op_result {
  OP_CONTINUE,
  OP_RETURN,   /* exec_entry should return JELLO_EXEC_OK */
  OP_TRAP,     /* goto exception_dispatch */
} op_result;

typedef op_result (*op_handler_fn)(exec_ctx* ctx, const jello_insn* ins);

/* --- trap/panic (used by interp + ops) --- */
jello_exec_status jello_vm_trap(jello_vm* vm, jello_trap_code code, const char* msg);
void jello_vm_panic(void);

int vm_arg_types_compatible(const jello_bc_module* m, uint32_t src_tid, uint32_t dst_tid);

/* --- reg.c --- */
jello_type_kind vm_reg_kind(const jello_bc_module* m, const jello_bc_function* f, uint32_t r);
static inline void* vm_reg_ptr(reg_frame* rf, uint32_t r) {
  return (void*)(rf->mem + rf->off[r]);
}
static inline uint32_t vm_load_u32(reg_frame* rf, uint32_t r) {
  return *(const uint32_t*)vm_reg_ptr(rf, r);
}
static inline void vm_store_u32(reg_frame* rf, uint32_t r, uint32_t v) {
  *(uint32_t*)vm_reg_ptr(rf, r) = v;
}
static inline void vm_store_u32_masked(reg_frame* rf, uint32_t r, uint32_t v, jello_type_kind k) {
  if(k == JELLO_T_I8) v = (uint32_t)(int32_t)(int8_t)(v & 0xFFu);
  else if(k == JELLO_T_I16) v = (uint32_t)(int32_t)(int16_t)(v & 0xFFFFu);
  vm_store_u32(rf, r, v);
}
float vm_load_f32(reg_frame* rf, uint32_t r);
void vm_store_f32(reg_frame* rf, uint32_t r, float v);
int32_t vm_load_i32ish(reg_frame* rf, uint32_t r);
int64_t vm_load_i64(reg_frame* rf, uint32_t r);
void vm_store_i64(reg_frame* rf, uint32_t r, int64_t v);
static inline double vm_load_f64(reg_frame* rf, uint32_t r) {
  return *(const double*)vm_reg_ptr(rf, r);
}
static inline void vm_store_f64(reg_frame* rf, uint32_t r, double v) {
  *(double*)vm_reg_ptr(rf, r) = v;
}
uint16_t vm_load_f16_bits(reg_frame* rf, uint32_t r);
void vm_store_f16_bits(reg_frame* rf, uint32_t r, uint16_t bits);
float vm_f16_bits_to_f32(uint16_t bits);
uint16_t vm_f32_to_f16_bits(float f);
static inline jello_value vm_load_val(reg_frame* rf, uint32_t r) {
  return *(const jello_value*)vm_reg_ptr(rf, r);
}
static inline void vm_store_val(reg_frame* rf, uint32_t r, jello_value v) {
  *(jello_value*)vm_reg_ptr(rf, r) = v;
}
static inline void* vm_load_ptr(reg_frame* rf, uint32_t r) {
  return *(void* const*)vm_reg_ptr(rf, r);
}
static inline void vm_store_ptr(reg_frame* rf, uint32_t r, void* p) {
  *(void**)vm_reg_ptr(rf, r) = p;
}

/* --- spill.c --- */
void vm_spill_push(jello_vm* vm, jello_value v);
jello_value vm_spill_pop(jello_vm* vm);

/* --- box.c --- */
jello_value vm_box_from_typed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f, reg_frame* rf, uint32_t r);
void vm_store_from_boxed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f, reg_frame* rf, uint32_t dst, jello_value v);
/* Store typed numeric into an existing object/array slot: mutate box in place when kinds match. */
int vm_store_num_inplace(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f,
                         reg_frame* rf, uint32_t src_reg, jello_value* slot);
jello_value vm_clone_numbox(jello_vm* vm, jello_value v);
/* Typed object atom get/set shared by interpreter hot path and JIT stubs.
 * Return 0 if a trap was raised (null / deep proto); 1 on success. */
int vm_obj_get_atom_typed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f,
                          reg_frame* rf, uint32_t dst, uint32_t obj_reg, uint32_t atom_id);
int vm_obj_set_atom_typed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f,
                          reg_frame* rf, uint32_t val_reg, uint32_t obj_reg, uint32_t atom_id);
int vm_checked_f64_to_i32(jello_vm* vm, double x, uint32_t* out_u32);
int vm_checked_f64_to_i64(jello_vm* vm, double x, int64_t* out_i64);
uint32_t vm_kindof_dynamic(jello_value v);
uint32_t vm_expected_obj_kind_for_typed_ptr(jello_type_kind k);

/* --- frame.c (type helpers) --- */
uint8_t vm_type_is_nonptr(jello_type_kind k);

/* --- call.c --- */
void vm_copy_arg_strict(jello_vm* vm,
                        const jello_bc_module* m,
                        const jello_bc_function* caller_f, reg_frame* caller_rf, uint32_t src,
                        const jello_bc_function* callee_f, reg_frame* callee_rf, uint32_t dst);
void vm_init_args_and_caps(jello_vm* vm, const jello_bc_module* m,
                           const jello_bc_function* callee_f, reg_frame* callee_rf,
                           const jello_bc_function* caller_f, reg_frame* caller_rf,
                           uint32_t first_arg, uint32_t nargs,
                           const jello_function* funobj);

/* --- frame.c --- */
void vm_rf_release(jello_vm* vm, reg_frame* rf);
const frame_layout* vm_get_frame_layout(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f);
void vm_frame_cache_shutdown(jello_vm* vm);
void vm_frame_stack_grow(jello_vm* vm, uint32_t need); /* cold: realloc + rebase */
static inline uint8_t* vm_frame_stack_bump(jello_vm* vm, uint32_t size) {
  if(size == 0u) return NULL;
  if(vm->frame_stack_top + size > vm->frame_stack_cap) vm_frame_stack_grow(vm, size);
  uint8_t* mem = vm->frame_stack + vm->frame_stack_top;
  vm->frame_stack_top += size;
  return mem;
}
#define VM_CALL_FRAMES_INITIAL 64u
void vm_call_frames_reserve(jello_vm* vm);
/* Grow call_frames if full; may rebase *fr_inout. */
void vm_call_frames_grow_if_full(jello_vm* vm, call_frame** fr_inout);
/* Numeric self-call: bump frame stack, copy args, push frame. Caller ensures
 * same function, !has_pointer_or_dynamic, nargs<=16, stack depth ok. */
call_frame* vm_push_self_numeric(jello_vm* vm, const jello_bc_module* m, call_frame* fr,
                                 uint32_t first_arg, uint32_t nargs, uint32_t caller_dst,
                                 void* jit_return_addr, uint8_t jit_entry_done);
int vm_push_frame(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* callee_f,
                  const jello_bc_function* caller_f, uint32_t caller_frame_index, uint32_t caller_dst,
                  uint32_t first_arg, uint32_t nargs, const jello_function* funobj, uint8_t has_caller);
int vm_push_frame_jello_call(jello_vm* vm, const jello_bc_module* m, jello_function* fn,
                             jello_value bound_this, const jello_value* args, uint32_t nargs,
                             uint8_t jdll_ret_capture);
int vm_replace_frame(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* callee_f,
                     const jello_bc_function* caller_f, uint32_t first_arg, uint32_t nargs,
                     const jello_function* funobj);
/* Phase 4: push frame with args from jello_value* (for chunk exec with pre-bound imports). */
int vm_push_frame_from_values(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* callee_f,
                              const jello_value* args, uint32_t nargs);
int vm_push_frame_closure_from_values(jello_vm* vm, const jello_bc_module* m, jello_function* fn,
                                      const jello_value* args, uint32_t nargs);

/* --- ops/ops_builtins.c --- */
#define JELLO_NATIVE_BUILTIN_COUNT 46u
int jello_is_native_builtin(uint32_t func_index);
void jello_invoke_native_builtin(exec_ctx* ctx, const jello_insn* ins, uint32_t func_index, uint32_t first_arg_reg);
int jello_is_jdll_prim(uint32_t func_index);

int jello_value_is_copy_safe(jello_value v);
int jello_value_is_thread_shareable(jello_value v);
int jello_value_is_thread_cap_safe(jello_value v);
int jello_function_spawn_caps_ok(jello_function* fn);
int jello_value_copy_to_vm(jello_vm* dst, jello_value src, jello_value* out);
int jello_value_share_to_vm(jello_vm* dst, jello_value src, jello_value* out);
jello_function* jello_closure_share_to_vm(jello_vm* dst, jello_function* src);

void jello_vm_bind_current(jello_vm* vm);

/* --- exc.c --- */
void vm_exc_push(jello_vm* vm, uint32_t frame_index, uint32_t catch_pc, uint32_t dst_reg, uint8_t trap_only);
int vm_exc_pop(jello_vm* vm, exc_handler* out);
void vm_exc_pop_for_frame(jello_vm* vm, uint32_t frame_index);
void vm_unwind_all_frames(jello_vm* vm);
/* Resolve a module atom id by UTF-8 name; returns def when not found. */
uint32_t vm_module_atom_id_or_default(const jello_bc_module* m, const char* name, uint32_t def);
/* Returns 1 if exec_entry should return JELLO_EXEC_TRAP, 0 to continue loop. */
int vm_exc_dispatch(jello_vm* vm, jello_value* out);

/* --- ops/dispatch.c --- */
/* Legacy single-instruction dispatch (used as fallback). */
op_result op_dispatch(exec_ctx* ctx, const jello_insn* ins);
/* Interpreter loop (runs until completion/trap). */
jello_exec_status vm_exec_loop(exec_ctx* ctx);

#endif /* JELLO_INTERNAL_VM_INTERNAL_H */

