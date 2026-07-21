// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/jdll.h>
#include <jello/internal/jdll_internal.h>

#include <string.h>

static int jdl_bad_arg(jdlo_ctx* c, int index) {
  return !c || c->failed || !jdl_caller_rf(c) || index < 0 || (uint32_t)index >= c->nargs;
}

static uint32_t jdl_arg_reg(jdlo_ctx* c, int index) {
  return c->first_arg_reg + (uint32_t)index;
}

int jdl_arg_count(jdlo_ctx* c) {
  return c ? (int)c->nargs : 0;
}

struct jello_vm* jdl_ctx_vm(jdlo_ctx* c) {
  return (c && c->xctx) ? c->xctx->vm : NULL;
}

const jello_bc_module* jdl_ctx_module(jdlo_ctx* c) {
  return (c && c->xctx) ? c->xctx->m : NULL;
}

int jdl_arg_bool(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  return (int)vm_load_u32(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

int8_t jdl_arg_i8(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  return (int8_t)vm_load_u32(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

int16_t jdl_arg_i16(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  return (int16_t)vm_load_u32(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

int32_t jdl_arg_i32(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  return (int32_t)vm_load_u32(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

int64_t jdl_arg_i64(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  return vm_load_i64(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

float jdl_arg_f16(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0.f;
  return vm_f16_bits_to_f32(vm_load_f16_bits(jdl_caller_rf(c), jdl_arg_reg(c, index)));
}

float jdl_arg_f32(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0.f;
  return vm_load_f32(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

double jdl_arg_f64(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0.0;
  return vm_load_f64(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

uint32_t jdl_arg_atom(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  return vm_load_u32(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

const uint8_t* jdl_arg_bytes_data(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return NULL;
  jello_bytes* b = (jello_bytes*)vm_load_ptr(jdl_caller_rf(c), jdl_arg_reg(c, index));
  return b ? b->data : NULL;
}

uint32_t jdl_arg_bytes_len(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return 0;
  jello_bytes* b = (jello_bytes*)vm_load_ptr(jdl_caller_rf(c), jdl_arg_reg(c, index));
  return b ? b->length : 0;
}

jello_object* jdl_arg_object(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return NULL;
  return (jello_object*)vm_load_ptr(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

jello_array* jdl_arg_array(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return NULL;
  exec_ctx* x = c->xctx;
  uint32_t r = jdl_arg_reg(c, index);
  if(vm_reg_kind(x->m, c->caller_f, r) == JELLO_T_DYNAMIC) {
    jello_value v = vm_load_val(jdl_caller_rf(c), r);
    if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_ARRAY)
      return (jello_array*)jello_as_ptr(v);
    return NULL;
  }
  return (jello_array*)vm_load_ptr(jdl_caller_rf(c), r);
}

jello_list* jdl_arg_list(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return NULL;
  return (jello_list*)vm_load_ptr(jdl_caller_rf(c), jdl_arg_reg(c, index));
}

jello_abstract* jdl_arg_abstract(jdlo_ctx* c, int index) {
  if(jdl_bad_arg(c, index)) return NULL;
  exec_ctx* x = c->xctx;
  uint32_t r = jdl_arg_reg(c, index);
  if(vm_reg_kind(x->m, c->caller_f, r) == JELLO_T_DYNAMIC) {
    jello_value v = vm_load_val(jdl_caller_rf(c), r);
    if(!jello_is_ptr(v) || jello_obj_kind_of(v) != (uint32_t)JELLO_OBJ_ABSTRACT) return NULL;
    return (jello_abstract*)jello_as_ptr(v);
  }
  return (jello_abstract*)vm_load_ptr(jdl_caller_rf(c), r);
}

void* jdl_arg_abstract_payload(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  return a ? a->payload : NULL;
}

void jdl_return_bool(jdlo_ctx* c, int v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_u32(rf, c->dst_reg, v ? 1u : 0u);
}

void jdl_return_i8(jdlo_ctx* c, int8_t v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_u32_masked(rf, c->dst_reg, (uint32_t)(uint8_t)v, JELLO_T_I8);
}

void jdl_return_i16(jdlo_ctx* c, int16_t v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_u32_masked(rf, c->dst_reg, (uint32_t)(uint16_t)v, JELLO_T_I16);
}

void jdl_return_i32(jdlo_ctx* c, int32_t v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_u32(rf, c->dst_reg, (uint32_t)v);
}

void jdl_return_i64(jdlo_ctx* c, int64_t v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_i64(rf, c->dst_reg, v);
}

void jdl_return_f16(jdlo_ctx* c, float v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_f16_bits(rf, c->dst_reg, vm_f32_to_f16_bits(v));
}

void jdl_return_f32(jdlo_ctx* c, float v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_f32(rf, c->dst_reg, v);
}

void jdl_return_f64(jdlo_ctx* c, double v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_f64(rf, c->dst_reg, v);
}

void jdl_return_atom(jdlo_ctx* c, uint32_t atom_id) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_u32(rf, c->dst_reg, atom_id);
}

void jdl_return_bytes_copy(jdlo_ctx* c, const uint8_t* data, uint32_t len) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  jello_vm* vm = c->xctx->vm;
  uint32_t type_id = c->caller_f->reg_types[c->dst_reg];
  jello_bytes* b = jello_bytes_new(vm, type_id, len);
  if(b && len && data) memcpy(b->data, data, len);
  vm_store_ptr(rf, c->dst_reg, b);
}

void jdl_return_object(jdlo_ctx* c, jello_object* o) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_ptr(rf, c->dst_reg, o);
}

void jdl_return_array(jdlo_ctx* c, jello_array* a) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_ptr(rf, c->dst_reg, a);
}

void jdl_return_list(jdlo_ctx* c, jello_list* l) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_ptr(rf, c->dst_reg, l);
}

void jdl_return_abstract(jdlo_ctx* c, void* payload, jello_abstract_finalizer fin) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  jello_vm* vm = c->xctx->vm;
  exec_ctx* x = c->xctx;
  uint32_t type_id = c->caller_f->reg_types[c->dst_reg];
  jello_abstract* a = jello_abstract_new_finalized(vm, type_id, payload, fin);
  if(vm_reg_kind(x->m, c->caller_f, c->dst_reg) == JELLO_T_DYNAMIC) {
    vm_store_val(rf, c->dst_reg, jello_from_ptr(a));
  } else {
    vm_store_ptr(rf, c->dst_reg, a);
  }
}

void jdl_close_abstract(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a) return;
  if(a->finalizer) a->finalizer(a->payload);
  a->payload = NULL;
  a->finalizer = NULL;
}

void jdl_return_null(jdlo_ctx* c) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  exec_ctx* x = c->xctx;
  if(vm_reg_kind(x->m, c->caller_f, c->dst_reg) == JELLO_T_DYNAMIC) {
    vm_store_val(rf, c->dst_reg, jello_make_null());
  } else {
    vm_store_ptr(rf, c->dst_reg, NULL);
  }
}

void jdl_return_value(jdlo_ctx* c, jdl_value v) {
  reg_frame* rf;
  if(!c || c->failed || !(rf = jdl_caller_rf(c))) return;
  vm_store_from_boxed(c->xctx->vm, c->xctx->m, c->caller_f, rf, c->dst_reg,
                      (jello_value)(uintptr_t)v);
}

int jdl_obj_has_atom(jello_object* o, uint32_t atom_id) {
  return o ? jello_object_has(o, atom_id) : 0;
}

jello_value jdl_obj_get_atom(jello_object* o, uint32_t atom_id) {
  if(!o) return jello_make_null();
  return jello_object_get(o, atom_id);
}

void jdl_obj_set_atom(jdlo_ctx* c, jello_object* o, uint32_t atom_id, jello_value v) {
  if(!c || c->failed || !o) return;
  jello_object_set(o, atom_id, v);
}

uint32_t jdl_array_len(jello_array* a) {
  return a ? a->length : 0;
}

jello_value jdl_array_get(jello_array* a, uint32_t index) {
  if(!a || index >= a->length) return jello_make_null();
  return a->data[index];
}

void jdl_fail(jdlo_ctx* c, const char* msg) {
  if(!c) return;
  c->failed = 1;
  c->fail_msg = msg ? msg : "jdll error";
}

static int jdl_arg_kind_compatible(jello_type_kind want, jello_type_kind got) {
  if(want == got) return 1;
  if(want == JELLO_T_DYNAMIC || got == JELLO_T_DYNAMIC) return 1;
  if(want == JELLO_T_FUNCTION && (got == JELLO_T_FUNCTION || got == JELLO_T_DYNAMIC)) return 1;
  if(want == JELLO_T_ABSTRACT && (got == JELLO_T_ABSTRACT || got == JELLO_T_DYNAMIC)) return 1;
  return 0;
}

static int jdl_validate_args(jdlo_ctx* c, const jdll_prim_caps* caps) {
  exec_ctx* x = c->xctx;
  if(caps->arity == JDLL_ABI_VARARGS_ARITY) {
    jello_type_kind want = (jello_type_kind)caps->arg_kinds[0];
    for(uint32_t i = 0; i < c->nargs; i++) {
      uint32_t r = c->first_arg_reg + i;
      jello_type_kind got = vm_reg_kind(x->m, x->f, r);
      if(!jdl_arg_kind_compatible(want, got)) {
        jdl_fail(c, "jdll arg type mismatch");
        return 0;
      }
    }
    return 1;
  }
  if(caps->arity == JDLL_ABI_MIXED_VARARGS_ARITY) {
    uint8_t nfixed = caps->arg_kinds[0];
    jello_type_kind elem = (jello_type_kind)caps->arg_kinds[nfixed + 1];
    if(c->nargs < nfixed) {
      jdl_fail(c, "jdll arg count mismatch");
      return 0;
    }
    for(uint8_t i = 0; i < nfixed; i++) {
      uint32_t r = c->first_arg_reg + i;
      jello_type_kind want = (jello_type_kind)caps->arg_kinds[1 + i];
      jello_type_kind got = vm_reg_kind(x->m, x->f, r);
      if(!jdl_arg_kind_compatible(want, got)) {
        jdl_fail(c, "jdll arg type mismatch");
        return 0;
      }
    }
    for(uint32_t i = nfixed; i < c->nargs; i++) {
      uint32_t r = c->first_arg_reg + i;
      jello_type_kind got = vm_reg_kind(x->m, x->f, r);
      if(!jdl_arg_kind_compatible(elem, got)) {
        jdl_fail(c, "jdll arg type mismatch");
        return 0;
      }
    }
    return 1;
  }
  for(uint8_t i = 0; i < caps->arity; i++) {
    uint32_t r = c->first_arg_reg + i;
    jello_type_kind want = (jello_type_kind)caps->arg_kinds[i];
    jello_type_kind got = vm_reg_kind(x->m, x->f, r);
    if(!jdl_arg_kind_compatible(want, got)) {
      jdl_fail(c, "jdll arg type mismatch");
      return 0;
    }
  }
  return 1;
}

int jello_jdll_invoke_prim(exec_ctx* ctx, const jello_insn* ins, const jello_function* fn, uint32_t first_arg_reg) {
  if(!fn || !fn->caps_are_raw || fn->raw_cap_size < sizeof(jdll_prim_caps)) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_TYPE_MISMATCH, "invalid jdll function");
    return 0;
  }
  jdll_prim_caps caps;
  memcpy(&caps, (const uint8_t*)&fn->caps[0], sizeof(caps));
  if(!caps.fn) {
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_TYPE_MISMATCH, "null jdll export");
    return 0;
  }
  uint32_t caller_frame_idx = 0;
  if(ctx->fr && ctx->frames) {
    caller_frame_idx = (uint32_t)(ctx->fr - ctx->frames);
  } else if(ctx->vm && ctx->vm->call_frames_len > 0) {
    caller_frame_idx = ctx->vm->call_frames_len - 1u;
  }
  jdlo_ctx jc = {
    .xctx = ctx,
    .dst_reg = ins->a,
    .first_arg_reg = first_arg_reg,
    .nargs = ins->c,
    .caller_f = ctx->f,
    .caller_frame_idx = caller_frame_idx,
    .ret_kind = caps.ret_kind,
    .failed = 0,
    .fail_msg = NULL,
  };
  if(!jdl_validate_args(&jc, &caps)) {
    char msg[512];
    snprintf(msg, sizeof msg, "jdll %s.%s: %s",
             caps.library_id ? caps.library_id : "?",
             caps.export_name ? caps.export_name : "?",
             jc.fail_msg ? jc.fail_msg : "jdll trap");
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_TYPE_MISMATCH, msg);
    return 0;
  }
  jello_jdll_set_active_prim(ctx->vm, &caps);
  caps.fn(&jc);
  jello_jdll_clear_active_prim(ctx->vm);
  if(jc.failed) {
    char msg[512];
    snprintf(msg, sizeof msg, "jdll %s.%s: %s",
             caps.library_id ? caps.library_id : "?",
             caps.export_name ? caps.export_name : "?",
             jc.fail_msg ? jc.fail_msg : "jdll trap");
    (void)jello_vm_trap(ctx->vm, JELLO_TRAP_TYPE_MISMATCH, msg);
    return 0;
  }
  return 1;
}

int jdl_is_null(jdl_value v) {
  return jello_is_null((jello_value)(uintptr_t)v);
}

int jdl_is_bool(jdl_value v) {
  return jello_is_bool((jello_value)(uintptr_t)v);
}

int jdl_is_i32(jdl_value v) {
  return jello_is_i32((jello_value)(uintptr_t)v);
}

int jdl_is_i64(jdl_value v) {
  return jello_is_box_i64((jello_value)(uintptr_t)v);
}

int jdl_is_f32(jdl_value v) {
  return jello_is_box_f32((jello_value)(uintptr_t)v);
}

int jdl_is_f16(jdl_value v) {
  return jello_is_box_f16((jello_value)(uintptr_t)v);
}

int jdl_is_f64(jdl_value v) {
  return jello_is_box_f64((jello_value)(uintptr_t)v);
}

int jdl_is_atom(jdl_value v) {
  return jello_is_atom((jello_value)(uintptr_t)v);
}

int jdl_is_bytes(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  return jello_is_ptr(jv) && jello_obj_kind_of(jv) == (uint32_t)JELLO_OBJ_BYTES;
}

int jdl_is_object(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  return jello_is_ptr(jv) && jello_obj_kind_of(jv) == (uint32_t)JELLO_OBJ_OBJECT;
}

int jdl_is_array(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  return jello_is_ptr(jv) && jello_obj_kind_of(jv) == (uint32_t)JELLO_OBJ_ARRAY;
}

int jdl_is_list(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  return jello_is_ptr(jv) && jello_obj_kind_of(jv) == (uint32_t)JELLO_OBJ_LIST;
}

int jdl_is_abstract(jdl_value v) {
  jello_value jv = (jello_value)(uintptr_t)v;
  return jello_is_ptr(jv) && jello_obj_kind_of(jv) == (uint32_t)JELLO_OBJ_ABSTRACT;
}

void jdl_check_bool(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_bool(jdl_arg_value(c, index))) jdl_fail(c, "expected bool");
}

void jdl_check_i32(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_i32(jdl_arg_value(c, index))) jdl_fail(c, "expected i32");
}

void jdl_check_i64(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_i64(jdl_arg_value(c, index))) jdl_fail(c, "expected i64");
}

void jdl_check_bytes(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_bytes(jdl_arg_value(c, index))) jdl_fail(c, "expected bytes");
}

void jdl_check_fun(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_fun(jdl_arg_value(c, index))) jdl_fail(c, "expected function");
}

void jdl_check_object(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_object(jdl_arg_value(c, index))) jdl_fail(c, "expected object");
}

void jdl_check_abstract(jdlo_ctx* c, int index) {
  if(!c || c->failed) return;
  if(!jdl_is_abstract(jdl_arg_value(c, index))) jdl_fail(c, "expected abstract handle");
}
