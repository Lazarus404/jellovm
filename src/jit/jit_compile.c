// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>
#include <jello/internal/jit_internal.h>

#include <stdio.h>
#include <string.h>

static jello_jit_code* jello_jit_compile_function(exec_ctx* ctx, const jello_bc_function* f) {
  if(!ctx || !ctx->vm || !ctx->m || !f || !ctx->vm->jit_state) return NULL;

  uint32_t fi = (uint32_t)(f - ctx->m->funcs);
  jello_jit_state* st = (jello_jit_state*)ctx->vm->jit_state;
  if(jello_jit_hot_is_rejected(st, ctx->m, fi)) return NULL;

  jello_jit_ir_func ir = jello_jit_ir_build(ctx->m, f);
  if(!ir.ok) {
    if(jello_jit_config_dump_enabled() && ir.reject_reason[0]) {
      fprintf(stderr, "JELLO_JIT: skip func %u: %s\n", (unsigned)fi, ir.reject_reason);
    }
    jello_jit_ir_func_free(&ir);
    jello_jit_hot_mark_rejected(st, ctx->m, fi);
    return NULL;
  }
  /* CALL/TAILCALL lower to JIR_SLOW (yield + resume / nested run). Compile
   * anyway so numeric/control between calls runs native. Self-recursion uses
   * JIR_CALL_SELF and does not set has_call_slow. */
  if(jello_jit_config_dump_enabled() && ir.has_call_slow) {
    fprintf(stderr, "JELLO_JIT: func %u has call slow ops (compiling with fallback)\n",
            (unsigned)fi);
  }
  /* Dense true JIR_SLOW ops (unproven calls / unsupported BC) can still lose
   * to the switch interpreter. Dedicated OBJ/ARRAY/BYTES IR ops do not count
   * toward nslow. Reject above ~1/3 slow. */
  if(ir.ninsns > 0u && ir.nslow * 3u > ir.ninsns) {
    if(jello_jit_config_dump_enabled()) {
      fprintf(stderr, "JELLO_JIT: skip func %u: mostly slow ops (%u/%u)\n", (unsigned)fi,
              (unsigned)ir.nslow, (unsigned)ir.ninsns);
      for(uint32_t i = 0; i < ir.ninsns; i++) {
        if(ir.insns[i].op == JIR_SLOW) {
          uint32_t bpc = ir.insns[i].bc_pc;
          uint32_t bop = (bpc < f->ninsns) ? (uint32_t)f->insns[bpc].op : 0u;
          fprintf(stderr, "JELLO_JIT:   slow ir[%u] bc_pc=%u bc_op=%u\n", (unsigned)i,
                  (unsigned)bpc, (unsigned)bop);
        }
      }
    }
    jello_jit_ir_func_free(&ir);
    jello_jit_hot_mark_rejected(st, ctx->m, fi);
    return NULL;
  }
  if(jello_jit_config_dump_enabled() && (ir.has_call_slow || ir.nslow)) {
    fprintf(stderr, "JELLO_JIT: func %u nslow=%u/%u call_slow=%u\n",
            (unsigned)fi, (unsigned)ir.nslow, (unsigned)ir.ninsns, (unsigned)ir.has_call_slow);
    for(uint32_t i = 0; i < ir.ninsns; i++) {
      if(ir.insns[i].op != JIR_SLOW) continue;
      uint32_t bpc = ir.insns[i].bc_pc;
      if(bpc >= f->ninsns) continue;
      const jello_insn* bi = &f->insns[bpc];
      fprintf(stderr, "JELLO_JIT:   slow ir[%u] bc_pc=%u bc_op=%u a=%u b=%u c=%u imm=%u\n",
              (unsigned)i, (unsigned)bpc, (unsigned)bi->op, (unsigned)bi->a, (unsigned)bi->b,
              (unsigned)bi->c, (unsigned)bi->imm);
    }
  }

  const frame_layout* layout = vm_get_frame_layout(ctx->vm, ctx->m, f);
  if(!layout) {
    jello_jit_ir_func_free(&ir);
    return NULL;
  }

  const jello_jit_backend* be = jello_jit_backend_select();
  if(!be || !be->emit_func) {
    jello_jit_ir_func_free(&ir);
    return NULL;
  }

  jello_jit_emit_buf buf = {0};
  size_t entry_off = 0;
  size_t body_off = 0;
  uint32_t* bc_pc_map = NULL;
  uint32_t nbc_pc_map = 0;
  if(be->emit_func(&ir, f, layout, ctx->m, &buf, &entry_off, &body_off, &bc_pc_map, &nbc_pc_map) != 0 ||
     !buf.data || buf.size == 0) {
    if(jello_jit_config_dump_enabled()) {
      fprintf(stderr, "JELLO_JIT: emit failed func %u (%s)\n", (unsigned)fi, be->name);
    }
    jello_jit_emit_buf_free(&buf);
    jello_jit_ir_func_free(&ir);
    jello_jit_hot_mark_rejected(st, ctx->m, fi);
    return NULL;
  }

  jello_jit_code* code = jello_jit_cache_insert(
      (jello_jit_state*)ctx->vm->jit_state,
      ctx->m,
      fi,
      buf.data,
      buf.size,
      entry_off,
      body_off,
      bc_pc_map,
      nbc_pc_map
  );

  size_t emitted = buf.size;
  if(code && jello_jit_config_dump_enabled()) {
    fprintf(stderr, "JELLO_JIT: compiled func %u (%s, %zu bytes, map=%u)\n",
            (unsigned)fi, be->name, emitted, (unsigned)nbc_pc_map);
  }
  if(!code) {
    /* Emit ok but map/insert failed (e.g. no RX pages). Reject so we do not
     * re-emit on every backedge — that burns the whole run into compile. */
    if(jello_jit_config_dump_enabled()) {
      fprintf(stderr, "JELLO_JIT: map/insert failed func %u (%s, %zu bytes)\n",
              (unsigned)fi, be->name, emitted);
    }
    jello_jit_hot_mark_rejected(st, ctx->m, fi);
  }
  free(bc_pc_map);
  jello_jit_emit_buf_free(&buf);
  jello_jit_ir_func_free(&ir);

  return code;
}

void jello_jit_init(jello_vm* vm) {
  if(!vm) return;
#ifdef JELLOVM_ENABLE_JIT
  if(!vm->jit_state) vm->jit_state = jello_jit_state_create();
  vm->jit_enabled = 1u;
#else
  vm->jit_enabled = 0u;
#endif
}

void jello_jit_shutdown(jello_vm* vm) {
  if(!vm) return;
#ifdef JELLOVM_ENABLE_JIT
  if(vm->jit_state) {
    jello_jit_state_destroy((jello_jit_state*)vm->jit_state);
    vm->jit_state = NULL;
  }
#endif
}

void jello_jit_on_enter(exec_ctx* ctx) {
  /* Self-rec: compile on first entry (no backedge). Other funcs: hot-count
   * entries so leaf helpers (nbodies pair) can compile without a loop. */
  if(!jello_jit_config_enabled(ctx->vm) || !ctx || !ctx->f || !ctx->m || !ctx->fr) return;
#ifdef JELLOVM_ENABLE_JIT
  const frame_layout* fl = vm_get_frame_layout(ctx->vm, ctx->m, ctx->f);
  if(!fl || fl->jit_ineligible) return;
  uint32_t fi = (uint32_t)(ctx->f - ctx->m->funcs);
  jello_jit_state* st = (jello_jit_state*)ctx->vm->jit_state;
  if(!st || jello_jit_hot_is_rejected(st, ctx->m, fi)) return;
  if(jello_jit_cache_lookup(st, ctx->m, fi)) return;
  if(fl->jit_self_rec) {
    (void)jello_jit_compile_function(ctx, ctx->f);
    return;
  }
  uint32_t* hot = jello_jit_hot_counter(st, ctx->m, fi);
  if(!hot || *hot == JELLO_JIT_HOT_REJECTED) return;
  if(*hot < JELLO_JIT_HOT_REJECTED) (*hot)++;
  if(*hot < jello_jit_config_hot_threshold()) return;
  (void)jello_jit_compile_function(ctx, ctx->f);
#else
  (void)ctx;
#endif
}

void jello_jit_on_backedge(exec_ctx* ctx) {
  if(!jello_jit_config_enabled(ctx->vm) || !ctx || !ctx->f || !ctx->m || !ctx->fr) return;
#ifdef JELLOVM_ENABLE_JIT
  uint32_t fi = (uint32_t)(ctx->f - ctx->m->funcs);
  jello_jit_state* st = (jello_jit_state*)ctx->vm->jit_state;
  if(!st) return;
  if(jello_jit_hot_is_rejected(st, ctx->m, fi)) return;

  jello_jit_code* code = jello_jit_cache_lookup(st, ctx->m, fi);
  if(!code) {
    uint32_t* hot = jello_jit_hot_counter(st, ctx->m, fi);
    if(!hot || *hot == JELLO_JIT_HOT_REJECTED) return;
    if(*hot < JELLO_JIT_HOT_REJECTED) (*hot)++;
    if(*hot < jello_jit_config_hot_threshold()) return;
    code = jello_jit_compile_function(ctx, ctx->f);
  }
  if(!code) return;

  uint32_t pc = ctx->fr->pc;
  if(!code->bc_pc_map || pc >= code->nbc_pc_map || code->bc_pc_map[pc] == 0u) {
    if(jello_jit_config_dump_enabled()) {
      fprintf(stderr, "JELLO_JIT: OSR miss func %u pc %u\n", (unsigned)fi, (unsigned)pc);
    }
    return;
  }
  ctx->fr->jit_osr_hint = 1u;
#endif
}

int jello_jit_try_enter(exec_ctx* ctx) {
  if(!jello_jit_config_enabled(ctx->vm) || !ctx || !ctx->f || !ctx->m) return 0;
#ifdef JELLOVM_ENABLE_JIT
  if(!jello_jit_config_run_enabled()) return 0;

  uint32_t pc = ctx->fr ? ctx->fr->pc : 0u;
  if(pc != 0u) {
    if(!ctx->fr || (!ctx->fr->jit_resume_hint && !ctx->fr->jit_osr_hint)) return 0;
    ctx->fr->jit_resume_hint = 0u;
    ctx->fr->jit_osr_hint = 0u;
  }

  uint32_t fi = (uint32_t)(ctx->f - ctx->m->funcs);
  jello_jit_code* code = jello_jit_cache_lookup((jello_jit_state*)ctx->vm->jit_state, ctx->m, fi);
  if(!code) return 0;

  if(pc != 0u) {
    if(!code->bc_pc_map || pc >= code->nbc_pc_map || code->bc_pc_map[pc] == 0u) {
      if(jello_jit_config_dump_enabled()) {
        fprintf(stderr, "JELLO_JIT: mid-enter miss func %u pc %u\n", (unsigned)fi, (unsigned)pc);
      }
      return 0;
    }
    ctx->jit_resume_entry = (uint8_t*)code->base + (size_t)code->bc_pc_map[pc];
  } else {
    ctx->jit_resume_entry = NULL;
  }

  jello_jit_run_result rr = jello_jit_runtime_run(ctx, code);
  ctx->jit_resume_entry = NULL;
  jello_jit_deopt_sync_ctx(ctx);
  if(rr == JELLO_JIT_RUN_RETURNED && ctx->vm->call_frames_len == 0u) return 1;
  if(rr == JELLO_JIT_RUN_RETURNED || rr == JELLO_JIT_RUN_STEPPED || rr == JELLO_JIT_RUN_NESTED_RET)
    return 2;
  return 0;
#else
  (void)ctx;
  return 0;
#endif
}

void jello_jit_module_unloaded(jello_vm* vm, const jello_bc_module* m) {
#ifdef JELLOVM_ENABLE_JIT
  if(!vm || !m || !vm->jit_state) return;
  jello_jit_cache_drop_module((jello_jit_state*)vm->jit_state, m);
#else
  (void)vm;
  (void)m;
#endif
}
