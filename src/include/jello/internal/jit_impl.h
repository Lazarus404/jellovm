// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

// JIT subsystem internals (config, cache, IR, runtime, backend).
// Include from src/jit/ only — VM integration uses jit_internal.h.
#ifndef JELLO_INTERNAL_JIT_IMPL_H
#define JELLO_INTERNAL_JIT_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include <jello.h>
#include <jello/internal/vm_internal.h>

struct jello_vm;
struct jello_bc_module;

#define JELLO_JIT_HOT_REJECTED 0xFFFFFFFEu

/* --- config ---------------------------------------------------------------- */

/* Runtime JIT toggle (VM flag + JELLO_JIT env). Does not reflect build-time support. */
int jello_jit_config_enabled(const struct jello_vm* vm);

uint32_t jello_jit_config_hot_threshold(void);
int jello_jit_config_run_enabled(void);
int jello_jit_config_dump_enabled(void);

/* --- cache ----------------------------------------------------------------- */

typedef struct jello_jit_code {
  void* base;
  void* entry;  /* prologue (C-frame setup); used by jello_jit_runtime_run */
  void* body;   /* after prologue; used by CALL_SELF/CALL_DIRECT (shared C frame) */
  size_t size;
  size_t map_size;
  uint32_t func_idx;
  uint32_t* bc_pc_map;
  uint32_t nbc_pc_map;
} jello_jit_code;

typedef struct jello_jit_state jello_jit_state;

jello_jit_state* jello_jit_state_create(void);
void jello_jit_state_destroy(jello_jit_state* st);

/* nest depth for callee chaining (internal; also used by runtime). */
uint32_t jello_jit_state_nest(const jello_jit_state* st);
void jello_jit_state_nest_inc(jello_jit_state* st);
void jello_jit_state_nest_dec(jello_jit_state* st);

jello_jit_code* jello_jit_cache_lookup(jello_jit_state* st, const struct jello_bc_module* m, uint32_t func_idx);
jello_jit_code* jello_jit_cache_insert(
    jello_jit_state* st,
    const struct jello_bc_module* m,
    uint32_t func_idx,
    const uint8_t* code,
    size_t size,
    size_t entry_off,
    size_t body_off,
    const uint32_t* bc_pc_map,
    uint32_t nbc_pc_map
);
void jello_jit_cache_drop_module(jello_jit_state* st, const struct jello_bc_module* m);

uint32_t* jello_jit_hot_counter(jello_jit_state* st, const struct jello_bc_module* m, uint32_t func_idx);
int jello_jit_hot_is_rejected(jello_jit_state* st, const struct jello_bc_module* m, uint32_t func_idx);
void jello_jit_hot_mark_rejected(jello_jit_state* st, const struct jello_bc_module* m, uint32_t func_idx);
int jello_jit_func_needs_enter(jello_vm* vm, const struct jello_bc_module* m, const jello_bc_function* f);
int jello_jit_func_is_compiled(jello_vm* vm, const struct jello_bc_module* m, const jello_bc_function* f);

/* --- IR -------------------------------------------------------------------- */

typedef enum jello_jit_ir_op {
  JIR_NOP = 0,
  JIR_MOV_REG,
  JIR_LOAD_I32,
  JIR_LOAD_I64,
  JIR_LOAD_F64,
  JIR_LOAD_F32,
  JIR_BIN_I32,
  JIR_BIN_I64,
  JIR_BIN_F32,
  JIR_BIN_F64,
  JIR_CMP_I32,
  JIR_CMP_I64,
  JIR_CMP_F32,
  JIR_CMP_F64,
  JIR_NEG_I32,
  JIR_NEG_I64,
  JIR_NEG_F64,      /* a=dst f64, b=src f64 */
  JIR_NEG_F32,      /* a=dst f32, b=src f32 */
  JIR_JMP,
  JIR_JMP_IF,
  JIR_RET,
  JIR_SLOW,
  JIR_CALL_SELF,   /* self-recursive CALL: a=dst, b=first_arg, c=nargs */
  /* same-module CALL: a=dst, b=first, c=nargs,
   * imm = (callee_reg<<16)|bytecode_fi; callee_reg=0xFFFF means no funobj (CALL). */
  JIR_CALL_DIRECT,
  JIR_FUEL_CHECK,
  JIR_BYTES_NEW,    /* a=dst bytes, b=len i32 */
  JIR_BYTES_LEN,    /* a=dst i32, b=bytes */
  JIR_BYTES_GET_U8, /* a=dst i32, b=bytes, c=idx i32 */
  JIR_BYTES_SET_U8, /* a=val i32, b=bytes, c=idx i32 */
  JIR_BYTES_READ,   /* a=dst, b=bytes, c=off i32, imm=read kind */
  JIR_BYTES_WRITE,  /* a=val, b=bytes, c=off i32, imm=write kind */
  JIR_ARRAY_LEN,    /* a=dst i32, b=array */
  JIR_ARRAY_GET,    /* a=dst, b=array, c=idx i32 */
  JIR_ARRAY_SET,    /* a=val, b=array, c=idx i32 */
  JIR_ARRAY_NEW,    /* a=dst array, b=len i32 */
  JIR_OBJ_GET_ATOM, /* a=dst, b=object, imm=atom_id */
  JIR_OBJ_SET_ATOM, /* a=val, b=object, imm=atom_id */
  JIR_OBJ_NEW,      /* a=dst object */
  JIR_SQRT_F64,     /* a=dst f64, b=src f64 — Math.sqrt native */
  JIR_LOAD_NULL,    /* a=dst — store tagged null (jello_make_null) */
  JIR_ASSERT,       /* a=cond i32, b=msg bytes when c=1, c=has_msg */
  JIR_CONST_BYTES,  /* a=dst bytes, imm=const_bytes index */
  JIR_CONST_FUN,    /* a=dst fun, imm=func_index (flyweight cache) */
  JIR_CLOSURE,      /* a=dst, b=first_cap, c=ncaps, imm=func_index */
  JIR_CONV,         /* a=dst, b=src, imm=jello_jit_conv kind */
} jello_jit_ir_op;

typedef enum jello_jit_ir_bin {
  JIR_BIN_ADD = 0,
  JIR_BIN_SUB,
  JIR_BIN_MUL,
  JIR_BIN_SDIV,
  JIR_BIN_MOD,
  JIR_BIN_SHL,
  JIR_BIN_SHR,
} jello_jit_ir_bin;

typedef enum jello_jit_ir_cmp {
  JIR_CMP_EQ = 0,
  JIR_CMP_LT,
} jello_jit_ir_cmp;

/* Width / float converts (imm on JIR_CONV). Checked kinds may TRAP. */
typedef enum jello_jit_conv {
  JIR_CONV_SEXT_I64 = 0,
  JIR_CONV_SEXT_I16,
  JIR_CONV_TRUNC_I8,
  JIR_CONV_TRUNC_I16,
  JIR_CONV_I32_FROM_I64,
  JIR_CONV_F64_FROM_I32,
  JIR_CONV_I32_FROM_F64,
  JIR_CONV_F64_FROM_I64,
  JIR_CONV_I64_FROM_F64,
  JIR_CONV_F32_FROM_I32,
  JIR_CONV_I32_FROM_F32,
  JIR_CONV_F64_FROM_F32,
  JIR_CONV_F32_FROM_F64,
  JIR_CONV_F32_FROM_I64,
  JIR_CONV_I64_FROM_F32,
} jello_jit_conv;

typedef enum jello_jit_bytes_read_kind {
  JIT_BREAD_U16_LE = 0,
  JIT_BREAD_U16_BE,
  JIT_BREAD_U32_LE,
  JIT_BREAD_U32_BE,
  JIT_BREAD_I32_LE,
  JIT_BREAD_I32_BE,
  JIT_BREAD_F32_LE,
  JIT_BREAD_F32_BE,
} jello_jit_bytes_read_kind;

typedef enum jello_jit_bytes_write_kind {
  JIT_BWRITE_U16_LE = 0,
  JIT_BWRITE_U16_BE,
  JIT_BWRITE_U32_LE,
  JIT_BWRITE_U32_BE,
  JIT_BWRITE_I32_LE,
  JIT_BWRITE_I32_BE,
  JIT_BWRITE_F32_LE,
  JIT_BWRITE_F32_BE,
} jello_jit_bytes_write_kind;

typedef struct jello_jit_ir_insn {
  jello_jit_ir_op op;
  uint16_t a;
  uint16_t b;
  uint16_t c;
  int32_t imm;
  uint32_t bc_pc;
} jello_jit_ir_insn;

typedef struct jello_jit_ir_func {
  jello_jit_ir_insn* insns;
  uint32_t ninsns;
  uint32_t nregs;
  uint8_t has_slow;
  uint8_t has_call_slow;   /* unspecialized CALL/CALLR/TAIL* */
  uint8_t has_self_call;   /* JIR_CALL_SELF present */
  uint8_t has_direct_call; /* JIR_CALL_DIRECT present */
  uint32_t nslow;
  int ok;
  char reject_reason[64];
} jello_jit_ir_func;

void jello_jit_ir_func_free(jello_jit_ir_func* ir);
jello_jit_ir_func jello_jit_ir_build(const jello_bc_module* m, const jello_bc_function* f);

/* --- runtime --------------------------------------------------------------- */

/* x64 emit is SysV (ctx in RDI, args in RSI/RDX/…). On Win64/MinGW the host
 * default is Microsoft x64 (ctx in RCX). Mark JIT entry + helpers SysV so
 * C↔native matches the emitter without a second Win64 emit path. */
#if defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define JELLO_JIT_SYSV __attribute__((sysv_abi))
#else
#define JELLO_JIT_SYSV
#endif

typedef enum jello_jit_exit {
  JELLO_JIT_EXIT_CONTINUE = 0,
  JELLO_JIT_EXIT_YIELD = 1,
  JELLO_JIT_EXIT_RETURNED = 2,
  JELLO_JIT_EXIT_TRAP = 3,
} jello_jit_exit;

typedef enum jello_jit_run_result {
  JELLO_JIT_RUN_CONTINUE = 0,
  JELLO_JIT_RUN_RETURNED = 1,
  JELLO_JIT_RUN_STEPPED = 2,
  JELLO_JIT_RUN_NESTED_RET = 3,
} jello_jit_run_result;

/* Native entry: (exec_ctx*) -> jello_jit_exit; SysV on Win64 (see JELLO_JIT_SYSV). */
typedef jello_jit_exit(JELLO_JIT_SYSV* jello_jit_entry_fn)(exec_ctx* ctx);

jello_jit_run_result jello_jit_runtime_run(exec_ctx* ctx, const jello_jit_code* code);

void jello_jit_deopt_sync_ctx(exec_ctx* ctx);
void jello_jit_deopt_continue(exec_ctx* ctx, uint32_t bc_pc);
jello_jit_exit jello_jit_runtime_yield_at_pc(exec_ctx* ctx, uint32_t bc_pc);

JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_slow_op(exec_ctx* ctx, uint32_t bc_pc);
/* Self-call: push frame; store native resume in callee->jit_return_addr. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_call_self(
    exec_ctx* ctx,
    uint32_t first_arg,
    uint32_t nargs,
    uint32_t caller_dst,
    uint32_t resume_pc,
    void* return_addr
);
/* Same-module call: push callee; if compiled, leave body in ctx->jit_call_entry.
 * callee_reg=0xFFFFFFFF: no funobj. Else load CALLR callee and install captures. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_call_direct(
    exec_ctx* ctx,
    uint32_t first_arg,
    uint32_t nargs,
    uint32_t caller_dst,
    uint32_t resume_pc,
    void* return_addr,
    uint32_t bytecode_fi,
    uint32_t callee_reg
);
/* Pop + copy return; if jit_return_addr set, leave it in ctx->jit_self_resume for br. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_ret_self(exec_ctx* ctx, uint32_t ret_reg);
void* jello_jit_runtime_take_resume(exec_ctx* ctx);
void* jello_jit_runtime_frame_mem(exec_ctx* ctx);
uint32_t jello_jit_runtime_load_u32(exec_ctx* ctx, uint32_t reg);
void jello_jit_runtime_ret(exec_ctx* ctx, uint32_t ret_reg);
jello_jit_exit jello_jit_runtime_ret_status(exec_ctx* ctx, uint32_t ret_reg);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_fuel_trap(exec_ctx* ctx);
jello_jit_exit jello_jit_runtime_fuel_check(exec_ctx* ctx);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_len(exec_ctx* ctx, uint32_t dst, uint32_t bytes_reg);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_get_u8(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t bytes_reg,
    uint32_t idx_reg
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_set_u8(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t bytes_reg,
    uint32_t idx_reg
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_read(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t bytes_reg,
    uint32_t off_reg,
    uint32_t kind
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_write(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t bytes_reg,
    uint32_t off_reg,
    uint32_t kind
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_len(exec_ctx* ctx, uint32_t dst, uint32_t array_reg);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_get(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t array_reg,
    uint32_t idx_reg
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_set(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t array_reg,
    uint32_t idx_reg
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_array_new(exec_ctx* ctx, uint32_t dst, uint32_t len_reg);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_bytes_new(exec_ctx* ctx, uint32_t dst, uint32_t len_reg);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_assert(exec_ctx* ctx, uint32_t cond_reg, uint32_t msg_reg, uint32_t has_msg);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_const_bytes(exec_ctx* ctx, uint32_t dst, uint32_t idx);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_const_fun(exec_ctx* ctx, uint32_t dst, uint32_t func_index);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_closure(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t first_cap,
    uint32_t ncaps,
    uint32_t func_index
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_conv(exec_ctx* ctx, uint32_t dst, uint32_t src, uint32_t kind);
/* OBJ_GET_ATOM slow path (C helper): null obj, proto chain walk, DYNAMIC dst
 * (numbox clone), or native unbox miss. x64 may inline own-table get for
 * F64/I32/I64/ptr dst when !proto || obj->proto==NULL. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_get_atom(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t obj_reg,
    uint32_t atom_id
);
/* OBJ_SET_ATOM slow path: null obj, __proto__, rehash/grow, boxing alloc,
 * or inplace miss. x64/ARM64 may inline occupied numeric inplace (F64/I64),
 * I32 tagged overwrite, and empty/tomb insert when load factor allows
 * (I32 fully native; F64/I64 via obj_insert_atom). */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_set_atom(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id
);
/* Insert into a known empty/tomb slot (caller checked load factor). Boxes
 * val_reg, writes key/state/val, increments len. */
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_insert_atom(
    exec_ctx* ctx,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id,
    uint32_t slot_i
);
JELLO_JIT_SYSV jello_jit_exit jello_jit_runtime_obj_new(exec_ctx* ctx, uint32_t dst);
int jello_jit_runtime_cmp_i32(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t lhs_reg,
    uint32_t rhs_reg,
    uint32_t cmp_kind,
    uint32_t rhs_imm,
    uint32_t rhs_is_imm
);
int jello_jit_runtime_cmp_f64(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t lhs_reg,
    uint32_t rhs_reg,
    uint32_t cmp_kind
);
int jello_jit_runtime_cmp_f32(
    exec_ctx* ctx,
    uint32_t dst,
    uint32_t lhs_reg,
    uint32_t rhs_reg,
    uint32_t cmp_kind
);

typedef struct jello_jit_layout {
  uint32_t exec_ctx_fr;
  uint32_t call_frame_rf_mem;
  uint32_t exec_ctx_jit_resume_entry;
  uint32_t exec_ctx_jit_self_resume;
  uint32_t exec_ctx_jit_call_entry;
  uint32_t vm_fuel_limit;
  uint32_t vm_fuel_remaining;
} jello_jit_layout;

const jello_jit_layout* jello_jit_runtime_layout(void);

/* --- backend --------------------------------------------------------------- */

typedef struct jello_jit_emit_buf {
  uint8_t* data;
  size_t size;
  size_t cap;
} jello_jit_emit_buf;

typedef struct jello_jit_backend {
  const char* name;
  int (*emit_func)(
      const jello_jit_ir_func* ir,
      const jello_bc_function* f,
      const frame_layout* layout,
      const jello_bc_module* m,
      jello_jit_emit_buf* out,
      size_t* out_entry_off,
      size_t* out_body_off,
      uint32_t** out_bc_pc_map,
      uint32_t* out_nbc_pc_map
  );
} jello_jit_backend;

void jello_jit_emit_buf_free(jello_jit_emit_buf* buf);
int jello_jit_emit_buf_reserve(jello_jit_emit_buf* buf, size_t extra);
int jello_jit_emit_u32(jello_jit_emit_buf* buf, uint32_t word);
int jello_jit_emit_bytes(jello_jit_emit_buf* buf, const uint8_t* bytes, size_t len);

const jello_jit_backend* jello_jit_backend_select(void);

#endif
