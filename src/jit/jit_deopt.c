// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>

void jello_jit_deopt_sync_ctx(exec_ctx* ctx) {
  if(!ctx || !ctx->vm) return;
  jello_vm* vm = ctx->vm;
  if(vm->call_frames_len == 0u) return;
  ctx->frames = (call_frame*)vm->call_frames;
  ctx->fr = &ctx->frames[vm->call_frames_len - 1u];
  ctx->f = ctx->fr->f;
}

void jello_jit_deopt_continue(exec_ctx* ctx, uint32_t bc_pc) {
  if(!ctx || !ctx->fr) return;
  ctx->fr->pc = bc_pc + 1u;
  jello_jit_deopt_sync_ctx(ctx);
}

jello_jit_exit jello_jit_runtime_yield_at_pc(exec_ctx* ctx, uint32_t bc_pc) {
  if(ctx && ctx->fr) ctx->fr->pc = bc_pc;
  jello_jit_deopt_sync_ctx(ctx);
  return JELLO_JIT_EXIT_YIELD;
}
