// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <string.h>

/* Rust embed-vm callbacks (weak stubs in macro_host_stub.c; strong defs from jelloc). */
extern int32_t jello_rust_macro_emit(int32_t frag_id);
extern int32_t jello_rust_macro_show(int32_t frag_id);
extern int32_t jello_rust_macro_parse(const char* src, size_t src_len, const char* kind,
                                      size_t kind_len, int32_t* out_id);
extern int32_t jello_rust_macro_gensym(const char* prefix, size_t prefix_len, int32_t* out_id);
extern int32_t jello_rust_macro_quote(uint32_t template_idx, int32_t* out_id);
extern int32_t jello_rust_macro_param(uint32_t param_idx, int32_t* out_id);
extern int32_t jello_rust_macro_emit_all(int32_t frag_id);

#define MACRO_OP_EMIT 0u
#define MACRO_OP_SHOW 1u
#define MACRO_OP_PARSE 2u
#define MACRO_OP_GENSYM 3u
#define MACRO_OP_QUOTE 4u
#define MACRO_OP_SPLICE 5u
#define MACRO_OP_PARAM 6u
#define MACRO_OP_EMIT_ALL 7u

static jello_bytes* load_bytes_arg(call_frame* fr, uint32_t reg) {
  jello_value v = vm_load_val(&fr->rf, reg);
  if(!jello_is_ptr(v)) return NULL;
  if(jello_obj_kind_of(v) != (uint32_t)JELLO_OBJ_BYTES) return NULL;
  return (jello_bytes*)jello_as_ptr(v);
}

static int32_t load_i32_arg(call_frame* fr, uint32_t reg) {
  return (int32_t)vm_load_u32(&fr->rf, reg);
}

static void native_macro_host(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  call_frame* fr = ctx->fr;
  uint32_t op = (uint32_t)load_i32_arg(fr, first_arg_reg);
  uint32_t nargs = ins->c >= 1u ? ins->c - 1u : 0u;
  uint32_t arg0 = first_arg_reg + 1u;
  int32_t out_id = 0;
  int32_t rc = 0;

  switch(op) {
    case MACRO_OP_EMIT:
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.emit expects fragment id"); return; }
      rc = jello_rust_macro_emit(load_i32_arg(fr, arg0));
      out_id = 0;
      break;
    case MACRO_OP_SHOW:
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.show expects fragment id"); return; }
      rc = jello_rust_macro_show(load_i32_arg(fr, arg0));
      out_id = 0;
      break;
    case MACRO_OP_PARSE: {
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.parse expects source"); return; }
      jello_bytes* src = load_bytes_arg(fr, arg0);
      if(!src) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.parse source must be Bytes"); return; }
      const char* kind = "auto";
      size_t kind_len = 4;
      if(nargs >= 2u) {
        jello_bytes* kb = load_bytes_arg(fr, arg0 + 1u);
        if(kb && kb->length > 0) { kind = (const char*)kb->data; kind_len = kb->length; }
      }
      rc = jello_rust_macro_parse((const char*)src->data, src->length, kind, kind_len, &out_id);
      break;
    }
    case MACRO_OP_GENSYM: {
      const char* prefix = "tmp";
      size_t prefix_len = 3;
      if(nargs >= 1u) {
        jello_bytes* pb = load_bytes_arg(fr, arg0);
        if(pb && pb->length > 0) { prefix = (const char*)pb->data; prefix_len = pb->length; }
      }
      rc = jello_rust_macro_gensym(prefix, prefix_len, &out_id);
      break;
    }
    case MACRO_OP_QUOTE: {
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.__quote expects template index"); return; }
      uint32_t tmpl = (uint32_t)load_i32_arg(fr, arg0);
      rc = jello_rust_macro_quote(tmpl, &out_id);
      break;
    }
    case MACRO_OP_SPLICE:
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.__splice expects fragment id"); return; }
      out_id = load_i32_arg(fr, arg0);
      rc = 0;
      break;
    case MACRO_OP_PARAM: {
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.__param expects index"); return; }
      uint32_t idx = (uint32_t)load_i32_arg(fr, arg0);
      rc = jello_rust_macro_param(idx, &out_id);
      break;
    }
    case MACRO_OP_EMIT_ALL:
      if(nargs < 1u) { jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "Macro.emit_all expects fragment id"); return; }
      rc = jello_rust_macro_emit_all(load_i32_arg(fr, arg0));
      out_id = 0;
      break;
    default:
      jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "unknown macro host op");
      return;
  }

  if(rc < 0) {
    jello_vm_trap(ctx->vm, JELLO_TRAP_THROWN, "macro host trap");
    return;
  }
  vm_store_u32(&fr->rf, ins->a, (uint32_t)out_id);
}

void jello_invoke_macro_host_native(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  native_macro_host(ctx, ins, first_arg_reg);
}
