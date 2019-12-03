// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

// x86-64 JIT code emission helpers (variable-length instructions).
#ifndef JELLO_INTERNAL_JIT_X64_H
#define JELLO_INTERNAL_JIT_X64_H

#include <stdint.h>
#include <string.h>

#include <jello/internal/jit_impl.h>

/* SysV register ids (0-15). */
enum {
  X64_RAX = 0,
  X64_RCX = 1,
  X64_RDX = 2,
  X64_RBX = 3,
  X64_RSP = 4,
  X64_RBP = 5,
  X64_RSI = 6,
  X64_RDI = 7,
  X64_R8 = 8,
  X64_R9 = 9,
  X64_R10 = 10,
  X64_R11 = 11,
  X64_R12 = 12,
  X64_R13 = 13,
  X64_R14 = 14,
  X64_R15 = 15,
};

static inline uint8_t x64_rex(int w, int r, int x, int b) {
  return (uint8_t)(0x40u | (((uint32_t)w & 1u) << 3) | (((uint32_t)r & 1u) << 2) |
                   (((uint32_t)x & 1u) << 1) | ((uint32_t)b & 1u));
}

static inline uint8_t x64_modrm(int mod, int reg, int rm) {
  uint32_t v = (((uint32_t)mod & 3u) << 6) | (((uint32_t)reg & 7u) << 3) | ((uint32_t)rm & 7u);
  return (uint8_t)v;
}

static inline void x64_reg_parts(int r, int* out_lo, int* out_rex_ext) {
  *out_lo = r & 7;
  *out_rex_ext = (r >> 3) & 1;
}

/* [base + disp32] uses a SIB byte when base's low 3 bits are 4 (r12 / rsp). */
static inline size_t x64_append_mem_disp32(
    uint8_t* bytes,
    size_t n,
    int mod,
    int reg,
    int base_lo,
    uint32_t disp
) {
  int needs_sib = (base_lo == 4);
  bytes[n++] = x64_modrm(mod, reg, needs_sib ? 4 : base_lo);
  if(needs_sib) bytes[n++] = 0x24u;
  bytes[n++] = (uint8_t)(disp & 0xFFu);
  bytes[n++] = (uint8_t)((disp >> 8) & 0xFFu);
  bytes[n++] = (uint8_t)((disp >> 16) & 0xFFu);
  bytes[n++] = (uint8_t)((disp >> 24) & 0xFFu);
  return n;
}

static inline int x64_emit_rex_rr(jello_jit_emit_buf* buf, int w, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  if(w || drex || srex) bytes[n++] = x64_rex(w, srex, 0, drex);
  bytes[n++] = 0x89u;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_mov_rr(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_rex_rr(buf, 1, dst, src);
}

static inline int x64_emit_mov_r32_imm(jello_jit_emit_buf* buf, int dst, uint32_t imm) {
  int lo = 0, rex = 0;
  x64_reg_parts(dst, &lo, &rex);
  uint8_t bytes[6];
  size_t n = 0;
  if(rex) bytes[n++] = x64_rex(0, 0, 0, rex);
  bytes[n++] = (uint8_t)(0xB8u + (uint32_t)lo);
  bytes[n++] = (uint8_t)(imm & 0xFFu);
  bytes[n++] = (uint8_t)((imm >> 8) & 0xFFu);
  bytes[n++] = (uint8_t)((imm >> 16) & 0xFFu);
  bytes[n++] = (uint8_t)((imm >> 24) & 0xFFu);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_mov_r64_imm(jello_jit_emit_buf* buf, int dst, uint64_t imm) {
  int lo = 0, rex = 0;
  x64_reg_parts(dst, &lo, &rex);
  uint8_t bytes[10];
  size_t n = 0;
  bytes[n++] = x64_rex(1, 0, 0, rex);
  bytes[n++] = (uint8_t)(0xB8u + (uint32_t)lo);
  for(int i = 0; i < 8; i++) bytes[n++] = (uint8_t)((imm >> (i * 8)) & 0xFFu);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_load_r32_disp(jello_jit_emit_buf* buf, int dst, int base, uint32_t disp) {
  int dlo = 0, blo = 0, drex = 0, brex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[10];
  size_t n = 0;
  if(brex || drex) bytes[n++] = x64_rex(0, drex, 0, brex);
  bytes[n++] = 0x8Bu;
  n = x64_append_mem_disp32(bytes, n, 2, dlo, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_store_r32_disp(jello_jit_emit_buf* buf, int src, int base, uint32_t disp) {
  int slo = 0, blo = 0, srex = 0, brex = 0;
  x64_reg_parts(src, &slo, &srex);
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[10];
  size_t n = 0;
  if(brex || srex) bytes[n++] = x64_rex(0, srex, 0, brex);
  bytes[n++] = 0x89u;
  n = x64_append_mem_disp32(bytes, n, 2, slo, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_load_r64_disp(jello_jit_emit_buf* buf, int dst, int base, uint32_t disp) {
  int dlo = 0, blo = 0, drex = 0, brex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[10];
  size_t n = 0;
  bytes[n++] = x64_rex(1, drex, 0, brex);
  bytes[n++] = 0x8Bu;
  n = x64_append_mem_disp32(bytes, n, 2, dlo, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_store_r64_disp(jello_jit_emit_buf* buf, int src, int base, uint32_t disp) {
  int slo = 0, blo = 0, srex = 0, brex = 0;
  x64_reg_parts(src, &slo, &srex);
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[10];
  size_t n = 0;
  bytes[n++] = x64_rex(1, srex, 0, brex);
  bytes[n++] = 0x89u;
  n = x64_append_mem_disp32(bytes, n, 2, slo, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_alu_rr(jello_jit_emit_buf* buf, uint8_t op, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  bytes[n++] = x64_rex(1, srex, 0, drex);
  bytes[n++] = op;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_add_rr(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_alu_rr(buf, 0x01u, dst, src);
}

static inline int x64_emit_sub_rr(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_alu_rr(buf, 0x29u, dst, src);
}

static inline int x64_emit_xor_r64(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_alu_rr(buf, 0x31u, dst, src);
}

static inline int x64_emit_xor_r32(jello_jit_emit_buf* buf, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  if(drex || srex) bytes[n++] = x64_rex(0, srex, 0, drex);
  bytes[n++] = 0x31u;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_sub_r64_imm8(jello_jit_emit_buf* buf, int dst, uint8_t imm) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[4];
  size_t n = 0;
  bytes[n++] = x64_rex(1, 0, 0, drex);
  bytes[n++] = 0x83u;
  bytes[n++] = x64_modrm(3, 5, dlo);
  bytes[n++] = imm;
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_neg_r64(jello_jit_emit_buf* buf, int dst) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[4];
  size_t n = 0;
  bytes[n++] = x64_rex(1, 0, 0, drex);
  bytes[n++] = 0xF7u;
  bytes[n++] = x64_modrm(3, 3, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_imul_r32(jello_jit_emit_buf* buf, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[5];
  size_t n = 0;
  if(drex || srex) bytes[n++] = x64_rex(0, srex, 0, drex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0xAFu;
  bytes[n++] = x64_modrm(3, dlo, slo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_cmp_r32_rr(jello_jit_emit_buf* buf, int lhs, int rhs) {
  return x64_emit_alu_rr(buf, 0x39u, lhs, rhs);
}

static inline int x64_emit_cmp_r64_rr(jello_jit_emit_buf* buf, int lhs, int rhs) {
  return x64_emit_alu_rr(buf, 0x39u, lhs, rhs);
}

static inline int x64_emit_cmp_r32_imm0(jello_jit_emit_buf* buf, int reg) {
  int lo = 0, rex = 0;
  x64_reg_parts(reg, &lo, &rex);
  uint8_t bytes[4];
  size_t n = 0;
  if(rex) bytes[n++] = x64_rex(0, 0, 0, rex);
  bytes[n++] = 0x83u;
  bytes[n++] = x64_modrm(3, 7, lo);
  bytes[n++] = 0u;
  return jello_jit_emit_bytes(buf, bytes, n);
}

/* ALU r32, imm: /digit in ModRM.reg. Prefer imm8 (83) when imm fits signed 8-bit. */
static inline int x64_emit_alu_r32_imm(jello_jit_emit_buf* buf, int digit, int dst, int32_t imm) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[8];
  size_t n = 0;
  if(drex) bytes[n++] = x64_rex(0, 0, 0, drex);
  if(imm >= -128 && imm <= 127) {
    bytes[n++] = 0x83u;
    bytes[n++] = x64_modrm(3, digit, dlo);
    bytes[n++] = (uint8_t)(int8_t)imm;
  } else {
    bytes[n++] = 0x81u;
    bytes[n++] = x64_modrm(3, digit, dlo);
    uint32_t u = (uint32_t)imm;
    bytes[n++] = (uint8_t)(u & 0xFFu);
    bytes[n++] = (uint8_t)((u >> 8) & 0xFFu);
    bytes[n++] = (uint8_t)((u >> 16) & 0xFFu);
    bytes[n++] = (uint8_t)((u >> 24) & 0xFFu);
  }
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_add_r32_imm(jello_jit_emit_buf* buf, int dst, int32_t imm) {
  return x64_emit_alu_r32_imm(buf, 0, dst, imm);
}

static inline int x64_emit_and_r32_imm(jello_jit_emit_buf* buf, int dst, int32_t imm) {
  return x64_emit_alu_r32_imm(buf, 4, dst, imm);
}

static inline int x64_emit_or_r32_imm(jello_jit_emit_buf* buf, int dst, int32_t imm) {
  return x64_emit_alu_r32_imm(buf, 1, dst, imm);
}

static inline int x64_emit_sub_r32_imm(jello_jit_emit_buf* buf, int dst, int32_t imm) {
  return x64_emit_alu_r32_imm(buf, 5, dst, imm);
}

static inline int x64_emit_cmp_r32_imm(jello_jit_emit_buf* buf, int dst, int32_t imm) {
  return x64_emit_alu_r32_imm(buf, 7, dst, imm);
}

/* shl r32, imm8 */
static inline int x64_emit_shl_r32_imm8(jello_jit_emit_buf* buf, int dst, uint8_t imm) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[5];
  size_t n = 0;
  if(drex) bytes[n++] = x64_rex(0, 0, 0, drex);
  bytes[n++] = 0xC1u;
  bytes[n++] = x64_modrm(3, 4, dlo);
  bytes[n++] = imm;
  return jello_jit_emit_bytes(buf, bytes, n);
}

/* movzx r32, BYTE PTR [base+disp32] */
static inline int x64_emit_movzx_r32_m8_disp(jello_jit_emit_buf* buf, int dst, int base, uint32_t disp) {
  int dlo = 0, blo = 0, drex = 0, brex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[11];
  size_t n = 0;
  if(brex || drex) bytes[n++] = x64_rex(0, drex, 0, brex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0xB6u;
  n = x64_append_mem_disp32(bytes, n, 2, dlo, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

/* mov BYTE PTR [base+disp32], imm8 */
static inline int x64_emit_store_imm8_disp(jello_jit_emit_buf* buf, int base, uint32_t disp, uint8_t imm) {
  int blo = 0, brex = 0;
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[12];
  size_t n = 0;
  if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
  bytes[n++] = 0xC6u;
  n = x64_append_mem_disp32(bytes, n, 2, 0, blo, disp);
  bytes[n++] = imm;
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_setcc(jello_jit_emit_buf* buf, uint8_t cc, int dst8) {
  int lo = 0, rex = 0;
  x64_reg_parts(dst8, &lo, &rex);
  uint8_t bytes[4];
  size_t n = 0;
  if(rex) bytes[n++] = x64_rex(0, 0, 0, rex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = cc;
  bytes[n++] = x64_modrm(3, 0, lo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_movzx_r32_r8(jello_jit_emit_buf* buf, int dst, int src8) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src8, &slo, &srex);
  uint8_t bytes[5];
  size_t n = 0;
  if(drex || srex) bytes[n++] = x64_rex(0, srex, 0, drex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0xB6u;
  bytes[n++] = x64_modrm(3, dlo, slo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_endbr64(jello_jit_emit_buf* buf) {
  /* CET/IBT landing pad for indirect call/jmp targets (NOP on older CPUs). */
  uint8_t bytes[4] = {0xF3u, 0x0Fu, 0x1Eu, 0xFAu};
  return jello_jit_emit_bytes(buf, bytes, 4u);
}

static inline int x64_emit_sub_rsp_imm8(jello_jit_emit_buf* buf, uint8_t imm) {
  uint8_t bytes[4] = {0x48u, 0x83u, 0xECu, imm};
  return jello_jit_emit_bytes(buf, bytes, 4u);
}

static inline int x64_emit_add_rsp_imm8(jello_jit_emit_buf* buf, uint8_t imm) {
  uint8_t bytes[4] = {0x48u, 0x83u, 0xC4u, imm};
  return jello_jit_emit_bytes(buf, bytes, 4u);
}

static inline int x64_emit_jmp_rel32(jello_jit_emit_buf* buf) {
  uint8_t bytes[5] = {0xE9u, 0, 0, 0, 0};
  return jello_jit_emit_bytes(buf, bytes, 5u);
}

/* lea r64, [rip + disp32] — 7 bytes; patch disp after knowing target. */
static inline int x64_emit_lea_rip_rel32(jello_jit_emit_buf* buf, int dst) {
  int dlo = 0, drex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  uint8_t bytes[7];
  size_t n = 0;
  bytes[n++] = x64_rex(1, drex, 0, 0);
  bytes[n++] = 0x8Du;
  bytes[n++] = x64_modrm(0, dlo, 5); /* RIP-relative */
  bytes[n++] = 0;
  bytes[n++] = 0;
  bytes[n++] = 0;
  bytes[n++] = 0;
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_patch_lea_rip_rel32(jello_jit_emit_buf* buf, size_t at, size_t target) {
  if(!buf || !buf->data || at + 7u > buf->size) return -1;
  int32_t off = (int32_t)((int64_t)target - (int64_t)(at + 7u));
  memcpy(buf->data + at + 3u, &off, 4u);
  return 0;
}

static inline int x64_emit_jcc_rel32(jello_jit_emit_buf* buf, uint8_t cc) {
  uint8_t bytes[6] = {0x0Fu, cc, 0, 0, 0, 0};
  return jello_jit_emit_bytes(buf, bytes, 6u);
}

static inline int x64_emit_call_rax(jello_jit_emit_buf* buf) {
  uint8_t bytes[2] = {0xFFu, 0xD0u};
  return jello_jit_emit_bytes(buf, bytes, 2u);
}

static inline int x64_emit_ret(jello_jit_emit_buf* buf) {
  uint8_t b = 0xC3u;
  return jello_jit_emit_bytes(buf, &b, 1u);
}

static inline int x64_emit_push_r64(jello_jit_emit_buf* buf, int reg) {
  int lo = 0, rex = 0;
  x64_reg_parts(reg, &lo, &rex);
  uint8_t bytes[2];
  size_t n = 0;
  if(rex) bytes[n++] = (uint8_t)(0x40u | ((uint32_t)rex << 3) | 1u);
  bytes[n++] = (uint8_t)(0x50u + (uint32_t)lo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_pop_r64(jello_jit_emit_buf* buf, int reg) {
  int lo = 0, rex = 0;
  x64_reg_parts(reg, &lo, &rex);
  uint8_t bytes[2];
  size_t n = 0;
  if(rex) bytes[n++] = (uint8_t)(0x40u | ((uint32_t)rex << 3) | 1u);
  bytes[n++] = (uint8_t)(0x58u + (uint32_t)lo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_test_rr(jello_jit_emit_buf* buf, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  if(drex || srex) bytes[n++] = x64_rex(0, srex, 0, drex);
  bytes[n++] = 0x85u;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_test_r64(jello_jit_emit_buf* buf, int dst, int src) {
  int dlo = 0, slo = 0, drex = 0, srex = 0;
  x64_reg_parts(dst, &dlo, &drex);
  x64_reg_parts(src, &slo, &srex);
  uint8_t bytes[4];
  size_t n = 0;
  bytes[n++] = x64_rex(1, srex, 0, drex);
  bytes[n++] = 0x85u;
  bytes[n++] = x64_modrm(3, slo, dlo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_jmp_r64(jello_jit_emit_buf* buf, int reg) {
  int lo = 0, rex = 0;
  x64_reg_parts(reg, &lo, &rex);
  uint8_t bytes[3];
  size_t n = 0;
  if(rex) bytes[n++] = (uint8_t)(0x40u | ((uint32_t)rex << 3) | 1u);
  bytes[n++] = 0xFFu;
  bytes[n++] = x64_modrm(3, 4, lo);
  return jello_jit_emit_bytes(buf, bytes, n);
}

/* SSE2 double. Legacy prefixes (F2/F3/66) must precede REX. */
static inline int x64_emit_load_xmm64_disp(jello_jit_emit_buf* buf, int xmm, int base, uint32_t disp) {
  int blo = 0, brex = 0;
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[11];
  size_t n = 0;
  bytes[n++] = 0xF2u;
  if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0x10u;
  n = x64_append_mem_disp32(bytes, n, 2, xmm & 7, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_store_xmm64_disp(jello_jit_emit_buf* buf, int xmm, int base, uint32_t disp) {
  int blo = 0, brex = 0;
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[11];
  size_t n = 0;
  bytes[n++] = 0xF2u;
  if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0x11u;
  n = x64_append_mem_disp32(bytes, n, 2, xmm & 7, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_sse_rr(jello_jit_emit_buf* buf, uint8_t op, int dst, int src) {
  uint8_t bytes[5] = {0xF2u, 0x0Fu, op, x64_modrm(3, dst & 7, src & 7), 0};
  bytes[3] = x64_modrm(3, dst & 7, src & 7);
  return jello_jit_emit_bytes(buf, bytes, 4u);
}

static inline int x64_emit_addsd(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse_rr(buf, 0x58u, dst, src);
}

static inline int x64_emit_subsd(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse_rr(buf, 0x5Cu, dst, src);
}

static inline int x64_emit_mulsd(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse_rr(buf, 0x59u, dst, src);
}

static inline int x64_emit_divsd(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse_rr(buf, 0x5Eu, dst, src);
}

static inline int x64_emit_sqrtsd(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse_rr(buf, 0x51u, dst, src);
}

static inline int x64_emit_comisd(jello_jit_emit_buf* buf, int lhs, int rhs) {
  uint8_t bytes[4] = {0x66u, 0x0Fu, 0x2Fu, x64_modrm(3, lhs & 7, rhs & 7)};
  return jello_jit_emit_bytes(buf, bytes, 4u);
}

/* SSE scalar float. Legacy prefixes (F2/F3/66) must precede REX. */
static inline int x64_emit_load_xmm32_disp(jello_jit_emit_buf* buf, int xmm, int base, uint32_t disp) {
  int blo = 0, brex = 0;
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[11];
  size_t n = 0;
  bytes[n++] = 0xF3u;
  if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0x10u;
  n = x64_append_mem_disp32(bytes, n, 2, xmm & 7, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_store_xmm32_disp(jello_jit_emit_buf* buf, int xmm, int base, uint32_t disp) {
  int blo = 0, brex = 0;
  x64_reg_parts(base, &blo, &brex);
  uint8_t bytes[11];
  size_t n = 0;
  bytes[n++] = 0xF3u;
  if(brex) bytes[n++] = x64_rex(0, 0, 0, brex);
  bytes[n++] = 0x0Fu;
  bytes[n++] = 0x11u;
  n = x64_append_mem_disp32(bytes, n, 2, xmm & 7, blo, disp);
  return jello_jit_emit_bytes(buf, bytes, n);
}

static inline int x64_emit_sse32_rr(jello_jit_emit_buf* buf, uint8_t op, int dst, int src) {
  uint8_t bytes[5] = {0xF3u, 0x0Fu, op, x64_modrm(3, dst & 7, src & 7), 0};
  bytes[3] = x64_modrm(3, dst & 7, src & 7);
  return jello_jit_emit_bytes(buf, bytes, 4u);
}

static inline int x64_emit_addss(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse32_rr(buf, 0x58u, dst, src);
}

static inline int x64_emit_subss(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse32_rr(buf, 0x5Cu, dst, src);
}

static inline int x64_emit_mulss(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse32_rr(buf, 0x59u, dst, src);
}

static inline int x64_emit_divss(jello_jit_emit_buf* buf, int dst, int src) {
  return x64_emit_sse32_rr(buf, 0x5Eu, dst, src);
}

static inline int x64_emit_comiss(jello_jit_emit_buf* buf, int lhs, int rhs) {
  uint8_t bytes[4] = {0x0Fu, 0x2Fu, x64_modrm(3, lhs & 7, rhs & 7), 0};
  bytes[2] = x64_modrm(3, lhs & 7, rhs & 7);
  return jello_jit_emit_bytes(buf, bytes, 3u);
}

#endif
