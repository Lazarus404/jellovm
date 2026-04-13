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

#include <jello/internal.h>
#include <jello/internal/ops_decl.h>

/* Boilerplate helpers (hot path): define handlers via macros. */
#define OP_UN_I32_MASKED(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a); \
    uint32_t x = vm_load_u32(&fr->rf, ins->b); \
    vm_store_u32_masked(&fr->rf, ins->a, (EXPR), k); \
    return OP_CONTINUE; \
  }

#define OP_UN_I64(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    uint64_t x = (uint64_t)vm_load_i64(&fr->rf, ins->b); \
    vm_store_i64(&fr->rf, ins->a, (int64_t)(EXPR)); \
    return OP_CONTINUE; \
  }

#define OP_UN_F32(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    float x = vm_load_f32(&fr->rf, ins->b); \
    vm_store_f32(&fr->rf, ins->a, (EXPR)); \
    return OP_CONTINUE; \
  }

#define OP_UN_F64(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    double x = vm_load_f64(&fr->rf, ins->b); \
    vm_store_f64(&fr->rf, ins->a, (EXPR)); \
    return OP_CONTINUE; \
  }

#define OP_NOT_BOOL(NAME) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    uint32_t x = vm_load_u32(&fr->rf, ins->b); \
    vm_store_u32(&fr->rf, ins->a, (uint32_t)(x == 0)); \
    return OP_CONTINUE; \
  }

#define OP_BIN_I32_MASKED(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a); \
    uint32_t a = vm_load_u32(&fr->rf, ins->b); \
    uint32_t b = vm_load_u32(&fr->rf, ins->c); \
    vm_store_u32_masked(&fr->rf, ins->a, (EXPR), k); \
    return OP_CONTINUE; \
  }

#define OP_BIN_I32_IMM_MASKED(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a); \
    uint32_t a = vm_load_u32(&fr->rf, ins->b); \
    int32_t imm = (int32_t)(int8_t)ins->c; \
    vm_store_u32_masked(&fr->rf, ins->a, (EXPR), k); \
    return OP_CONTINUE; \
  }

#define OP_BIN_F32(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    float a = vm_load_f32(&fr->rf, ins->b); \
    float b = vm_load_f32(&fr->rf, ins->c); \
    vm_store_f32(&fr->rf, ins->a, (EXPR)); \
    return OP_CONTINUE; \
  }

#define OP_BIN_F64(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    double a = vm_load_f64(&fr->rf, ins->b); \
    double b = vm_load_f64(&fr->rf, ins->c); \
    vm_store_f64(&fr->rf, ins->a, (EXPR)); \
    return OP_CONTINUE; \
  }

#define OP_BIN_F16(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    float a = vm_f16_bits_to_f32(vm_load_f16_bits(&fr->rf, ins->b)); \
    float b = vm_f16_bits_to_f32(vm_load_f16_bits(&fr->rf, ins->c)); \
    vm_store_f16_bits(&fr->rf, ins->a, vm_f32_to_f16_bits((EXPR))); \
    return OP_CONTINUE; \
  }

#define OP_BIN_I64(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    uint64_t a = (uint64_t)vm_load_i64(&fr->rf, ins->b); \
    uint64_t b = (uint64_t)vm_load_i64(&fr->rf, ins->c); \
    vm_store_i64(&fr->rf, ins->a, (int64_t)(EXPR)); \
    return OP_CONTINUE; \
  }

#define OP_CMP_I32(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    int32_t a = vm_load_i32ish(&fr->rf, ins->b); \
    int32_t b = vm_load_i32ish(&fr->rf, ins->c); \
    vm_store_u32(&fr->rf, ins->a, (uint32_t)((EXPR) ? 1 : 0)); \
    return OP_CONTINUE; \
  }

#define OP_CMP_I32_IMM(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    int32_t a = vm_load_i32ish(&fr->rf, ins->b); \
    int32_t imm = (int32_t)(int8_t)ins->c; \
    vm_store_u32(&fr->rf, ins->a, (uint32_t)((EXPR) ? 1 : 0)); \
    return OP_CONTINUE; \
  }

#define OP_CMP_F32(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    float a = vm_load_f32(&fr->rf, ins->b); \
    float b = vm_load_f32(&fr->rf, ins->c); \
    vm_store_u32(&fr->rf, ins->a, (uint32_t)((EXPR) ? 1 : 0)); \
    return OP_CONTINUE; \
  }

#define OP_CMP_F64(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    double a = vm_load_f64(&fr->rf, ins->b); \
    double b = vm_load_f64(&fr->rf, ins->c); \
    vm_store_u32(&fr->rf, ins->a, (uint32_t)((EXPR) ? 1 : 0)); \
    return OP_CONTINUE; \
  }

#define OP_CMP_I64(NAME, EXPR) \
  op_result NAME(exec_ctx* ctx, const jello_insn* ins) { \
    call_frame* fr = ctx->fr; \
    int64_t a = vm_load_i64(&fr->rf, ins->b); \
    int64_t b = vm_load_i64(&fr->rf, ins->c); \
    vm_store_u32(&fr->rf, ins->a, (uint32_t)((EXPR) ? 1 : 0)); \
    return OP_CONTINUE; \
  }

OP_UN_I32_MASKED(op_neg_i32, (uint32_t)0u - x)
OP_UN_I64(op_neg_i64, 0ull - x)
OP_UN_F32(op_neg_f32, -x)
OP_UN_F64(op_neg_f64, -x)
OP_NOT_BOOL(op_not_bool)

OP_BIN_I32_MASKED(op_add_i32, a + b)
OP_BIN_I32_IMM_MASKED(op_add_i32_imm, a + (uint32_t)imm)
OP_BIN_I32_IMM_MASKED(op_sub_i32_imm, a - (uint32_t)imm)
OP_BIN_I32_IMM_MASKED(op_mul_i32_imm, a * (uint32_t)imm)

OP_BIN_F32(op_add_f32, a + b)
OP_BIN_F64(op_add_f64, a + b)

OP_BIN_I32_MASKED(op_sub_i32, a - b)
OP_BIN_F32(op_sub_f32, a - b)
OP_BIN_F64(op_sub_f64, a - b)

OP_BIN_I32_MASKED(op_mul_i32, a * b)
OP_BIN_F32(op_mul_f32, a * b)
OP_BIN_F64(op_mul_f64, a * b)

OP_BIN_F16(op_add_f16, a + b)
OP_BIN_F16(op_sub_f16, a - b)
OP_BIN_F16(op_mul_f16, a * b)

OP_BIN_I64(op_add_i64, a + b)
OP_BIN_I64(op_sub_i64, a - b)
OP_BIN_I64(op_mul_i64, a * b)

OP_BIN_I32_MASKED(op_mod_i32, (uint32_t)((int32_t)a % (int32_t)b))
OP_BIN_I64(op_mod_i64, (int64_t)((int64_t)a % (int64_t)b))

op_result op_shl_i32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a);
  uint32_t a = vm_load_u32(&fr->rf, ins->b);
  uint32_t b = vm_load_u32(&fr->rf, ins->c);
  vm_store_u32_masked(&fr->rf, ins->a, (uint32_t)((int32_t)a << (int32_t)(b & 31u)), k);
  return OP_CONTINUE;
}
op_result op_shl_i64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int64_t a = vm_load_i64(&fr->rf, ins->b);
  uint64_t b = (uint64_t)vm_load_i64(&fr->rf, ins->c);
  vm_store_i64(&fr->rf, ins->a, (int64_t)((uint64_t)a << (b & 63u)));
  return OP_CONTINUE;
}
op_result op_shr_i32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  jello_type_kind k = vm_reg_kind(ctx->m, ctx->f, ins->a);
  uint32_t a = vm_load_u32(&fr->rf, ins->b);
  uint32_t b = vm_load_u32(&fr->rf, ins->c);
  vm_store_u32_masked(&fr->rf, ins->a, (uint32_t)((int32_t)a >> (int32_t)(b & 31u)), k);
  return OP_CONTINUE;
}
op_result op_shr_i64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int64_t a = vm_load_i64(&fr->rf, ins->b);
  uint64_t b = (uint64_t)vm_load_i64(&fr->rf, ins->c);
  vm_store_i64(&fr->rf, ins->a, (int64_t)a >> (int)(b & 63u));
  return OP_CONTINUE;
}

OP_CMP_I32(op_eq_i32, a == b)
OP_CMP_I32_IMM(op_eq_i32_imm, a == imm)
OP_CMP_I32_IMM(op_lt_i32_imm, a < imm)
OP_CMP_F32(op_eq_f32, a == b)
OP_CMP_F64(op_eq_f64, a == b)
OP_CMP_I64(op_eq_i64, a == b)
OP_CMP_I32(op_lt_i32, a < b)
OP_CMP_I64(op_lt_i64, a < b)
OP_CMP_F32(op_lt_f32, a < b)
OP_CMP_F64(op_lt_f64, a < b)

op_result op_sext_i64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int32_t x = (int32_t)vm_load_u32(&fr->rf, ins->b);
  vm_store_i64(&fr->rf, ins->a, (int64_t)x);
  return OP_CONTINUE;
}

op_result op_sext_i16(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  uint32_t x = vm_load_u32(&fr->rf, ins->b);
  uint32_t v = (uint32_t)(int32_t)(int16_t)(int8_t)(x & 0xFFu);
  vm_store_u32(&fr->rf, ins->a, v);
  return OP_CONTINUE;
}

op_result op_trunc_i8(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  uint32_t x = vm_load_u32(&fr->rf, ins->b);
  vm_store_u32_masked(&fr->rf, ins->a, x, JELLO_T_I8);
  return OP_CONTINUE;
}

op_result op_trunc_i16(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  uint32_t x = vm_load_u32(&fr->rf, ins->b);
  vm_store_u32_masked(&fr->rf, ins->a, x, JELLO_T_I16);
  return OP_CONTINUE;
}

op_result op_i32_from_i64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  uint64_t x = (uint64_t)vm_load_i64(&fr->rf, ins->b);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)x);
  return OP_CONTINUE;
}

op_result op_f64_from_i32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int32_t x = (int32_t)vm_load_u32(&fr->rf, ins->b);
  vm_store_f64(&fr->rf, ins->a, (double)x);
  return OP_CONTINUE;
}

op_result op_i32_from_f64(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  double x = vm_load_f64(&fr->rf, ins->b);
  uint32_t out_u32 = 0;
  if(!vm_checked_f64_to_i32(vm, x, &out_u32)) return OP_CONTINUE;
  vm_store_u32(&fr->rf, ins->a, out_u32);
  return OP_CONTINUE;
}

op_result op_f64_from_i64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int64_t x = vm_load_i64(&fr->rf, ins->b);
  vm_store_f64(&fr->rf, ins->a, (double)x);
  return OP_CONTINUE;
}

op_result op_i64_from_f64(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  double x = vm_load_f64(&fr->rf, ins->b);
  int64_t out_i64 = 0;
  if(!vm_checked_f64_to_i64(vm, x, &out_i64)) return OP_CONTINUE;
  vm_store_i64(&fr->rf, ins->a, out_i64);
  return OP_CONTINUE;
}

op_result op_f32_from_i32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int32_t x = (int32_t)vm_load_u32(&fr->rf, ins->b);
  vm_store_f32(&fr->rf, ins->a, (float)x);
  return OP_CONTINUE;
}

op_result op_i32_from_f32(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  float x = vm_load_f32(&fr->rf, ins->b);
  uint32_t out_u32 = 0;
  if(!vm_checked_f64_to_i32(vm, (double)x, &out_u32)) return OP_CONTINUE;
  vm_store_u32(&fr->rf, ins->a, out_u32);
  return OP_CONTINUE;
}

OP_BIN_F32(op_div_f32, a / b)
OP_BIN_F64(op_div_f64, a / b)

op_result op_f64_from_f32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  float x = vm_load_f32(&fr->rf, ins->b);
  vm_store_f64(&fr->rf, ins->a, (double)x);
  return OP_CONTINUE;
}

op_result op_f32_from_f64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  double x = vm_load_f64(&fr->rf, ins->b);
  vm_store_f32(&fr->rf, ins->a, (float)x);
  return OP_CONTINUE;
}

op_result op_f16_from_f32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  float x = vm_load_f32(&fr->rf, ins->b);
  uint16_t bits = vm_f32_to_f16_bits(x);
  vm_store_f16_bits(&fr->rf, ins->a, bits);
  return OP_CONTINUE;
}

op_result op_f32_from_f16(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  uint16_t bits = vm_load_f16_bits(&fr->rf, ins->b);
  float x = vm_f16_bits_to_f32(bits);
  vm_store_f32(&fr->rf, ins->a, x);
  return OP_CONTINUE;
}

op_result op_f16_from_i32(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int32_t x = (int32_t)vm_load_u32(&fr->rf, ins->b);
  uint16_t bits = vm_f32_to_f16_bits((float)x);
  vm_store_f16_bits(&fr->rf, ins->a, bits);
  return OP_CONTINUE;
}

op_result op_i32_from_f16(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  uint16_t bits = vm_load_f16_bits(&fr->rf, ins->b);
  float x = vm_f16_bits_to_f32(bits);
  uint32_t out_u32 = 0;
  if(!vm_checked_f64_to_i32(vm, (double)x, &out_u32)) return OP_CONTINUE;
  vm_store_u32(&fr->rf, ins->a, out_u32);
  return OP_CONTINUE;
}

op_result op_f32_from_i64(exec_ctx* ctx, const jello_insn* ins) {
  call_frame* fr = ctx->fr;
  int64_t x = vm_load_i64(&fr->rf, ins->b);
  vm_store_f32(&fr->rf, ins->a, (float)x);
  return OP_CONTINUE;
}

op_result op_i64_from_f32(exec_ctx* ctx, const jello_insn* ins) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  float x = vm_load_f32(&fr->rf, ins->b);
  int64_t out_i64 = 0;
  if(!vm_checked_f64_to_i64(vm, (double)x, &out_i64)) return OP_CONTINUE;
  vm_store_i64(&fr->rf, ins->a, out_i64);
  return OP_CONTINUE;
}

op_result op_switch_kind(exec_ctx* ctx, const jello_insn* ins) {
  const jello_bc_function* f = ctx->f;
  call_frame* fr = ctx->fr;
  const jello_insn* insns = f->insns;

  uint32_t kind = vm_load_u32(&fr->rf, ins->a);
  uint32_t ncases = (uint32_t)ins->b;
  uint32_t table_first = fr->pc;
  uint32_t table_end = table_first + ncases;
  if(table_end > f->ninsns) jello_vm_panic();

  int32_t d = (int32_t)ins->imm;
  for(uint32_t i = 0; i < ncases; i++) {
    const jello_insn* ci = &insns[table_first + i];
    if((uint32_t)ci->a == kind) {
      d = (int32_t)ci->imm;
      break;
    }
  }
  int32_t npc = (int32_t)table_end + d;
  if(npc < 0 || npc > (int32_t)f->ninsns) jello_vm_panic();
  fr->pc = (uint32_t)npc;
  return OP_CONTINUE;
}

op_result op_case_kind(exec_ctx* ctx, const jello_insn* ins) {
  (void)ctx;
  (void)ins;
  jello_vm_panic();
  return OP_CONTINUE;
}
