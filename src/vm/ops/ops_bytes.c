// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

#include <string.h>

op_result op_bytes_new(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  uint32_t len = vm_load_u32(&fr->rf, ins->b);
  if(vm->max_bytes_len && len > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "bytes_new length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* b = jello_bytes_new(vm, type_id, len);
  vm_store_ptr(&fr->rf, ins->a, b);
  return OP_CONTINUE;
}

op_result op_bytes_len(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_bytes* b = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!b) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_len on null");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, b->length);
  return OP_CONTINUE;
}

op_result op_bytes_get_u8(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_bytes* b = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!b) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_get_u8 on null");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= b->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_get_u8 index out of bounds");
    return OP_CONTINUE;
  }
  vm_store_u32(&fr->rf, ins->a, (uint32_t)b->data[idx]);
  return OP_CONTINUE;
}

op_result op_bytes_set_u8(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;

  jello_bytes* b = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!b) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_set_u8 on null");
    return OP_CONTINUE;
  }
  uint32_t idx = vm_load_u32(&fr->rf, ins->c);
  if(idx >= b->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_set_u8 index out of bounds");
    return OP_CONTINUE;
  }
  uint32_t v = vm_load_u32(&fr->rf, ins->a);
  if(v > 255u) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_set_u8 value out of range");
    return OP_CONTINUE;
  }
  b->data[idx] = (uint8_t)v;
  return OP_CONTINUE;
}

op_result op_bytes_concat2(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_bytes* x = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!x) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat2 on null lhs");
    return OP_CONTINUE;
  }
  jello_bytes* y = (jello_bytes*)vm_load_ptr(&fr->rf, ins->c);
  if(!y) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat2 on null rhs");
    return OP_CONTINUE;
  }
  uint64_t total64 = (uint64_t)x->length + (uint64_t)y->length;
  if(total64 > 0xffffffffu) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_concat2 length overflow");
    return OP_CONTINUE;
  }
  uint32_t total = (uint32_t)total64;
  if(vm->max_bytes_len && total > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "bytes_concat2 length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* outb = jello_bytes_new(vm, type_id, total);
  jello_gc_push_root(vm, jello_from_ptr(outb));
  if(x->length) memcpy(outb->data, x->data, x->length);
  if(y->length) memcpy(outb->data + x->length, y->data, y->length);
  vm_store_ptr(&fr->rf, ins->a, outb);
  jello_gc_pop_roots(vm, 1);
  return OP_CONTINUE;
}

op_result op_bytes_concat_many(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_array* parts = (jello_array*)vm_load_ptr(&fr->rf, ins->b);
  if(!parts) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat_many on null");
    return OP_CONTINUE;
  }

  uint64_t total64 = 0;
  for(uint32_t i = 0; i < parts->length; i++) {
    jello_value pv = parts->data[i];
    if(jello_is_null(pv)) {
      (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, "bytes_concat_many element is null");
      return OP_CONTINUE;
    }
    if(!jello_is_ptr(pv) || jello_obj_kind_of(pv) != (uint32_t)JELLO_OBJ_BYTES) {
      (void)jello_vm_trap(vm, JELLO_TRAP_TYPE_MISMATCH, "bytes_concat_many element not bytes");
      return OP_CONTINUE;
    }
    jello_bytes* b = (jello_bytes*)jello_as_ptr(pv);
    total64 += (uint64_t)b->length;
    if(total64 > 0xffffffffu) {
      (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes_concat_many length overflow");
      return OP_CONTINUE;
    }
  }

  uint32_t total = (uint32_t)total64;
  if(vm->max_bytes_len && total > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, "bytes_concat_many length exceeds limit");
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* outb = jello_bytes_new(vm, type_id, total);
  jello_gc_push_root(vm, jello_from_ptr(outb));
  uint32_t w = 0;
  for(uint32_t i = 0; i < parts->length; i++) {
    jello_bytes* b = (jello_bytes*)jello_as_ptr(parts->data[i]);
    if(b->length) {
      memcpy(outb->data + w, b->data, b->length);
      w += b->length;
    }
  }
  vm_store_ptr(&fr->rf, ins->a, outb);
  jello_gc_pop_roots(vm, 1);
  return OP_CONTINUE;
}

static op_result op_bytes_bitwise2(
    exec_ctx* ctx,
    const jello_insn* ins,
    const char* null_lhs,
    const char* null_rhs,
    const char* limit_msg,
    uint8_t (*binop)(uint8_t, uint8_t)) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;

  jello_bytes* x = (jello_bytes*)vm_load_ptr(&fr->rf, ins->b);
  if(!x) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, null_lhs);
    return OP_CONTINUE;
  }
  jello_bytes* y = (jello_bytes*)vm_load_ptr(&fr->rf, ins->c);
  if(!y) {
    (void)jello_vm_trap(vm, JELLO_TRAP_NULL_DEREF, null_rhs);
    return OP_CONTINUE;
  }
  if(x->length != y->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes bitwise operands must have equal length");
    return OP_CONTINUE;
  }
  uint32_t n = x->length;
  if(vm->max_bytes_len && n > vm->max_bytes_len) {
    (void)jello_vm_trap(vm, JELLO_TRAP_LIMIT, limit_msg);
    return OP_CONTINUE;
  }
  uint32_t type_id = f->reg_types[ins->a];
  jello_bytes* outb = jello_bytes_new(vm, type_id, n);
  jello_gc_push_root(vm, jello_from_ptr(outb));
  for(uint32_t i = 0; i < n; i++) {
    outb->data[i] = binop(x->data[i], y->data[i]);
  }
  vm_store_ptr(&fr->rf, ins->a, outb);
  jello_gc_pop_roots(vm, 1);
  return OP_CONTINUE;
}

static uint8_t bytes_and(uint8_t a, uint8_t b) { return (uint8_t)(a & b); }
static uint8_t bytes_or(uint8_t a, uint8_t b) { return (uint8_t)(a | b); }
static uint8_t bytes_xor(uint8_t a, uint8_t b) { return (uint8_t)(a ^ b); }

op_result op_bytes_bitand2(exec_ctx* ctx, const jello_insn* ins) {
  return op_bytes_bitwise2(
      ctx,
      ins,
      "bytes_bitand2 on null lhs",
      "bytes_bitand2 on null rhs",
      "bytes_bitand2 length exceeds limit",
      bytes_and);
}

op_result op_bytes_bitor2(exec_ctx* ctx, const jello_insn* ins) {
  return op_bytes_bitwise2(
      ctx,
      ins,
      "bytes_bitor2 on null lhs",
      "bytes_bitor2 on null rhs",
      "bytes_bitor2 length exceeds limit",
      bytes_or);
}

op_result op_bytes_bitxor2(exec_ctx* ctx, const jello_insn* ins) {
  return op_bytes_bitwise2(
      ctx,
      ins,
      "bytes_bitxor2 on null lhs",
      "bytes_bitxor2 on null rhs",
      "bytes_bitxor2 length exceeds limit",
      bytes_xor);
}

#if defined(__GNUC__) || defined(__clang__)
#define JELLO_BSWAP16(x) __builtin_bswap16((uint16_t)(x))
#define JELLO_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#else
static uint16_t jello_bswap16(uint16_t x) {
  return (uint16_t)((x >> 8) | (x << 8));
}
static uint32_t jello_bswap32(uint32_t x) {
  return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) |
         ((x & 0xFF000000u) >> 24);
}
#define JELLO_BSWAP16(x) jello_bswap16((uint16_t)(x))
#define JELLO_BSWAP32(x) jello_bswap32((uint32_t)(x))
#endif

static jello_bytes* bytes_load(exec_ctx* ctx, uint8_t reg, const char* null_msg) {
  jello_bytes* b = (jello_bytes*)vm_load_ptr(&ctx->fr->rf, reg);
  if(!b) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_NULL_DEREF, null_msg);
    return NULL;
  }
  return b;
}

static int32_t bytes_off(exec_ctx* ctx, uint8_t reg) {
  return (int32_t)vm_load_u32(&ctx->fr->rf, reg);
}

static int bytes_rw_ok(jello_vm* vm, jello_bytes* b, int32_t off, uint32_t width, const char* align_msg) {
  if(off < 0 || (uint32_t)off % width != 0u) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, align_msg);
    return 0;
  }
  if((uint32_t)off + width > b->length) {
    (void)jello_vm_trap(vm, JELLO_TRAP_BOUNDS, "bytes read/write offset out of bounds");
    return 0;
  }
  return 1;
}

static uint16_t read_u16_le(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

static uint16_t read_u16_be(const uint8_t* p) {
  return JELLO_BSWAP16(read_u16_le(p));
}

static void write_u16_le(uint8_t* p, uint16_t v) {
  memcpy(p, &v, 2);
}

static void write_u16_be(uint8_t* p, uint16_t v) {
  v = JELLO_BSWAP16(v);
  memcpy(p, &v, 2);
}

static uint32_t read_u32_le(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

static uint32_t read_u32_be(const uint8_t* p) {
  return JELLO_BSWAP32(read_u32_le(p));
}

static void write_u32_le(uint8_t* p, uint32_t v) {
  memcpy(p, &v, 4);
}

static void write_u32_be(uint8_t* p, uint32_t v) {
  v = JELLO_BSWAP32(v);
  memcpy(p, &v, 4);
}

#define DEF_BYTES_READ_U16(NAME, READ_FN) \
  op_result op_##NAME(exec_ctx* ctx, const jello_insn* ins) { \
    jello_bytes* b = bytes_load(ctx, ins->b, #NAME " on null"); \
    if(!b) return OP_CONTINUE; \
    int32_t off = bytes_off(ctx, ins->c); \
    if(!bytes_rw_ok(ctx->vm, b, off, 2u, #NAME " requires 2-byte aligned offset")) return OP_CONTINUE; \
    uint16_t v = READ_FN(b->data + off); \
    vm_store_u32(&ctx->fr->rf, ins->a, (uint32_t)v); \
    return OP_CONTINUE; \
  }

#define DEF_BYTES_READ_U32(NAME, READ_FN) \
  op_result op_##NAME(exec_ctx* ctx, const jello_insn* ins) { \
    jello_bytes* b = bytes_load(ctx, ins->b, #NAME " on null"); \
    if(!b) return OP_CONTINUE; \
    int32_t off = bytes_off(ctx, ins->c); \
    if(!bytes_rw_ok(ctx->vm, b, off, 4u, #NAME " requires 4-byte aligned offset")) return OP_CONTINUE; \
    uint32_t v = READ_FN(b->data + off); \
    vm_store_u32(&ctx->fr->rf, ins->a, v); \
    return OP_CONTINUE; \
  }

#define DEF_BYTES_WRITE_U16(NAME, WRITE_FN) \
  op_result op_##NAME(exec_ctx* ctx, const jello_insn* ins) { \
    jello_bytes* b = bytes_load(ctx, ins->b, #NAME " on null"); \
    if(!b) return OP_CONTINUE; \
    int32_t off = bytes_off(ctx, ins->c); \
    if(!bytes_rw_ok(ctx->vm, b, off, 2u, #NAME " requires 2-byte aligned offset")) return OP_CONTINUE; \
    WRITE_FN(b->data + off, (uint16_t)vm_load_u32(&ctx->fr->rf, ins->a)); \
    return OP_CONTINUE; \
  }

#define DEF_BYTES_WRITE_U32(NAME, WRITE_FN) \
  op_result op_##NAME(exec_ctx* ctx, const jello_insn* ins) { \
    jello_bytes* b = bytes_load(ctx, ins->b, #NAME " on null"); \
    if(!b) return OP_CONTINUE; \
    int32_t off = bytes_off(ctx, ins->c); \
    if(!bytes_rw_ok(ctx->vm, b, off, 4u, #NAME " requires 4-byte aligned offset")) return OP_CONTINUE; \
    WRITE_FN(b->data + off, vm_load_u32(&ctx->fr->rf, ins->a)); \
    return OP_CONTINUE; \
  }

DEF_BYTES_READ_U16(bytes_read_u16_le, read_u16_le)
DEF_BYTES_READ_U16(bytes_read_u16_be, read_u16_be)
DEF_BYTES_READ_U32(bytes_read_u32_le, read_u32_le)
DEF_BYTES_READ_U32(bytes_read_u32_be, read_u32_be)

op_result op_bytes_read_i32_le(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_read_i32_le on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_read_i32_le requires 4-byte aligned offset")) return OP_CONTINUE;
  int32_t v;
  memcpy(&v, b->data + off, 4);
  vm_store_u32(&ctx->fr->rf, ins->a, (uint32_t)v);
  return OP_CONTINUE;
}

op_result op_bytes_read_i32_be(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_read_i32_be on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_read_i32_be requires 4-byte aligned offset")) return OP_CONTINUE;
  uint32_t bits = read_u32_be(b->data + off);
  int32_t v = (int32_t)bits;
  vm_store_u32(&ctx->fr->rf, ins->a, (uint32_t)v);
  return OP_CONTINUE;
}

op_result op_bytes_read_f32_le(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_read_f32_le on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_read_f32_le requires 4-byte aligned offset")) return OP_CONTINUE;
  float v;
  memcpy(&v, b->data + off, 4);
  vm_store_f32(&ctx->fr->rf, ins->a, v);
  return OP_CONTINUE;
}

op_result op_bytes_read_f32_be(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_read_f32_be on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_read_f32_be requires 4-byte aligned offset")) return OP_CONTINUE;
  uint32_t bits = read_u32_be(b->data + off);
  float v;
  memcpy(&v, &bits, 4);
  vm_store_f32(&ctx->fr->rf, ins->a, v);
  return OP_CONTINUE;
}

DEF_BYTES_WRITE_U16(bytes_write_u16_le, write_u16_le)
DEF_BYTES_WRITE_U16(bytes_write_u16_be, write_u16_be)
DEF_BYTES_WRITE_U32(bytes_write_u32_le, write_u32_le)
DEF_BYTES_WRITE_U32(bytes_write_u32_be, write_u32_be)

op_result op_bytes_write_i32_le(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_write_i32_le on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_write_i32_le requires 4-byte aligned offset")) return OP_CONTINUE;
  int32_t v = (int32_t)vm_load_u32(&ctx->fr->rf, ins->a);
  memcpy(b->data + off, &v, 4);
  return OP_CONTINUE;
}

op_result op_bytes_write_i32_be(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_write_i32_be on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_write_i32_be requires 4-byte aligned offset")) return OP_CONTINUE;
  int32_t v = (int32_t)vm_load_u32(&ctx->fr->rf, ins->a);
  uint32_t bits = JELLO_BSWAP32((uint32_t)v);
  memcpy(b->data + off, &bits, 4);
  return OP_CONTINUE;
}

op_result op_bytes_write_f32_le(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_write_f32_le on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_write_f32_le requires 4-byte aligned offset")) return OP_CONTINUE;
  float v = vm_load_f32(&ctx->fr->rf, ins->a);
  memcpy(b->data + off, &v, 4);
  return OP_CONTINUE;
}

op_result op_bytes_write_f32_be(exec_ctx* ctx, const jello_insn* ins) {
  jello_bytes* b = bytes_load(ctx, ins->b, "bytes_write_f32_be on null");
  if(!b) return OP_CONTINUE;
  int32_t off = bytes_off(ctx, ins->c);
  if(!bytes_rw_ok(ctx->vm, b, off, 4u, "bytes_write_f32_be requires 4-byte aligned offset")) return OP_CONTINUE;
  float v = vm_load_f32(&ctx->fr->rf, ins->a);
  uint32_t bits;
  memcpy(&bits, &v, 4);
  write_u32_be(b->data + off, bits);
  return OP_CONTINUE;
}
