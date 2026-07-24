// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>
#include <jello/internal/jit_arm64.h>
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
_Static_assert(offsetof(jello_object, ic_atom) == 48u, "jello_object.ic_atom");
_Static_assert(offsetof(jello_object, ic_slot) == 52u, "jello_object.ic_slot");
_Static_assert(offsetof(jello_object, ic_cap) == 56u, "jello_object.ic_cap");
_Static_assert(offsetof(jello_array, length) == 8u, "jello_array.length");
_Static_assert(offsetof(jello_array, data) == 16u, "jello_array.data");

#define JIT_ARRAY_OFF_LEN 8u
#define JIT_ARRAY_OFF_DATA 16u

#define JIT_REG_CTX 19
#define JIT_REG_BASE 20
#define JIT_REG_T0 21
#define JIT_REG_T1 22
#define JIT_REG_T2 23
#define JIT_REG_I32C 24 /* BB-local cached I32 slot value (callee-saved) */

#define JIT_FP_T0 0
#define JIT_FP_T1 1

#define JIT_I32C_NONE 0xFFFFu

#define JIT_OBJ_OFF_PROTO 8u
#define JIT_OBJ_OFF_CAP 16u
#define JIT_OBJ_OFF_LEN 20u
#define JIT_OBJ_OFF_KEYS 24u
#define JIT_OBJ_OFF_VALS 32u
#define JIT_OBJ_OFF_STATES 40u
#define JIT_OBJ_OFF_IC_ATOM 48u
#define JIT_OBJ_OFF_IC_SLOT 52u
#define JIT_OBJ_OFF_IC_CAP 56u

/* Scratch for object probe (caller-saved). */
#define JIT_X_KEYS 10
#define JIT_X_VALS 11
#define JIT_X_STATES 12
#define JIT_X_MASK 13
#define JIT_X_IDX 14
#define JIT_X_TMP 15
#define JIT_X_OBJ JIT_REG_T1
#define JIT_X_BOX 9
#define JIT_X_TOMB 8 /* first_tomb for upsert probe */

static int emit_mov_w_imm(jello_jit_emit_buf* buf, uint8_t rd, uint32_t imm) {
  if(imm <= 0xFFFFu) return jello_jit_emit_u32(buf, a64_movz_w(rd, (uint16_t)imm, 0));
  if(jello_jit_emit_u32(buf, a64_movz_w(rd, (uint16_t)(imm & 0xFFFFu), 0)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_movk_w(rd, (uint16_t)((imm >> 16) & 0xFFFFu), 16));
}

static int emit_mov_x64(jello_jit_emit_buf* buf, uint8_t rd, uint64_t val) {
  if(jello_jit_emit_u32(buf, 0xD2800000u | ((uint32_t)rd) | (uint32_t)((val & 0xFFFFu) << 5)) != 0) return -1;
  if(((val >> 16) & 0xFFFFu) &&
     jello_jit_emit_u32(buf, 0xF2A00000u | ((uint32_t)rd) | (uint32_t)(((val >> 16) & 0xFFFFu) << 5)) != 0)
    return -1;
  if(((val >> 32) & 0xFFFFu) &&
     jello_jit_emit_u32(buf, 0xF2C00000u | ((uint32_t)rd) | (uint32_t)(((val >> 32) & 0xFFFFu) << 5)) != 0)
    return -1;
  if(((val >> 48) & 0xFFFFu) &&
     jello_jit_emit_u32(buf, 0xF2E00000u | ((uint32_t)rd) | (uint32_t)(((val >> 48) & 0xFFFFu) << 5)) != 0)
    return -1;
  return 0;
}

static int emit_call_fn(jello_jit_emit_buf* buf, void* fn) {
  if(emit_mov_x64(buf, 16, (uint64_t)(uintptr_t)fn) != 0) return -1;
  return jello_jit_emit_u32(buf, 0xD63F0200u); /* blr x16 */
}

/* After FCMP/FCOMP, ordered float compares use MI/GT/EQ — not integer LT. */
static uint8_t fp_cset_cond(jello_jit_ir_cmp cmp) {
  return (uint8_t)(cmp == JIR_CMP_LT ? 4u : 0u); /* MI=lt, EQ=eq */
}

static int emit_load_frame_base(jello_jit_emit_buf* buf) {
  const jello_jit_layout* lay = jello_jit_runtime_layout();
  if(!lay) return -1;
  if(lay->exec_ctx_fr > 16380u || (lay->exec_ctx_fr % 8u) != 0u) return -1;
  if(lay->call_frame_rf_mem > 16380u || (lay->call_frame_rf_mem % 8u) != 0u) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_REG_T2, JIT_REG_CTX, lay->exec_ctx_fr)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_REG_BASE, JIT_REG_T2, lay->call_frame_rf_mem)) != 0) return -1;
  return 0;
}

static int emit_refresh_base(jello_jit_emit_buf* buf) {
  return emit_load_frame_base(buf);
}

static int emit_mov_x_imm(jello_jit_emit_buf* buf, uint8_t rd, uint64_t imm) {
  if(imm <= 0xFFFFu) return jello_jit_emit_u32(buf, 0xD2800000u | (uint32_t)rd | (uint32_t)(imm << 5));
  return emit_mov_x64(buf, rd, imm);
}

static int emit_ldr_w_slot(jello_jit_emit_buf* buf, uint8_t rt, uint8_t base, uint32_t off) {
  if(off <= 16380u && (off % 4u) == 0u) return jello_jit_emit_u32(buf, a64_ldr_w_uimm(rt, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ldr_w_uimm(rt, JIT_REG_T2, 0));
}

static int emit_str_w_slot(jello_jit_emit_buf* buf, uint8_t rt, uint8_t base, uint32_t off) {
  if(off <= 16380u && (off % 4u) == 0u) return jello_jit_emit_u32(buf, a64_str_w_uimm(rt, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_str_w_uimm(rt, JIT_REG_T2, 0));
}

static int emit_ldr_x_slot(jello_jit_emit_buf* buf, uint8_t rt, uint8_t base, uint32_t off) {
  if(off <= 32760u && (off % 8u) == 0u) return jello_jit_emit_u32(buf, a64_ldr_x_uimm(rt, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ldr_x_uimm(rt, JIT_REG_T2, 0));
}

static int emit_str_x_slot(jello_jit_emit_buf* buf, uint8_t rt, uint8_t base, uint32_t off) {
  if(off <= 32760u && (off % 8u) == 0u) return jello_jit_emit_u32(buf, a64_str_x_uimm(rt, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_str_x_uimm(rt, JIT_REG_T2, 0));
}

static int emit_ldr_d_slot(jello_jit_emit_buf* buf, uint8_t dt, uint8_t base, uint32_t off) {
  if(off <= 32760u && (off % 8u) == 0u) return jello_jit_emit_u32(buf, a64_ldr_d_uimm(dt, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ldr_d_uimm(dt, JIT_REG_T2, 0));
}

static int emit_str_d_slot(jello_jit_emit_buf* buf, uint8_t dt, uint8_t base, uint32_t off) {
  if(off <= 32760u && (off % 8u) == 0u) return jello_jit_emit_u32(buf, a64_str_d_uimm(dt, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_str_d_uimm(dt, JIT_REG_T2, 0));
}

static int emit_ldr_s_slot(jello_jit_emit_buf* buf, uint8_t st, uint8_t base, uint32_t off) {
  if(off <= 16380u && (off % 4u) == 0u) return jello_jit_emit_u32(buf, a64_ldr_s_uimm(st, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ldr_s_uimm(st, JIT_REG_T2, 0));
}

static int emit_str_s_slot(jello_jit_emit_buf* buf, uint8_t st, uint8_t base, uint32_t off) {
  if(off <= 16380u && (off % 4u) == 0u) return jello_jit_emit_u32(buf, a64_str_s_uimm(st, base, off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_str_s_uimm(st, JIT_REG_T2, 0));
}

static uint32_t slot_off(const frame_layout* layout, uint32_t reg) {
  return layout->off[reg];
}

static size_t reg_slot_sz(const jello_bc_module* m, const jello_bc_function* f, uint32_t reg) {
  return jello_slot_size(vm_reg_kind(m, f, reg));
}

static int emit_bin_i64(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return jello_jit_emit_u32(buf, a64_sub_x_rr(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
    case JIR_BIN_MUL:
      return jello_jit_emit_u32(buf, a64_mul_x(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
    case JIR_BIN_SDIV:
      return jello_jit_emit_u32(buf, a64_sdiv_x(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
    case JIR_BIN_MOD:
      if(jello_jit_emit_u32(buf, a64_sdiv_x(JIT_REG_T2, JIT_REG_T0, JIT_REG_T1)) != 0) return -1;
      return jello_jit_emit_u32(buf, a64_msub_x(JIT_REG_T0, JIT_REG_T2, JIT_REG_T1, JIT_REG_T0));
    case JIR_BIN_SHL:
      return jello_jit_emit_u32(buf, a64_lslv_x(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
    case JIR_BIN_SHR:
      return jello_jit_emit_u32(buf, a64_lsrv_x(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
    case JIR_BIN_XOR:
      return jello_jit_emit_u32(buf, a64_eor_x_rr(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
    default:
      return jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1));
  }
}

static int emit_bin_f64(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return jello_jit_emit_u32(buf, a64_fsub_d(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
    case JIR_BIN_MUL:
      return jello_jit_emit_u32(buf, a64_fmul_d(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
    case JIR_BIN_SDIV:
      return jello_jit_emit_u32(buf, a64_fdiv_d(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
    default:
      return jello_jit_emit_u32(buf, a64_fadd_d(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
  }
}

static int emit_bin_f32(jello_jit_emit_buf* buf, jello_jit_ir_bin bin) {
  switch(bin) {
    case JIR_BIN_SUB:
      return jello_jit_emit_u32(buf, a64_fsub_s(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
    case JIR_BIN_MUL:
      return jello_jit_emit_u32(buf, a64_fmul_s(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
    case JIR_BIN_SDIV:
      return jello_jit_emit_u32(buf, a64_fdiv_s(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
    default:
      return jello_jit_emit_u32(buf, a64_fadd_s(JIT_FP_T0, JIT_FP_T0, JIT_FP_T1));
  }
}

typedef struct jello_jit_patch_site {
  size_t at;
  uint32_t bc_tgt;
  uint8_t kind; /* 0=b, 1=cb/cbnz w0 */
} jello_jit_patch_site;

static int patch_branch(jello_jit_emit_buf* buf, size_t at, size_t target) {
  int32_t off = (int32_t)(((int64_t)target - (int64_t)at) / 4);
  uint32_t word = 0;
  memcpy(&word, buf->data + at, 4u);
  word = (word & 0xFC000000u) | ((uint32_t)off & 0x03FFFFFFu);
  memcpy(buf->data + at, &word, 4u);
  return 0;
}

static int patch_cb(jello_jit_emit_buf* buf, size_t at, size_t target) {
  int32_t off = (int32_t)(((int64_t)target - (int64_t)at) / 4);
  uint32_t word = 0;
  memcpy(&word, buf->data + at, 4u);
  word = (word & 0xFF00001Fu) | ((((uint32_t)off & 0x7FFFFu) << 5));
  memcpy(buf->data + at, &word, 4u);
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

static uint32_t switch_kind_target(uint32_t switch_pc, uint32_t ncases, int32_t delta) {
  return switch_pc + 1u + ncases + (uint32_t)delta;
}

static void mark_bc_target(uint8_t* is_target, const jello_jit_ir_func* ir, uint32_t bc_tgt) {
  for(uint32_t k = 0; k < ir->ninsns; k++) {
    if(ir->insns[k].bc_pc != bc_tgt) continue;
    if(ir->insns[k].op == JIR_FUEL_CHECK || ir->insns[k].op == JIR_SWITCH_CASE) continue;
    is_target[k] = 1u;
    break;
  }
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
      if(patch_branch(buf, ps->at, target) != 0) return -1;
    } else {
      if(patch_cb(buf, ps->at, target) != 0) return -1;
    }
  }
  return 0;
}

static int emit_prologue(jello_jit_emit_buf* buf, const jello_jit_layout* lay) {
  if(jello_jit_emit_u32(buf, a64_stp_xpre(29, 30, 31, -2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, 0x910003FDu) != 0) return -1; /* mov x29, sp */
  if(jello_jit_emit_u32(buf, a64_stp_xpre(19, 20, 31, -2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_stp_xpre(21, 22, 31, -2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_stp_xpre(23, 24, 31, -2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, 0xAA0003F3u) != 0) return -1; /* mov x19, x0 (ctx) */
  if(emit_load_frame_base(buf) != 0) return -1;
  if(!lay || lay->exec_ctx_jit_resume_entry > 16380u || (lay->exec_ctx_jit_resume_entry % 8u) != 0u) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(16, JIT_REG_CTX, lay->exec_ctx_jit_resume_entry)) != 0) return -1;
  size_t cb_at = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(16, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, 0xD61F0000u | ((uint32_t)16 << 5)) != 0) return -1; /* br x16 */
  size_t cont = buf->size;
  if(patch_cb(buf, cb_at, cont) != 0) return -1;
  return 0;
}

static int emit_restore_callee_saved(jello_jit_emit_buf* buf) {
  if(jello_jit_emit_u32(buf, a64_ldp_xpost(23, 24, 31, 2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldp_xpost(21, 22, 31, 2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldp_xpost(19, 20, 31, 2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldp_xpost(29, 30, 31, 2)) != 0) return -1;
  return 0;
}

static int emit_epilogue(jello_jit_emit_buf* buf, uint32_t return_code) {
  if(emit_mov_w_imm(buf, 0, return_code) != 0) return -1;
  if(emit_restore_callee_saved(buf) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ret());
}

static int emit_restore_and_ret(jello_jit_emit_buf* buf) {
  if(emit_restore_callee_saved(buf) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ret());
}

/* After x0=ctx and x1.. args set: call fn; CONTINUE (w0==0) → refresh base; else restore+ret. */
static int emit_rt_call_continue(jello_jit_emit_buf* buf, void* fn) {
  if(emit_call_fn(buf, fn) != 0) return -1;
  size_t cb_at = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_w(0, 0)) != 0) return -1;
  if(emit_restore_and_ret(buf) != 0) return -1;
  size_t cont = buf->size;
  if(patch_cb(buf, cb_at, cont) != 0) return -1;
  return emit_refresh_base(buf);
}

/* Like emit_rt_call_continue but keep JIT_REG_BASE (helper did not rebase rf.mem). */
static int emit_rt_call_continue_keep_base(jello_jit_emit_buf* buf, void* fn) {
  if(emit_call_fn(buf, fn) != 0) return -1;
  size_t cb_at = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_w(0, 0)) != 0) return -1;
  if(emit_restore_and_ret(buf) != 0) return -1;
  size_t cont = buf->size;
  return patch_cb(buf, cb_at, cont);
}

static int emit_ldr_x_field(jello_jit_emit_buf* buf, uint8_t rt, uint8_t base, uint32_t byte_off) {
  if(byte_off <= 32760u && (byte_off % 8u) == 0u) return jello_jit_emit_u32(buf, a64_ldr_x_uimm(rt, base, byte_off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, byte_off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_ldr_x_uimm(rt, JIT_REG_T2, 0));
}

static int emit_str_x_field(jello_jit_emit_buf* buf, uint8_t rt, uint8_t base, uint32_t byte_off) {
  if(byte_off <= 32760u && (byte_off % 8u) == 0u) return jello_jit_emit_u32(buf, a64_str_x_uimm(rt, base, byte_off));
  if(emit_mov_x_imm(buf, JIT_REG_T2, byte_off) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_REG_T2, base, JIT_REG_T2)) != 0) return -1;
  return jello_jit_emit_u32(buf, a64_str_x_uimm(rt, JIT_REG_T2, 0));
}

static int emit_inline_fuel_check(jello_jit_emit_buf* buf) {
  const jello_jit_layout* lay = jello_jit_runtime_layout();
  if(!lay) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(21, JIT_REG_CTX, 0)) != 0) return -1; /* vm = ctx->vm */
  if(emit_ldr_x_field(buf, 22, 21, lay->vm_fuel_limit) != 0) return -1;
  size_t skip_cb = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(22, 0)) != 0) return -1;
  if(emit_ldr_x_field(buf, 22, 21, lay->vm_fuel_remaining) != 0) return -1;
  size_t trap_cb = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(22, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_sub_x_imm(22, 22, 1u)) != 0) return -1;
  if(emit_str_x_field(buf, 22, 21, lay->vm_fuel_remaining) != 0) return -1;
  size_t b_done_at = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;
  size_t trap_at = buf->size;
  if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) return -1; /* mov x0, x19 */
  if(emit_call_fn(buf, (void*)jello_jit_runtime_fuel_trap) != 0) return -1;
  if(emit_restore_and_ret(buf) != 0) return -1;
  size_t done_at = buf->size;
  if(patch_cb(buf, skip_cb, done_at) != 0) return -1;
  if(patch_cb(buf, trap_cb, trap_at) != 0) return -1;
  return patch_branch(buf, b_done_at, done_at);
}

static int patch_adr(jello_jit_emit_buf* buf, size_t at, size_t target, uint8_t rd) {
  if(!buf || !buf->data || at + 4 > buf->size) return -1;
  int64_t imm = (int64_t)target - (int64_t)at;
  if(imm < -(1 << 20) || imm >= (1 << 20)) return -1;
  uint32_t insn = a64_adr(rd, (int32_t)imm);
  memcpy(buf->data + at, &insn, 4);
  return 0;
}

static int emit_ldr_i32_cached(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint16_t* i32c_reg,
    uint32_t src_reg,
    uint8_t dst_phys
) {
  if(*i32c_reg == src_reg) {
    /* mov Wd, W24 */
    return jello_jit_emit_u32(buf, 0x2A0003E0u | ((uint32_t)JIT_REG_I32C << 16) | (uint32_t)dst_phys);
  }
  return emit_ldr_w_slot(buf, dst_phys, JIT_REG_BASE, slot_off(layout, src_reg));
}

static int emit_str_i32_cached(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint16_t* i32c_reg,
    uint32_t dst_reg,
    uint8_t src_phys
) {
  if(emit_str_w_slot(buf, src_phys, JIT_REG_BASE, slot_off(layout, dst_reg)) != 0) return -1;
  /* Keep a copy in W24 for the next load in this basic block. */
  if(jello_jit_emit_u32(buf, 0x2A0003E0u | ((uint32_t)src_phys << 16) | (uint32_t)JIT_REG_I32C) != 0)
    return -1;
  *i32c_reg = (uint16_t)dst_reg;
  return 0;
}

static uint32_t jit_obj_hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

static jello_type_kind vm_array_elem_kind(
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t arr_reg
) {
  if(vm_reg_kind(m, f, arr_reg) != JELLO_T_ARRAY) return (jello_type_kind)0;
  jello_type_id arr_tid = f->reg_types[arr_reg];
  if(arr_tid >= m->ntypes) return (jello_type_kind)0;
  const jello_type_entry* te = &m->types[arr_tid];
  if(te->kind != JELLO_T_ARRAY) return (jello_type_kind)0;
  uint32_t elem_tid = te->as.unary.elem;
  if(elem_tid >= m->ntypes) return (jello_type_kind)0;
  return m->types[elem_tid].kind;
}

static int emit_array_get_f64_helper(
    jello_jit_emit_buf* buf,
    uint32_t dst,
    uint32_t arr_reg,
    uint32_t idx_reg
) {
  if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) return -1;
  if(emit_mov_w_imm(buf, 1, dst) != 0) return -1;
  if(emit_mov_w_imm(buf, 2, arr_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 3, idx_reg) != 0) return -1;
  return emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_array_get);
}

static int emit_array_get_f64(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t dst,
    uint32_t arr_reg,
    uint32_t idx_reg
) {
  if(vm_reg_kind(m, f, dst) != JELLO_T_F64 || vm_array_elem_kind(m, f, arr_reg) != JELLO_T_F64)
    return emit_array_get_f64_helper(buf, dst, arr_reg, idx_reg);

  size_t j_slow[6];
  int n_slow = 0;

  if(emit_ldr_x_slot(buf, JIT_X_OBJ, JIT_REG_BASE, slot_off(layout, arr_reg)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_OBJ, 0)) != 0) return -1;

  if(emit_ldr_w_slot(buf, JIT_X_IDX, JIT_REG_BASE, slot_off(layout, idx_reg)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_ARRAY_OFF_LEN)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_X_IDX, JIT_X_TMP)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_hs(0)) != 0) return -1;

  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_ARRAY_OFF_DATA)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_lsl_w_imm(JIT_X_IDX, JIT_X_IDX, 3)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_X_TMP, JIT_X_TMP, JIT_X_IDX)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_BOX, JIT_X_TMP, 0)) != 0) return -1;

  if(emit_mov_w_imm(buf, 0, 7) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_and_w_rr(JIT_X_TMP, JIT_X_BOX, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_NULL)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbnz_w(JIT_X_TMP, 0)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_BOX, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_BOX, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_BOX_F64)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_d_uimm(JIT_FP_T0, JIT_X_BOX, 8)) != 0) return -1;
  if(emit_str_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;

  size_t slow_at = buf->size;
  for(int i = 0; i < n_slow; i++) {
    if(patch_cb(buf, j_slow[i], slow_at) != 0) return -1;
  }
  return emit_array_get_f64_helper(buf, dst, arr_reg, idx_reg);
}

static int emit_array_set_f64_helper(
    jello_jit_emit_buf* buf,
    uint32_t val_reg,
    uint32_t arr_reg,
    uint32_t idx_reg
) {
  if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) return -1;
  if(emit_mov_w_imm(buf, 1, val_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 2, arr_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 3, idx_reg) != 0) return -1;
  return emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_array_set);
}

static int emit_array_set_f64(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t val_reg,
    uint32_t arr_reg,
    uint32_t idx_reg
) {
  if(vm_reg_kind(m, f, val_reg) != JELLO_T_F64 || vm_array_elem_kind(m, f, arr_reg) != JELLO_T_F64)
    return emit_array_set_f64_helper(buf, val_reg, arr_reg, idx_reg);

  size_t j_slow[6];
  int n_slow = 0;

  if(emit_ldr_x_slot(buf, JIT_X_OBJ, JIT_REG_BASE, slot_off(layout, arr_reg)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_OBJ, 0)) != 0) return -1;

  if(emit_ldr_w_slot(buf, JIT_X_IDX, JIT_REG_BASE, slot_off(layout, idx_reg)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_ARRAY_OFF_LEN)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_X_IDX, JIT_X_TMP)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_hs(0)) != 0) return -1;

  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_ARRAY_OFF_DATA)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_lsl_w_imm(JIT_X_IDX, JIT_X_IDX, 3)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_X_TMP, JIT_X_TMP, JIT_X_IDX)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_BOX, JIT_X_TMP, 0)) != 0) return -1;

  if(emit_mov_w_imm(buf, 0, 7) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_and_w_rr(1, JIT_X_BOX, 0)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbnz_w(1, 0)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_BOX, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_BOX, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_BOX_F64)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
  if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_str_d_uimm(JIT_FP_T0, JIT_X_BOX, 8)) != 0) return -1;

  size_t slow_at = buf->size;
  for(int i = 0; i < n_slow; i++) {
    if(patch_cb(buf, j_slow[i], slow_at) != 0) return -1;
  }
  return emit_array_set_f64_helper(buf, val_reg, arr_reg, idx_reg);
}

static int emit_obj_get_atom_helper(
    jello_jit_emit_buf* buf,
    uint32_t dst,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) return -1; /* mov x0, x19 */
  if(emit_mov_w_imm(buf, 1, dst) != 0) return -1;
  if(emit_mov_w_imm(buf, 2, obj_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 3, atom_id) != 0) return -1;
  return emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_obj_get_atom);
}

static int emit_obj_set_atom_helper(
    jello_jit_emit_buf* buf,
    uint32_t val_reg,
    uint32_t obj_reg,
    uint32_t atom_id
) {
  if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) return -1;
  if(emit_mov_w_imm(buf, 1, val_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 2, obj_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 3, atom_id) != 0) return -1;
  return emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_obj_set_atom);
}

static int emit_obj_new(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint32_t dst
) {
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(0, JIT_REG_CTX, (uint32_t)offsetof(exec_ctx, vm))) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_REG_T0, JIT_REG_CTX, (uint32_t)offsetof(exec_ctx, f))) != 0)
    return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_REG_T0, JIT_REG_T0, (uint32_t)offsetof(jello_bc_function, reg_types))) != 0)
    return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(1, JIT_REG_T0, dst * 4u)) != 0) return -1;
  if(emit_call_fn(buf, (void*)jello_jit_object_new) != 0) return -1;
  return emit_str_x_slot(buf, 0, JIT_REG_BASE, slot_off(layout, dst));
}

static int emit_obj_probe_setup(
    jello_jit_emit_buf* buf,
    const frame_layout* layout,
    uint32_t obj_reg,
    uint32_t atom_id,
    uint32_t h0,
    size_t* j_slow,
    int* n_slow,
    int allow_empty_len_slow
) {
  if(emit_ldr_x_slot(buf, JIT_X_OBJ, JIT_REG_BASE, slot_off(layout, obj_reg)) != 0) return -1;
  j_slow[(*n_slow)++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_OBJ, 0)) != 0) return -1;

  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_OBJ_OFF_LEN)) != 0) return -1;
  if(allow_empty_len_slow) {
    j_slow[(*n_slow)++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbz_w(JIT_X_TMP, 0)) != 0) return -1;
  }

  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_KEYS, JIT_X_OBJ, JIT_OBJ_OFF_KEYS)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_VALS, JIT_X_OBJ, JIT_OBJ_OFF_VALS)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_STATES, JIT_X_OBJ, JIT_OBJ_OFF_STATES)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_MASK, JIT_X_OBJ, JIT_OBJ_OFF_CAP)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_sub_w_imm(JIT_X_MASK, JIT_X_MASK, 1)) != 0) return -1;
  if(emit_mov_w_imm(buf, JIT_X_IDX, h0) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_and_w_rr(JIT_X_IDX, JIT_X_IDX, JIT_X_MASK)) != 0) return -1;
  (void)atom_id;
  return 0;
}

/* Probe open-addressing table. Empty → *j_empty; occupied key match → *j_hit.
 * If track_tomb: JIT_X_TOMB holds first tomb (init -1 before call); on empty,
 * replaces IDX with first_tomb when set. */
static int emit_obj_probe_loop(
    jello_jit_emit_buf* buf,
    uint32_t atom_id,
    size_t* j_empty,
    size_t* j_hit,
    int track_tomb
) {
  size_t loop_at = buf->size;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_X_TMP, JIT_X_STATES, JIT_X_IDX)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldrb_w_uimm(0, JIT_X_TMP, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_SLOT_EMPTY)) != 0) return -1;
  *j_empty = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_SLOT_OCCUPIED)) != 0) return -1;
  size_t j_tomb_or_next = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;

  if(jello_jit_emit_u32(buf, a64_lsl_w_imm(JIT_X_TMP, JIT_X_IDX, 2)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_X_TMP, JIT_X_KEYS, JIT_X_TMP)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_TMP, 0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)atom_id)) != 0) return -1;
  *j_hit = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
  size_t j_to_next = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;

  size_t tomb_at = buf->size;
  if(patch_cb(buf, j_tomb_or_next, tomb_at) != 0) return -1;
  if(track_tomb) {
    if(emit_mov_w_imm(buf, 0, 0xFFFFFFFFu) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_X_TOMB, 0)) != 0) return -1;
    size_t j_skip = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_mov_w_rr(JIT_X_TOMB, JIT_X_IDX)) != 0) return -1;
    size_t skip_at = buf->size;
    if(patch_cb(buf, j_skip, skip_at) != 0) return -1;
  }

  size_t next_at = buf->size;
  if(patch_branch(buf, j_to_next, next_at) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_w_imm(JIT_X_IDX, JIT_X_IDX, 1)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_and_w_rr(JIT_X_IDX, JIT_X_IDX, JIT_X_MASK)) != 0) return -1;
  size_t j_loop = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;
  if(patch_branch(buf, j_loop, loop_at) != 0) return -1;
  return 0;
}

static int emit_obj_insert_pick_slot(jello_jit_emit_buf* buf) {
  /* IDX = TOMB != -1 ? TOMB : IDX */
  if(emit_mov_w_imm(buf, 0, 0xFFFFFFFFu) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_X_TOMB, 0)) != 0) return -1;
  size_t j_keep = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_mov_w_rr(JIT_X_IDX, JIT_X_TOMB)) != 0) return -1;
  size_t keep_at = buf->size;
  return patch_cb(buf, j_keep, keep_at);
}

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
  if(!inline_ok || atom_id > 0xFFFu) return emit_obj_get_atom_helper(buf, dst, obj_reg, atom_id);

  int need_proto = m->proto_enabled && atom_id != JELLO_ATOM___PROTO__;
  uint32_t h0 = jit_obj_hash_u32(atom_id);
  size_t j_slow[10];
  int n_slow = 0;
  size_t j_miss[6];
  int n_miss = 0;
  size_t j_done[4];
  int n_done = 0;

  if(emit_ldr_x_slot(buf, JIT_X_OBJ, JIT_REG_BASE, slot_off(layout, obj_reg)) != 0) return -1;
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_OBJ, 0)) != 0) return -1;

  if(need_proto) {
    if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_OBJ_OFF_PROTO)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbnz_x(JIT_X_TMP, 0)) != 0) return -1;
  }

  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_OBJ_OFF_LEN)) != 0) return -1;
  j_miss[n_miss++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_w(JIT_X_TMP, 0)) != 0) return -1;

  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_KEYS, JIT_X_OBJ, JIT_OBJ_OFF_KEYS)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_VALS, JIT_X_OBJ, JIT_OBJ_OFF_VALS)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_STATES, JIT_X_OBJ, JIT_OBJ_OFF_STATES)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_MASK, JIT_X_OBJ, JIT_OBJ_OFF_CAP)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_sub_w_imm(JIT_X_MASK, JIT_X_MASK, 1)) != 0) return -1;
  if(emit_mov_w_imm(buf, JIT_X_IDX, h0) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_and_w_rr(JIT_X_IDX, JIT_X_IDX, JIT_X_MASK)) != 0) return -1;

  size_t j_empty = 0, j_hit = 0;
  if(emit_obj_probe_loop(buf, atom_id, &j_empty, &j_hit, 0) != 0) return -1;
  j_miss[n_miss++] = j_empty;

  size_t hit_at = buf->size;
  if(patch_cb(buf, j_hit, hit_at) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_lsl_w_imm(JIT_X_TMP, JIT_X_IDX, 3)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_X_TMP, JIT_X_VALS, JIT_X_TMP)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_BOX, JIT_X_TMP, 0)) != 0) return -1;

  if(emit_mov_w_imm(buf, 0, 7) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_and_w_rr(JIT_X_TMP, JIT_X_BOX, 0)) != 0) return -1; /* tag */

  if(k == JELLO_T_F64) {
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_NULL)) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbnz_w(JIT_X_TMP, 0)) != 0) return -1; /* not ptr */
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_BOX_F64)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_ldr_d_uimm(JIT_FP_T0, JIT_X_BOX, 8)) != 0) return -1;
    if(emit_str_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  } else if(k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32) {
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_NULL)) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_I32)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_lsr_x_imm(JIT_REG_T0, JIT_X_BOX, 3)) != 0) return -1;
    if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  } else if(k == JELLO_T_I64) {
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_NULL)) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_I32)) != 0) return -1;
    size_t j_i32 = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbnz_w(JIT_X_TMP, 0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_BOX_I64)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_REG_T0, JIT_X_BOX, 8)) != 0) return -1;
    if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
    j_done[n_done++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;
    size_t i32_at = buf->size;
    if(patch_cb(buf, j_i32, i32_at) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_lsr_x_imm(JIT_REG_T0, JIT_X_BOX, 3)) != 0) return -1;
    /* sxtw x21, w21 */
    if(jello_jit_emit_u32(buf, 0x93407C00u | ((uint32_t)JIT_REG_T0 << 5) | (uint32_t)JIT_REG_T0) != 0)
      return -1;
    if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  } else {
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_X_TMP, (uint16_t)JELLO_TAG_NULL)) != 0) return -1;
    j_miss[n_miss++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbnz_w(JIT_X_TMP, 0)) != 0) return -1;
    if(emit_str_x_slot(buf, JIT_X_BOX, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  }

  j_done[n_done++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;

  size_t miss_at = buf->size;
  for(int i = 0; i < n_miss; i++) {
    if(patch_cb(buf, j_miss[i], miss_at) != 0) return -1;
  }
  if(jello_jit_emit_u32(buf, a64_mov_x_rr(JIT_REG_T0, 31)) != 0) return -1; /* xzr */
  if(k == JELLO_T_I8 || k == JELLO_T_I16 || k == JELLO_T_I32) {
    if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  } else {
    if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, dst)) != 0) return -1;
  }
  j_done[n_done++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;

  size_t slow_at = buf->size;
  for(int i = 0; i < n_slow; i++) {
    if(patch_cb(buf, j_slow[i], slow_at) != 0) return -1;
  }
  if(emit_obj_get_atom_helper(buf, dst, obj_reg, atom_id) != 0) return -1;

  size_t done_at = buf->size;
  for(int i = 0; i < n_done; i++) {
    if(patch_branch(buf, j_done[i], done_at) != 0) return -1;
  }
  return 0;
}

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
  int inline_ok = (k == JELLO_T_F64 || k == JELLO_T_I64 || k == JELLO_T_I32 || k == JELLO_T_I8 ||
                   k == JELLO_T_I16);
  if(!inline_ok || atom_id == JELLO_ATOM___PROTO__ || atom_id > 0xFFFu)
    return emit_obj_set_atom_helper(buf, val_reg, obj_reg, atom_id);

  uint32_t h0 = jit_obj_hash_u32(atom_id);
  size_t j_slow[10];
  int n_slow = 0;
  size_t j_done[4];
  int n_done = 0;
  size_t j_insert[4];
  int n_insert = 0;

  /* allow_empty_len_slow=0: len==0 falls through; we branch to insert below. */
  if(emit_obj_probe_setup(buf, layout, obj_reg, atom_id, h0, j_slow, &n_slow, 0) != 0) return -1;

  if(emit_mov_w_imm(buf, JIT_X_TOMB, 0xFFFFFFFFu) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(JIT_X_TMP, JIT_X_OBJ, JIT_OBJ_OFF_LEN)) != 0) return -1;
  j_insert[n_insert++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_cbz_w(JIT_X_TMP, 0)) != 0) return -1;

  size_t j_empty = 0, j_hit = 0;
  if(emit_obj_probe_loop(buf, atom_id, &j_empty, &j_hit, 1) != 0) return -1;
  j_insert[n_insert++] = j_empty;

  size_t hit_at = buf->size;
  if(patch_cb(buf, j_hit, hit_at) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_lsl_w_imm(JIT_X_TMP, JIT_X_IDX, 3)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_x_rr(JIT_X_TMP, JIT_X_VALS, JIT_X_TMP)) != 0) return -1;

  if(k == JELLO_T_F64) {
    if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_BOX, JIT_X_TMP, 0)) != 0) return -1;
    if(emit_mov_w_imm(buf, 0, 7) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_and_w_rr(1, JIT_X_BOX, 0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbnz_w(1, 0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_BOX_F64)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
    if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_str_d_uimm(JIT_FP_T0, JIT_X_BOX, 8)) != 0) return -1;
  } else if(k == JELLO_T_I64) {
    if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(JIT_X_BOX, JIT_X_TMP, 0)) != 0) return -1;
    if(emit_mov_w_imm(buf, 0, 7) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_and_w_rr(1, JIT_X_BOX, 0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbnz_w(1, 0)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_cbz_x(JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_BOX, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_cmp_w_imm(0, (uint16_t)JELLO_OBJ_BOX_I64)) != 0) return -1;
    j_slow[n_slow++] = buf->size;
    if(jello_jit_emit_u32(buf, a64_b_ne(0)) != 0) return -1;
    if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_str_x_uimm(JIT_REG_T0, JIT_X_BOX, 8)) != 0) return -1;
  } else {
    if(emit_ldr_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, val_reg)) != 0) return -1;
    /* jello_make_i32: LSL/ORR must be 64-bit or high payload bits of negative I32 are lost. */
    if(jello_jit_emit_u32(buf, a64_lsl_x_imm(JIT_REG_T0, JIT_REG_T0, 3)) != 0) return -1;
    if(emit_mov_w_imm(buf, 0, (uint32_t)JELLO_TAG_I32) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_orr_x_rr(JIT_REG_T0, JIT_REG_T0, 0)) != 0) return -1;
    if(jello_jit_emit_u32(buf, a64_str_x_uimm(JIT_REG_T0, JIT_X_TMP, 0)) != 0) return -1;
  }

  j_done[n_done++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;

  /* insert at EMPTY (or len==0 hash slot): load-factor check then thin helper. */
  size_t insert_at = buf->size;
  for(int i = 0; i < n_insert; i++) {
    if(patch_cb(buf, j_insert[i], insert_at) != 0) return -1;
  }
  if(emit_obj_insert_pick_slot(buf) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(0, JIT_X_OBJ, JIT_OBJ_OFF_LEN)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_add_w_imm(0, 0, 1)) != 0) return -1;
  if(emit_mov_w_imm(buf, 1, (uint32_t)JELLO_OBJECT_LOAD_NUM) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_mul_w(0, 0, 1)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_ldr_w_uimm(1, JIT_X_OBJ, JIT_OBJ_OFF_CAP)) != 0) return -1;
  if(emit_mov_w_imm(buf, JIT_X_TMP, (uint32_t)JELLO_OBJECT_LOAD_DEN) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_mul_w(1, 1, JIT_X_TMP)) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, 0, 1)) != 0) return -1; /* cmp */
  j_slow[n_slow++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b_hs(0)) != 0) return -1;

  if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) return -1; /* x0 = ctx */
  if(emit_mov_w_imm(buf, 1, val_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 2, obj_reg) != 0) return -1;
  if(emit_mov_w_imm(buf, 3, atom_id) != 0) return -1;
  if(jello_jit_emit_u32(buf, a64_mov_w_rr(4, JIT_X_IDX)) != 0) return -1;
  if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_obj_insert_atom) != 0) return -1;

  j_done[n_done++] = buf->size;
  if(jello_jit_emit_u32(buf, a64_b(0)) != 0) return -1;

  size_t slow_at = buf->size;
  for(int i = 0; i < n_slow; i++) {
    if(patch_cb(buf, j_slow[i], slow_at) != 0) return -1;
  }
  if(emit_obj_set_atom_helper(buf, val_reg, obj_reg, atom_id) != 0) return -1;

  size_t done_at = buf->size;
  for(int i = 0; i < n_done; i++) {
    if(patch_branch(buf, j_done[i], done_at) != 0) return -1;
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
  (void)m;
  (void)f;
  (void)layout;
  const jello_jit_layout* lay = jello_jit_runtime_layout();
  /* Jump imm is a bytecode PC; mark first IR insn at that PC as a BB entry. */
  uint8_t* is_target = (uint8_t*)calloc((size_t)ir->ninsns, 1);
  if(!is_target && ir->ninsns) return -1;
  for(uint32_t j = 0; j < ir->ninsns; j++) {
    const jello_jit_ir_insn* t = &ir->insns[j];
    if(t->op == JIR_JMP || t->op == JIR_JMP_IF) {
      mark_bc_target(is_target, ir, (uint32_t)t->imm);
    } else if(t->op == JIR_SWITCH) {
      uint32_t spc = t->bc_pc;
      uint32_t nc = (uint32_t)t->b;
      mark_bc_target(is_target, ir, switch_kind_target(spc, nc, t->imm));
      for(uint32_t c = 0; c < nc; c++) {
        const jello_jit_ir_insn* cs = &ir->insns[j + 1u + c];
        if(cs->op != JIR_SWITCH_CASE) continue;
        mark_bc_target(is_target, ir, switch_kind_target(spc, nc, cs->imm));
      }
    }
  }
  uint16_t i32c_reg = JIT_I32C_NONE;
  for(uint32_t i = 0; i < ir->ninsns; i++) {
    const jello_jit_ir_insn* in = &ir->insns[i];
    if(in->op == JIR_SWITCH_CASE) continue;
    ir_off[i] = buf->size;
    if(is_target[i]) i32c_reg = JIT_I32C_NONE;

    switch(in->op) {
      case JIR_NOP:
        break;
      case JIR_LOAD_I32:
        if(emit_mov_w_imm(buf, JIT_REG_T0, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_str_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        break;
      case JIR_LOAD_I64: {
        uint32_t idx = (uint32_t)in->imm;
        if(idx >= m->nconst_i64) goto fail_ir;
        if(emit_mov_x64(buf, JIT_REG_T0, (uint64_t)m->const_i64[idx]) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_LOAD_F64: {
        uint32_t idx = (uint32_t)in->imm;
        if(idx >= m->nconst_f64) goto fail_ir;
        uint64_t bits = 0;
        memcpy(&bits, &m->const_f64[idx], sizeof(bits));
        if(emit_mov_x64(buf, JIT_REG_T0, bits) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_LOAD_F32:
        if(emit_mov_w_imm(buf, JIT_REG_T0, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      case JIR_MOV_REG: {
        size_t sz = reg_slot_sz(m, f, in->a);
        if(sz == 8u) {
          if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
          if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
          i32c_reg = JIT_I32C_NONE;
        } else {
          if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->b, JIT_REG_T0) != 0) goto fail_ir;
          if(emit_str_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        }
        break;
      }
      case JIR_BIN_I32: {
        jello_op bc_op = (jello_op)f->insns[in->bc_pc].op;
        int imm_rhs =
            (bc_op == JOP_ADD_I32_IMM || bc_op == JOP_SUB_I32_IMM || bc_op == JOP_MUL_I32_IMM);
        int32_t imm8 = (int32_t)(int8_t)(uint8_t)in->c;
        jello_jit_ir_bin bop = (jello_jit_ir_bin)in->imm;
        if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->b, JIT_REG_T0) != 0) goto fail_ir;
        if(imm_rhs && (bop == JIR_BIN_ADD || bop == JIR_BIN_SUB) && imm8 >= 0 && imm8 <= 4095) {
          uint32_t insn = (bop == JIR_BIN_SUB) ? a64_sub_w_imm(JIT_REG_T0, JIT_REG_T0, (uint32_t)imm8)
                                               : a64_add_w_imm(JIT_REG_T0, JIT_REG_T0, (uint32_t)imm8);
          if(jello_jit_emit_u32(buf, insn) != 0) goto fail_ir;
        } else {
          if(imm_rhs) {
            if(emit_mov_w_imm(buf, JIT_REG_T1, (uint32_t)imm8) != 0) goto fail_ir;
          } else {
            if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->c, JIT_REG_T1) != 0) goto fail_ir;
          }
          switch(bop) {
            case JIR_BIN_SUB:
              if(jello_jit_emit_u32(buf, a64_sub_w_rr(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
            case JIR_BIN_MUL:
              if(jello_jit_emit_u32(buf, a64_mul_w(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
            case JIR_BIN_SDIV:
              if(jello_jit_emit_u32(buf, a64_sdiv_w(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
            case JIR_BIN_MOD:
              if(jello_jit_emit_u32(buf, a64_sdiv_w(JIT_REG_T2, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              if(jello_jit_emit_u32(buf, a64_msub_w(JIT_REG_T0, JIT_REG_T2, JIT_REG_T1, JIT_REG_T0)) !=
                 0)
                goto fail_ir;
              break;
            case JIR_BIN_SHL:
              if(jello_jit_emit_u32(buf, a64_lslv_w(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
            case JIR_BIN_SHR:
              if(jello_jit_emit_u32(buf, a64_lsrv_w(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
            case JIR_BIN_XOR:
              if(jello_jit_emit_u32(buf, a64_eor_w_rr(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
            default:
              if(jello_jit_emit_u32(buf, a64_add_w_rr(JIT_REG_T0, JIT_REG_T0, JIT_REG_T1)) != 0)
                goto fail_ir;
              break;
          }
        }
        if(emit_str_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        break;
      }
      case JIR_BIN_I64: {
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_x_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(emit_bin_i64(buf, (jello_jit_ir_bin)in->imm) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
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
        if(jello_jit_emit_u32(buf, a64_fsqrt_d(JIT_FP_T0, JIT_FP_T0)) != 0) goto fail_ir;
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
        if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->b, JIT_REG_T0) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_neg_w(JIT_REG_T0, JIT_REG_T0)) != 0) goto fail_ir;
        if(emit_str_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        break;
      case JIR_NEG_I64:
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_neg_x(JIT_REG_T0, JIT_REG_T0)) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      case JIR_NEG_F64:
        if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_fneg_d(JIT_FP_T0, JIT_FP_T0)) != 0) goto fail_ir;
        if(emit_str_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      case JIR_NEG_F32:
        if(emit_ldr_s_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_fneg_s(JIT_FP_T0, JIT_FP_T0)) != 0) goto fail_ir;
        if(emit_str_s_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      case JIR_LOAD_NULL: {
        /* Tagged null (jello_make_null), not a C NULL pointer. */
        if(emit_mov_x64(buf, JIT_REG_T0, (uint64_t)jello_make_null()) != 0) goto fail_ir;
        if(emit_str_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_CMP_I32: {
        jello_op bc_op = (jello_op)f->insns[in->bc_pc].op;
        /* Imm ops store signed int8 in c (so -1 is 255). NOT_BOOL uses c=255 as
         * "compare to 0" only when the bytecode op is not *_I32_IMM. */
        uint32_t rhs_is_imm = (bc_op == JOP_EQ_I32_IMM || bc_op == JOP_LT_I32_IMM) ? 1u : 0u;
        if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->b, JIT_REG_T0) != 0) goto fail_ir;
        if(rhs_is_imm) {
          int32_t imm8 = (int32_t)(int8_t)(uint8_t)in->c;
          if(imm8 >= 0 && imm8 <= 4095) {
            if(jello_jit_emit_u32(buf, a64_cmp_w_imm(JIT_REG_T0, (uint16_t)imm8)) != 0) goto fail_ir;
          } else {
            if(emit_mov_w_imm(buf, JIT_REG_T1, (uint32_t)imm8) != 0) goto fail_ir;
            if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_REG_T0, JIT_REG_T1)) != 0) goto fail_ir;
          }
        } else if(in->c == 255u) {
          if(jello_jit_emit_u32(buf, a64_cmp_w_imm0(JIT_REG_T0)) != 0) goto fail_ir;
        } else {
          if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->c, JIT_REG_T1) != 0) goto fail_ir;
          if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_REG_T0, JIT_REG_T1)) != 0) goto fail_ir;
        }
        uint8_t cond = (uint8_t)((jello_jit_ir_cmp)in->imm == JIR_CMP_LT ? 11u : 0u);
        if(jello_jit_emit_u32(buf, a64_cset_w(JIT_REG_T0, cond)) != 0) goto fail_ir;
        if(emit_str_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        break;
      }
      case JIR_CMP_I64: {
        if(emit_ldr_x_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_x_slot(buf, JIT_REG_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_subs_x_rr(31, JIT_REG_T0, JIT_REG_T1)) != 0) goto fail_ir;
        uint8_t cond = (uint8_t)((jello_jit_ir_cmp)in->imm == JIR_CMP_LT ? 11u : 0u);
        if(jello_jit_emit_u32(buf, a64_cset_w(JIT_REG_T0, cond)) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_CMP_F64: {
        if(emit_ldr_d_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_d_slot(buf, JIT_FP_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_fcmp_d(JIT_FP_T0, JIT_FP_T1)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_cset_w(JIT_REG_T0, fp_cset_cond((jello_jit_ir_cmp)in->imm))) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_CMP_F32: {
        if(emit_ldr_s_slot(buf, JIT_FP_T0, JIT_REG_BASE, slot_off(layout, in->b)) != 0) goto fail_ir;
        if(emit_ldr_s_slot(buf, JIT_FP_T1, JIT_REG_BASE, slot_off(layout, in->c)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_fcmp_s(JIT_FP_T0, JIT_FP_T1)) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_cset_w(JIT_REG_T0, fp_cset_cond((jello_jit_ir_cmp)in->imm))) != 0) goto fail_ir;
        if(emit_str_w_slot(buf, JIT_REG_T0, JIT_REG_BASE, slot_off(layout, in->a)) != 0) goto fail_ir;
        break;
      }
      case JIR_JMP: {
        uint32_t bc_tgt = (uint32_t)in->imm;
        if(bc_tgt >= f->ninsns) goto fail_ir;
        if(*npatch_sites >= patch_cap) goto fail_ir;
        size_t at = buf->size;
        if(jello_jit_emit_u32(buf, a64_b(0)) != 0) goto fail_ir;
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
        if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        size_t cb_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_cbnz_w(JIT_REG_T0, 0)) != 0) goto fail_ir;
        patch_sites[*npatch_sites].at = cb_at;
        patch_sites[*npatch_sites].bc_tgt = bc_tgt;
        patch_sites[*npatch_sites].kind = 1u;
        (*npatch_sites)++;
        break;
      }
      case JIR_SWITCH: {
        uint32_t switch_pc = in->bc_pc;
        uint32_t ncases = (uint32_t)in->b;
        if(i + ncases >= ir->ninsns) goto fail_ir;
        if(emit_ldr_i32_cached(buf, layout, &i32c_reg, in->a, JIT_REG_T0) != 0) goto fail_ir;
        for(uint32_t c = 0; c < ncases; c++) {
          const jello_jit_ir_insn* cs = &ir->insns[i + 1u + c];
          if(cs->op != JIR_SWITCH_CASE) goto fail_ir;
          if(emit_mov_w_imm(buf, JIT_REG_T1, (uint32_t)cs->a) != 0) goto fail_ir;
          if(jello_jit_emit_u32(buf, a64_subs_w_rr(31, JIT_REG_T0, JIT_REG_T1)) != 0) goto fail_ir;
          if(*npatch_sites >= patch_cap) goto fail_ir;
          size_t at = buf->size;
          if(jello_jit_emit_u32(buf, a64_b_eq(0)) != 0) goto fail_ir;
          patch_sites[*npatch_sites].at = at;
          patch_sites[*npatch_sites].bc_tgt = switch_kind_target(switch_pc, ncases, cs->imm);
          patch_sites[*npatch_sites].kind = 1u; /* b.eq — patch_cb, not patch_branch */
          (*npatch_sites)++;
        }
        if(*npatch_sites >= patch_cap) goto fail_ir;
        size_t def_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_b(0)) != 0) goto fail_ir;
        patch_sites[*npatch_sites].at = def_at;
        patch_sites[*npatch_sites].bc_tgt = switch_kind_target(switch_pc, ncases, in->imm);
        patch_sites[*npatch_sites].kind = 0u;
        (*npatch_sites)++;
        i += ncases;
        break;
      }
      case JIR_FUEL_CHECK:
        if(emit_inline_fuel_check(buf) != 0) goto fail_ir;
        i32c_reg = JIT_I32C_NONE;
        break;
      case JIR_BYTES_LEN: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir; /* x0 = ctx */
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_bytes_len) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_EQ: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir; /* x0 = ctx */
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_bytes_eq) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_GET_U8: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_bytes_get_u8) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_SET_U8: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_bytes_set_u8) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_LEN: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_array_len) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_GET: {
        if(emit_array_get_f64(buf, layout, m, f, in->a, in->b, in->c) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_SET: {
        if(emit_array_set_f64(buf, layout, m, f, in->a, in->b, in->c) != 0) goto fail_ir;
        break;
      }
      case JIR_ARRAY_NEW: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_array_new) != 0) goto fail_ir;
        break;
      }
      case JIR_BYTES_NEW: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_bytes_new) != 0) goto fail_ir;
        break;
      }
      case JIR_ASSERT: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->c) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_assert) != 0) goto fail_ir;
        break;
      }
      case JIR_CONST_BYTES: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_const_bytes) != 0) goto fail_ir;
        break;
      }
      case JIR_CONST_FUN: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_const_fun) != 0) goto fail_ir;
        break;
      }
      case JIR_CLOSURE: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->c) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 4, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue(buf, (void*)jello_jit_runtime_closure) != 0) goto fail_ir;
        break;
      }
      case JIR_CONV: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue_keep_base(buf, (void*)jello_jit_runtime_conv) != 0) goto fail_ir;
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
        if(emit_obj_new(buf, layout, in->a) != 0) goto fail_ir;
        break;
      }
      case JIR_SLOW: {
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir; /* mov x0, x19 (ctx) */
        if(emit_mov_w_imm(buf, 1, (uint32_t)in->imm) != 0) goto fail_ir;
        if(emit_rt_call_continue(buf, (void*)jello_jit_runtime_slow_op) != 0) goto fail_ir;
        break;
      }
      case JIR_CALL_SELF: {
        /* call_self(ctx, first, nargs, dst, resume_pc, return_addr); then b body. */
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir; /* x0 = ctx */
        if(emit_mov_w_imm(buf, 1, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->c) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 4, in->bc_pc + 1u) != 0) goto fail_ir;
        size_t adr_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_adr(5, 0)) != 0) goto fail_ir; /* x5 = resume (patched) */
        if(emit_rt_call_continue(buf, (void*)jello_jit_runtime_call_self) != 0) goto fail_ir;
        size_t b_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_b(0)) != 0) goto fail_ir;
        size_t resume = buf->size;
        if(patch_adr(buf, adr_at, resume, 5) != 0) goto fail_ir;
        if(patch_branch(buf, b_at, body_entry) != 0) goto fail_ir;
        break;
      }
      case JIR_CALL_DIRECT: {
        /* call_direct(...); CONTINUE → br callee body; resume = next IR. */
        uint32_t bidx = (uint32_t)in->imm & 0xFFFFu;
        uint32_t callee_reg = (uint32_t)in->imm >> 16;
        if(callee_reg == 0xFFFFu) callee_reg = 0xFFFFFFFFu;
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->b) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 2, in->c) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 3, in->a) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 4, in->bc_pc + 1u) != 0) goto fail_ir;
        size_t adr_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_adr(5, 0)) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 6, bidx) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 7, callee_reg) != 0) goto fail_ir;
        if(emit_call_fn(buf, (void*)jello_jit_runtime_call_direct) != 0) goto fail_ir;
        size_t cb_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_cbz_w(0, 0)) != 0) goto fail_ir;
        if(emit_restore_and_ret(buf) != 0) goto fail_ir; /* YIELD/TRAP */
        size_t cont = buf->size;
        if(patch_cb(buf, cb_at, cont) != 0) goto fail_ir;
        if(!lay || lay->exec_ctx_jit_call_entry > 16380u ||
           (lay->exec_ctx_jit_call_entry % 8u) != 0u)
          goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(16, JIT_REG_CTX, lay->exec_ctx_jit_call_entry)) !=
           0)
          goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_str_x_uimm(31, JIT_REG_CTX, lay->exec_ctx_jit_call_entry)) !=
           0)
          goto fail_ir;
        if(emit_refresh_base(buf) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, 0xD61F0000u | ((uint32_t)16 << 5)) != 0) goto fail_ir; /* br x16 */
        size_t resume = buf->size;
        if(patch_adr(buf, adr_at, resume, 5) != 0) goto fail_ir;
        break;
      }
      case JIR_RET: {
        /* Always ret_self so cross-function native calls can br to resume. */
        if(jello_jit_emit_u32(buf, 0xAA1303E0u) != 0) goto fail_ir;
        if(emit_mov_w_imm(buf, 1, in->a) != 0) goto fail_ir;
        if(emit_call_fn(buf, (void*)jello_jit_runtime_ret_self) != 0) goto fail_ir;
        size_t cb_at = buf->size;
        if(jello_jit_emit_u32(buf, a64_cbz_w(0, 0)) != 0) goto fail_ir;
        if(emit_restore_and_ret(buf) != 0) goto fail_ir;
        size_t cont = buf->size;
        if(patch_cb(buf, cb_at, cont) != 0) goto fail_ir;
        if(!lay || lay->exec_ctx_jit_self_resume > 16380u ||
           (lay->exec_ctx_jit_self_resume % 8u) != 0u)
          goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_ldr_x_uimm(16, JIT_REG_CTX, lay->exec_ctx_jit_self_resume)) !=
           0)
          goto fail_ir;
        if(jello_jit_emit_u32(buf, a64_str_x_uimm(31, JIT_REG_CTX, lay->exec_ctx_jit_self_resume)) !=
           0)
          goto fail_ir;
        if(emit_refresh_base(buf) != 0) goto fail_ir;
        if(jello_jit_emit_u32(buf, 0xD61F0000u | ((uint32_t)16 << 5)) != 0) goto fail_ir;
        *saw_ret = 1;
        break;
      }
      default:
        goto fail_ir;
    }
    /* Runtime helpers / calls may mutate slots or rebase rf.mem. */
    if(in->op == JIR_SLOW || in->op == JIR_CALL_SELF || in->op == JIR_CALL_DIRECT ||
       in->op == JIR_RET || in->op == JIR_BYTES_LEN || in->op == JIR_BYTES_EQ || in->op == JIR_BYTES_GET_U8 ||
       in->op == JIR_BYTES_SET_U8 || in->op == JIR_BYTES_NEW || in->op == JIR_ARRAY_LEN ||
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

static int arm64_emit_func(
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

const jello_jit_backend jello_jit_backend_arm64 = {
  .name = "arm64",
  .emit_func = arm64_emit_func,
};
