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

#include <jello.h>
#include <jello/internal.h>

#include <stdlib.h>
#include <string.h>

/* Opcode names for profile dump. Index = jello_op value. */
static const char* const op_names[JOP_COUNT] = {
  [JOP_NOP] = "nop",
  [JOP_RET] = "ret",
  [JOP_MOV] = "mov",
  [JOP_CALL] = "call",
  [JOP_CALLR] = "callr",
  [JOP_CONST_FUN] = "const_fun",
  [JOP_CLOSURE] = "closure",
  [JOP_BIND_THIS] = "bind_this",
  [JOP_CONST_I32] = "const_i32",
  [JOP_CONST_I8_IMM] = "const_i8_imm",
  [JOP_CONST_BOOL] = "const_bool",
  [JOP_CONST_NULL] = "const_null",
  [JOP_CONST_ATOM] = "const_atom",
  [JOP_CONST_F16] = "const_f16",
  [JOP_CONST_F32] = "const_f32",
  [JOP_CONST_I64] = "const_i64",
  [JOP_CONST_F64] = "const_f64",
  [JOP_CONST_BYTES] = "const_bytes",
  [JOP_BYTES_CONCAT2] = "bytes_concat2",
  [JOP_BYTES_CONCAT_MANY] = "bytes_concat_many",
  [JOP_JMP] = "jmp",
  [JOP_JMP_IF] = "jmp_if",
  [JOP_TRY] = "try",
  [JOP_ENDTRY] = "endtry",
  [JOP_THROW] = "throw",
  [JOP_ASSERT] = "assert",
  [JOP_ADD_I32] = "add_i32",
  [JOP_SUB_I32] = "sub_i32",
  [JOP_MUL_I32] = "mul_i32",
  [JOP_DIV_I32] = "div_i32",
  [JOP_ADD_I32_IMM] = "add_i32_imm",
  [JOP_SUB_I32_IMM] = "sub_i32_imm",
  [JOP_MUL_I32_IMM] = "mul_i32_imm",
  [JOP_ADD_I64] = "add_i64",
  [JOP_SUB_I64] = "sub_i64",
  [JOP_MUL_I64] = "mul_i64",
  [JOP_DIV_I64] = "div_i64",
  [JOP_MOD_I32] = "mod_i32",
  [JOP_MOD_I64] = "mod_i64",
  [JOP_SHL_I32] = "shl_i32",
  [JOP_SHL_I64] = "shl_i64",
  [JOP_SHR_I32] = "shr_i32",
  [JOP_SHR_I64] = "shr_i64",
  [JOP_ADD_F16] = "add_f16",
  [JOP_SUB_F16] = "sub_f16",
  [JOP_MUL_F16] = "mul_f16",
  [JOP_ADD_F32] = "add_f32",
  [JOP_SUB_F32] = "sub_f32",
  [JOP_MUL_F32] = "mul_f32",
  [JOP_DIV_F32] = "div_f32",
  [JOP_ADD_F64] = "add_f64",
  [JOP_SUB_F64] = "sub_f64",
  [JOP_MUL_F64] = "mul_f64",
  [JOP_DIV_F64] = "div_f64",
  [JOP_NEG_I32] = "neg_i32",
  [JOP_NEG_I64] = "neg_i64",
  [JOP_NEG_F32] = "neg_f32",
  [JOP_NEG_F64] = "neg_f64",
  [JOP_NOT_BOOL] = "not_bool",
  [JOP_EQ_I32] = "eq_i32",
  [JOP_LT_I32] = "lt_i32",
  [JOP_EQ_I32_IMM] = "eq_i32_imm",
  [JOP_LT_I32_IMM] = "lt_i32_imm",
  [JOP_EQ_I64] = "eq_i64",
  [JOP_EQ_F32] = "eq_f32",
  [JOP_EQ_F64] = "eq_f64",
  [JOP_TO_DYN] = "to_dyn",
  [JOP_FROM_DYN_I8] = "from_dyn_i8",
  [JOP_FROM_DYN_I16] = "from_dyn_i16",
  [JOP_FROM_DYN_I32] = "from_dyn_i32",
  [JOP_FROM_DYN_I64] = "from_dyn_i64",
  [JOP_FROM_DYN_F16] = "from_dyn_f16",
  [JOP_FROM_DYN_F32] = "from_dyn_f32",
  [JOP_FROM_DYN_F64] = "from_dyn_f64",
  [JOP_FROM_DYN_BOOL] = "from_dyn_bool",
  [JOP_FROM_DYN_ATOM] = "from_dyn_atom",
  [JOP_FROM_DYN_PTR] = "from_dyn_ptr",
  [JOP_SPILL_PUSH] = "spill_push",
  [JOP_SPILL_POP] = "spill_pop",
  [JOP_SEXT_I64] = "sext_i64",
  [JOP_I32_FROM_I64] = "i32_from_i64",
  [JOP_F64_FROM_I32] = "f64_from_i32",
  [JOP_I32_FROM_F64] = "i32_from_f64",
  [JOP_F64_FROM_I64] = "f64_from_i64",
  [JOP_I64_FROM_F64] = "i64_from_f64",
  [JOP_F32_FROM_I32] = "f32_from_i32",
  [JOP_I32_FROM_F32] = "i32_from_f32",
  [JOP_F64_FROM_F32] = "f64_from_f32",
  [JOP_F32_FROM_F64] = "f32_from_f64",
  [JOP_F32_FROM_I64] = "f32_from_i64",
  [JOP_I64_FROM_F32] = "i64_from_f32",
  [JOP_SEXT_I16] = "sext_i16",
  [JOP_TRUNC_I8] = "trunc_i8",
  [JOP_TRUNC_I16] = "trunc_i16",
  [JOP_F16_FROM_F32] = "f16_from_f32",
  [JOP_F32_FROM_F16] = "f32_from_f16",
  [JOP_F16_FROM_I32] = "f16_from_i32",
  [JOP_I32_FROM_F16] = "i32_from_f16",
  [JOP_PHYSEQ] = "physeq",
  [JOP_KINDOF] = "kindof",
  [JOP_SWITCH_KIND] = "switch_kind",
  [JOP_CASE_KIND] = "case_kind",
  [JOP_LIST_NIL] = "list_nil",
  [JOP_LIST_CONS] = "list_cons",
  [JOP_LIST_HEAD] = "list_head",
  [JOP_LIST_TAIL] = "list_tail",
  [JOP_LIST_IS_NIL] = "list_is_nil",
  [JOP_ARRAY_NEW] = "array_new",
  [JOP_ARRAY_LEN] = "array_len",
  [JOP_ARRAY_GET] = "array_get",
  [JOP_ARRAY_SET] = "array_set",
  [JOP_BYTES_NEW] = "bytes_new",
  [JOP_BYTES_LEN] = "bytes_len",
  [JOP_BYTES_GET_U8] = "bytes_get_u8",
  [JOP_BYTES_SET_U8] = "bytes_set_u8",
  [JOP_OBJ_NEW] = "obj_new",
  [JOP_OBJ_HAS_ATOM] = "obj_has_atom",
  [JOP_OBJ_GET_ATOM] = "obj_get_atom",
  [JOP_OBJ_SET_ATOM] = "obj_set_atom",
  [JOP_OBJ_GET] = "obj_get",
  [JOP_OBJ_SET] = "obj_set",
  [JOP_LT_I64] = "lt_i64",
  [JOP_LT_F32] = "lt_f32",
  [JOP_LT_F64] = "lt_f64",
  [JOP_TAILCALL] = "tailcall",
  [JOP_TAILCALLR] = "tailcallr",
};

void jello_vm_set_profile(jello_vm* vm, uint8_t enable) {
  if(!vm) return;
  if(enable) {
    if(!vm->op_counts) {
      vm->op_counts = (uint64_t*)calloc(JOP_COUNT, sizeof(uint64_t));
      if(!vm->op_counts) return;
    }
    vm->profile_enabled = 1;
  } else {
    vm->profile_enabled = 0;
    /* Keep op_counts allocated so we can dump after exec. */
  }
}

void jello_vm_profile_dump(const jello_vm* vm, FILE* out) {
  if(!vm || !out || !vm->op_counts) return;
  uint64_t total = 0;
  for(uint32_t i = 0; i < JOP_COUNT; i++) total += vm->op_counts[i];
  if(total == 0) return;

  /* Build sorted list: (op, count) by count descending. */
  typedef struct { uint32_t op; uint64_t count; } op_count_t;
  op_count_t sorted[JOP_COUNT];
  uint32_t n = 0;
  for(uint32_t i = 0; i < JOP_COUNT; i++) {
    if(vm->op_counts[i] > 0) {
      sorted[n].op = i;
      sorted[n].count = vm->op_counts[i];
      n++;
    }
  }
  /* Simple bubble sort by count (n is small, typically < 50). */
  for(uint32_t i = 0; i < n; i++) {
    for(uint32_t j = i + 1; j < n; j++) {
      if(sorted[j].count > sorted[i].count) {
        op_count_t t = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = t;
      }
    }
  }

  fprintf(out, "JELLO_PROFILE opcodes (total=%" PRIu64 "):\n", total);
  uint32_t show = n > 20 ? 20u : n;
  for(uint32_t i = 0; i < show; i++) {
    uint32_t op = sorted[i].op;
    uint64_t c = sorted[i].count;
    double pct = total > 0 ? (100.0 * (double)c / (double)total) : 0.0;
    const char* name = (op < JOP_COUNT && op_names[op]) ? op_names[op] : "?";
    fprintf(out, "  %6" PRIu64 " %5.1f%%  %s\n", c, pct, name);
  }
  if(n > show) fprintf(out, "  ... and %u more\n", n - show);

#if defined(JELLOVM_PROFILE)
  /* Call/frame metrics (function calls, closures, recursion). */
  uint64_t push = vm->profile_push_frame;
  uint64_t repl = vm->profile_replace_frame;
  uint64_t push_fast = vm->profile_push_fast_arg;
  uint64_t repl_fast = vm->profile_replace_fast;
  uint64_t repl_slow = vm->profile_replace_slow;
  uint64_t stack_alloc = vm->profile_frame_stack_alloc;
  uint64_t pool_alloc = vm->profile_frame_pool_alloc;
  uint64_t calls_total = push + repl;
  if(calls_total > 0 || stack_alloc > 0) {
    fprintf(out, "JELLO_PROFILE calls/frames:\n");
    fprintf(out, "  push_frame=%" PRIu64 "  replace_frame=%" PRIu64 "  total_calls=%" PRIu64 "\n",
            push, repl, calls_total);
    if(push > 0)
      fprintf(out, "  push_fast_arg=%.1f%% (%" PRIu64 "/%" PRIu64 ")\n",
              100.0 * (double)push_fast / (double)push, push_fast, push);
    if(repl > 0)
      fprintf(out, "  replace_fast=%.1f%%  replace_slow=%.1f%%\n",
              100.0 * (double)repl_fast / (double)repl,
              100.0 * (double)repl_slow / (double)repl);
    fprintf(out, "  frame_stack_alloc=%" PRIu64 "  frame_pool_alloc=%" PRIu64 "\n",
            stack_alloc, pool_alloc);
    /* Opcode-level call summary. */
    uint64_t call_op = vm->op_counts[JOP_CALL] + vm->op_counts[JOP_CALLR];
    uint64_t ret_op = vm->op_counts[JOP_RET];
    uint64_t tail_op = vm->op_counts[JOP_TAILCALL] + vm->op_counts[JOP_TAILCALLR];
    uint64_t closure_op = vm->op_counts[JOP_CLOSURE];
    uint64_t const_fun_op = vm->op_counts[JOP_CONST_FUN];
    fprintf(out, "  opcodes: call+callr=%" PRIu64 " ret=%" PRIu64 " tailcall+tailcallr=%" PRIu64
                " closure=%" PRIu64 " const_fun=%" PRIu64 "\n",
            call_op, ret_op, tail_op, closure_op, const_fun_op);
  }
#endif
}

void jello_vm_profile_free(jello_vm* vm) {
  if(!vm) return;
  free(vm->op_counts);
  vm->op_counts = NULL;
  vm->profile_enabled = 0;
}
