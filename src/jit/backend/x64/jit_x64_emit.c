// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>
#include <jello/internal/jit_x64.h>
#include <jello/internal/object_internal.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(jello_object, proto) == 8u, "jello_object.proto");
_Static_assert(offsetof(jello_object, cap) == 16u, "jello_object.cap");
_Static_assert(offsetof(jello_object, len) == 20u, "jello_object.len");
_Static_assert(offsetof(jello_object, keys) == 24u, "jello_object.keys");
_Static_assert(offsetof(jello_object, vals) == 32u, "jello_object.vals");
_Static_assert(offsetof(jello_object, states) == 40u, "jello_object.states");
_Static_assert(offsetof(jello_box_f64, value) == 8u, "jello_box_f64.value");
_Static_assert(offsetof(jello_box_i64, value) == 8u, "jello_box_i64.value");

#define JIT_REG_CTX X64_R12
#define JIT_REG_BASE X64_R13
#define JIT_REG_T0 X64_R14
#define JIT_REG_T1 X64_R15
#define JIT_REG_T2 X64_R10 /* scratch (RBX holds BB-local I32 cache) */
#define JIT_REG_I32C X64_RBX

#define JIT_FP_T0 0
#define JIT_FP_T1 1

#define JIT_I32C_NONE 0xFFFFu

#define X64_CC_JZ 0x84u
#define X64_CC_JNZ 0x85u
#define X64_CC_JE 0x84u
#define X64_CC_JNE 0x85u
#define X64_CC_SETE 0x94u
#define X64_CC_SETL 0x9Cu
#define X64_CC_SETB 0x92u

#define JIT_OBJ_OFF_PROTO 8u
#define JIT_OBJ_OFF_CAP 16u
#define JIT_OBJ_OFF_LEN 20u
#define JIT_OBJ_OFF_KEYS 24u
#define JIT_OBJ_OFF_VALS 32u
#define JIT_OBJ_OFF_STATES 40u

static int emit_mov_r32_imm(jello_jit_emit_buf* buf, int rd, uint32_t imm) {
  return x64_emit_mov_r32_imm(buf, rd, imm);
}

static int emit_mov_r64_imm(jello_jit_emit_buf* buf, int rd, uint64_t imm) {
  return x64_emit_mov_r64_imm(buf, rd, imm);
}

static int emit_call_fn(jello_jit_emit_buf* buf, void (*fn)(void)) {
  uintptr_t addr = 0;
  memcpy(&addr, &fn, sizeof(addr));
  if(emit_mov_r64_imm(buf, X64_RAX, (uint64_t)addr) != 0) return -1;
  return x64_emit_call_rax(buf);
}

static int emit_neg_r32(jello_jit_emit_buf* buf, int dst) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[4];
  size_t n = 0;
  if(drex) bytes[n++] = x64_rex(0, 0, 0, drex);
  bytes[n++] = 0xF7u;
  bytes[n++] = x64_modrm(3, 3, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_imul_r64(jello_jit_emit_buf* buf, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[5];
  size_t n = 0;
  bytes[n++] = x64_rex(1, srex, 0, drex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0xAFu;
  bytes[n++] = x64_modrm(3, dlo, slo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_cdq(jello_jit_emit_buf* buf) {
  uint8_t b = 0x99u;
  return jello_jit_emit_bytes(buf, &b, 1u);
}

static int emit_cqo(jello_jit_emit_buf* buf) {
  uint8_t bytes[2] = {0x48u, 0x99u};
  return jello_jit_emit_bytes(buf, bytes, 2u);
}

static int emit_idiv_r32(jello_jit_emit_buf* buf, int divisor) {
  int lo = 0, rex = 0;
  x64_reg_parts(divisor, &lo, &rex);
  uint8_t bytes[4];
  size_t n = 0;
  if(rex) bytes[n++] = x64_rex(0, 0, 0, rex);
  bytes[n++] = 0xF7u;
  bytes[n++] = x64_modrm(3, 7, lo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_idiv_r64(jello_jit_emit_buf* buf, int divisor) {
  int lo = 0, rex = 0;
  x64_reg_parts(divisor, &lo, &rex);
  uint8_t bytes[4];
  size_t n = 0;
  bytes[n++] = x64_rex(1, 0, 0, rex);
  bytes[n++] = 0xF7u;
  bytes[n++] = x64_modrm(3, 7, lo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_shift_r32_cl(jello_jit_emit_buf* buf, uint8_t op_reg, int dst) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[4];
  size_t n = 0;
  if(drex) bytes[n++] = x64_rex(0, 0, 0, drex);
  bytes[n++] = 0xD3u;
  bytes[n++] = x64_modrm(3, op_reg, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_shift_r64_cl(jello_jit_emit_buf* buf, uint8_t op_reg, int dst) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[4];
  size_t n = 0;
  bytes[n++] = x64_rex(1, 0, 0, drex);
  bytes[n++] = 0xD3u;
  bytes[n++] = x64_modrm(3, op_reg, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_mov_r32_rr(jello_jit_emit_buf* buf, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  if(drex || srex) bytes[n++] = x64_rex(0, srex, 0, drex);
  bytes[n++] = 0x89u;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_alu_r32_rr(jello_jit_emit_buf* buf, uint8_t op, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  if(drex || srex) bytes[n++] = x64_rex(0, srex, 0, drex);
  bytes[n++] = op;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static int emit_add_r32_rr(jello_jit_emit_buf* buf, int dst, int src) {
  return emit_alu_r32_rr(buf, 0x01u, dst, src);
}

static int emit_and_r32_rr(jello_jit_emit_buf* buf, int dst, int src) {
  return emit_alu_r32_rr(buf, 0x21u, dst, src);
}

static int emit_sub_r32_rr(jello_jit_emit_buf* buf, int dst, int src) {
  return emit_alu_r32_rr(buf, 0x29u, dst, src);
}

static int emit_cmp_r32_rr(jello_jit_emit_buf* buf, int lhs, int rhs) {
  return emit_alu_r32_rr(buf, 0x39u, lhs, rhs);
}

static int emit_load_frame_base(jello_jit_emit_buf* buf) {
  const jello_jit_layout* lay = jello_jit_runtime_layout();
  if(!lay) return -1;
  if(lay->exec_ctx_fr > 0x7FFFFFFFu || (lay->exec_ctx_fr % 8u) != 0u) return -1;
  if(lay->call_frame_rf_mem > 0x7FFFFFFFu || (lay->call_frame_rf_mem % 8u) != 0u) return -1;
  if(x64_emit_load_r64_disp(buf, JIT_REG_T2, JIT_REG_CTX, lay->exec_ctx_fr) != 0) return -1;
  return x64_emit_load_r64_disp(buf, JIT_REG_BASE, JIT_REG_T2, lay->call_frame_rf_mem);
}

static int emit_refresh_base(jello_jit_emit_buf* buf) {
  return emit_load_frame_base(buf);
}

static int emit_ldr_w_slot(jello_jit_emit_buf* buf, int rt, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 4u) == 0u) return x64_emit_load_r32_disp(buf, rt, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_load_r32_disp(buf, rt, JIT_REG_T2, 0);
}

static int emit_str_w_slot(jello_jit_emit_buf* buf, int rt, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 4u) == 0u) return x64_emit_store_r32_disp(buf, rt, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_store_r32_disp(buf, rt, JIT_REG_T2, 0);
}

static int emit_ldr_x_slot(jello_jit_emit_buf* buf, int rt, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 8u) == 0u) return x64_emit_load_r64_disp(buf, rt, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_load_r64_disp(buf, rt, JIT_REG_T2, 0);
}

static int emit_str_x_slot(jello_jit_emit_buf* buf, int rt, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 8u) == 0u) return x64_emit_store_r64_disp(buf, rt, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_store_r64_disp(buf, rt, JIT_REG_T2, 0);
}

static int emit_ldr_d_slot(jello_jit_emit_buf* buf, int dt, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 8u) == 0u) return x64_emit_load_xmm64_disp(buf, dt, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_load_xmm64_disp(buf, dt, JIT_REG_T2, 0);
}

static int emit_str_d_slot(jello_jit_emit_buf* buf, int dt, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 8u) == 0u) return x64_emit_store_xmm64_disp(buf, dt, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_store_xmm64_disp(buf, dt, JIT_REG_T2, 0);
}

static int emit_ldr_s_slot(jello_jit_emit_buf* buf, int st, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 4u) == 0u) return x64_emit_load_xmm32_disp(buf, st, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_load_xmm32_disp(buf, st, JIT_REG_T2, 0);
}

static int emit_str_s_slot(jello_jit_emit_buf* buf, int st, int base, uint32_t off) {
  if(off <= 0x7FFFFFFFu && (off % 4u) == 0u) return x64_emit_store_xmm32_disp(buf, st, base, off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_store_xmm32_disp(buf, st, JIT_REG_T2, 0);
}

static uint32_t slot_off(const frame_layout* layout, uint32_t reg) {
  return layout->off[reg];
}

static size_t reg_slot_sz(const jello_bc_module* m, const jello_bc_function* f, uint32_t reg) {
  return jello_slot_size(vm_reg_kind(m, f, reg));
}

static int emit_bin_i32(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return emit_sub_r32_rr(buf, JIT_REG_T0, JIT_REG_T1);
    case JIR_BIN_MUL:
      return x64_emit_imul_r32(buf, JIT_REG_T0, JIT_REG_T1);
    case JIR_BIN_SDIV:
      if(emit_mov_r32_rr(buf, X64_RAX, JIT_REG_T0) != 0) return -1;
      if(emit_cdq(buf) != 0) return -1;
      if(emit_idiv_r32(buf, JIT_REG_T1) != 0) return -1;
      return emit_mov_r32_rr(buf, JIT_REG_T0, X64_RAX);
    case JIR_BIN_MOD:
      if(emit_mov_r32_rr(buf, X64_RAX, JIT_REG_T0) != 0) return -1;
      if(emit_cdq(buf) != 0) return -1;
      if(emit_idiv_r32(buf, JIT_REG_T1) != 0) return -1;
      return emit_mov_r32_rr(buf, JIT_REG_T0, X64_RDX);
    case JIR_BIN_SHL:
      if(emit_mov_r32_rr(buf, X64_RCX, JIT_REG_T1) != 0) return -1;
      return emit_shift_r32_cl(buf, 4, JIT_REG_T0);
    case JIR_BIN_SHR:
      if(emit_mov_r32_rr(buf, X64_RCX, JIT_REG_T1) != 0) return -1;
      return emit_shift_r32_cl(buf, 7, JIT_REG_T0);
    default:
      return emit_add_r32_rr(buf, JIT_REG_T0, JIT_REG_T1);
  }
}

static int emit_bin_i64(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return x64_emit_sub_rr(buf, JIT_REG_T0, JIT_REG_T1);
    case JIR_BIN_MUL:
      return emit_imul_r64(buf, JIT_REG_T0, JIT_REG_T1);
    case JIR_BIN_SDIV:
      if(x64_emit_mov_rr(buf, X64_RAX, JIT_REG_T0) != 0) return -1;
      if(emit_cqo(buf) != 0) return -1;
      if(emit_idiv_r64(buf, JIT_REG_T1) != 0) return -1;
      return x64_emit_mov_rr(buf, JIT_REG_T0, X64_RAX);
    case JIR_BIN_MOD:
      if(x64_emit_mov_rr(buf, X64_RAX, JIT_REG_T0) != 0) return -1;
      if(emit_cqo(buf) != 0) return -1;
      if(emit_idiv_r64(buf, JIT_REG_T1) != 0) return -1;
      return x64_emit_mov_rr(buf, JIT_REG_T0, X64_RDX);
    case JIR_BIN_SHL:
      if(emit_mov_r32_rr(buf, X64_RCX, JIT_REG_T1) != 0) return -1;
      return emit_shift_r64_cl(buf, 4, JIT_REG_T0);
    case JIR_BIN_SHR:
      if(emit_mov_r32_rr(buf, X64_RCX, JIT_REG_T1) != 0) return -1;
      return emit_shift_r64_cl(buf, 7, JIT_REG_T0);
    default:
      return x64_emit_add_rr(buf, JIT_REG_T0, JIT_REG_T1);
  }
}

static int emit_bin_f64(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return x64_emit_subsd(buf, JIT_FP_T0, JIT_FP_T1);
    case JIR_BIN_MUL:
      return x64_emit_mulsd(buf, JIT_FP_T0, JIT_FP_T1);
    case JIR_BIN_SDIV:
      return x64_emit_divsd(buf, JIT_FP_T0, JIT_FP_T1);
    default:
      return x64_emit_addsd(buf, JIT_FP_T0, JIT_FP_T1);
  }
}

static int emit_bin_f32(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return x64_emit_subss(buf, JIT_FP_T0, JIT_FP_T1);
    case JIR_BIN_MUL:
      return x64_emit_mulss(buf, JIT_FP_T0, JIT_FP_T1);
    case JIR_BIN_SDIV:
      return x64_emit_divss(buf, JIT_FP_T0, JIT_FP_T1);
    default:
      return x64_emit_addss(buf, JIT_FP_T0, JIT_FP_T1);
  }
}

typedef struct jello_jit_patch_site {
  size_t at;
  uint32_t bc_tgt;
  uint8_t kind; /* 0=jmp rel32, 1=jcc rel32 */
} jello_jit_patch_site;

static int patch_jmp_rel32(jello_jit_emit_buf* buf, size_t at, size_t target) {
  int32_t off = (int32_t)((int64_t)target - (int64_t)(at + 5u));
  memcpy(buf->data + at + 1u, &off, 4u);
  return 0;
}

static int patch_jcc_rel32(jello_jit_emit_buf* buf, size_t at, size_t target) {
  int32_t off = (int32_t)((int64_t)target - (int64_t)(at + 6u));
  memcpy(buf->data + at + 2u, &off, 4u);
  return 0;
}

static size_t ir_off_for_bc(const jello_jit_ir_func* ir, const size_t* ir_off, uint32_t bc_pc) {
  for(uint32_t i = 0; i < ir->ninsns; i++) {
    if(ir->insns[i].bc_pc == bc_pc) {
      if(ir->insns[i].op == JIR_FUEL_CHECK) continue;
      return ir_off[i];
    }
  }
  for(uint32_t i = 0; i < ir->ninsns; i++) {
    if(ir->insns[i].bc_pc == bc_pc) return ir_off[i];
  }
  return 0;
}

static int patch_branch_sites(
    jello_jit_emit_buf* buf,
    const jello_jit_ir_func* ir,
    const size_t* ir_off,
    const jello_jit_patch_site* sites,
    size_t nsites,
    size_t prologue_end
) {
  for(size_t s = 0; s < nsites; s++) {
    const jello_jit_patch_site* ps = &sites[s];
    size_t target = ir_off_for_bc(ir, ir_off, ps->bc_tgt);
    if(target < prologue_end) return -1;
    if(ps->kind == 0u) {
      if(patch_jmp_rel32(buf, ps->at, target) != 0) return -1;
    } else {
      if(patch_jcc_rel32(buf, ps->at, target) != 0) return -1;
    }
  }
  return 0;
}

static int emit_prologue(jello_jit_emit_buf* buf, const jello_jit_layout* lay) {
  if(x64_emit_endbr64(buf) != 0) return -1;
  if(x64_emit_push_r64(buf, X64_RBP) != 0) return -1;
  if(x64_emit_push_r64(buf, X64_RBX) != 0) return -1;
  if(x64_emit_push_r64(buf, X64_R12) != 0) return -1;
  if(x64_emit_push_r64(buf, X64_R13) != 0) return -1;
  if(x64_emit_push_r64(buf, X64_R14) != 0) return -1;
  if(x64_emit_push_r64(buf, X64_R15) != 0) return -1;
  /* SysV AMD64 (also Win64 via JELLO_JIT_SYSV): rsp%16==8 before helper calls. */
  if(x64_emit_sub_rsp_imm8(buf, 8u) != 0) return -1;
  /* Entry arg0 = ctx in RDI (SysV); host uses jello_jit_entry_fn. */
  if(x64_emit_mov_rr(buf, JIT_REG_CTX, X64_RDI) != 0) return -1;
  if(emit_load_frame_base(buf) != 0) return -1;
  if(!lay || lay->exec_ctx_jit_resume_entry > 0x7FFFFFFFu || (lay->exec_ctx_jit_resume_entry % 8u) != 0u)
    return -1;
  if(x64_emit_load_r64_disp(buf, X64_RAX, JIT_REG_CTX, lay->exec_ctx_jit_resume_entry) != 0) return -1;
  if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) return -1;
  size_t jcc_at = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
  if(x64_emit_jmp_r64(buf, X64_RAX) != 0) return -1;
  size_t cont = buf->size;
  return patch_jcc_rel32(buf, jcc_at, cont);
}

static int emit_restore_callee_saved(jello_jit_emit_buf* buf) {
  if(x64_emit_add_rsp_imm8(buf, 8u) != 0) return -1;
  if(x64_emit_pop_r64(buf, X64_R15) != 0) return -1;
  if(x64_emit_pop_r64(buf, X64_R14) != 0) return -1;
  if(x64_emit_pop_r64(buf, X64_R13) != 0) return -1;
  if(x64_emit_pop_r64(buf, X64_R12) != 0) return -1;
  if(x64_emit_pop_r64(buf, X64_RBX) != 0) return -1;
  return x64_emit_pop_r64(buf, X64_RBP);
}

static int emit_epilogue(jello_jit_emit_buf* buf, uint32_t return_code) {
  if(emit_mov_r32_imm(buf, X64_RAX, return_code) != 0) return -1;
  if(emit_restore_callee_saved(buf) != 0) return -1;
  return x64_emit_ret(buf);
}

static int emit_restore_and_ret(jello_jit_emit_buf* buf) {
  if(emit_restore_callee_saved(buf) != 0) return -1;
  return x64_emit_ret(buf);
}

/* After SysV args set: call fn; CONTINUE (rax==0) → refresh base; else restore+ret. */
static int emit_rt_call_continue(jello_jit_emit_buf* buf, void (*fn)(void)) {
  if(emit_call_fn(buf, fn) != 0) return -1;
  if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) return -1;
  size_t jcc_at = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
  if(emit_restore_and_ret(buf) != 0) return -1;
  size_t cont = buf->size;
  if(patch_jcc_rel32(buf, jcc_at, cont) != 0) return -1;
  return emit_refresh_base(buf);
}

/* Like emit_rt_call_continue but keep JIT_REG_BASE (helper did not rebase rf.mem). */
static int emit_rt_call_continue_keep_base(jello_jit_emit_buf* buf, void (*fn)(void)) {
  if(emit_call_fn(buf, fn) != 0) return -1;
  if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) return -1;
  size_t jcc_at = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
  if(emit_restore_and_ret(buf) != 0) return -1;
  size_t cont = buf->size;
  return patch_jcc_rel32(buf, jcc_at, cont);
}

static int emit_ldr_x_field(jello_jit_emit_buf* buf, int rt, int base, uint32_t byte_off) {
  if(byte_off <= 0x7FFFFFFFu && (byte_off % 8u) == 0u) return x64_emit_load_r64_disp(buf, rt, base, byte_off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, byte_off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_load_r64_disp(buf, rt, JIT_REG_T2, 0);
}

static int emit_str_x_field(jello_jit_emit_buf* buf, int rt, int base, uint32_t byte_off) {
  if(byte_off <= 0x7FFFFFFFu && (byte_off % 8u) == 0u) return x64_emit_store_r64_disp(buf, rt, base, byte_off);
  if(emit_mov_r64_imm(buf, JIT_REG_T2, byte_off) != 0) return -1;
  if(x64_emit_add_rr(buf, JIT_REG_T2, base) != 0) return -1;
  return x64_emit_store_r64_disp(buf, rt, JIT_REG_T2, 0);
}

static int emit_inline_fuel_check(jello_jit_emit_buf* buf) {
  const jello_jit_layout* lay = jello_jit_runtime_layout();
  if(!lay) return -1;
  if(x64_emit_load_r64_disp(buf, JIT_REG_T0, JIT_REG_CTX, 0) != 0) return -1; /* vm = ctx->vm */
  if(emit_ldr_x_field(buf, JIT_REG_T1, JIT_REG_T0, lay->vm_fuel_limit) != 0) return -1;
  if(x64_emit_test_rr(buf, JIT_REG_T1, JIT_REG_T1) != 0) return -1;
  size_t skip_jcc = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
  if(emit_ldr_x_field(buf, JIT_REG_T1, JIT_REG_T0, lay->vm_fuel_remaining) != 0) return -1;
  if(x64_emit_test_rr(buf, JIT_REG_T1, JIT_REG_T1) != 0) return -1;
  size_t trap_jcc = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
  if(x64_emit_sub_r64_imm8(buf, JIT_REG_T1, 1) != 0) return -1;
  if(emit_str_x_field(buf, JIT_REG_T1, JIT_REG_T0, lay->vm_fuel_remaining) != 0) return -1;
  size_t jmp_done_at = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;
  size_t trap_at = buf->size;
  if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) return -1;
  if(emit_call_fn(buf, (void (*)(void))jello_jit_runtime_fuel_trap) != 0) return -1;
  if(emit_restore_and_ret(buf) != 0) return -1;
  size_t done_at = buf->size;
  if(patch_jcc_rel32(buf, skip_jcc, done_at) != 0) return -1;
  if(patch_jcc_rel32(buf, trap_jcc, trap_at) != 0) return -1;
  return patch_jmp_rel32(buf, jmp_done_at, done_at);
}

static int emit_cmp_set_i32(jello_jit_emit_buf* buf, jello_jit_ir_cmp cmp) {
  uint8_t cc = (uint8_t)(cmp == JIR_CMP_LT ? X64_CC_SETL : X64_CC_SETE);
  if(x64_emit_setcc(buf, cc, JIT_REG_T0) != 0) return -1;
  return x64_emit_movzx_r32_r8(buf, JIT_REG_T0, JIT_REG_T0);
}

/* RBX holds last stored I32 slot when *i32c_reg != NONE (see arm64 JIT_REG_I32C). */
static int emit_ldr_i32_t0(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint16_t* i32c_reg,
    uint32_t src_reg
) {
  if(*i32c_reg == src_reg) return emit_mov_r32_rr(buf, JIT_REG_T0, JIT_REG_I32C);
  return emit_ldr_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, src_reg));
}

static int emit_ldr_i32_t1(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint16_t* i32c_reg,
    uint32_t src_reg
) {
  if(*i32c_reg == src_reg) return emit_mov_r32_rr(buf, JIT_REG_T1, JIT_REG_I32C);
  return emit_ldr_w_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, src_reg));
}

static int emit_str_i32_t0(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint16_t* i32c_reg,
    uint32_t dst_reg
) {
  if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, dst_reg)) != 0) return -1;
  if(emit_mov_r32_rr(buf, JIT_REG_I32C, JIT_REG_T0) != 0) return -1;
  *i32c_reg = (uint16_t)dst_reg;
  return 0;
}

/* Same mix as object.c hash_u32 — atom_id is an emit-time constant. */
static uint32_t jit_obj_hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static int emit_obj_get_atom_helper(
    jello_jit_emit_buf* buf,
    uint32_t dst,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_RSI, dst) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_RDX, obj_reg) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_RCX, atom_id) != 0) return -1;
  return emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_obj_get_atom);
}

/* Inline own-table OBJ_GET_ATOM for common dst kinds. Slow path: null, proto,
 * DYNAMIC/rare unbox → existing C helper. */
static int emit_obj_get_atom(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t dst,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  jello_type_kind k = vm_reg_kind(m, f, dst);
  int inline_ok = 0;
  switch(k) {
    case JELLO_T_F64:
    case JELLO_T_I8:
    case JELLO_T_I16:
    case JELLO_T_I32:
    case JELLO_T_I64:
    case JELLO_T_BYTES:
    case JELLO_T_FUNCTION:
    case JELLO_T_LIST:
    case JELLO_T_ARRAY:
    case JELLO_T_OBJECT:
    case JELLO_T_ABSTRACT:
    case JELLO_T_ENUM:
      inline_ok = 1;
      break;
    default:
      break;
  }
  if(!inline_ok || atom_id > 0x7FFFFFFFu) return emit_obj_get_atom_helper(buf, dst, obj_reg, atom_id);

  int need_proto = m->proto_enabled && atom_id != JELLO_ATOM___PROTO__;
  uint32_t h0 = jit_obj_hash_u32(atom_id);
  size_t j_slow[10];
  int n_slow = 0;
  size_t j_miss[6];
  int n_miss = 0;
  size_t j_done[6];
  int n_done = 0;

  if(emit_ldr_x_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, obj_reg)) != 0) return -1;
  if(x64_emit_test_r64(buf, JIT_REG_T1, JIT_REG_T1) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;

  if(need_proto) {
    if(x64_emit_load_r64_disp(buf, X64_RAX, JIT_REG_T1, JIT_OBJ_OFF_PROTO) != 0) return -1;
    if(x64_emit_test_r64(buf, X64_RAX, X64_RAX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) return -1;
  }

  if(x64_emit_load_r32_disp(buf, X64_RAX, JIT_REG_T1, JIT_OBJ_OFF_LEN) != 0) return -1;
  if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) return -1;
  j_miss[n_miss++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;

  if(x64_emit_load_r64_disp(buf, X64_R10, JIT_REG_T1, JIT_OBJ_OFF_KEYS) != 0) return -1;
  if(x64_emit_load_r64_disp(buf, X64_RDX, JIT_REG_T1, JIT_OBJ_OFF_VALS) != 0) return -1;
  if(x64_emit_load_r64_disp(buf, X64_RCX, JIT_REG_T1, JIT_OBJ_OFF_STATES) != 0) return -1;
  if(x64_emit_load_r32_disp(buf, X64_R8, JIT_REG_T1, JIT_OBJ_OFF_CAP) != 0) return -1;
  if(x64_emit_sub_r32_imm(buf, X64_R8, 1) != 0) return -1; /* mask */
  if(emit_mov_r32_imm(buf, X64_R9, h0) != 0) return -1;
  if(emit_and_r32_rr(buf, X64_R9, X64_R8) != 0) return -1;
  if(emit_mov_r32_rr(buf, X64_R11, X64_R9) != 0) return -1; /* i0 for wrap→miss */

  size_t loop_at = buf->size;
  /* state = states[i] */
  if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
  if(x64_emit_add_rr(buf, X64_RAX, X64_RCX) != 0) return -1;
  if(x64_emit_movzx_r32_m8_disp(buf, X64_RAX, X64_RAX, 0) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_RAX, (int32_t)JELLO_OBJ_SLOT_EMPTY) != 0) return -1;
  j_miss[n_miss++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_RAX, (int32_t)JELLO_OBJ_SLOT_OCCUPIED) != 0) return -1;
  size_t j_next = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;

  /* keys[i] == atom_id? */
  if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
  if(x64_emit_shl_r32_imm8(buf, X64_RAX, 2) != 0) return -1;
  if(x64_emit_add_rr(buf, X64_RAX, X64_R10) != 0) return -1;
  if(x64_emit_load_r32_disp(buf, X64_RAX, X64_RAX, 0) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_RAX, (int32_t)atom_id) != 0) return -1;
  size_t j_hit = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;

  size_t next_at = buf->size;
  if(patch_jcc_rel32(buf, j_next, next_at) != 0) return -1;
  if(x64_emit_add_r32_imm(buf, X64_R9, 1) != 0) return -1;
  if(emit_and_r32_rr(buf, X64_R9, X64_R8) != 0) return -1;
  if(emit_cmp_r32_rr(buf, X64_R9, X64_R11) != 0) return -1;
  j_miss[n_miss++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
  size_t j_loop = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;
  if(patch_jmp_rel32(buf, j_loop, loop_at) != 0) return -1;

  size_t hit_at = buf->size;
  if(patch_jcc_rel32(buf, j_hit, hit_at) != 0) return -1;
  if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
  if(x64_emit_shl_r32_imm8(buf, X64_RAX, 3) != 0) return -1;
  if(x64_emit_add_rr(buf, X64_RAX, X64_RDX) != 0) return -1;
  if(x64_emit_load_r64_disp(buf, X64_RAX, X64_RAX, 0) != 0) return -1;

  /* RAX = boxed value. Unbox for dst kind; rare shapes → slow. */
  if(x64_emit_mov_rr(buf, X64_RCX, X64_RAX) != 0) return -1;
  if(x64_emit_and_r32_imm(buf, X64_RCX, 7) != 0) return -1;

  if(k == JELLO_T_F64) {
    if(x64_emit_cmp_r32_imm(buf, X64_RCX, (int32_t)JELLO_TAG_NULL) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
    if(x64_emit_test_rr(buf, X64_RCX, X64_RCX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) return -1; /* not ptr */
    if(x64_emit_test_r64(buf, X64_RAX, X64_RAX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1; /* null ptr */
    /* cmp dword [rax], BOX_F64 */
    {
      int blo = 0, brex = 0;
      x64_reg_parts(X64_RAX, &blo, &brex);
      uint8_t bytes[12];
      size_t n = 0;
      if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
      bytes[n++] = 0x81u;
      n = x64_append_mem_disp32(bytes, n, 2, 7, blo, 0); /* /7 cmp */
      uint32_t imm = (uint32_t)JELLO_OBJ_BOX_F64;
      bytes[n++] = (uint8_t)(imm & 0xFFu);
      bytes[n++] = (uint8_t)((imm >> 8) & 0xFFu);
      bytes[n++] = (uint8_t)((imm >> 16) & 0xFFu);
      bytes[n++] = (uint8_t)((imm >> 24) & 0xFFu);
      if(jello_jit_emit_bytes(buf, bytes, n) != 0) return -1;
    }
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;
    if(x64_emit_load_xmm64_disp(buf, JIT_FP_T0, X64_RAX, 8) != 0) return -1;
    if(emit_str_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
    j_done[n_done++] = buf->size;
    if(x64_emit_jmp_rel32(buf) != 0) return -1;
  } else if(k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32) {
    if(x64_emit_cmp_r32_imm(buf, X64_RCX, (int32_t)JELLO_TAG_NULL) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
    if(x64_emit_cmp_r32_imm(buf, X64_RCX, (int32_t)JELLO_TAG_I32) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;
    /* shr rax, 3 */
    {
      int dlo = 0, drex = 0;
      x64_reg_parts(X64_RAX, &dlo, &drex);
      uint8_t bytes[5];
      size_t n = 0;
      bytes[n++] = x64_rex(1, 0, 0, drex);
      bytes[n++] = 0xC1u;
      bytes[n++] = x64_modrm(3, 5, dlo); /* /5 shr */
      bytes[n++] = 3;
      if(jello_jit_emit_bytes(buf, bytes, n) != 0) return -1;
    }
    if(emit_str_w_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
    j_done[n_done++] = buf->size;
    if(x64_emit_jmp_rel32(buf) != 0) return -1;
  } else if(k == JELLO_T_I64) {
    if(x64_emit_cmp_r32_imm(buf, X64_RCX, (int32_t)JELLO_TAG_NULL) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
    if(x64_emit_cmp_r32_imm(buf, X64_RCX, (int32_t)JELLO_TAG_I32) != 0) return -1;
    size_t j_i32 = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
    if(x64_emit_test_rr(buf, X64_RCX, X64_RCX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) return -1;
    if(x64_emit_test_r64(buf, X64_RAX, X64_RAX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
    {
      int blo = 0, brex = 0;
      x64_reg_parts(X64_RAX, &blo, &brex);
      uint8_t bytes[12];
      size_t n = 0;
      if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
      bytes[n++] = 0x81u;
      n = x64_append_mem_disp32(bytes, n, 2, 7, blo, 0);
      uint32_t imm = (uint32_t)JELLO_OBJ_BOX_I64;
      bytes[n++] = (uint8_t)(imm & 0xFFu);
      bytes[n++] = (uint8_t)((imm >> 8) & 0xFFu);
      bytes[n++] = (uint8_t)((imm >> 16) & 0xFFu);
      bytes[n++] = (uint8_t)((imm >> 24) & 0xFFu);
      if(jello_jit_emit_bytes(buf, bytes, n) != 0) return -1;
    }
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;
    if(x64_emit_load_r64_disp(buf, X64_RAX, X64_RAX, 8) != 0) return -1;
    if(emit_str_x_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
    j_done[n_done++] = buf->size;
    if(x64_emit_jmp_rel32(buf) != 0) return -1;
    size_t i32_at = buf->size;
    if(patch_jcc_rel32(buf, j_i32, i32_at) != 0) return -1;
    {
      int dlo = 0, drex = 0;
      x64_reg_parts(X64_RAX, &dlo, &drex);
      uint8_t bytes[5];
      size_t n = 0;
      bytes[n++] = x64_rex(1, 0, 0, drex);
      bytes[n++] = 0xC1u;
      bytes[n++] = x64_modrm(3, 5, dlo);
      bytes[n++] = 3;
      if(jello_jit_emit_bytes(buf, bytes, n) != 0) return -1;
    }
    /* movsxd rax, eax */
    {
      int dlo = 0, slo = 0, drex = 0, srex = 0;
      x64_reg_parts(X64_RAX, &dlo, &drex);
      x64_reg_parts(X64_RAX, &slo, &srex);
      uint8_t bytes[4];
      size_t n = 0;
      bytes[n++] = x64_rex(1, drex, 0, srex);
      bytes[n++] = 0x63u;
      bytes[n++] = x64_modrm(3, dlo, slo);
      if(jello_jit_emit_bytes(buf, bytes, n) != 0) return -1;
    }
    if(emit_str_x_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
    j_done[n_done++] = buf->size;
    if(x64_emit_jmp_rel32(buf) != 0) return -1;
  } else {
    /* pointer-like */
    if(x64_emit_cmp_r32_imm(buf, X64_RCX, (int32_t)JELLO_TAG_NULL) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
    if(x64_emit_test_rr(buf, X64_RCX, X64_RCX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) return -1;
    if(emit_str_x_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
    j_done[n_done++] = buf->size;
    if(x64_emit_jmp_rel32(buf) != 0) return -1;
  }

  size_t miss_at = buf->size;
  for(int i = 0; i < n_miss; i++) {
    if(patch_jcc_rel32(buf, j_miss[i], miss_at) != 0) return -1;
  }
  if(x64_emit_xor_r64(buf, X64_RAX, X64_RAX) != 0) return -1;
  if(k == JELLO_T_F64) {
    if(emit_str_x_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  } else if(k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32) {
    if(emit_str_w_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  } else {
    if(emit_str_x_slot(buf, X64_RAX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  }
  j_done[n_done++] = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;

  size_t slow_at = buf->size;
  for(int i = 0; i < n_slow; i++) {
    if(patch_jcc_rel32(buf, j_slow[i], slow_at) != 0) return -1;
  }
  if(emit_obj_get_atom_helper(buf, dst, obj_reg, atom_id) != 0) return -1;

  size_t done_at = buf->size;
  for(int i = 0; i < n_done; i++) {
    if(patch_jmp_rel32(buf, j_done[i], done_at) != 0) return -1;
  }
  return 0;
}

static int emit_obj_set_atom_helper(
    jello_jit_emit_buf* buf,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_RSI, val_reg) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_RDX, obj_reg) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_RCX, atom_id) != 0) return -1;
  return emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_obj_set_atom);
}

static int emit_cmp_m32_imm0(jello_jit_emit_buf* buf, int base, uint32_t disp, uint32_t imm) {
  int blo = 0, brex = 0;
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[16];
  size_t n = 0;
  if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
  bytes[n++] = 0x81u;
  n = x64_append_mem_disp32(bytes, n, 2, 7, blo, disp);
  bytes[n++] = (uint8_t)(imm & 0xFFu);
  bytes[n++] = (uint8_t)((imm >> 8) & 0xFFu);
  bytes[n++] = (uint8_t)((imm >> 16) & 0xFFu);
  bytes[n++] = (uint8_t)((imm >> 24) & 0xFFu);
  return jello_jit_emit_bytes(buf, bytes, n);
}

/* Occupied inplace (F64/I64/I32) + empty/tomb insert when load factor allows. */
static int emit_obj_set_atom(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  jello_type_kind k = vm_reg_kind(m, f, val_reg);
  /* Match arm64: only inline scalar upserts; large atom imm → helper. */
  int inline_ok = (k == JELLO_T_F64 || k == JELLO_T_I64 || k == JELLO_T_I32 || k == JELLO_T_I8 ||
                   k == JELLO_T_I16);
  if(!inline_ok || atom_id == JELLO_ATOM___PROTO__ || atom_id > 0xFFFu)
    return emit_obj_set_atom_helper(buf, val_reg, obj_reg, atom_id);

  uint32_t h0 = jit_obj_hash_u32(atom_id);
  size_t j_slow[12];
  int n_slow = 0;
  size_t j_done[4];
  int n_done = 0;
  size_t j_insert[4];
  int n_insert = 0;

  if(emit_ldr_x_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, obj_reg)) != 0) return -1;
  if(x64_emit_test_r64(buf, JIT_REG_T1, JIT_REG_T1) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;

  /* Prefer Win64-volatile regs for table ptrs (avoid RSI/RDI). */
  if(x64_emit_load_r64_disp(buf, X64_R10, JIT_REG_T1, JIT_OBJ_OFF_KEYS) != 0) return -1;
  if(x64_emit_load_r64_disp(buf, X64_RDX, JIT_REG_T1, JIT_OBJ_OFF_VALS) != 0) return -1;
  if(x64_emit_load_r64_disp(buf, X64_RCX, JIT_REG_T1, JIT_OBJ_OFF_STATES) != 0) return -1;
  if(x64_emit_load_r32_disp(buf, X64_R8, JIT_REG_T1, JIT_OBJ_OFF_CAP) != 0) return -1;
  if(x64_emit_sub_r32_imm(buf, X64_R8, 1) != 0) return -1; /* mask */
  if(emit_mov_r32_imm(buf, X64_R9, h0) != 0) return -1;
  if(emit_and_r32_rr(buf, X64_R9, X64_R8) != 0) return -1;
  if(emit_mov_r32_rr(buf, JIT_REG_T0, X64_R9) != 0) return -1; /* i0 for wrap→slow */
  if(emit_mov_r32_imm(buf, X64_R11, 0xFFFFFFFFu) != 0) return -1; /* first_tomb */

  if(x64_emit_load_r32_disp(buf, X64_RAX, JIT_REG_T1, JIT_OBJ_OFF_LEN) != 0) return -1;
  if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) return -1;
  j_insert[n_insert++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1; /* len==0 → insert at i */

  size_t loop_at = buf->size;
  if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
  if(x64_emit_add_rr(buf, X64_RAX, X64_RCX) != 0) return -1;
  if(x64_emit_movzx_r32_m8_disp(buf, X64_RAX, X64_RAX, 0) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_RAX, (int32_t)JELLO_OBJ_SLOT_EMPTY) != 0) return -1;
  j_insert[n_insert++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_RAX, (int32_t)JELLO_OBJ_SLOT_OCCUPIED) != 0) return -1;
  size_t j_tomb = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1; /* tomb or other → tomb path */

  /* occupied: key match? */
  if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
  if(x64_emit_shl_r32_imm8(buf, X64_RAX, 2) != 0) return -1;
  if(x64_emit_add_rr(buf, X64_RAX, X64_R10) != 0) return -1;
  if(x64_emit_load_r32_disp(buf, X64_RAX, X64_RAX, 0) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_RAX, (int32_t)atom_id) != 0) return -1;
  size_t j_hit = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
  size_t j_to_next = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;

  /* tomb: record first_tomb once */
  size_t tomb_at = buf->size;
  if(patch_jcc_rel32(buf, j_tomb, tomb_at) != 0) return -1;
  if(x64_emit_cmp_r32_imm(buf, X64_R11, -1) != 0) return -1;
  size_t j_skip_tomb = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;
  if(x64_emit_mov_rr(buf, X64_R11, X64_R9) != 0) return -1;
  size_t skip_tomb_at = buf->size;
  if(patch_jcc_rel32(buf, j_skip_tomb, skip_tomb_at) != 0) return -1;

  size_t next_at = buf->size;
  if(patch_jmp_rel32(buf, j_to_next, next_at) != 0) return -1;
  if(x64_emit_add_r32_imm(buf, X64_R9, 1) != 0) return -1;
  if(emit_and_r32_rr(buf, X64_R9, X64_R8) != 0) return -1;
  /* Full probe lap with no empty/hit → C helper (grow/rehash). Prevents spin. */
  if(emit_cmp_r32_rr(buf, X64_R9, JIT_REG_T0) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1;
  size_t j_loop = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;
  if(patch_jmp_rel32(buf, j_loop, loop_at) != 0) return -1;

  /* ---- hit: occupied inplace / I32 overwrite ---- */
  size_t hit_at = buf->size;
  if(patch_jcc_rel32(buf, j_hit, hit_at) != 0) return -1;
  if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
  if(x64_emit_shl_r32_imm8(buf, X64_RAX, 3) != 0) return -1;
  if(x64_emit_add_rr(buf, X64_RAX, X64_RDX) != 0) return -1;

  if(k == JELLO_T_F64) {
    if(x64_emit_load_r64_disp(buf, X64_RCX, X64_RAX, 0) != 0) return -1;
    if(x64_emit_mov_rr(buf, X64_R8, X64_RCX) != 0) return -1;
    if(x64_emit_and_r32_imm(buf, X64_R8, 7) != 0) return -1;
    if(x64_emit_test_rr(buf, X64_R8, X64_R8) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) return -1;
    if(x64_emit_test_r64(buf, X64_RCX, X64_RCX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
    if(emit_cmp_m32_imm0(buf, X64_RCX, 0, (uint32_t)JELLO_OBJ_BOX_F64) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;
    if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    if(x64_emit_store_xmm64_disp(buf, JIT_FP_T0, X64_RCX, 8) != 0) return -1;
  } else if(k == JELLO_T_I64) {
    if(x64_emit_load_r64_disp(buf, X64_RCX, X64_RAX, 0) != 0) return -1;
    if(x64_emit_mov_rr(buf, X64_R8, X64_RCX) != 0) return -1;
    if(x64_emit_and_r32_imm(buf, X64_R8, 7) != 0) return -1;
    if(x64_emit_test_rr(buf, X64_R8, X64_R8) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) return -1;
    if(x64_emit_test_r64(buf, X64_RCX, X64_RCX) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) return -1;
    if(emit_cmp_m32_imm0(buf, X64_RCX, 0, (uint32_t)JELLO_OBJ_BOX_I64) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(x64_emit_jcc_rel32(buf, X64_CC_JNE) != 0) return -1;
    if(emit_ldr_x_slot(buf, X64_R8, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    if(x64_emit_store_r64_disp(buf, X64_R8, X64_RCX, 8) != 0) return -1;
  } else {
    if(emit_ldr_w_slot(buf, X64_R8, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    /* jello_make_i32: shift/or must be 64-bit or high payload bits of negative I32 are lost. */
    if(x64_emit_shl_r64_imm8(buf, X64_R8, 3) != 0) return -1;
    if(x64_emit_or_r64_imm(buf, X64_R8, (int32_t)JELLO_TAG_I32) != 0) return -1;
    if(x64_emit_store_r64_disp(buf, X64_R8, X64_RAX, 0) != 0) return -1;
  }
  j_done[n_done++] = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;

  /* ---- insert: slot = first_tomb != -1 ? first_tomb : i ---- */
  size_t insert_at = buf->size;
  for(int i = 0; i < n_insert; i++) {
    if(patch_jcc_rel32(buf, j_insert[i], insert_at) != 0) return -1;
  }
  if(x64_emit_cmp_r32_imm(buf, X64_R11, -1) != 0) return -1;
  size_t j_use_i = buf->size;
  if(x64_emit_jcc_rel32(buf, X64_CC_JE) != 0) return -1; /* first_tomb==-1 → keep i in r9 */
  if(x64_emit_mov_rr(buf, X64_R9, X64_R11) != 0) return -1;
  size_t use_i_at = buf->size;
  if(patch_jcc_rel32(buf, j_use_i, use_i_at) != 0) return -1;

  /* load factor: (len+1)*10 < cap*7 (reload cap; R10 holds keys) */
  if(x64_emit_load_r32_disp(buf, X64_RAX, JIT_REG_T1, JIT_OBJ_OFF_LEN) != 0) return -1;
  if(x64_emit_add_r32_imm(buf, X64_RAX, 1) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_R8, (uint32_t)JELLO_OBJECT_LOAD_NUM) != 0) return -1;
  if(x64_emit_imul_r32(buf, X64_RAX, X64_R8) != 0) return -1;
  if(x64_emit_load_r32_disp(buf, X64_R8, JIT_REG_T1, JIT_OBJ_OFF_CAP) != 0) return -1;
  if(emit_mov_r32_imm(buf, X64_R11, (uint32_t)JELLO_OBJECT_LOAD_DEN) != 0) return -1;
  if(x64_emit_imul_r32(buf, X64_R8, X64_R11) != 0) return -1;
  if(x64_emit_cmp_r32_rr(buf, X64_RAX, X64_R8) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(x64_emit_jcc_rel32(buf, 0x83u /* JAE */) != 0) return -1; /* would rehash */

  if(k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32) {
    /* keys[i]=atom; states[i]=OCCUPIED; vals[i]=make_i32; len++ */
    if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
    if(x64_emit_shl_r32_imm8(buf, X64_RAX, 2) != 0) return -1;
    if(x64_emit_add_rr(buf, X64_RAX, X64_R10) != 0) return -1;
    if(emit_mov_r32_imm(buf, X64_R8, atom_id) != 0) return -1;
    if(x64_emit_store_r32_disp(buf, X64_R8, X64_RAX, 0) != 0) return -1;
    if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
    if(x64_emit_add_rr(buf, X64_RAX, X64_RCX) != 0) return -1;
    if(x64_emit_store_imm8_disp(buf, X64_RAX, 0, (uint8_t)JELLO_OBJ_SLOT_OCCUPIED) != 0) return -1;
    if(emit_ldr_w_slot(buf, X64_R8, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    /* jello_make_i32: shift/or must be 64-bit or high payload bits of negative I32 are lost. */
    if(x64_emit_shl_r64_imm8(buf, X64_R8, 3) != 0) return -1;
    if(x64_emit_or_r64_imm(buf, X64_R8, (int32_t)JELLO_TAG_I32) != 0) return -1;
    if(x64_emit_mov_rr(buf, X64_RAX, X64_R9) != 0) return -1;
    if(x64_emit_shl_r32_imm8(buf, X64_RAX, 3) != 0) return -1;
    if(x64_emit_add_rr(buf, X64_RAX, X64_RDX) != 0) return -1;
    if(x64_emit_store_r64_disp(buf, X64_R8, X64_RAX, 0) != 0) return -1;
    if(x64_emit_load_r32_disp(buf, X64_RAX, JIT_REG_T1, JIT_OBJ_OFF_LEN) != 0) return -1;
    if(x64_emit_add_r32_imm(buf, X64_RAX, 1) != 0) return -1;
    if(x64_emit_store_r32_disp(buf, X64_RAX, JIT_REG_T1, JIT_OBJ_OFF_LEN) != 0) return -1;
  } else {
    /* F64/I64: thin insert helper (boxes + writes slot). */
    if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) return -1;
    if(emit_mov_r32_imm(buf, X64_RSI, val_reg) != 0) return -1;
    if(emit_mov_r32_imm(buf, X64_RDX, obj_reg) != 0) return -1;
    if(emit_mov_r32_imm(buf, X64_RCX, atom_id) != 0) return -1;
    if(x64_emit_mov_rr(buf, X64_R8, X64_R9) != 0) return -1;
    if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_obj_insert_atom) != 0)
      return -1;
  }
  j_done[n_done++] = buf->size;
  if(x64_emit_jmp_rel32(buf) != 0) return -1;

  size_t slow_at = buf->size;
  for(int i = 0; i < n_slow; i++) {
    if(patch_jcc_rel32(buf, j_slow[i], slow_at) != 0) return -1;
  }
  if(emit_obj_set_atom_helper(buf, val_reg, obj_reg, atom_id) != 0) return -1;

  size_t done_at = buf->size;
  for(int i = 0; i < n_done; i++) {
    if(patch_jmp_rel32(buf, j_done[i], done_at) != 0) return -1;
  }
  return 0;
}

static int emit_ir(
    jello_jit_emit_buf* buf,
    const jello_jit_ir_func* ir,
    const frame_layout* layout,
    const jello_bc_function* f,
    const jello_bc_module* m,
    size_t* ir_off,
    jello_jit_patch_site* patch_sites,
    size_t* npatch_sites,
    size_t patch_cap,
    size_t body_entry,
    int* saw_ret
) {
  const jello_jit_layout* lay = jello_jit_runtime_layout();
  uint8_t* is_target = (uint8_t*)calloc((size_t)ir->ninsns, 1);
  if(!is_target && ir->ninsns) return -1;
  for(uint32_t j = 0; j < ir->ninsns; j++) {
    const jello_jit_ir_insn* t = &ir->insns[j];
    if(t->op != JIR_JMP && t->op != JIR_JMP_IF) continue;
    uint32_t bc_tgt = (uint32_t)t->imm;
    /* Match ir_off_for_bc: skip leading FUEL_CHECK at the target PC. */
    for(uint32_t k = 0; k < ir->ninsns; k++) {
      if(ir->insns[k].bc_pc != bc_tgt) continue;
      if(ir->insns[k].op == JIR_FUEL_CHECK) continue;
      is_target[k] = 1u;
      break;
    }
  }
  uint16_t i32c_reg = JIT_I32C_NONE;
  for(uint32_t i = 0; i < ir->ninsns; i++) {
    const jello_jit_ir_insn* in = &ir->insns[i];
    ir_off[i] = buf->size;
    if(is_target[i]) i32c_reg = JIT_I32C_NONE;
    if(x64_emit_endbr64(buf) != 0) goto fail_ir;

    switch(in->op) {
      case JIR_NOP:
        break;
      case JIR_LOAD_I32:
        if(emit_mov_r32_imm(buf, JIT_REG_T0, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_str_i32_t0(buf, layout, &i32c_reg, in->a) != 0) goto fail_ir;
        break;
      case JIR_LOAD_I64: {
        uint32_t idx = (uint32_t)in->imm;
        if(idx >= m->nconst_i64) goto fail_ir;
        if(emit_mov_r64_imm(buf, JIT_REG_T0, (uint64_t)m->const_i64[idx]) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      }
      case JIR_LOAD_F64: {
        uint32_t idx = (uint32_t)in->imm;
        if(idx >= m->nconst_f64) goto fail_ir;
        uint64_t bits = 0;
        memcpy(&bits, &m->const_f64[idx], sizeof(bits));
        if(emit_mov_r64_imm(buf, JIT_REG_T0, bits) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      }
      case JIR_LOAD_F32:
        if(emit_mov_r32_imm(buf, JIT_REG_T0, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_MOV_REG: {
        size_t sz = reg_slot_sz(m, f, in->a);
        if(sz == 8u) {
          if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
          if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
          i32c_reg = JIT_I32C_NONE;
        } else {
          if(emit_ldr_i32_t0(buf, layout, &i32c_reg, in->b) != 0) goto fail_ir;
          if(emit_str_i32_t0(buf, layout, &i32c_reg, in->a) != 0) goto fail_ir;
        }
        break;
      }
      case JIR_BIN_I32: {
        jello_op bc_op = (jello_op)f->insns[in->bc_pc].op;
        int imm_rhs =
            (bc_op == JOP_ADD_I32_IMM || bc_op == JOP_SUB_I32_IMM || bc_op == JOP_MUL_I32_IMM);
        int32_t imm8 = (int32_t)(int8_t)(uint8_t)in->c;
        jello_jit_ir_bin bop = (jello_jit_ir_bin)in->imm;
        if(emit_ldr_i32_t0(buf, layout, &i32c_reg, in->b) != 0) goto fail_ir;
        if(imm_rhs && (bop == JIR_BIN_ADD || bop == JIR_BIN_SUB)) {
          if(bop == JIR_BIN_SUB) {
            if(x64_emit_sub_r32_imm(buf, JIT_REG_T0, imm8) != 0) goto fail_ir;
          } else {
            if(x64_emit_add_r32_imm(buf, JIT_REG_T0, imm8) != 0) goto fail_ir;
          }
        } else {
          if(imm_rhs) {
            if(emit_mov_r32_imm(buf, JIT_REG_T1, (uint32_t)imm8) != 0) goto fail_ir;
          } else {
            if(emit_ldr_i32_t1(buf, layout, &i32c_reg, in->c) != 0) goto fail_ir;
          }
          if(emit_bin_i32(buf, bop) != 0) goto fail_ir;
        }
        if(emit_str_i32_t0(buf, layout, &i32c_reg, in->a) != 0) goto fail_ir;
        break;
      }
      case JIR_BIN_I64: {
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_x_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(emit_bin_i64(buf, (jello_jit_ir_bin)in->imm) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      }
      case JIR_BIN_F64: {
        if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_d_slot(buf, JIT_FP_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(emit_bin_f64(buf, (jello_jit_ir_bin)in->imm) != 0) goto fail_ir;
        if(emit_str_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_SQRT_F64: {
        if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(x64_emit_sqrtsd(buf, JIT_FP_T0, JIT_FP_T0) != 0) goto fail_ir;
        if(emit_str_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_BIN_F32: {
        if(emit_ldr_s_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_s_slot(buf, JIT_FP_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(emit_bin_f32(buf, (jello_jit_ir_bin)in->imm) != 0) goto fail_ir;
        if(emit_str_s_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_NEG_I32:
        if(emit_ldr_i32_t0(buf, layout, &i32c_reg, in->b) != 0) goto fail_ir;
        if(emit_neg_r32(buf, JIT_REG_T0) != 0) goto fail_ir;
        if(emit_str_i32_t0(buf, layout, &i32c_reg, in->a) != 0) goto fail_ir;
        break;
      case JIR_NEG_I64:
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(x64_emit_neg_r64(buf, JIT_REG_T0) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_NEG_F64:
        /* Flip IEEE sign bit via integer XOR (avoids constant pool). */
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_mov_r64_imm(buf, JIT_REG_T1, 0x8000000000000000ull) != 0) goto fail_ir;
        if(x64_emit_xor_r64(buf, JIT_REG_T0, JIT_REG_T1) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_NEG_F32:
        if(emit_ldr_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, JIT_REG_T1, 0x80000000u) != 0) goto fail_ir;
        if(x64_emit_xor_r32(buf, JIT_REG_T0, JIT_REG_T1) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_LOAD_NULL:
        if(emit_mov_r64_imm(buf, JIT_REG_T0, (uint64_t)jello_make_null()) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_CMP_I32: {
        jello_op bc_op = (jello_op)f->insns[in->bc_pc].op;
        /* Imm ops store signed int8 in c (so -1 is 255). NOT_BOOL uses c=255 as
         * "compare to 0" only when the bytecode op is not *_I32_IMM. */
        uint32_t rhs_is_imm = (bc_op == JOP_EQ_I32_IMM || bc_op == JOP_LT_I32_IMM) ? 1u : 0u;
        if(emit_ldr_i32_t0(buf, layout, &i32c_reg, in->b) != 0) goto fail_ir;
        if(rhs_is_imm) {
          int32_t imm8 = (int32_t)(int8_t)(uint8_t)in->c;
          if(x64_emit_cmp_r32_imm(buf, JIT_REG_T0, imm8) != 0) goto fail_ir;
        } else if(in->c == 255u) {
          if(x64_emit_cmp_r32_imm0(buf, JIT_REG_T0) != 0) goto fail_ir;
        } else {
          if(emit_ldr_i32_t1(buf, layout, &i32c_reg, in->c) != 0) goto fail_ir;
          if(emit_cmp_r32_rr(buf, JIT_REG_T0, JIT_REG_T1) != 0) goto fail_ir;
        }
        if(emit_cmp_set_i32(buf, (jello_jit_ir_cmp)in->imm) != 0) goto fail_ir;
        if(emit_str_i32_t0(buf, layout, &i32c_reg, in->a) != 0) goto fail_ir;
        break;
      }
      case JIR_CMP_I64: {
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_x_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(x64_emit_cmp_r64_rr(buf, JIT_REG_T0, JIT_REG_T1) != 0) goto fail_ir;
        if(emit_cmp_set_i32(buf, (jello_jit_ir_cmp)in->imm) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      }
      case JIR_CMP_F64: {
        if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_d_slot(buf, JIT_FP_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(x64_emit_comisd(buf, JIT_FP_T0, JIT_FP_T1) != 0) goto fail_ir;
        {
          uint8_t cc = (uint8_t)((jello_jit_ir_cmp)in->imm == JIR_CMP_LT ? X64_CC_SETB : X64_CC_SETE);
          if(x64_emit_setcc(buf, cc, JIT_REG_T0) != 0) goto fail_ir;
          if(x64_emit_movzx_r32_r8(buf, JIT_REG_T0, JIT_REG_T0) != 0) goto fail_ir;
        }
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      }
      case JIR_CMP_F32: {
        if(emit_ldr_s_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_s_slot(buf, JIT_FP_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(x64_emit_comiss(buf, JIT_FP_T0, JIT_FP_T1) != 0) goto fail_ir;
        {
          uint8_t cc = (uint8_t)((jello_jit_ir_cmp)in->imm == JIR_CMP_LT ? X64_CC_SETB : X64_CC_SETE);
          if(x64_emit_setcc(buf, cc, JIT_REG_T0) != 0) goto fail_ir;
          if(x64_emit_movzx_r32_r8(buf, JIT_REG_T0, JIT_REG_T0) != 0) goto fail_ir;
        }
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      }
      case JIR_JMP: {
        uint32_t bc_tgt = (uint32_t)in->imm;
        if(bc_tgt >= f->ninsns) goto fail_ir;
        if(*npatch_sites >= patch_cap) goto fail_ir;
        size_t at = buf->size;
        if(x64_emit_jmp_rel32(buf) != 0) goto fail_ir;
        patch_sites[*npatch_sites].at = at;
        patch_sites[*npatch_sites].bc_tgt = bc_tgt;
        patch_sites[*npatch_sites].kind = 0u;
        (*npatch_sites)++;
        break;
      }
      case JIR_JMP_IF: {
        uint32_t bc_tgt = (uint32_t)in->imm;
        if(bc_tgt >= f->ninsns) goto fail_ir;
        if(*npatch_sites >= patch_cap) goto fail_ir;
        if(emit_ldr_i32_t0(buf, layout, &i32c_reg, in->a) != 0) goto fail_ir;
        if(x64_emit_test_rr(buf, JIT_REG_T0, JIT_REG_T0) != 0) goto fail_ir;
        size_t jcc_at = buf->size;
        if(x64_emit_jcc_rel32(buf, X64_CC_JNZ) != 0) goto fail_ir;
        patch_sites[*npatch_sites].at = jcc_at;
        patch_sites[*npatch_sites].bc_tgt = bc_tgt;
        patch_sites[*npatch_sites].kind = 1u;
        (*npatch_sites)++;
        break;
      }
      case JIR_FUEL_CHECK:
        if(emit_inline_fuel_check(buf) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_BYTES_LEN: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_bytes_len) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_GET_U8: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_bytes_get_u8) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_SET_U8: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_bytes_set_u8) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_READ: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_R8, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_bytes_read) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_WRITE: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_R8, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_bytes_write) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_LEN: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_array_len) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_GET: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_array_get) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_SET: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_array_set) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_NEW: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_array_new) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_NEW: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_bytes_new) != 0) goto fail_ir;
        break;
      }
      case JIR_ASSERT: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_assert) != 0) goto fail_ir;
        break;
      }
      case JIR_CONST_BYTES: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_const_bytes) != 0) goto fail_ir;
        break;
      }
      case JIR_CONST_FUN: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_const_fun) != 0) goto fail_ir;
        break;
      }
      case JIR_CLOSURE: {
        /* SysV: rdi=ctx, rsi=dst, rdx=first, rcx=ncaps, r8=func_index */
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->c) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_R8, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue(buf, (void (*)(void))jello_jit_runtime_closure) != 0) goto fail_ir;
        break;
      }
      case JIR_CONV: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_conv) != 0) goto fail_ir;
        break;
      }
      case JIR_OBJ_GET_ATOM: {
        if(emit_obj_get_atom(buf, layout, m, f, in->a, in->b, (uint32_t)in->imm) != 0) goto fail_ir;
        break;
      }
      case JIR_OBJ_SET_ATOM: {
        if(emit_obj_set_atom(buf, layout, m, f, in->a, in->b, (uint32_t)in->imm) != 0) goto fail_ir;
        break;
      }
      case JIR_OBJ_NEW: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void (*)(void))jello_jit_runtime_obj_new) != 0) goto fail_ir;
        break;
      }
      case JIR_SLOW: {
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue(buf, (void (*)(void))jello_jit_runtime_slow_op) != 0) goto fail_ir;
        break;
      }
      case JIR_CALL_SELF: {
        /* SysV: rdi..r9 = ctx,first,nargs,dst,resume_pc,return_addr; then jmp body. */
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->c) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_R8, in->bc_pc + 1u) != 0) goto fail_ir;
        size_t lea_at = buf->size;
        if(x64_emit_lea_rip_rel32(buf, X64_R9) != 0) goto fail_ir;
        if(emit_rt_call_continue(buf, (void (*)(void))jello_jit_runtime_call_self) != 0) goto fail_ir;
        size_t jmp_at = buf->size;
        if(x64_emit_jmp_rel32(buf) != 0) goto fail_ir;
        size_t resume = buf->size;
        if(x64_patch_lea_rip_rel32(buf, lea_at, resume) != 0) goto fail_ir;
        if(patch_jmp_rel32(buf, jmp_at, body_entry) != 0) goto fail_ir;
        break;
      }
      case JIR_CALL_DIRECT: {
        /* 8 args: rdi..r9 + stack (bidx, callee_reg). CONTINUE → jmp *jit_call_entry. */
        uint32_t bidx = (uint32_t)in->imm & 0xFFFFu;
        uint32_t callee_reg = (uint32_t)in->imm >> 16;
        if(callee_reg == 0xFFFFu) callee_reg = 0xFFFFFFFFu;
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->b) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RDX, in->c) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RCX, in->a) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_R8, in->bc_pc + 1u) != 0) goto fail_ir;
        size_t lea_at = buf->size;
        if(x64_emit_lea_rip_rel32(buf, X64_R9) != 0) goto fail_ir;
        if(x64_emit_sub_rsp_imm8(buf, 16u) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RAX, bidx) != 0) goto fail_ir;
        if(x64_emit_store_r64_disp(buf, X64_RAX, X64_RSP, 0) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RAX, callee_reg) != 0) goto fail_ir;
        if(x64_emit_store_r64_disp(buf, X64_RAX, X64_RSP, 8) != 0) goto fail_ir;
        if(emit_call_fn(buf, (void (*)(void))jello_jit_runtime_call_direct) != 0) goto fail_ir;
        if(x64_emit_add_rsp_imm8(buf, 16u) != 0) goto fail_ir;
        if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) goto fail_ir;
        size_t jcc_at = buf->size;
        if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) goto fail_ir;
        if(emit_restore_and_ret(buf) != 0) goto fail_ir;
        size_t cont = buf->size;
        if(patch_jcc_rel32(buf, jcc_at, cont) != 0) goto fail_ir;
        if(!lay || lay->exec_ctx_jit_call_entry > 0x7FFFFFFFu ||
           (lay->exec_ctx_jit_call_entry % 8u) != 0u)
          goto fail_ir;
        if(x64_emit_load_r64_disp(buf, X64_RAX, JIT_REG_CTX, lay->exec_ctx_jit_call_entry) != 0)
          goto fail_ir;
        if(emit_mov_r64_imm(buf, JIT_REG_T0, 0) != 0) goto fail_ir;
        if(x64_emit_store_r64_disp(buf, JIT_REG_T0, JIT_REG_CTX, lay->exec_ctx_jit_call_entry) != 0)
          goto fail_ir;
        if(emit_refresh_base(buf) != 0) goto fail_ir;
        if(x64_emit_jmp_r64(buf, X64_RAX) != 0) goto fail_ir;
        size_t resume = buf->size;
        if(x64_patch_lea_rip_rel32(buf, lea_at, resume) != 0) goto fail_ir;
        break;
      }
      case JIR_RET: {
        /* Always ret_self so cross-function native calls can jmp to resume. */
        if(x64_emit_mov_rr(buf, X64_RDI, JIT_REG_CTX) != 0) goto fail_ir;
        if(emit_mov_r32_imm(buf, X64_RSI, in->a) != 0) goto fail_ir;
        if(emit_call_fn(buf, (void (*)(void))jello_jit_runtime_ret_self) != 0) goto fail_ir;
        if(x64_emit_test_rr(buf, X64_RAX, X64_RAX) != 0) goto fail_ir;
        size_t jcc_at = buf->size;
        if(x64_emit_jcc_rel32(buf, X64_CC_JZ) != 0) goto fail_ir;
        if(emit_restore_and_ret(buf) != 0) goto fail_ir;
        size_t cont = buf->size;
        if(patch_jcc_rel32(buf, jcc_at, cont) != 0) goto fail_ir;
        if(!lay || lay->exec_ctx_jit_self_resume > 0x7FFFFFFFu ||
           (lay->exec_ctx_jit_self_resume % 8u) != 0u)
          goto fail_ir;
        if(x64_emit_load_r64_disp(buf, X64_RAX, JIT_REG_CTX, lay->exec_ctx_jit_self_resume) != 0)
          goto fail_ir;
        if(emit_mov_r64_imm(buf, JIT_REG_T0, 0) != 0) goto fail_ir;
        if(x64_emit_store_r64_disp(buf, JIT_REG_T0, JIT_REG_CTX, lay->exec_ctx_jit_self_resume) != 0)
          goto fail_ir;
        if(emit_refresh_base(buf) != 0) goto fail_ir;
        if(x64_emit_jmp_r64(buf, X64_RAX) != 0) goto fail_ir;
        *saw_ret = 1;
        break;
      }
      default:
        goto fail_ir;
    }
    /* Runtime helpers / calls may mutate slots or rebase rf.mem. */
    if(in->op == JIR_SLOW || in->op == JIR_CALL_SELF || in->op == JIR_CALL_DIRECT ||
       in->op == JIR_RET || in->op == JIR_BYTES_LEN || in->op == JIR_BYTES_GET_U8 ||
       in->op == JIR_BYTES_SET_U8 || in->op == JIR_BYTES_READ || in->op == JIR_BYTES_WRITE ||
       in->op == JIR_BYTES_NEW || in->op == JIR_ARRAY_LEN ||
       in->op == JIR_ARRAY_GET || in->op == JIR_ARRAY_SET || in->op == JIR_ARRAY_NEW ||
       in->op == JIR_OBJ_GET_ATOM || in->op == JIR_OBJ_SET_ATOM || in->op == JIR_OBJ_NEW ||
       in->op == JIR_ASSERT || in->op == JIR_CONST_BYTES || in->op == JIR_CONST_FUN ||
       in->op == JIR_CLOSURE || in->op == JIR_CONV)
      i32c_reg = JIT_I32C_NONE;
  }
  free(is_target);
  return 0;
fail_ir:
  free(is_target);
  return -1;
}

static int build_bc_pc_map(
    const jello_jit_ir_func* ir,
    const size_t* ir_off,
    const jello_bc_function* f,
    uint32_t** out_map,
    uint32_t* out_len
) {
  if(!ir || !ir_off || !f || !out_map || !out_len) return -1;
  uint32_t n = f->ninsns;
  uint32_t* map = (uint32_t*)calloc((size_t)n, sizeof(uint32_t));
  if(!map) return -1;
  for(uint32_t i = 0; i < ir->ninsns; i++) {
    const jello_jit_ir_insn* in = &ir->insns[i];
    /* Map first native site per bytecode PC (incl. fuel checks and SLOW
     * stubs). Resume after CALL often lands on a SLOW op; skipping those
     * caused mid-enter miss storms. */
    uint32_t pc = in->bc_pc;
    if(pc >= n) continue;
    if(map[pc] == 0u) map[pc] = (uint32_t)ir_off[i];
  }
  *out_map = map;
  *out_len = n;
  return 0;
}

static int x64_emit_func(
    const jello_jit_ir_func* ir,
    const jello_bc_function* f,
    const frame_layout* layout,
    const jello_bc_module* m,
    jello_jit_emit_buf* out,
    size_t* out_entry_off,
    size_t* out_body_off,
    uint32_t** out_bc_pc_map,
    uint32_t* out_nbc_pc_map
) {
  if(!ir || !f || !layout || !m || !out || !out_entry_off || !out_body_off || !out_bc_pc_map ||
     !out_nbc_pc_map || !ir->ok)
    return -1;
  const jello_jit_layout* lay = jello_jit_runtime_layout();

  size_t* ir_off = (size_t*)calloc((size_t)ir->ninsns, sizeof(size_t));
  jello_jit_patch_site* patch_sites =
      (jello_jit_patch_site*)calloc((size_t)ir->ninsns, sizeof(jello_jit_patch_site));
  if(!ir_off || !patch_sites) {
    free(ir_off);
    free(patch_sites);
    return -1;
  }

  *out_entry_off = 0;
  if(emit_prologue(out, lay) != 0) goto fail;
  size_t prologue_end = out->size;
  *out_body_off = prologue_end;
  {
    size_t npatch = 0;
    int saw_ret = 0;
    if(emit_ir(out, ir, layout, f, m, ir_off, patch_sites, &npatch, ir->ninsns, prologue_end,
               &saw_ret) != 0)
      goto fail;
    if(patch_branch_sites(out, ir, ir_off, patch_sites, npatch, prologue_end) != 0) goto fail;
    if(!saw_ret && emit_epilogue(out, (uint32_t)JELLO_JIT_EXIT_YIELD) != 0) goto fail;
  }

  if(build_bc_pc_map(ir, ir_off, f, out_bc_pc_map, out_nbc_pc_map) != 0) goto fail;

  free(ir_off);
  free(patch_sites);
  return 0;
fail:
  free(*out_bc_pc_map);
  *out_bc_pc_map = NULL;
  *out_nbc_pc_map = 0;
  free(ir_off);
  free(patch_sites);
  return -1;
}

const jello_jit_backend jello_jit_backend_x64 = {
  .name = "x64",
  .emit_func = x64_emit_func,
};
