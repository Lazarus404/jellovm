// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>

#include <jello/internal/vm_internal.h>

#include <stdlib.h>
#include <string.h>

static jello_jit_ir_bin bin_from_op(jello_op op) {
  switch(op) {
    case JOP_ADD_I32:
    case JOP_ADD_I32_IMM:
    case JOP_ADD_I64:
    case JOP_ADD_F32:
    case JOP_ADD_F64:
      return JIR_BIN_ADD;
    case JOP_SUB_I32:
    case JOP_SUB_I32_IMM:
    case JOP_SUB_I64:
    case JOP_SUB_F32:
    case JOP_SUB_F64:
      return JIR_BIN_SUB;
    case JOP_MUL_I32:
    case JOP_MUL_I32_IMM:
    case JOP_MUL_I64:
    case JOP_MUL_F32:
    case JOP_MUL_F64:
      return JIR_BIN_MUL;
    case JOP_DIV_I32:
    case JOP_DIV_I64:
    case JOP_DIV_F32:
    case JOP_DIV_F64:
      return JIR_BIN_SDIV;
    case JOP_MOD_I32:
    case JOP_MOD_I64:
      return JIR_BIN_MOD;
    case JOP_SHL_I32:
    case JOP_SHL_I64:
      return JIR_BIN_SHL;
    case JOP_SHR_I32:
    case JOP_SHR_I64:
      return JIR_BIN_SHR;
    default:
      return JIR_BIN_ADD;
  }
}

static int append_ir(jello_jit_ir_func* ir, jello_jit_ir_insn ins) {
  if(ins.op == JIR_SLOW) ir->has_slow = 1u;
  jello_jit_ir_insn* n = (jello_jit_ir_insn*)realloc(ir->insns, (size_t)(ir->ninsns + 1u) * sizeof(jello_jit_ir_insn));
  if(!n) return -1;
  ir->insns = n;
  ir->insns[ir->ninsns++] = ins;
  return 0;
}

static int insn_is_self_const_fun(
    const jello_bc_module* m,
    const jello_bc_function* f,
    const jello_insn* ins
) {
  if((jello_op)ins->op != JOP_CONST_FUN) return 0;
  uint32_t fi = ins->imm;
  if(jello_is_native_builtin(fi)) return 0;
  uint32_t bidx = fi - JELLO_NATIVE_BUILTIN_COUNT;
  return bidx < m->nfuncs && &m->funcs[bidx] == f;
}

/* Resolve reg to a same-module bytecode function: CONST_FUN, CLOSURE, or MOV. */
static int32_t prove_reg_bytecode_fun(
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t reg,
    uint32_t before_pc
) {
  uint32_t cur = reg;
  uint32_t limit = before_pc;
  for(int depth = 0; depth < 8; depth++) {
    int moved = 0;
    for(int32_t j = (int32_t)limit - 1; j >= 0; j--) {
      const jello_insn* w = &f->insns[j];
      if(w->a != cur) continue;
      jello_op op = (jello_op)w->op;
      if(op == JOP_CONST_FUN || op == JOP_CLOSURE) {
        uint32_t fi = w->imm;
        if(jello_is_native_builtin(fi) || jello_is_jdll_prim(fi)) return -1;
        uint32_t bi = fi - JELLO_NATIVE_BUILTIN_COUNT;
        if(bi >= m->nfuncs) return -1;
        return (int32_t)bi;
      }
      if(op == JOP_MOV) {
        cur = w->b;
        limit = (uint32_t)j;
        moved = 1;
        break;
      }
      return -1;
    }
    if(!moved) return -1;
  }
  return -1;
}

/* True if any CLOSURE site installs captures into this bytecode function. */
static int func_needs_captures(const jello_bc_module* m, uint32_t bidx) {
  uint32_t want = bidx + JELLO_NATIVE_BUILTIN_COUNT;
  for(uint32_t gi = 0; gi < m->nfuncs; gi++) {
    const jello_bc_function* g = &m->funcs[gi];
    for(uint32_t i = 0; i < g->ninsns; i++) {
      const jello_insn* w = &g->insns[i];
      if((jello_op)w->op == JOP_CLOSURE && w->imm == want && w->c > 0u) return 1;
    }
  }
  return 0;
}

/* Prove CALLR callee is a capture of a bare CONST_FUN (all CLOSURE sites agree). */
static int32_t prove_capture_callee_bidx(
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t callee_reg
) {
  if(callee_reg < f->cap_start) return -1;
  uint32_t off = callee_reg - f->cap_start;
  uint32_t self_fi = (uint32_t)(f - m->funcs);
  uint32_t want = self_fi + JELLO_NATIVE_BUILTIN_COUNT;
  int32_t agreed = -1;
  int saw = 0;
  for(uint32_t gi = 0; gi < m->nfuncs; gi++) {
    const jello_bc_function* g = &m->funcs[gi];
    for(uint32_t i = 0; i < g->ninsns; i++) {
      const jello_insn* w = &g->insns[i];
      if((jello_op)w->op != JOP_CLOSURE || w->imm != want) continue;
      if(off >= (uint32_t)w->c) return -1;
      int32_t bi = prove_reg_bytecode_fun(m, g, (uint32_t)w->b + off, i);
      if(bi < 0) return -1;
      if(!saw) {
        agreed = bi;
        saw = 1;
      } else if(agreed != bi) {
        return -1;
      }
    }
  }
  return saw ? agreed : -1;
}

/* True when CONST_FUN loads this function and every use of the dest is as the
 * callee of a self CALLR, or the flyweight is dead after peephole folded
 * CALLR→CALL (imm already names this function). Safe to emit as NOP. */
static int const_fun_self_elidable(
    const jello_bc_module* m,
    const jello_bc_function* f,
    uint32_t pc
) {
  const jello_insn* ins = &f->insns[pc];
  if(!insn_is_self_const_fun(m, f, ins)) return 0;
  uint32_t dst = ins->a;
  uint32_t self_fi = (uint32_t)(f - m->funcs) + JELLO_NATIVE_BUILTIN_COUNT;
  for(uint32_t i = 0; i < f->ninsns; i++) {
    if(i == pc) continue;
    const jello_insn* w = &f->insns[i];
    jello_op op = (jello_op)w->op;

    if(op == JOP_CALLR && w->b == dst) {
      int is_self = 0;
      for(int32_t j = (int32_t)i - 1; j >= 0; j--) {
        const jello_insn* prev = &f->insns[j];
        if(prev->a != dst) continue;
        is_self = insn_is_self_const_fun(m, f, prev);
        break;
      }
      if(!is_self) return 0;
      uint32_t first = w->imm, na = w->c;
      if(na && dst >= first && dst < first + na) return 0;
      continue;
    }

    if(op == JOP_CALL) {
      uint32_t first = w->b, na = w->c;
      if(na && dst >= first && dst < first + na) return 0;
      /* Peephole may leave a dead CONST_FUN while CALL.imm is already self. */
      if(w->imm == self_fi) continue;
    } else if(op == JOP_CALLR) {
      uint32_t first = w->imm, na = w->c;
      if(na && dst >= first && dst < first + na) return 0;
    } else if(op == JOP_TAILCALL) {
      uint32_t first = w->b, na = w->c;
      if(na && dst >= first && dst < first + na) return 0;
      if(w->imm == self_fi) continue;
    } else if(op == JOP_TAILCALLR) {
      if(w->b == dst) {
        uint32_t first = w->imm, na = w->c;
        if(na && dst >= first && dst < first + na) return 0;
        continue;
      }
      uint32_t first = w->imm, na = w->c;
      if(na && dst >= first && dst < first + na) return 0;
    }

    /* Redefine of dst: only another self CONST_FUN is ok. */
    if(w->a == dst) {
      if(op == JOP_RET || op == JOP_JMP_IF) return 0; /* a is a source */
      if(insn_is_self_const_fun(m, f, w)) continue;
      return 0;
    }

    /* Source uses in b/c — ignore when dst==0 (unused fields often zero). */
    if(dst == 0u) return 0;
    if(w->b == dst || w->c == dst) return 0;
  }
  return 1;
}

void jello_jit_ir_func_free(jello_jit_ir_func* ir) {
  if(!ir) return;
  free(ir->insns);
  ir->insns = NULL;
  ir->ninsns = 0;
}

static int lower_one(const jello_bc_module* m, const jello_bc_function* f, uint32_t pc, jello_jit_ir_func* ir) {
  const jello_insn* ins = &f->insns[pc];
  jello_jit_ir_insn out = {0};
  out.bc_pc = pc;

  switch((jello_op)ins->op) {
    case JOP_NOP:
      out.op = JIR_NOP;
      break;
    case JOP_RET:
      out.op = JIR_RET;
      out.a = ins->a;
      break;
    case JOP_MOV:
      out.op = JIR_MOV_REG;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_JMP: {
      int32_t target = (int32_t)pc + 1 + (int32_t)ins->imm;
      if(target <= (int32_t)pc) {
        jello_jit_ir_insn chk = {0};
        chk.op = JIR_FUEL_CHECK;
        chk.bc_pc = pc;
        if(append_ir(ir, chk) != 0) return -1;
      }
      out.op = JIR_JMP;
      out.imm = target;
      break;
    }
    case JOP_JMP_IF:
      out.op = JIR_JMP_IF;
      out.a = ins->a;
      out.imm = (int32_t)pc + 1 + (int32_t)ins->imm;
      break;
    case JOP_CONST_I32:
      out.op = JIR_LOAD_I32;
      out.a = ins->a;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_CONST_I64:
      out.op = JIR_LOAD_I64;
      out.a = ins->a;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_CONST_F64:
      out.op = JIR_LOAD_F64;
      out.a = ins->a;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_CONST_F32:
      out.op = JIR_LOAD_F32;
      out.a = ins->a;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_CONST_I8_IMM:
      out.op = JIR_LOAD_I32;
      out.a = ins->a;
      out.imm = (int32_t)(int8_t)(uint8_t)ins->c;
      break;
    case JOP_CONST_BOOL:
      out.op = JIR_LOAD_I32;
      out.a = ins->a;
      out.imm = (int32_t)(ins->c & 1u);
      break;
    case JOP_NOT_BOOL:
      out.op = JIR_CMP_I32;
      out.a = ins->a;
      out.b = ins->b;
      out.c = 255u;
      out.imm = (int32_t)JIR_CMP_EQ;
      break;
    case JOP_ADD_I32:
    case JOP_SUB_I32:
    case JOP_MUL_I32:
    case JOP_DIV_I32:
    case JOP_MOD_I32:
    case JOP_SHL_I32:
    case JOP_SHR_I32:
      out.op = JIR_BIN_I32;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)bin_from_op((jello_op)ins->op);
      break;
    case JOP_ADD_I64:
    case JOP_SUB_I64:
    case JOP_MUL_I64:
    case JOP_DIV_I64:
    case JOP_MOD_I64:
    case JOP_SHL_I64:
    case JOP_SHR_I64:
      out.op = JIR_BIN_I64;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)bin_from_op((jello_op)ins->op);
      break;
    case JOP_ADD_F64:
    case JOP_SUB_F64:
    case JOP_MUL_F64:
    case JOP_DIV_F64:
      out.op = JIR_BIN_F64;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)bin_from_op((jello_op)ins->op);
      break;
    case JOP_ADD_F32:
    case JOP_SUB_F32:
    case JOP_MUL_F32:
    case JOP_DIV_F32:
      out.op = JIR_BIN_F32;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)bin_from_op((jello_op)ins->op);
      break;
    case JOP_ADD_I32_IMM:
    case JOP_SUB_I32_IMM:
    case JOP_MUL_I32_IMM:
      out.op = JIR_BIN_I32;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)bin_from_op((jello_op)ins->op);
      out.c = (uint16_t)ins->c;
      break;
    case JOP_EQ_I32:
    case JOP_LT_I32:
      out.op = JIR_CMP_I32;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)((jello_op)ins->op == JOP_EQ_I32 ? JIR_CMP_EQ : JIR_CMP_LT);
      break;
    case JOP_EQ_I64:
    case JOP_LT_I64:
      out.op = JIR_CMP_I64;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)((jello_op)ins->op == JOP_EQ_I64 ? JIR_CMP_EQ : JIR_CMP_LT);
      break;
    case JOP_EQ_F64:
    case JOP_LT_F64:
      out.op = JIR_CMP_F64;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)((jello_op)ins->op == JOP_EQ_F64 ? JIR_CMP_EQ : JIR_CMP_LT);
      break;
    case JOP_EQ_F32:
    case JOP_LT_F32:
      out.op = JIR_CMP_F32;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)((jello_op)ins->op == JOP_EQ_F32 ? JIR_CMP_EQ : JIR_CMP_LT);
      break;
    case JOP_EQ_I32_IMM:
    case JOP_LT_I32_IMM:
      out.op = JIR_CMP_I32;
      out.a = ins->a;
      out.b = ins->b;
      out.c = (uint16_t)ins->c;
      out.imm = (int32_t)((jello_op)ins->op == JOP_EQ_I32_IMM ? JIR_CMP_EQ : JIR_CMP_LT);
      break;
    case JOP_NEG_I32:
      out.op = JIR_NEG_I32;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_NEG_I64:
      out.op = JIR_NEG_I64;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_NEG_F64:
      out.op = JIR_NEG_F64;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_NEG_F32:
      out.op = JIR_NEG_F32;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_CONST_NULL:
      out.op = JIR_LOAD_NULL;
      out.a = ins->a;
      break;
    case JOP_ASSERT:
      out.op = JIR_ASSERT;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      break;
    case JOP_CONST_BYTES:
      out.op = JIR_CONST_BYTES;
      out.a = ins->a;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_SEXT_I64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_SEXT_I64;
      break;
    case JOP_SEXT_I16:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_SEXT_I16;
      break;
    case JOP_TRUNC_I8:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_TRUNC_I8;
      break;
    case JOP_TRUNC_I16:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_TRUNC_I16;
      break;
    case JOP_I32_FROM_I64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_I32_FROM_I64;
      break;
    case JOP_F64_FROM_I32:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_F64_FROM_I32;
      break;
    case JOP_I32_FROM_F64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_I32_FROM_F64;
      break;
    case JOP_F64_FROM_I64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_F64_FROM_I64;
      break;
    case JOP_I64_FROM_F64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_I64_FROM_F64;
      break;
    case JOP_F32_FROM_I32:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_F32_FROM_I32;
      break;
    case JOP_I32_FROM_F32:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_I32_FROM_F32;
      break;
    case JOP_F64_FROM_F32:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_F64_FROM_F32;
      break;
    case JOP_F32_FROM_F64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_F32_FROM_F64;
      break;
    case JOP_F32_FROM_I64:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_F32_FROM_I64;
      break;
    case JOP_I64_FROM_F32:
      out.op = JIR_CONV;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)JIR_CONV_I64_FROM_F32;
      break;
    case JOP_CONST_FUN:
      /* Self flyweight only used as CALLR callee → CALL_SELF; skip materialize. */
      if(const_fun_self_elidable(m, f, pc)) {
        out.op = JIR_NOP;
        break;
      }
      out.op = JIR_CONST_FUN;
      out.a = ins->a;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_CLOSURE:
      out.op = JIR_CLOSURE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_BYTES_NEW:
      out.op = JIR_BYTES_NEW;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_BYTES_LEN:
      out.op = JIR_BYTES_LEN;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_BYTES_GET_U8:
      out.op = JIR_BYTES_GET_U8;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      break;
    case JOP_BYTES_SET_U8:
      out.op = JIR_BYTES_SET_U8;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      break;
    case JOP_BYTES_READ_U16_LE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_U16_LE;
      break;
    case JOP_BYTES_READ_U16_BE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_U16_BE;
      break;
    case JOP_BYTES_READ_U32_LE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_U32_LE;
      break;
    case JOP_BYTES_READ_U32_BE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_U32_BE;
      break;
    case JOP_BYTES_READ_I32_LE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_I32_LE;
      break;
    case JOP_BYTES_READ_I32_BE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_I32_BE;
      break;
    case JOP_BYTES_READ_F32_LE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_F32_LE;
      break;
    case JOP_BYTES_READ_F32_BE:
      out.op = JIR_BYTES_READ;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BREAD_F32_BE;
      break;
    case JOP_BYTES_WRITE_U16_LE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_U16_LE;
      break;
    case JOP_BYTES_WRITE_U16_BE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_U16_BE;
      break;
    case JOP_BYTES_WRITE_U32_LE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_U32_LE;
      break;
    case JOP_BYTES_WRITE_U32_BE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_U32_BE;
      break;
    case JOP_BYTES_WRITE_I32_LE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_I32_LE;
      break;
    case JOP_BYTES_WRITE_I32_BE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_I32_BE;
      break;
    case JOP_BYTES_WRITE_F32_LE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_F32_LE;
      break;
    case JOP_BYTES_WRITE_F32_BE:
      out.op = JIR_BYTES_WRITE;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      out.imm = JIT_BWRITE_F32_BE;
      break;
    case JOP_ARRAY_LEN:
      out.op = JIR_ARRAY_LEN;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_ARRAY_GET:
      out.op = JIR_ARRAY_GET;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      break;
    case JOP_ARRAY_SET:
      out.op = JIR_ARRAY_SET;
      out.a = ins->a;
      out.b = ins->b;
      out.c = ins->c;
      break;
    case JOP_ARRAY_NEW:
      out.op = JIR_ARRAY_NEW;
      out.a = ins->a;
      out.b = ins->b;
      break;
    case JOP_OBJ_GET_ATOM:
      out.op = JIR_OBJ_GET_ATOM;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_OBJ_SET_ATOM:
      out.op = JIR_OBJ_SET_ATOM;
      out.a = ins->a;
      out.b = ins->b;
      out.imm = (int32_t)ins->imm;
      break;
    case JOP_OBJ_NEW:
      out.op = JIR_OBJ_NEW;
      out.a = ins->a;
      break;
    case JOP_CALL: {
      uint32_t fi = ins->imm;
      /* Math.sqrt (native index 0): inline F64 sqrt. */
      if(fi == 0u && ins->c == 1u) {
        out.op = JIR_SQRT_F64;
        out.a = ins->a;
        out.b = ins->b;
        break;
      }
      if(!jello_is_native_builtin(fi) && !jello_is_jdll_prim(fi)) {
        uint32_t bidx = fi - JELLO_NATIVE_BUILTIN_COUNT;
        if(bidx < m->nfuncs && ins->c <= 16u && !func_needs_captures(m, bidx)) {
          if(&m->funcs[bidx] == f) {
            out.op = JIR_CALL_SELF;
            out.a = ins->a;
            out.b = ins->b;
            out.c = ins->c;
            ir->has_self_call = 1u;
            break;
          }
          out.op = JIR_CALL_DIRECT;
          out.a = ins->a;
          out.b = ins->b;
          out.c = ins->c;
          /* No funobj: high half 0xFFFF. */
          out.imm = (int32_t)((0xFFFFu << 16) | bidx);
          ir->has_direct_call = 1u;
          break;
        }
      }
      out.op = JIR_SLOW;
      out.imm = (int32_t)pc;
      ir->has_call_slow = 1u;
      ir->nslow++;
      break;
    }
    case JOP_CALLR: {
      /* Prove callee: local CONST_FUN/MOV, or capture of a bare CONST_FUN. */
      uint32_t callee_reg = ins->b;
      uint32_t first = ins->imm;
      uint32_t na = ins->c;
      int32_t bidx = prove_reg_bytecode_fun(m, f, callee_reg, pc);
      if(bidx < 0) bidx = prove_capture_callee_bidx(m, f, callee_reg);
      if(bidx >= 0 && na <= 16u && first <= 0xffffu && callee_reg <= 0xffffu) {
        if(&m->funcs[bidx] == f && !func_needs_captures(m, (uint32_t)bidx)) {
          out.op = JIR_CALL_SELF;
          out.a = ins->a;
          out.b = (uint16_t)first;
          out.c = (uint16_t)na;
          ir->has_self_call = 1u;
          break;
        }
        /* Pass callee_reg so runtime can install captures from the funobj. */
        out.op = JIR_CALL_DIRECT;
        out.a = ins->a;
        out.b = (uint16_t)first;
        out.c = (uint16_t)na;
        out.imm = (int32_t)((callee_reg << 16) | (uint32_t)bidx);
        ir->has_direct_call = 1u;
        break;
      }
      out.op = JIR_SLOW;
      out.imm = (int32_t)pc;
      ir->has_call_slow = 1u;
      ir->nslow++;
      break;
    }
    default:
      out.op = JIR_SLOW;
      out.imm = (int32_t)pc;
      if((jello_op)ins->op == JOP_TAILCALL || (jello_op)ins->op == JOP_TAILCALLR)
        ir->has_call_slow = 1u;
      ir->nslow++;
      break;
  }

  return append_ir(ir, out);
}

jello_jit_ir_func jello_jit_ir_build(const jello_bc_module* m, const jello_bc_function* f) {
  jello_jit_ir_func ir = {0};
  if(!m || !f) {
    ir.ok = 0;
    strcpy(ir.reject_reason, "null module/function");
    return ir;
  }
  ir.nregs = f->nregs;
  for(uint32_t pc = 0; pc < f->ninsns; pc++) {
    if((jello_op)f->insns[pc].op == JOP_TRY) {
      ir.ok = 0;
      strcpy(ir.reject_reason, "try not supported in jit v1");
      jello_jit_ir_func_free(&ir);
      return ir;
    }
  }
  for(uint32_t pc = 0; pc < f->ninsns; pc++) {
    if(lower_one(m, f, pc, &ir) != 0) {
      ir.ok = 0;
      strcpy(ir.reject_reason, "oom building ir");
      jello_jit_ir_func_free(&ir);
      return ir;
    }
  }
  /* Self-call: allow numeric + Function (ConstFun self ref). Reject heap. */
  if(ir.has_self_call) {
    for(uint32_t r = 0; r < f->nregs; r++) {
      jello_type_kind k = vm_reg_kind(m, f, r);
      if(k == JELLO_T_DYNAMIC || k == JELLO_T_BYTES || k == JELLO_T_LIST || k == JELLO_T_ARRAY ||
         k == JELLO_T_OBJECT || k == JELLO_T_ABSTRACT || k == JELLO_T_ENUM) {
        ir.ok = 0;
        strcpy(ir.reject_reason, "self-call with heap regs");
        jello_jit_ir_func_free(&ir);
        return ir;
      }
    }
  }
  ir.ok = 1;
  return ir;
}
