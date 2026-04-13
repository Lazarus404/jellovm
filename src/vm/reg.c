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

#include <jello/internal.h>

#include <string.h>

size_t jello_slot_align(jello_type_kind k) {
  switch(k) {
    case JELLO_T_I64:
    case JELLO_T_F64:
      return 8;
    case JELLO_T_I8:
    case JELLO_T_I16:
    case JELLO_T_I32:
    case JELLO_T_F32:
    case JELLO_T_F16:
    case JELLO_T_BOOL:
    case JELLO_T_ATOM:
      return 4;
    case JELLO_T_DYNAMIC:
      return sizeof(jello_value);
    case JELLO_T_BYTES:
    case JELLO_T_FUNCTION:
    case JELLO_T_LIST:
    case JELLO_T_ARRAY:
    case JELLO_T_OBJECT:
    case JELLO_T_ABSTRACT:
      return sizeof(void*);
    default:
      return sizeof(void*);
  }
}

size_t jello_slot_size(jello_type_kind k) {
  switch(k) {
    case JELLO_T_I8:
    case JELLO_T_I16:
    case JELLO_T_I32:
    case JELLO_T_F32:
    case JELLO_T_F16:
    case JELLO_T_BOOL:
    case JELLO_T_ATOM:
      return 4;
    case JELLO_T_I64:
    case JELLO_T_F64:
      return 8;
    case JELLO_T_DYNAMIC:
      return sizeof(jello_value);
    case JELLO_T_BYTES:
    case JELLO_T_FUNCTION:
    case JELLO_T_LIST:
    case JELLO_T_ARRAY:
    case JELLO_T_OBJECT:
    case JELLO_T_ABSTRACT:
      return sizeof(void*);
    default:
      return sizeof(void*);
  }
}

jello_type_kind vm_reg_kind(const jello_bc_module* m, const jello_bc_function* f, uint32_t r) {
  jello_type_id tid = f->reg_types[r];
  return m->types[tid].kind;
}

/* vm_reg_ptr, vm_load_u32, vm_store_u32 are static inline in vm_internal.h for hot path. */

/* Direct loads/stores for aligned primitives (frame layout guarantees alignment). */

/* vm_store_u32_masked, vm_load_f64, vm_store_f64, vm_load_ptr, vm_store_ptr are static inline in vm_internal.h. */

float vm_load_f32(reg_frame* rf, uint32_t r) {
  return *(const float*)vm_reg_ptr(rf, r);
}

void vm_store_f32(reg_frame* rf, uint32_t r, float v) {
  *(float*)vm_reg_ptr(rf, r) = v;
}

int32_t vm_load_i32ish(reg_frame* rf, uint32_t r) {
  return (int32_t)vm_load_u32(rf, r);
}

int64_t vm_load_i64(reg_frame* rf, uint32_t r) {
  return *(const int64_t*)vm_reg_ptr(rf, r);
}

void vm_store_i64(reg_frame* rf, uint32_t r, int64_t v) {
  *(int64_t*)vm_reg_ptr(rf, r) = v;
}

/* F16: IEEE 754 binary16 stored in low 16 bits of 32-bit slot. */
uint16_t vm_load_f16_bits(reg_frame* rf, uint32_t r) {
  return (uint16_t)(vm_load_u32(rf, r) & 0xFFFFu);
}

void vm_store_f16_bits(reg_frame* rf, uint32_t r, uint16_t bits) {
  vm_store_u32(rf, r, (uint32_t)bits);
}

/* F16 <-> F32 conversion (IEEE 754 binary16). */
float vm_f16_bits_to_f32(uint16_t bits) {
  if((bits & 0x7FFFu) == 0) return (bits & 0x8000u) ? -0.0f : 0.0f;
  uint32_t sign = ((uint32_t)bits & 0x8000u) << 16;
  uint32_t exp = (bits >> 10) & 0x1Fu;
  uint32_t mant = (uint32_t)(bits & 0x3FFu) << 13;
  if(exp == 0) {
    while(!(mant & 0x800000u)) { mant <<= 1; exp--; }
    exp++;
  } else if(exp == 31) {
    return (sign ? -1.0f : 1.0f) * (mant ? 0.0f / 0.0f : 1.0f / 0.0f);
  }
  exp += (127 - 15);
  uint32_t u32 = sign | (exp << 23) | mant;
  float f;
  memcpy(&f, &u32, sizeof(f));
  return f;
}

uint16_t vm_f32_to_f16_bits(float f) {
  uint32_t u32;
  memcpy(&u32, &f, sizeof(u32));
  uint32_t sign = (u32 >> 16) & 0x8000u;
  uint32_t exp = (u32 >> 23) & 0xFFu;
  uint32_t mant = u32 & 0x7FFFFFu;
  if(exp == 0xFF) {
    return (uint16_t)(sign | 0x7C00u | (mant ? 0x200u : 0));
  }
  if(exp == 0 && mant == 0) return (uint16_t)sign;
  int32_t exp16 = (int32_t)exp - 127 + 15;
  if(exp16 >= 31) return (uint16_t)(sign | 0x7C00u);
  if(exp16 <= 0) return (uint16_t)sign;
  return (uint16_t)(sign | ((uint32_t)exp16 << 10) | (mant >> 13));
}
