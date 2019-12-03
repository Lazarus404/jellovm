// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>

#include <stdlib.h>
#include <math.h>

int vm_checked_f64_to_i32(jello_vm* vm, double x, uint32_t* out_u32) {
  if(!isfinite(x)) {
    vm->trap_code = JELLO_TRAP_TYPE_MISMATCH;
    vm->trap_msg = "f64->i32 conversion overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_TYPE_MISMATCH);
    return 0;
  }
  if(x > 2147483647.0 || x < -2147483648.0) {
    vm->trap_code = JELLO_TRAP_TYPE_MISMATCH;
    vm->trap_msg = "f64->i32 conversion overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_TYPE_MISMATCH);
    return 0;
  }
  int32_t v = (int32_t)x;
  *out_u32 = (uint32_t)v;
  return 1;
}

int vm_checked_f64_to_i64(jello_vm* vm, double x, int64_t* out_i64) {
  if(!isfinite(x)) {
    vm->trap_code = JELLO_TRAP_TYPE_MISMATCH;
    vm->trap_msg = "f64->i64 conversion overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_TYPE_MISMATCH);
    return 0;
  }
  if(x > 9223372036854775807.0 || x < -9223372036854775808.0) {
    vm->trap_code = JELLO_TRAP_TYPE_MISMATCH;
    vm->trap_msg = "f64->i64 conversion overflow";
    vm->exc_pending = 1;
    vm->exc_payload = jello_make_i32((int32_t)JELLO_TRAP_TYPE_MISMATCH);
    return 0;
  }
  *out_i64 = (int64_t)x;
  return 1;
}

uint32_t vm_expected_obj_kind_for_typed_ptr(jello_type_kind k) {
  switch(k) {
    case JELLO_T_BYTES: return (uint32_t)JELLO_OBJ_BYTES;
    case JELLO_T_FUNCTION: return (uint32_t)JELLO_OBJ_FUNCTION;
    case JELLO_T_LIST: return (uint32_t)JELLO_OBJ_LIST;
    case JELLO_T_ARRAY: return (uint32_t)JELLO_OBJ_ARRAY;
    case JELLO_T_OBJECT: return (uint32_t)JELLO_OBJ_OBJECT;
    case JELLO_T_ENUM: return (uint32_t)JELLO_OBJ_ENUM;
    case JELLO_T_ABSTRACT: return (uint32_t)JELLO_OBJ_ABSTRACT;
    default: return 0;
  }
}

uint32_t vm_kindof_dynamic(jello_value v) {
  if(jello_is_null(v)) return JELLO_KINDOF_NULL;
  if(jello_is_bool(v)) return JELLO_KINDOF_BOOL;
  if(jello_is_i32(v)) return JELLO_KINDOF_I32;
  if(jello_is_atom(v)) return JELLO_KINDOF_ATOM;
  if(!jello_is_ptr(v)) return JELLO_KINDOF_UNKNOWN;
  switch(jello_obj_kind_of(v)) {
    case JELLO_OBJ_BYTES: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_BYTES);
    case JELLO_OBJ_FUNCTION: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_FUNCTION);
    case JELLO_OBJ_LIST: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_LIST);
    case JELLO_OBJ_ARRAY: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_ARRAY);
    case JELLO_OBJ_OBJECT: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_OBJECT);
    case JELLO_OBJ_ABSTRACT: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_ABSTRACT);
    case JELLO_OBJ_BOX_I64: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_BOX_I64);
    case JELLO_OBJ_BOX_F64: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_BOX_F64);
    case JELLO_OBJ_BOX_F32: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_BOX_F32);
    case JELLO_OBJ_BOX_F16: return JELLO_KINDOF_FROM_OBJ(JELLO_OBJ_BOX_F16);
    case JELLO_OBJ_ENUM: return JELLO_KINDOF_ENUM;
    default: return JELLO_KINDOF_UNKNOWN;
  }
}

jello_value vm_box_from_typed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f, reg_frame* rf, uint32_t r) {
  switch(vm_reg_kind(m, f, r)) {
    case JELLO_T_I8:
    case JELLO_T_I16:
    case JELLO_T_I32:
      return jello_make_i32((int32_t)vm_load_u32(rf, r));
    case JELLO_T_F32: {
      uint32_t type_id = f->reg_types[r];
      jello_box_f32* b = jello_box_f32_new(vm, type_id, vm_load_f32(rf, r));
      return jello_from_ptr(b);
    }
    case JELLO_T_F16: {
      uint32_t type_id = f->reg_types[r];
      uint16_t bits = vm_load_f16_bits(rf, r);
      jello_box_f16* b = jello_box_f16_new(vm, type_id, bits);
      return jello_from_ptr(b);
    }
    case JELLO_T_I64: {
      uint32_t type_id = f->reg_types[r];
      jello_box_i64* b = jello_box_i64_new(vm, type_id, vm_load_i64(rf, r));
      return jello_from_ptr(b);
    }
    case JELLO_T_BOOL:
      return jello_make_bool((int)(vm_load_u32(rf, r) != 0));
    case JELLO_T_ATOM:
      return jello_make_atom(vm_load_u32(rf, r));
    case JELLO_T_F64: {
      uint32_t type_id = f->reg_types[r];
      jello_box_f64* b = jello_box_f64_new(vm, type_id, vm_load_f64(rf, r));
      return jello_from_ptr(b);
    }
    case JELLO_T_DYNAMIC:
      return vm_load_val(rf, r);
    case JELLO_T_BYTES:
    case JELLO_T_FUNCTION:
    case JELLO_T_LIST:
    case JELLO_T_ARRAY:
    case JELLO_T_OBJECT:
    case JELLO_T_ABSTRACT:
    case JELLO_T_ENUM: {
      void* p = vm_load_ptr(rf, r);
      if(p == NULL) return jello_make_null();
      return jello_from_ptr(p);
    }
    default:
      jello_vm_panic();
      return jello_make_null();
  }
}

static void unbox_to_i32(reg_frame* rf, uint32_t dst, jello_value v) {
  if(!jello_is_i32(v)) jello_vm_panic();
  vm_store_u32(rf, dst, (uint32_t)jello_as_i32(v));
}

jello_value vm_clone_numbox(jello_vm* vm, jello_value v) {
  if(!vm || !jello_is_ptr(v)) return v;
  uint32_t kind = jello_obj_kind_of(v);
  if(kind == (uint32_t)JELLO_OBJ_BOX_F64) {
    const jello_box_f64* b = (const jello_box_f64*)jello_as_ptr(v);
    return jello_from_ptr(jello_box_f64_new(vm, b->h.type_id, b->value));
  }
  if(kind == (uint32_t)JELLO_OBJ_BOX_F32) {
    const jello_box_f32* b = (const jello_box_f32*)jello_as_ptr(v);
    return jello_from_ptr(jello_box_f32_new(vm, b->h.type_id, b->value));
  }
  if(kind == (uint32_t)JELLO_OBJ_BOX_F16) {
    const jello_box_f16* b = (const jello_box_f16*)jello_as_ptr(v);
    return jello_from_ptr(jello_box_f16_new(vm, b->h.type_id, b->value));
  }
  if(kind == (uint32_t)JELLO_OBJ_BOX_I64) {
    const jello_box_i64* b = (const jello_box_i64*)jello_as_ptr(v);
    return jello_from_ptr(jello_box_i64_new(vm, b->h.type_id, b->value));
  }
  return v;
}

int vm_store_num_inplace(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f,
                         reg_frame* rf, uint32_t src_reg, jello_value* slot) {
  if(!vm || !m || !f || !rf || !slot) return 0;
  jello_type_kind k = vm_reg_kind(m, f, src_reg);
  jello_value cur = *slot;
  switch(k) {
    case JELLO_T_F64:
      if(jello_is_box_f64(cur)) {
        ((jello_box_f64*)jello_as_ptr(cur))->value = vm_load_f64(rf, src_reg);
        return 1;
      }
      break;
    case JELLO_T_F32:
      if(jello_is_box_f32(cur)) {
        ((jello_box_f32*)jello_as_ptr(cur))->value = vm_load_f32(rf, src_reg);
        return 1;
      }
      break;
    case JELLO_T_F16:
      if(jello_is_box_f16(cur)) {
        ((jello_box_f16*)jello_as_ptr(cur))->value = vm_load_f16_bits(rf, src_reg);
        return 1;
      }
      break;
    case JELLO_T_I64:
      if(jello_is_box_i64(cur)) {
        ((jello_box_i64*)jello_as_ptr(cur))->value = vm_load_i64(rf, src_reg);
        return 1;
      }
      break;
    default:
      break;
  }
  return 0;
}

void vm_store_from_boxed(jello_vm* vm, const jello_bc_module* m, const jello_bc_function* f, reg_frame* rf, uint32_t dst, jello_value v) {
  switch(vm_reg_kind(m, f, dst)) {
    case JELLO_T_I8:
    case JELLO_T_I16:
    case JELLO_T_I32:
      if(jello_is_null(v)) {
        vm_store_u32(rf, dst, 0);
        return;
      }
      if(jello_is_i32(v)) {
        vm_store_u32(rf, dst, (uint32_t)jello_as_i32(v));
        return;
      }
      if(jello_is_box_f64(v)) {
        int32_t x = (int32_t)jello_as_box_f64(v);
        vm_store_u32(rf, dst, (uint32_t)x);
        return;
      }
      if(jello_is_box_i64(v)) {
        int32_t x = (int32_t)jello_as_box_i64(v);
        vm_store_u32(rf, dst, (uint32_t)x);
        return;
      }
      unbox_to_i32(rf, dst, v);
      return;
    case JELLO_T_F32:
      if(jello_is_i32(v)) {
        vm_store_f32(rf, dst, (float)jello_as_i32(v));
        return;
      }
      if(jello_is_null(v)) {
        vm_store_f32(rf, dst, 0.0f);
        return;
      }
      if(jello_is_box_f64(v)) {
        vm_store_f32(rf, dst, (float)jello_as_box_f64(v));
        return;
      }
      if(!jello_is_box_f32(v)) jello_vm_panic();
      vm_store_f32(rf, dst, jello_as_box_f32(v));
      return;
    case JELLO_T_F16:
      if(jello_is_i32(v)) {
        vm_store_f16_bits(rf, dst, vm_f32_to_f16_bits((float)jello_as_i32(v)));
        return;
      }
      if(jello_is_null(v)) {
        vm_store_f16_bits(rf, dst, 0);
        return;
      }
      if(jello_is_box_f64(v)) {
        vm_store_f16_bits(rf, dst, vm_f32_to_f16_bits((float)jello_as_box_f64(v)));
        return;
      }
      if(!jello_is_box_f16(v)) jello_vm_panic();
      vm_store_f16_bits(rf, dst, jello_as_box_f16(v));
      return;
    case JELLO_T_I64:
      if(jello_is_i32(v)) {
        vm_store_i64(rf, dst, (int64_t)jello_as_i32(v));
        return;
      }
      if(jello_is_null(v)) {
        vm_store_i64(rf, dst, 0);
        return;
      }
      if(jello_is_box_f64(v)) {
        vm_store_i64(rf, dst, (int64_t)jello_as_box_f64(v));
        return;
      }
      if(!jello_is_box_i64(v)) jello_vm_panic();
      vm_store_i64(rf, dst, jello_as_box_i64(v));
      return;
    case JELLO_T_BOOL:
      if(!jello_is_bool(v)) jello_vm_panic();
      vm_store_u32(rf, dst, (uint32_t)jello_as_bool(v));
      return;
    case JELLO_T_ATOM:
      if(!jello_is_atom(v)) jello_vm_panic();
      vm_store_u32(rf, dst, jello_as_atom(v));
      return;
    case JELLO_T_F64:
      if(jello_is_i32(v)) {
        vm_store_f64(rf, dst, (double)jello_as_i32(v));
        return;
      }
      if(jello_is_null(v)) {
        vm_store_f64(rf, dst, 0.0);
        return;
      }
      if(jello_is_box_f32(v)) {
        vm_store_f64(rf, dst, (double)jello_as_box_f32(v));
        return;
      }
      if(!jello_is_box_f64(v)) jello_vm_panic();
      vm_store_f64(rf, dst, jello_as_box_f64(v));
      return;
    case JELLO_T_DYNAMIC:
      /* Numeric boxes may be mutated in place in object/array slots; clone on escape. */
      vm_store_val(rf, dst, vm_clone_numbox(vm, v));
      return;
    case JELLO_T_BYTES:
    case JELLO_T_FUNCTION:
    case JELLO_T_LIST:
    case JELLO_T_ARRAY:
    case JELLO_T_OBJECT:
    case JELLO_T_ABSTRACT:
    case JELLO_T_ENUM:
      if(jello_is_null(v)) {
        vm_store_ptr(rf, dst, NULL);
        return;
      }
      if(!jello_is_ptr(v)) jello_vm_panic();
      vm_store_ptr(rf, dst, jello_as_ptr(v));
      return;
    default:
      jello_vm_panic();
      return;
  }
}
