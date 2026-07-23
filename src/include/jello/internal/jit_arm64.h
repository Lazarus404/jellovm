// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

// AArch64 JIT code emission helpers.
#ifndef JELLO_INTERNAL_JIT_ARM64_H
#define JELLO_INTERNAL_JIT_ARM64_H

#include <stdint.h>

#include <jello/internal/jit_impl.h>

/* AArch64 A64 helpers (32-bit instruction words). */

static inline uint32_t a64_movz_w(uint8_t rd, uint16_t imm, uint8_t shift) {
  return 0x52800000u | (((uint32_t)(shift / 16u) & 3u) << 21) | ((uint32_t)imm << 5) | (uint32_t)rd;
}

static inline uint32_t a64_movk_w(uint8_t rd, uint16_t imm, uint8_t shift) {
  return 0x72800000u | (((uint32_t)(shift / 16u) & 3u) << 21) | ((uint32_t)imm << 5) | (uint32_t)rd;
}

static inline uint32_t a64_add_w_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x0B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_sub_w_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x4B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

/* ADD Wd, Wn, #imm12 (shift=0). imm12 must be 0..4095. */
static inline uint32_t a64_add_w_imm(uint8_t rd, uint8_t rn, uint32_t imm12) {
  return 0x11000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

/* SUB Wd, Wn, #imm12 (shift=0). imm12 must be 0..4095. */
static inline uint32_t a64_sub_w_imm(uint8_t rd, uint8_t rn, uint32_t imm12) {
  return 0x51000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_subs_w_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x6B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_add_x_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x8B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_cset_w(uint8_t rd, uint8_t cond) {
  /* CSET Wd, cond  ==  CSINC Wd, WZR, WZR, invert(cond) with o2=01. */
  uint8_t inv = (uint8_t)(cond ^ 1u);
  return 0x1A800000u | (1u << 10) | ((uint32_t)31u << 16) | ((uint32_t)inv << 12) | ((uint32_t)31u << 5) |
         (uint32_t)rd;
}

static inline uint32_t a64_cmp_w_imm0(uint8_t rn) {
  /* CMP Wn, #0  ==  SUBS WZR, Wn, #0 */
  return 0x71000000u | ((uint32_t)rn << 5) | 0x1Fu;
}

static inline uint32_t a64_mul_w(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x1B007C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_sdiv_w(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x1AC00C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_msub_w(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t ra) {
  return 0x1B008000u | ((uint32_t)rm << 16) | ((uint32_t)ra << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_lslv_w(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x1AC02000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_lsrv_w(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x1AC02400u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_neg_w(uint8_t rd, uint8_t rm) {
  return 0x4B0003E0u | ((uint32_t)rm << 16) | (uint32_t)rd;
}

static inline uint32_t a64_ldr_w_uimm(uint8_t rt, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 4u;
  return 0xB9400000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static inline uint32_t a64_str_w_uimm(uint8_t rt, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 4u;
  return 0xB9000000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static inline uint32_t a64_ldr_x_uimm(uint8_t rt, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 8u;
  return 0xF9400000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static inline uint32_t a64_str_x_uimm(uint8_t rt, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 8u;
  return 0xF9000000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static inline uint32_t a64_sub_x_imm(uint8_t rd, uint8_t rn, uint32_t imm12) {
  return 0xD1000000u | ((imm12 & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_sub_x_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0xCB000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_subs_x_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0xEB000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_mul_x(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9B007C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_sdiv_x(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC00C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_msub_x(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t ra) {
  return 0x9B008000u | ((uint32_t)rm << 16) | ((uint32_t)ra << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_lslv_x(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC02000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_lsrv_x(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC02400u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_neg_x(uint8_t rd, uint8_t rm) {
  return 0xCB0003E0u | ((uint32_t)rm << 16) | (uint32_t)rd;
}

static inline uint32_t a64_ldr_d_uimm(uint8_t dt, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 8u;
  return 0xFD400000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)dt;
}

static inline uint32_t a64_str_d_uimm(uint8_t dt, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 8u;
  return 0xFD000000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)dt;
}

static inline uint32_t a64_fadd_d(uint8_t dd, uint8_t dn, uint8_t dm) {
  return 0x1E602800u | ((uint32_t)dm << 16) | ((uint32_t)dn << 5) | (uint32_t)dd;
}

static inline uint32_t a64_fsub_d(uint8_t dd, uint8_t dn, uint8_t dm) {
  return 0x1E603800u | ((uint32_t)dm << 16) | ((uint32_t)dn << 5) | (uint32_t)dd;
}

static inline uint32_t a64_fmul_d(uint8_t dd, uint8_t dn, uint8_t dm) {
  return 0x1E600800u | ((uint32_t)dm << 16) | ((uint32_t)dn << 5) | (uint32_t)dd;
}

static inline uint32_t a64_fdiv_d(uint8_t dd, uint8_t dn, uint8_t dm) {
  return 0x1E601800u | ((uint32_t)dm << 16) | ((uint32_t)dn << 5) | (uint32_t)dd;
}

static inline uint32_t a64_fsqrt_d(uint8_t dd, uint8_t dn) {
  /* FSQRT <Dd>, <Dn> */
  return 0x1E61C000u | ((uint32_t)dn << 5) | (uint32_t)dd;
}

static inline uint32_t a64_fneg_d(uint8_t dd, uint8_t dn) {
  /* FNEG <Dd>, <Dn> */
  return 0x1E614000u | ((uint32_t)dn << 5) | (uint32_t)dd;
}

static inline uint32_t a64_fneg_s(uint8_t sd, uint8_t sn) {
  /* FNEG <Sd>, <Sn> */
  return 0x1E214000u | ((uint32_t)sn << 5) | (uint32_t)sd;
}

static inline uint32_t a64_fcmp_d(uint8_t dn, uint8_t dm) {
  /* FCMP <Dn>, <Dm>: opcode2[4:0] must be 00000 (not 11111). */
  return 0x1E602000u | ((uint32_t)dm << 16) | ((uint32_t)dn << 5);
}

static inline uint32_t a64_ldr_s_uimm(uint8_t st, uint8_t rn, uint32_t byte_off) {
  uint32_t imm12 = byte_off / 4u;
  return 0xBD400000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)st;
}

static inline uint32_t a64_str_s_uimm(uint8_t st, uint8_t rn, uint32_t byte_off) {
  /* STR St, [Xn, #imm] — 0xBD00… (not STR W 0xBC00…). */
  uint32_t imm12 = byte_off / 4u;
  return 0xBD000000u | (imm12 << 10) | ((uint32_t)rn << 5) | (uint32_t)st;
}

static inline uint32_t a64_fadd_s(uint8_t sd, uint8_t sn, uint8_t sm) {
  /* Mirror F64 bases with ftype=00 (single). */
  return 0x1E202800u | ((uint32_t)sm << 16) | ((uint32_t)sn << 5) | (uint32_t)sd;
}

static inline uint32_t a64_fsub_s(uint8_t sd, uint8_t sn, uint8_t sm) {
  return 0x1E203800u | ((uint32_t)sm << 16) | ((uint32_t)sn << 5) | (uint32_t)sd;
}

static inline uint32_t a64_fmul_s(uint8_t sd, uint8_t sn, uint8_t sm) {
  return 0x1E200800u | ((uint32_t)sm << 16) | ((uint32_t)sn << 5) | (uint32_t)sd;
}

static inline uint32_t a64_fdiv_s(uint8_t sd, uint8_t sn, uint8_t sm) {
  return 0x1E201800u | ((uint32_t)sm << 16) | ((uint32_t)sn << 5) | (uint32_t)sd;
}

static inline uint32_t a64_fcmp_s(uint8_t sn, uint8_t sm) {
  /* FCMP <Sn>, <Sm>: opcode2[4:0] must be 00000. */
  return 0x1E202000u | ((uint32_t)sm << 16) | ((uint32_t)sn << 5);
}

static inline uint32_t a64_cbz_x(uint8_t rt, int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0xB4000000u | imm19 | (uint32_t)rt;
}

static inline uint32_t a64_cbz_w(uint8_t rt, int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0x34000000u | imm19 | (uint32_t)rt;
}

static inline uint32_t a64_cbnz_w(uint8_t rt, int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0x35000000u | imm19 | (uint32_t)rt;
}

static inline uint32_t a64_b(int32_t off_insns) {
  return 0x14000000u | ((uint32_t)off_insns & 0x03FFFFFFu);
}

/* ADR Xd, label — imm = label_pc - adr_pc (bytes), ±1MB. */
static inline uint32_t a64_adr(uint8_t rd, int32_t imm_bytes) {
  uint32_t immlo = (uint32_t)imm_bytes & 3u;
  uint32_t immhi = ((uint32_t)imm_bytes >> 2) & 0x7FFFFu;
  return 0x10000000u | (immlo << 29) | (immhi << 5) | (uint32_t)rd;
}

static inline uint32_t a64_bl(int32_t off_insns) {
  return 0x94000000u | ((uint32_t)off_insns & 0x03FFFFFFu);
}

static inline uint32_t a64_cmp_w_imm(uint8_t rn, uint16_t imm) {
  return 0x71000000u | ((uint32_t)imm << 10) | ((uint32_t)rn << 5) | 0x1Fu;
}

static inline uint32_t a64_and_w_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x0A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_orr_w_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x2A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static inline uint32_t a64_orr_x_rr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0xAA000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}

/* ORR Wd, WZR, Wm, LSL #sh */
static inline uint32_t a64_lsl_w_imm(uint8_t rd, uint8_t rm, uint8_t sh) {
  return 0x2A0003E0u | ((uint32_t)rm << 16) | ((uint32_t)sh << 10) | (uint32_t)rd;
}

/* ORR Xd, XZR, Xm, LSL #sh — required for jello_make_i32 (full pointer-width tag). */
static inline uint32_t a64_lsl_x_imm(uint8_t rd, uint8_t rm, uint8_t sh) {
  return 0xAA0003E0u | ((uint32_t)rm << 16) | ((uint32_t)sh << 10) | (uint32_t)rd;
}

/* ORR Wd, WZR, Wm, LSR #sh */
static inline uint32_t a64_lsr_w_imm(uint8_t rd, uint8_t rm, uint8_t sh) {
  return 0x2A4003E0u | ((uint32_t)rm << 16) | ((uint32_t)sh << 10) | (uint32_t)rd;
}

/* ORR Xd, XZR, Xm, LSR #sh — required for jello_as_i32 (full pointer-width tag). */
static inline uint32_t a64_lsr_x_imm(uint8_t rd, uint8_t rm, uint8_t sh) {
  return 0xAA4003E0u | ((uint32_t)rm << 16) | ((uint32_t)sh << 10) | (uint32_t)rd;
}

static inline uint32_t a64_mov_w_rr(uint8_t rd, uint8_t rm) {
  return 0x2A0003E0u | ((uint32_t)rm << 16) | (uint32_t)rd;
}

static inline uint32_t a64_mov_x_rr(uint8_t rd, uint8_t rm) {
  return 0xAA0003E0u | ((uint32_t)rm << 16) | (uint32_t)rd;
}

/* LDRB Wt, [Xn, #imm12] */
static inline uint32_t a64_ldrb_w_uimm(uint8_t rt, uint8_t rn, uint32_t byte_off) {
  return 0x39400000u | ((byte_off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

/* STRB Wt, [Xn, #imm12] */
static inline uint32_t a64_strb_w_uimm(uint8_t rt, uint8_t rn, uint32_t byte_off) {
  return 0x39000000u | ((byte_off & 0xFFFu) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static inline uint32_t a64_cbnz_x(uint8_t rt, int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0xB5000000u | imm19 | (uint32_t)rt;
}

static inline uint32_t a64_b_eq(int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0x54000000u | imm19 | 0x0u;
}

static inline uint32_t a64_b_ne(int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0x54000000u | imm19 | 0x1u;
}

/* b.hs / b.cs — unsigned higher-or-same */
static inline uint32_t a64_b_hs(int32_t off_insns) {
  uint32_t imm19 = ((uint32_t)off_insns & 0x7FFFFu) << 5;
  return 0x54000000u | imm19 | 0x2u;
}

static inline uint32_t a64_ret(void) {
  return 0xD65F03C0u;
}

static inline uint32_t a64_stp_xpre(int rt, int rt2, int rn, int imm) {
  return 0xA9800000u | (((uint32_t)imm & 0x7Fu) << 15) | ((uint32_t)rt2 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static inline uint32_t a64_ldp_xpost(int rt, int rt2, int rn, int imm) {
  return 0xA8C00000u | (((uint32_t)imm & 0x7Fu) << 15) | ((uint32_t)rt2 << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

#endif
