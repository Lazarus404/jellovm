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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
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
#include <stdio.h>

/*
  Bytecode instruction validation that depends on opcode semantics.

  Kept separate from the binary loader/parsing code to maintain separation of
  concerns: `loader.c` parses and owns memory; `check.c` validates instruction
  semantics against the typed register model.
*/

static size_t slot_size(const jello_bc_module* m, jello_type_id tid) {
  return jello_slot_size(m->types[tid].kind);
}

static jello_bc_result ok(void) {
  jello_bc_result r = { JELLO_BC_OK, "ok", 0 };
  return r;
}

static jello_bc_result err(jello_bc_error e, const char* msg, size_t off) {
  jello_bc_result r = { e, msg, off };
  return r;
}

static jello_type_kind rk(const jello_bc_module* m, const jello_type_id* reg_types, uint32_t r) {
  return m->types[reg_types[r]].kind;
}

jello_bc_result jello_bc_validate_insn(const jello_bc_module* m,
                                      const jello_type_id* reg_types,
                                      const jello_insn* ins,
                                      uint32_t nregs,
                                      uint32_t pc,
                                      uint32_t ninsns,
                                      uint32_t nfuncs) {
  const uint8_t op = ins->op;

  (void)ninsns;
  (void)nfuncs;

  // Most ops use ins->a as destination.
  switch((jello_op)op) {
    case JOP_NOP:
      return ok();
    case JOP_RET:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "ret reg out of range", 0);
      return ok();
    case JOP_MOV:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "mov reg out of range", 0);
      if(reg_types[ins->a] != reg_types[ins->b]) {
        jello_type_kind ka = rk(m, reg_types, ins->a);
        jello_type_kind kb = rk(m, reg_types, ins->b);
        size_t sa = slot_size(m, reg_types[ins->a]);
        size_t sb = slot_size(m, reg_types[ins->b]);
        /* Allow when kinds match (nominal subtypes). */
        if(ka == kb) {
          /* same kind, different type ids - ok */
        } else if(sa == sb && sa > 0) {
          /* Same slot size: raw copy. Used for phi-elim across type boundaries
           * (e.g. Bytes from different modules). Dynamic<->ptr uses same layout. */
        } else {
          return err(JELLO_BC_BAD_FORMAT, "mov type mismatch", 0);
        }
      }
      return ok();
    case JOP_JMP: {
      int32_t d = (int32_t)ins->imm;
      int32_t tgt = (int32_t)(pc + 1u) + d;
      if(tgt < 0 || tgt > (int32_t)ninsns) return err(JELLO_BC_BAD_FORMAT, "jmp target out of range", 0);
      return ok();
    }
    case JOP_JMP_IF: {
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "jmp_if cond reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "jmp_if cond must be bool", 0);
      int32_t d = (int32_t)ins->imm;
      int32_t tgt = (int32_t)(pc + 1u) + d;
      if(tgt < 0 || tgt > (int32_t)ninsns) return err(JELLO_BC_BAD_FORMAT, "jmp_if target out of range", 0);
      return ok();
    }
    case JOP_ASSERT:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "assert cond reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "assert cond must be bool", 0);
      return ok();
    case JOP_TRY: {
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "try reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_DYNAMIC) return err(JELLO_BC_BAD_FORMAT, "try dst must be Dynamic", 0);
      if(ins->b > 1) return err(JELLO_BC_BAD_FORMAT, "try b must be 0/1 (trap_only flag)", 0);
      int32_t d = (int32_t)ins->imm;
      int32_t tgt = (int32_t)(pc + 1u) + d;
      if(tgt < 0 || tgt > (int32_t)ninsns) return err(JELLO_BC_BAD_FORMAT, "try catch target out of range", 0);
      return ok();
    }
    case JOP_ENDTRY:
      return ok();
    case JOP_THROW:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "throw reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_DYNAMIC) return err(JELLO_BC_BAD_FORMAT, "throw payload must be Dynamic", 0);
      return ok();
    case JOP_CLOSURE: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "closure reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_FUNCTION) return err(JELLO_BC_BAD_FORMAT, "closure dst must be function", 0);
      /* Logical index: 0..NATIVE-1=native, NATIVE..NATIVE+nfuncs-1=bytecode */
      if(ins->imm >= JELLO_NATIVE_BUILTIN_COUNT + nfuncs) return err(JELLO_BC_BAD_FORMAT, "closure func index out of range", 0);
      uint32_t first = ins->b;
      uint32_t ncaps = ins->c;
      if(first + ncaps > nregs) return err(JELLO_BC_BAD_FORMAT, "closure capture range out of range", 0);
      return ok();
    }
    case JOP_BIND_THIS:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "bind_this reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_FUNCTION || rk(m, reg_types, ins->b) != JELLO_T_FUNCTION) return err(JELLO_BC_BAD_FORMAT, "bind_this requires function regs", 0);
      return ok();
    case JOP_CALL: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "call reg out of range", 0);
      /* Logical index: 0..NATIVE-1=native, NATIVE..NATIVE+nfuncs-1=bytecode */
      if(ins->imm >= JELLO_NATIVE_BUILTIN_COUNT + nfuncs) return err(JELLO_BC_BAD_FORMAT, "call func index out of range", 0);
      // args are in a contiguous range [b, b+c)
      uint32_t first = ins->b;
      uint32_t nargs = ins->c;
      if(first + nargs > nregs) return err(JELLO_BC_BAD_FORMAT, "call arg range out of range", 0);
      return ok();
    }
    case JOP_CALLR: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "callr reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_FUNCTION) return err(JELLO_BC_BAD_FORMAT, "callr callee must be function", 0);
      uint32_t first = ins->imm;
      uint32_t nargs = ins->c;
      if(first >= nregs) return err(JELLO_BC_BAD_FORMAT, "callr arg base out of range", 0);
      if(first + nargs > nregs) return err(JELLO_BC_BAD_FORMAT, "callr arg range out of range", 0);
      return ok();
    }
    case JOP_TAILCALL: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "tailcall reg out of range", 0);
      if(ins->imm >= JELLO_NATIVE_BUILTIN_COUNT + nfuncs) return err(JELLO_BC_BAD_FORMAT, "tailcall func index out of range", 0);
      uint32_t first = ins->b;
      uint32_t nargs = ins->c;
      if(first + nargs > nregs) return err(JELLO_BC_BAD_FORMAT, "tailcall arg range out of range", 0);
      return ok();
    }
    case JOP_TAILCALLR: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "tailcallr reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_FUNCTION) return err(JELLO_BC_BAD_FORMAT, "tailcallr callee must be function", 0);
      uint32_t first = ins->imm;
      uint32_t nargs = ins->c;
      if(first >= nregs) return err(JELLO_BC_BAD_FORMAT, "tailcallr arg base out of range", 0);
      if(first + nargs > nregs) return err(JELLO_BC_BAD_FORMAT, "tailcallr arg range out of range", 0);
      return ok();
    }
    case JOP_CONST_I32:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I8 &&
         rk(m, reg_types, ins->a) != JELLO_T_I16 &&
         rk(m, reg_types, ins->a) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "const_i32 dst must be i8/i16/i32", 0);
      // Range constraints for small ints (stored as i32 slots in the VM).
      if(rk(m, reg_types, ins->a) == JELLO_T_I8) {
        int32_t v = (int32_t)ins->imm;
        if(v < -128 || v > 127) return err(JELLO_BC_BAD_FORMAT, "const_i32 out of range for i8", 0);
      } else if(rk(m, reg_types, ins->a) == JELLO_T_I16) {
        int32_t v = (int32_t)ins->imm;
        if(v < -32768 || v > 32767) return err(JELLO_BC_BAD_FORMAT, "const_i32 out of range for i16", 0);
      }
      return ok();
    case JOP_CONST_I8_IMM:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I8) return err(JELLO_BC_BAD_FORMAT, "const_i8_imm dst must be i8", 0);
      return ok();
    case JOP_CONST_F16:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F16) return err(JELLO_BC_BAD_FORMAT, "const_f16 dst must be f16", 0);
      return ok();
    case JOP_CONST_BOOL:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "const_bool dst must be bool", 0);
      if(ins->c > 1) return err(JELLO_BC_BAD_FORMAT, "const_bool imm must be 0/1", 0);
      return ok();
    case JOP_CONST_NULL:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_DYNAMIC) return err(JELLO_BC_BAD_FORMAT, "const_null dst must be Dynamic", 0);
      return ok();
    case JOP_CONST_ATOM:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_ATOM) return err(JELLO_BC_BAD_FORMAT, "const_atom dst must be atom", 0);
      if(ins->imm >= m->natoms) return err(JELLO_BC_BAD_FORMAT, "const_atom atom id out of range", 0);
      return ok();
    case JOP_CONST_F32:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "const_f32 dst must be f32", 0);
      return ok();
    case JOP_CONST_I64:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "const_i64 dst must be i64", 0);
      if(ins->imm >= m->nconst_i64) return err(JELLO_BC_BAD_FORMAT, "const_i64 pool index out of range", 0);
      return ok();
    case JOP_CONST_F64:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "const_f64 dst must be f64", 0);
      if(ins->imm >= m->nconst_f64) return err(JELLO_BC_BAD_FORMAT, "const_f64 pool index out of range", 0);
      return ok();
    case JOP_CONST_BYTES:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BYTES) return err(JELLO_BC_BAD_FORMAT, "const_bytes dst must be bytes", 0);
      if(ins->imm >= m->nconst_bytes) return err(JELLO_BC_BAD_FORMAT, "const_bytes pool index out of range", 0);
      return ok();
    case JOP_CONST_FUN:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "const reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_FUNCTION) return err(JELLO_BC_BAD_FORMAT, "const_fun dst must be function", 0);
      /* Logical index: 0..NATIVE-1=native, NATIVE..NATIVE+nfuncs-1=bytecode */
      if(ins->imm >= JELLO_NATIVE_BUILTIN_COUNT + nfuncs) return err(JELLO_BC_BAD_FORMAT, "const_fun func index out of range", 0);
      return ok();
    case JOP_ADD_I32:
    case JOP_SUB_I32:
    case JOP_MUL_I32:
    case JOP_MOD_I32:
    case JOP_SHL_I32:
    case JOP_SHR_I32:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "arith reg out of range", 0);
      if((rk(m, reg_types, ins->a) != JELLO_T_I8 && rk(m, reg_types, ins->a) != JELLO_T_I16 && rk(m, reg_types, ins->a) != JELLO_T_I32) ||
         (rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32) ||
         (rk(m, reg_types, ins->c) != JELLO_T_I8 && rk(m, reg_types, ins->c) != JELLO_T_I16 && rk(m, reg_types, ins->c) != JELLO_T_I32))
        return err(JELLO_BC_BAD_FORMAT, "arith i8/i16/i32 types required", 0);
      return ok();
    case JOP_ADD_I32_IMM:
    case JOP_SUB_I32_IMM:
    case JOP_MUL_I32_IMM:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "arith_imm reg out of range", 0);
      if((rk(m, reg_types, ins->a) != JELLO_T_I8 && rk(m, reg_types, ins->a) != JELLO_T_I16 && rk(m, reg_types, ins->a) != JELLO_T_I32) ||
         (rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32))
        return err(JELLO_BC_BAD_FORMAT, "arith_imm i8/i16/i32 types required", 0);
      return ok();
    case JOP_ADD_I64:
    case JOP_SUB_I64:
    case JOP_MUL_I64:
    case JOP_MOD_I64:
    case JOP_SHL_I64:
    case JOP_SHR_I64:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "arith reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I64 ||
         rk(m, reg_types, ins->b) != JELLO_T_I64 ||
         rk(m, reg_types, ins->c) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "arith i64 types required", 0);
      return ok();
    case JOP_ADD_F32:
    case JOP_SUB_F32:
    case JOP_MUL_F32:
    case JOP_DIV_F32:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "arith reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32 ||
         rk(m, reg_types, ins->b) != JELLO_T_F32 ||
         rk(m, reg_types, ins->c) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "arith f32 types required", 0);
      return ok();
    case JOP_ADD_F16:
    case JOP_SUB_F16:
    case JOP_MUL_F16:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "arith reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F16 ||
         rk(m, reg_types, ins->b) != JELLO_T_F16 ||
         rk(m, reg_types, ins->c) != JELLO_T_F16) return err(JELLO_BC_BAD_FORMAT, "arith f16 types required", 0);
      return ok();
    case JOP_ADD_F64:
    case JOP_SUB_F64:
    case JOP_MUL_F64:
    case JOP_DIV_F64:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "arith reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F64 ||
         rk(m, reg_types, ins->b) != JELLO_T_F64 ||
         rk(m, reg_types, ins->c) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "arith f64 types required", 0);
      return ok();
    case JOP_SEXT_I64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "sext_i64 dst must be i64", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32)
        return err(JELLO_BC_BAD_FORMAT, "sext_i64 src must be i8/i16/i32", 0);
      return ok();
    case JOP_SEXT_I16:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I16 || rk(m, reg_types, ins->b) != JELLO_T_I8)
        return err(JELLO_BC_BAD_FORMAT, "sext_i16 dst must be i16, src i8", 0);
      return ok();
    case JOP_TRUNC_I8:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I8) return err(JELLO_BC_BAD_FORMAT, "trunc_i8 dst must be i8", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32)
        return err(JELLO_BC_BAD_FORMAT, "trunc_i8 src must be i16 or i32", 0);
      return ok();
    case JOP_TRUNC_I16:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I16 || rk(m, reg_types, ins->b) != JELLO_T_I32)
        return err(JELLO_BC_BAD_FORMAT, "trunc_i16 dst must be i16, src i32", 0);
      return ok();
    case JOP_I32_FROM_I64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "i32_from_i64 types required", 0);
      return ok();
    case JOP_F64_FROM_I32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F64 || rk(m, reg_types, ins->b) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "f64_from_i32 types required", 0);
      return ok();
    case JOP_I32_FROM_F64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "i32_from_f64 types required", 0);
      return ok();
    case JOP_F64_FROM_I64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F64 || rk(m, reg_types, ins->b) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "f64_from_i64 types required", 0);
      return ok();
    case JOP_I64_FROM_F64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I64 || rk(m, reg_types, ins->b) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "i64_from_f64 types required", 0);
      return ok();
    case JOP_F32_FROM_I32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32 || rk(m, reg_types, ins->b) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "f32_from_i32 types required", 0);
      return ok();
    case JOP_I32_FROM_F32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "i32_from_f32 types required", 0);
      return ok();
    case JOP_F64_FROM_F32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F64 || rk(m, reg_types, ins->b) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "f64_from_f32 types required", 0);
      return ok();
    case JOP_F32_FROM_F64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32 || rk(m, reg_types, ins->b) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "f32_from_f64 types required", 0);
      return ok();
    case JOP_F16_FROM_F32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F16 || rk(m, reg_types, ins->b) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "f16_from_f32 types required", 0);
      return ok();
    case JOP_F32_FROM_F16:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32 || rk(m, reg_types, ins->b) != JELLO_T_F16) return err(JELLO_BC_BAD_FORMAT, "f32_from_f16 types required", 0);
      return ok();
    case JOP_F16_FROM_I32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F16 || rk(m, reg_types, ins->b) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "f16_from_i32 types required", 0);
      return ok();
    case JOP_I32_FROM_F16:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_F16) return err(JELLO_BC_BAD_FORMAT, "i32_from_f16 types required", 0);
      return ok();
    case JOP_F32_FROM_I64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32 || rk(m, reg_types, ins->b) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "f32_from_i64 types required", 0);
      return ok();
    case JOP_I64_FROM_F32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I64 || rk(m, reg_types, ins->b) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "i64_from_f32 types required", 0);
      return ok();
    case JOP_EQ_I32:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "eq reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "eq_i32 dst must be bool", 0);
      if((rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32) ||
         (rk(m, reg_types, ins->c) != JELLO_T_I8 && rk(m, reg_types, ins->c) != JELLO_T_I16 && rk(m, reg_types, ins->c) != JELLO_T_I32))
        return err(JELLO_BC_BAD_FORMAT, "eq_i32 operands must be i8/i16/i32", 0);
      return ok();
    case JOP_EQ_I32_IMM:
    case JOP_LT_I32_IMM:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "eq/lt_imm reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "eq/lt_imm dst must be bool", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32)
        return err(JELLO_BC_BAD_FORMAT, "eq/lt_imm src must be i8/i16/i32", 0);
      return ok();
    case JOP_EQ_I64:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "eq reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL ||
         rk(m, reg_types, ins->b) != JELLO_T_I64 ||
         rk(m, reg_types, ins->c) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "eq_i64 types required", 0);
      return ok();
    case JOP_LT_I64:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "lt reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL ||
         rk(m, reg_types, ins->b) != JELLO_T_I64 ||
         rk(m, reg_types, ins->c) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "lt_i64 types required", 0);
      return ok();
    case JOP_EQ_F32:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "eq reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL ||
         rk(m, reg_types, ins->b) != JELLO_T_F32 ||
         rk(m, reg_types, ins->c) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "eq_f32 types required", 0);
      return ok();
    case JOP_LT_F32:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "lt reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL ||
         rk(m, reg_types, ins->b) != JELLO_T_F32 ||
         rk(m, reg_types, ins->c) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "lt_f32 types required", 0);
      return ok();
    case JOP_EQ_F64:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "eq reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL ||
         rk(m, reg_types, ins->b) != JELLO_T_F64 ||
         rk(m, reg_types, ins->c) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "eq_f64 types required", 0);
      return ok();
    case JOP_LT_F64:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "lt reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL ||
         rk(m, reg_types, ins->b) != JELLO_T_F64 ||
         rk(m, reg_types, ins->c) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "lt_f64 types required", 0);
      return ok();
    case JOP_TO_DYN:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_DYNAMIC) return err(JELLO_BC_BAD_FORMAT, "to_dyn dst must be Dynamic", 0);
      return ok();
    case JOP_FROM_DYN_I8:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_I8) return err(JELLO_BC_BAD_FORMAT, "from_dyn_i8 types required", 0);
      return ok();
    case JOP_FROM_DYN_I16:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_I16) return err(JELLO_BC_BAD_FORMAT, "from_dyn_i16 types required", 0);
      return ok();
    case JOP_FROM_DYN_I32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "from_dyn_i32 types required", 0);
      return ok();
    case JOP_FROM_DYN_I64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "from_dyn_i64 types required", 0);
      return ok();
    case JOP_FROM_DYN_F16:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_F16) return err(JELLO_BC_BAD_FORMAT, "from_dyn_f16 types required", 0);
      return ok();
    case JOP_FROM_DYN_F32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "from_dyn_f32 types required", 0);
      return ok();
    case JOP_FROM_DYN_F64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "from_dyn_f64 types required", 0);
      return ok();
    case JOP_FROM_DYN_BOOL:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "from_dyn_bool types required", 0);
      return ok();
    case JOP_FROM_DYN_ATOM:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC || rk(m, reg_types, ins->a) != JELLO_T_ATOM) return err(JELLO_BC_BAD_FORMAT, "from_dyn_atom types required", 0);
      return ok();
    case JOP_FROM_DYN_PTR: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "dyn conv reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC) return err(JELLO_BC_BAD_FORMAT, "from_dyn_ptr src must be Dynamic", 0);
      jello_type_kind dk = rk(m, reg_types, ins->a);
      switch(dk) {
        case JELLO_T_BYTES:
        case JELLO_T_FUNCTION:
        case JELLO_T_LIST:
        case JELLO_T_ARRAY:
        case JELLO_T_OBJECT:
        case JELLO_T_ABSTRACT:
          return ok();
        default:
          return err(JELLO_BC_BAD_FORMAT, "from_dyn_ptr dst must be pointer-kind", 0);
      }
    }
    case JOP_SPILL_PUSH:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "spill push reg out of range", 0);
      return ok();
    case JOP_SPILL_POP:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "spill pop reg out of range", 0);
      return ok();
    case JOP_NEG_I32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "neg reg out of range", 0);
      if((rk(m, reg_types, ins->a) != JELLO_T_I8 && rk(m, reg_types, ins->a) != JELLO_T_I16 && rk(m, reg_types, ins->a) != JELLO_T_I32) ||
         (rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32))
        return err(JELLO_BC_BAD_FORMAT, "neg_i32 types must be i8/i16/i32", 0);
      return ok();
    case JOP_NEG_I64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "neg reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I64 || rk(m, reg_types, ins->b) != JELLO_T_I64) return err(JELLO_BC_BAD_FORMAT, "neg_i64 types required", 0);
      return ok();
    case JOP_NEG_F32:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "neg reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F32 || rk(m, reg_types, ins->b) != JELLO_T_F32) return err(JELLO_BC_BAD_FORMAT, "neg_f32 types required", 0);
      return ok();
    case JOP_NEG_F64:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "neg reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_F64 || rk(m, reg_types, ins->b) != JELLO_T_F64) return err(JELLO_BC_BAD_FORMAT, "neg_f64 types required", 0);
      return ok();
    case JOP_NOT_BOOL:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "not reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL || rk(m, reg_types, ins->b) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "not_bool types required", 0);
      return ok();
    case JOP_PHYSEQ:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "physeq reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "physeq dst must be bool", 0);
      return ok();
    case JOP_LT_I32:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "lt_i32 reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL) return err(JELLO_BC_BAD_FORMAT, "lt_i32 dst must be bool", 0);
      if((rk(m, reg_types, ins->b) != JELLO_T_I8 && rk(m, reg_types, ins->b) != JELLO_T_I16 && rk(m, reg_types, ins->b) != JELLO_T_I32) ||
         (rk(m, reg_types, ins->c) != JELLO_T_I8 && rk(m, reg_types, ins->c) != JELLO_T_I16 && rk(m, reg_types, ins->c) != JELLO_T_I32))
        return err(JELLO_BC_BAD_FORMAT, "lt_i32 operands must be i8/i16/i32", 0);
      return ok();
    case JOP_KINDOF:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "kindof reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_DYNAMIC) return err(JELLO_BC_BAD_FORMAT, "kindof types required", 0);
      return ok();
    case JOP_SWITCH_KIND: {
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "switch_kind reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "switch_kind src must be i32", 0);
      uint32_t ncases = (uint32_t)ins->b;
      if(pc + 1u + ncases > ninsns) return err(JELLO_BC_BAD_FORMAT, "switch_kind case table out of range", 0);
      // Full table-shape + delta validation is done in jello_bc_validate_function_semantics().
      return ok();
    }
    case JOP_CASE_KIND:
      // Data-only instruction; validated in jello_bc_validate_function_semantics().
      return ok();
    case JOP_LIST_NIL:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "list nil reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_LIST) return err(JELLO_BC_BAD_FORMAT, "list_nil dst must be list", 0);
      return ok();
    case JOP_LIST_CONS:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "list cons reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_LIST) return err(JELLO_BC_BAD_FORMAT, "list_cons dst must be list", 0);
      if(rk(m, reg_types, ins->c) != JELLO_T_LIST) return err(JELLO_BC_BAD_FORMAT, "list_cons tail must be list", 0);
      if(reg_types[ins->a] != reg_types[ins->c]) return err(JELLO_BC_BAD_FORMAT, "list_cons list type mismatch", 0);
      if(m->types[reg_types[ins->a]].as.unary.elem != reg_types[ins->b]) return err(JELLO_BC_BAD_FORMAT, "list_cons head type mismatch", 0);
      return ok();
    case JOP_LIST_HEAD:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "list head reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_LIST) return err(JELLO_BC_BAD_FORMAT, "list_head src must be list", 0);
      if(m->types[reg_types[ins->b]].as.unary.elem != reg_types[ins->a]) return err(JELLO_BC_BAD_FORMAT, "list_head dst type mismatch", 0);
      return ok();
    case JOP_LIST_TAIL:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "list tail reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_LIST || rk(m, reg_types, ins->b) != JELLO_T_LIST) return err(JELLO_BC_BAD_FORMAT, "list_tail types required", 0);
      if(reg_types[ins->a] != reg_types[ins->b]) return err(JELLO_BC_BAD_FORMAT, "list_tail list type mismatch", 0);
      return ok();
    case JOP_LIST_IS_NIL:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "list is_nil reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL || rk(m, reg_types, ins->b) != JELLO_T_LIST) return err(JELLO_BC_BAD_FORMAT, "list_is_nil types required", 0);
      return ok();
    case JOP_ARRAY_NEW:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "array new reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_ARRAY || rk(m, reg_types, ins->b) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "array_new types required", 0);
      return ok();
    case JOP_ARRAY_LEN:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "array len reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_ARRAY) return err(JELLO_BC_BAD_FORMAT, "array_len types required", 0);
      return ok();
    case JOP_ARRAY_GET:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "array get reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_ARRAY || rk(m, reg_types, ins->c) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "array_get types required", 0);
      if(m->types[reg_types[ins->b]].as.unary.elem != reg_types[ins->a]) return err(JELLO_BC_BAD_FORMAT, "array_get dst type mismatch", 0);
      return ok();
    case JOP_ARRAY_SET:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "array set reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_ARRAY || rk(m, reg_types, ins->c) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "array_set types required", 0);
      if(m->types[reg_types[ins->b]].as.unary.elem != reg_types[ins->a]) return err(JELLO_BC_BAD_FORMAT, "array_set src type mismatch", 0);
      return ok();
    case JOP_BYTES_NEW:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "bytes new reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BYTES || rk(m, reg_types, ins->b) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "bytes_new types required", 0);
      return ok();
    case JOP_BYTES_LEN:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "bytes len reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_BYTES) return err(JELLO_BC_BAD_FORMAT, "bytes_len types required", 0);
      return ok();
    case JOP_BYTES_GET_U8:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "bytes get_u8 reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_BYTES || rk(m, reg_types, ins->c) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "bytes_get_u8 types required", 0);
      return ok();
    case JOP_BYTES_SET_U8:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "bytes set_u8 reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_I32 || rk(m, reg_types, ins->b) != JELLO_T_BYTES || rk(m, reg_types, ins->c) != JELLO_T_I32) return err(JELLO_BC_BAD_FORMAT, "bytes_set_u8 types required", 0);
      return ok();
    case JOP_BYTES_CONCAT2:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "bytes concat2 reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BYTES ||
         rk(m, reg_types, ins->b) != JELLO_T_BYTES ||
         rk(m, reg_types, ins->c) != JELLO_T_BYTES) return err(JELLO_BC_BAD_FORMAT, "bytes_concat2 types required", 0);
      return ok();
    case JOP_BYTES_CONCAT_MANY: {
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "bytes concat_many reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BYTES) return err(JELLO_BC_BAD_FORMAT, "bytes_concat_many dst must be bytes", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_ARRAY) return err(JELLO_BC_BAD_FORMAT, "bytes_concat_many src must be array", 0);
      jello_type_id arr_tid = reg_types[ins->b];
      jello_type_id elem_tid = m->types[arr_tid].as.unary.elem;
      if(m->types[elem_tid].kind != JELLO_T_BYTES) return err(JELLO_BC_BAD_FORMAT, "bytes_concat_many requires Array<bytes>", 0);
      return ok();
    }
    case JOP_OBJ_NEW:
      if(ins->a >= nregs) return err(JELLO_BC_BAD_FORMAT, "obj new reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_OBJECT) return err(JELLO_BC_BAD_FORMAT, "obj_new dst must be object", 0);
      return ok();
    case JOP_OBJ_HAS_ATOM:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "obj has reg out of range", 0);
      if(rk(m, reg_types, ins->a) != JELLO_T_BOOL || rk(m, reg_types, ins->b) != JELLO_T_OBJECT) return err(JELLO_BC_BAD_FORMAT, "obj_has types required", 0);
      if(ins->imm >= m->natoms) return err(JELLO_BC_BAD_FORMAT, "obj_has atom id out of range", 0);
      return ok();
    case JOP_OBJ_GET_ATOM:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "obj get reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_OBJECT) return err(JELLO_BC_BAD_FORMAT, "obj_get src must be object", 0);
      if(ins->imm >= m->natoms) return err(JELLO_BC_BAD_FORMAT, "obj_get atom id out of range", 0);
      return ok();
    case JOP_OBJ_SET_ATOM:
      if(ins->a >= nregs || ins->b >= nregs) return err(JELLO_BC_BAD_FORMAT, "obj set reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_OBJECT) return err(JELLO_BC_BAD_FORMAT, "obj_set dst must be object", 0);
      if(ins->imm >= m->natoms) return err(JELLO_BC_BAD_FORMAT, "obj_set atom id out of range", 0);
      return ok();
    case JOP_OBJ_GET:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "obj get reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_OBJECT) return err(JELLO_BC_BAD_FORMAT, "obj_get src must be object", 0);
      if(rk(m, reg_types, ins->c) != JELLO_T_ATOM) return err(JELLO_BC_BAD_FORMAT, "obj_get key must be atom", 0);
      return ok();
    case JOP_OBJ_SET:
      if(ins->a >= nregs || ins->b >= nregs || ins->c >= nregs) return err(JELLO_BC_BAD_FORMAT, "obj set reg out of range", 0);
      if(rk(m, reg_types, ins->b) != JELLO_T_OBJECT) return err(JELLO_BC_BAD_FORMAT, "obj_set dst must be object", 0);
      if(rk(m, reg_types, ins->c) != JELLO_T_ATOM) return err(JELLO_BC_BAD_FORMAT, "obj_set key must be atom", 0);
      return ok();
    default:
      return err(JELLO_BC_BAD_FORMAT, "unknown opcode", 0);
  }
}

static jello_bc_result validate_switch_kind_tables(const jello_bc_module* m,
                                                   const jello_type_id* reg_types,
                                                   const jello_insn* insns,
                                                   uint32_t nregs,
                                                   uint32_t ninsns) {
  (void)m;
  (void)reg_types;
  (void)nregs;

  for(uint32_t pc = 0; pc < ninsns; pc++) {
    const jello_insn* ins = &insns[pc];
    if((jello_op)ins->op == JOP_SWITCH_KIND) {
      uint32_t ncases = (uint32_t)ins->b;
      uint32_t table_first = pc + 1u;
      uint32_t table_end = table_first + ncases;
      if(table_end > ninsns) return err(JELLO_BC_BAD_FORMAT, "switch_kind case table out of range", 0);

      // default delta is relative to end-of-table.
      int32_t dd = (int32_t)ins->imm;
      int32_t dtgt = (int32_t)table_end + dd;
      if(dtgt < 0 || dtgt > (int32_t)ninsns) return err(JELLO_BC_BAD_FORMAT, "switch_kind default target out of range", 0);

      for(uint32_t i = 0; i < ncases; i++) {
        const jello_insn* ci = &insns[table_first + i];
        if((jello_op)ci->op != JOP_CASE_KIND) return err(JELLO_BC_BAD_FORMAT, "switch_kind expects case_kind entries", 0);
        int32_t cd = (int32_t)ci->imm;
        int32_t ctgt = (int32_t)table_end + cd;
        if(ctgt < 0 || ctgt > (int32_t)ninsns) return err(JELLO_BC_BAD_FORMAT, "case_kind target out of range", 0);
      }

      // Skip validating the interior entries again.
      pc = table_end - 1u;
      continue;
    }

    if((jello_op)ins->op == JOP_CASE_KIND) {
      // A case_kind may only appear as part of a switch_kind table.
      return err(JELLO_BC_BAD_FORMAT, "case_kind without switch_kind", 0);
    }
  }

  return ok();
}

static jello_bc_result validate_try_stack_linear(const jello_bc_module* m,
                                                const jello_type_id* reg_types,
                                                const jello_insn* insns,
                                                uint32_t nregs,
                                                uint32_t ninsns) {
  (void)m;
  (void)reg_types;
  (void)nregs;
  uint32_t depth = 0;
  for(uint32_t pc = 0; pc < ninsns; pc++) {
    const jello_insn* ins = &insns[pc];
    if((jello_op)ins->op == JOP_TRY) {
      depth++;
      continue;
    }
    if((jello_op)ins->op == JOP_ENDTRY) {
      if(depth == 0) return err(JELLO_BC_BAD_FORMAT, "endtry without try (linear)", 0);
      depth--;
      continue;
    }
  }
  if(depth != 0) return err(JELLO_BC_BAD_FORMAT, "unterminated try (linear)", 0);
  return ok();
}

jello_bc_result jello_bc_validate_function_semantics(const jello_bc_module* m,
                                                    const jello_type_id* reg_types,
                                                    const jello_insn* insns,
                                                    uint32_t nregs,
                                                    uint32_t ninsns) {
  jello_bc_result r = validate_switch_kind_tables(m, reg_types, insns, nregs, ninsns);
  if(r.err) return r;
  r = validate_try_stack_linear(m, reg_types, insns, nregs, ninsns);
  if(r.err) return r;
  return ok();
}

