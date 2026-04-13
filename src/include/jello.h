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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
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

#ifndef JELLO_H
#define JELLO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <inttypes.h>
#include <stdarg.h>

// `char16_t`/`char32_t` are normally provided by `<uchar.h>` in C11, but some
// libcs/SDKs (notably some Apple SDK configurations) don't ship that header.
#if !defined(__cplusplus)
#  if defined(__CHAR16_TYPE__) && defined(__CHAR32_TYPE__)
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
#  else
typedef uint16_t char16_t;
typedef uint32_t char32_t;
#  endif
#endif

#define JELLO_VERSION 0x011000u
#define JELLO_WSIZE ((uint32_t)sizeof(void*))
#define JELLO_IS_64 ((UINTPTR_MAX) > 0xffffffffu)
#define JELLO_PTR_FMT "%" PRIxPTR
#define JELLO_THREAD_LOCAL _Thread_local

#ifndef JELLO_API
#define JELLO_API extern
#endif

typedef intptr_t int_val;
typedef int64_t int64;
typedef uint64_t uint64;
typedef char16_t uchar;
#define USTR(str) u##str

/* --- type.h --- */
typedef uint32_t jello_type_id;

typedef enum jello_type_kind {
  /* Keep in sync with `jelloc/src/jlo/format.rs` (TypeKind). */
  JELLO_T_BOOL = 1,
  JELLO_T_ATOM = 2,
  JELLO_T_I8 = 3,
  JELLO_T_I16 = 4,
  JELLO_T_I32 = 5,
  JELLO_T_I64 = 6,
  JELLO_T_F16 = 7,
  JELLO_T_F32 = 8,
  JELLO_T_F64 = 9,
  JELLO_T_BYTES = 10,
  JELLO_T_LIST = 11,
  JELLO_T_ARRAY = 12,
  JELLO_T_OBJECT = 13,
  JELLO_T_FUNCTION = 14,
  JELLO_T_ABSTRACT = 15,
  JELLO_T_DYNAMIC = 16,
} jello_type_kind;

typedef struct jello_type_entry {
  jello_type_kind kind;
  union {
    struct { jello_type_id elem; } unary;
    struct { uint32_t sig_id; } fun;
    struct { uint32_t abstract_id; } abs;
  } as;
} jello_type_entry;

typedef struct jello_fun_sig {
  jello_type_id ret;
  uint16_t nargs;
  const jello_type_id* args;
} jello_fun_sig;

/* --- value.h --- */
typedef uintptr_t jello_value;

enum {
  JELLO_TAG_PTR  = 0x0u,
  JELLO_TAG_I32  = 0x1u,
  JELLO_TAG_ATOM = 0x2u,
  JELLO_TAG_BOOL = 0x3u,
  JELLO_TAG_NULL = 0x4u,
};

static inline jello_value jello_make_null(void) {
  return (jello_value)JELLO_TAG_NULL;
}
static inline int jello_is_null(jello_value v) {
  return (unsigned)(v & 0x7u) == JELLO_TAG_NULL;
}
static inline jello_value jello_make_bool(int b) {
  return ((jello_value)((b != 0) ? 1u : 0u) << 3) | (jello_value)JELLO_TAG_BOOL;
}
static inline int jello_is_bool(jello_value v) {
  return (unsigned)(v & 0x7u) == JELLO_TAG_BOOL;
}
static inline int jello_as_bool(jello_value v) {
  return (int)((v >> 3) & 1u);
}
static inline jello_value jello_make_atom(uint32_t atom_id) {
  return ((jello_value)atom_id << 3) | (jello_value)JELLO_TAG_ATOM;
}
static inline int jello_is_atom(jello_value v) {
  return (unsigned)(v & 0x7u) == JELLO_TAG_ATOM;
}
static inline uint32_t jello_as_atom(jello_value v) {
  return (uint32_t)(v >> 3);
}
static inline jello_value jello_make_i32(int32_t x) {
  return ((jello_value)(uintptr_t)(uint32_t)x << 3) | (jello_value)JELLO_TAG_I32;
}
static inline int jello_is_i32(jello_value v) {
  return (unsigned)(v & 0x7u) == JELLO_TAG_I32;
}
static inline int32_t jello_as_i32(jello_value v) {
  return (int32_t)(uint32_t)(v >> 3);
}
static inline int jello_is_ptr(jello_value v) {
  return (unsigned)(v & 0x7u) == JELLO_TAG_PTR;
}
static inline void* jello_as_ptr(jello_value v) {
  return (void*)(uintptr_t)v;
}
static inline jello_value jello_from_ptr(void* p) {
  return (jello_value)(uintptr_t)p;
}

typedef enum jello_obj_kind {
  JELLO_OBJ_BYTES = 1,
  JELLO_OBJ_FUNCTION = 2,
  JELLO_OBJ_LIST = 3,
  JELLO_OBJ_ARRAY = 4,
  JELLO_OBJ_OBJECT = 5,
  JELLO_OBJ_ABSTRACT = 6,
  JELLO_OBJ_BOX_I64 = 7,
  JELLO_OBJ_BOX_F64 = 8,
  JELLO_OBJ_BOX_F32 = 9,
  JELLO_OBJ_BOX_F16 = 10,
} jello_obj_kind;

typedef struct jello_obj_header {
  uint32_t kind;
  uint32_t type_id;
} jello_obj_header;

static inline const jello_obj_header* jello_obj_header_of(jello_value v) {
  return (const jello_obj_header*)jello_as_ptr(v);
}
static inline uint32_t jello_obj_kind_of(jello_value v) {
  return jello_obj_header_of(v)->kind;
}

/* --- bytecode.h --- */
#define JELLO_BC_MAGIC 0x4A4C5942u

typedef enum jello_bc_feature_flags {
  JELLO_BC_FEAT_NONE = 0,
  JELLO_BC_FEAT_CONST64 = 1u << 0,
  JELLO_BC_FEAT_CONSTBYTES = 1u << 1,
  JELLO_BC_FEAT_CAP_START = 1u << 2,
} jello_bc_feature_flags;

typedef enum jello_op {
  /* Control / misc */
  JOP_NOP = 0,
  JOP_RET = 1,
  JOP_JMP = 2,
  JOP_JMP_IF = 3,
  JOP_MOV = 4,
  JOP_TRY = 5,
  JOP_ENDTRY = 6,
  JOP_THROW = 7,
  JOP_ASSERT = 8,

  /* Calls / closures */
  JOP_CALL = 9,
  JOP_CALLR = 10,
  JOP_TAILCALL = 11,
  JOP_TAILCALLR = 12,
  JOP_CONST_FUN = 13,
  JOP_CLOSURE = 14,
  JOP_BIND_THIS = 15,

  /* Typed constants */
  JOP_CONST_I32 = 16,
  JOP_CONST_I8_IMM = 17,
  JOP_CONST_BOOL = 18,
  JOP_CONST_NULL = 19,
  JOP_CONST_ATOM = 20,
  JOP_CONST_F16 = 21,
  JOP_CONST_F32 = 22,
  JOP_CONST_I64 = 23,
  JOP_CONST_F64 = 24,
  JOP_CONST_BYTES = 25,

  /* Bytes helpers */
  JOP_BYTES_CONCAT2 = 26,
  JOP_BYTES_CONCAT_MANY = 27,

  /* I32 arithmetic */
  JOP_ADD_I32 = 28,
  JOP_SUB_I32 = 29,
  JOP_MUL_I32 = 30,
  JOP_DIV_I32 = 31,
  JOP_MOD_I32 = 32,
  JOP_SHL_I32 = 33,
  JOP_SHR_I32 = 34,
  JOP_ADD_I32_IMM = 35,
  JOP_SUB_I32_IMM = 36,
  JOP_MUL_I32_IMM = 37,

  /* I64 arithmetic */
  JOP_ADD_I64 = 38,
  JOP_SUB_I64 = 39,
  JOP_MUL_I64 = 40,
  JOP_DIV_I64 = 41,
  JOP_MOD_I64 = 42,
  JOP_SHL_I64 = 43,
  JOP_SHR_I64 = 44,

  /* Float arithmetic */
  JOP_ADD_F16 = 45,
  JOP_SUB_F16 = 46,
  JOP_MUL_F16 = 47,
  JOP_ADD_F32 = 48,
  JOP_SUB_F32 = 49,
  JOP_MUL_F32 = 50,
  JOP_DIV_F32 = 51,
  JOP_ADD_F64 = 52,
  JOP_SUB_F64 = 53,
  JOP_MUL_F64 = 54,
  JOP_DIV_F64 = 55,

  /* Unary */
  JOP_NEG_I32 = 56,
  JOP_NEG_I64 = 57,
  JOP_NEG_F32 = 58,
  JOP_NEG_F64 = 59,
  JOP_NOT_BOOL = 60,

  /* Comparisons */
  JOP_EQ_I32 = 61,
  JOP_LT_I32 = 62,
  JOP_EQ_I32_IMM = 63,
  JOP_LT_I32_IMM = 64,
  JOP_EQ_I64 = 65,
  JOP_LT_I64 = 66,
  JOP_EQ_F32 = 67,
  JOP_LT_F32 = 68,
  JOP_EQ_F64 = 69,
  JOP_LT_F64 = 70,

  /* Conversions / width changes */
  JOP_SEXT_I64 = 71,
  JOP_SEXT_I16 = 72,
  JOP_TRUNC_I8 = 73,
  JOP_TRUNC_I16 = 74,
  JOP_I32_FROM_I64 = 75,
  JOP_F64_FROM_I32 = 76,
  JOP_I32_FROM_F64 = 77,
  JOP_F64_FROM_I64 = 78,
  JOP_I64_FROM_F64 = 79,
  JOP_F32_FROM_I32 = 80,
  JOP_I32_FROM_F32 = 81,
  JOP_F64_FROM_F32 = 82,
  JOP_F32_FROM_F64 = 83,
  JOP_F32_FROM_I64 = 84,
  JOP_I64_FROM_F32 = 85,
  JOP_F16_FROM_F32 = 86,
  JOP_F32_FROM_F16 = 87,
  JOP_F16_FROM_I32 = 88,
  JOP_I32_FROM_F16 = 89,

  /* Boxing/unboxing boundary + spill */
  JOP_TO_DYN = 90,
  JOP_FROM_DYN_I8 = 91,
  JOP_FROM_DYN_I16 = 92,
  JOP_FROM_DYN_I32 = 93,
  JOP_FROM_DYN_I64 = 94,
  JOP_FROM_DYN_F16 = 95,
  JOP_FROM_DYN_F32 = 96,
  JOP_FROM_DYN_F64 = 97,
  JOP_FROM_DYN_BOOL = 98,
  JOP_FROM_DYN_ATOM = 99,
  JOP_FROM_DYN_PTR = 100,
  JOP_SPILL_PUSH = 101,
  JOP_SPILL_POP = 102,

  /* Identity / type introspection */
  JOP_PHYSEQ = 103,
  JOP_KINDOF = 104,
  JOP_SWITCH_KIND = 105,
  JOP_CASE_KIND = 106,

  /* Containers */
  JOP_LIST_NIL = 107,
  JOP_LIST_CONS = 108,
  JOP_LIST_HEAD = 109,
  JOP_LIST_TAIL = 110,
  JOP_LIST_IS_NIL = 111,
  JOP_ARRAY_NEW = 112,
  JOP_ARRAY_LEN = 113,
  JOP_ARRAY_GET = 114,
  JOP_ARRAY_SET = 115,
  JOP_BYTES_NEW = 116,
  JOP_BYTES_LEN = 117,
  JOP_BYTES_GET_U8 = 118,
  JOP_BYTES_SET_U8 = 119,
  JOP_OBJ_NEW = 120,
  JOP_OBJ_HAS_ATOM = 121,
  JOP_OBJ_GET_ATOM = 122,
  JOP_OBJ_SET_ATOM = 123,
  JOP_OBJ_GET = 124,
  JOP_OBJ_SET = 125,
} jello_op;

#define JOP_COUNT (JOP_OBJ_SET + 1)

#define JELLO_ATOM___PROTO__ 0u
#define JELLO_ATOM_INIT 1u

typedef struct jello_insn {
  uint8_t op;
  uint8_t a;
  uint8_t b;
  uint8_t c;
  uint32_t imm;
} jello_insn;

typedef struct jello_bc_function {
  uint32_t nregs;
  uint32_t cap_start; /* first capture slot; used when JELLO_BC_FEAT_CAP_START */
  const jello_type_id* reg_types;
  uint32_t ninsns;
  const jello_insn* insns;
} jello_bc_function;

struct jello_bc_module;
typedef struct jello_bc_module jello_bc_module;

/* --- loader.h --- */
typedef enum jello_bc_error {
  JELLO_BC_OK = 0,
  JELLO_BC_EOF,
  JELLO_BC_BAD_MAGIC,
  JELLO_BC_UNSUPPORTED_VERSION,
  JELLO_BC_BAD_FORMAT,
  JELLO_BC_OUT_OF_MEMORY,
} jello_bc_error;

typedef struct jello_bc_result {
  jello_bc_error err;
  const char* msg;
  size_t offset;
} jello_bc_result;

jello_bc_result jello_bc_read(const uint8_t* data, size_t size, jello_bc_module** out);
void jello_bc_free(jello_bc_module* m);
uint32_t jello_bc_get_entry(const jello_bc_module* m);

/* --- vm.h --- */
typedef enum jello_exec_status {
  JELLO_EXEC_OK = 0,
  JELLO_EXEC_TRAP = 1,
} jello_exec_status;

typedef enum jello_trap_code {
  JELLO_TRAP_NONE = 0,
  JELLO_TRAP_TYPE_MISMATCH = 1,
  JELLO_TRAP_BOUNDS = 2,
  JELLO_TRAP_NULL_DEREF = 3,
  JELLO_TRAP_THROWN = 4,
  JELLO_TRAP_STACK_OVERFLOW = 5,
  JELLO_TRAP_FUEL = 6,
  JELLO_TRAP_LIMIT = 7,
} jello_trap_code;

struct jello_vm;
typedef struct jello_vm jello_vm;

jello_vm* jello_vm_create(void);
void jello_vm_destroy(jello_vm* vm);
size_t jello_slot_align(jello_type_kind k);
size_t jello_slot_size(jello_type_kind k);
jello_value jello_vm_exec(jello_vm* vm, const jello_bc_module* m);
jello_exec_status jello_vm_exec_status(jello_vm* vm, const jello_bc_module* m, jello_value* out);
/** REPL: run module and optionally capture the entry module's exports object.
 *  entry_module_idx: which module's exports to capture (0 for single module, 1 for [prior,new]).
 *  out_exports: if non-NULL, receives the exports object (for next incremental run). */
jello_exec_status jello_vm_exec_status_exports(jello_vm* vm, const jello_bc_module* m,
    uint32_t entry_module_idx, jello_value* out, jello_value* out_exports);
/** Phase 4: run a specific function with pre-bound args (chunk exec).
 *  entry_func_index: which function to run. args/nargs: initial reg 0..nargs-1.
 *  out_exports: if non-NULL and entry_module_idx != UINT32_MAX, capture reg[entry_module_idx] on return. */
jello_exec_status jello_vm_exec_status_chunk(jello_vm* vm, const jello_bc_module* m,
    uint32_t entry_func_index, const jello_value* args, uint32_t nargs,
    uint32_t entry_module_idx, jello_value* out, jello_value* out_exports);
void jello_vm_clear_trap(jello_vm* vm);
jello_trap_code jello_vm_last_trap_code(const jello_vm* vm);
const char* jello_vm_last_trap_msg(const jello_vm* vm);
/** REPL: invalidate frame cache after freeing bytecode module (avoids dangling frame_layouts_mod). */
void jello_vm_invalidate_frame_cache(jello_vm* vm);

/* --- safety limits (optional) --- */
/* Fuel (instruction budget): 0 disables. If enabled, fuel is reset per exec(). */
void jello_vm_set_fuel(jello_vm* vm, uint64_t fuel);
/* Maximum Bytes length permitted by ops that allocate Bytes (0 disables). */
void jello_vm_set_max_bytes_len(jello_vm* vm, uint32_t max_len);
/* Maximum Array length permitted by ops that allocate Arrays (0 disables). */
void jello_vm_set_max_array_len(jello_vm* vm, uint32_t max_len);

/* --- list.h --- */
typedef struct jello_list {
  jello_obj_header h;
  jello_value head;
  struct jello_list* tail;
} jello_list;

jello_list* jello_list_cons(struct jello_vm* vm, uint32_t type_id, jello_value head, jello_list* tail);

/* --- array.h --- */
typedef struct jello_array {
  jello_obj_header h;
  uint32_t length;
  jello_value* data;
} jello_array;

jello_array* jello_array_new(struct jello_vm* vm, uint32_t type_id, uint32_t length);

/* --- bytes.h --- */
typedef struct jello_bytes {
  jello_obj_header h;
  uint32_t length;
  uint8_t data[];
} jello_bytes;

jello_bytes* jello_bytes_new(struct jello_vm* vm, uint32_t type_id, uint32_t length);

/* --- object.h --- */
typedef struct jello_object {
  jello_obj_header h;
  struct jello_object* proto;
  uint32_t cap;
  uint32_t len;
  uint32_t* keys;
  jello_value* vals;
  uint8_t* states;
} jello_object;

jello_object* jello_object_new(struct jello_vm* vm, uint32_t type_id);
int jello_object_has(jello_object* o, uint32_t atom_id);
jello_value jello_object_get(jello_object* o, uint32_t atom_id);
void jello_object_set(jello_object* o, uint32_t atom_id, jello_value v);

/* --- function.h --- */
typedef struct jello_function {
  jello_obj_header h;
  uint32_t func_index;
  uint32_t ncaps;
  jello_value bound_this;
  uint8_t caps_are_raw;   /* 1 if trailing bytes are raw (no GC roots), 0 if jello_value[] */
  uint8_t _pad[3];
  uint32_t raw_cap_size; /* when caps_are_raw: total bytes of raw capture data */
  jello_value caps[];   /* when !caps_are_raw; when caps_are_raw, raw bytes at same offset */
} jello_function;

jello_function* jello_function_new(struct jello_vm* vm, uint32_t type_id, uint32_t func_index);
jello_function* jello_closure_new(struct jello_vm* vm, uint32_t type_id, uint32_t func_index, uint32_t ncaps, const jello_value* caps);
jello_function* jello_closure_new_raw(struct jello_vm* vm, uint32_t type_id, uint32_t func_index, uint32_t ncaps, uint32_t raw_cap_size, const uint8_t* raw_caps);
jello_function* jello_function_bind_this(struct jello_vm* vm, uint32_t type_id, const jello_function* f, jello_value bound_this);

/* --- abstract.h --- */
typedef void (*jello_abstract_finalizer)(void* payload);

typedef struct jello_abstract {
  jello_obj_header h;
  void* payload;
  jello_abstract_finalizer finalizer;
} jello_abstract;

jello_abstract* jello_abstract_new(struct jello_vm* vm, uint32_t type_id, void* payload);
jello_abstract* jello_abstract_new_finalized(struct jello_vm* vm, uint32_t type_id, void* payload, jello_abstract_finalizer finalizer);

/* --- box.h --- */
typedef struct jello_box_i64 {
  jello_obj_header h;
  int64_t value;
} jello_box_i64;

typedef struct jello_box_f64 {
  jello_obj_header h;
  double value;
} jello_box_f64;

typedef struct jello_box_f32 {
  jello_obj_header h;
  float value;
} jello_box_f32;

typedef struct jello_box_f16 {
  jello_obj_header h;
  uint16_t value; /* IEEE 754 binary16 bits */
} jello_box_f16;

jello_box_i64* jello_box_i64_new(struct jello_vm* vm, uint32_t type_id, int64_t v);
jello_box_f64* jello_box_f64_new(struct jello_vm* vm, uint32_t type_id, double v);
jello_box_f32* jello_box_f32_new(struct jello_vm* vm, uint32_t type_id, float v);
jello_box_f16* jello_box_f16_new(struct jello_vm* vm, uint32_t type_id, uint16_t bits);

static inline int jello_is_box_i64(jello_value v) {
  return jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BOX_I64;
}
static inline int jello_is_box_f64(jello_value v) {
  return jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BOX_F64;
}
static inline int jello_is_box_f32(jello_value v) {
  return jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BOX_F32;
}
static inline int64_t jello_as_box_i64(jello_value v) {
  return ((const jello_box_i64*)jello_as_ptr(v))->value;
}
static inline double jello_as_box_f64(jello_value v) {
  return ((const jello_box_f64*)jello_as_ptr(v))->value;
}
static inline float jello_as_box_f32(jello_value v) {
  return ((const jello_box_f32*)jello_as_ptr(v))->value;
}
static inline int jello_is_box_f16(jello_value v) {
  return jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BOX_F16;
}
static inline uint16_t jello_as_box_f16(jello_value v) {
  return ((const jello_box_f16*)jello_as_ptr(v))->value;
}

/* --- gc.h --- */
void jello_gc_init(struct jello_vm* vm);
void jello_gc_shutdown(struct jello_vm* vm);
void* jello_gc_alloc(struct jello_vm* vm, size_t size);
void jello_gc_push_root(struct jello_vm* vm, jello_value v);
void jello_gc_pop_roots(struct jello_vm* vm, uint32_t n);
void jello_gc_collect(struct jello_vm* vm);

/* --- UTF-16 helpers --- */
JELLO_API double utod(const uchar* str, uchar** end);
JELLO_API int utoi(const uchar* str, uchar** end);
JELLO_API int ustrlen(const uchar* str);
JELLO_API uchar* ustrdup(const uchar* str);
JELLO_API int ucmp(const uchar* a, const uchar* b);
JELLO_API int utostr(char* out, int out_size, const uchar* str);
JELLO_API int usprintf(uchar* out, int out_size, const uchar* fmt, ...);
JELLO_API int uvszprintf(uchar* out, int out_size, const uchar* fmt, va_list arglist);
JELLO_API void uprintf(const uchar* fmt, const uchar* str);

/* --- Debug/attributes --- */
#ifdef JELLO_DEBUG
#define jello_debug_break() abort()
#else
#define jello_debug_break() ((void)0)
#endif

#define JELLO_NO_RETURN(decl) noreturn decl
#define JELLO_UNREACHABLE() abort()

#endif /* JELLO_H */
