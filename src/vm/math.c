// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/math.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

static double jello_load_f64(call_frame* fr, uint32_t reg) {
  return vm_load_f64(&fr->rf, reg);
}

static void jello_store_f64(call_frame* fr, uint32_t reg, double y) {
  vm_store_f64(&fr->rf, reg, y);
}

static void jello_store_i32(call_frame* fr, uint32_t reg, int32_t y) {
  vm_store_u32(&fr->rf, reg, (uint32_t)y);
}

/* ECMAScript ToUint32. */
static uint32_t jello_to_uint32(double x) {
  if(isnan(x) || isinf(x) || x == 0.0 || x == -0.0) {
    return 0u;
  }
  if(x < 0.0) {
    return (uint32_t)(int64_t)x;
  }
  if(x >= 4294967296.0) {
    return 0xFFFFFFFFu;
  }
  return (uint32_t)(uint64_t)x;
}

static uint32_t jello_clz32(uint32_t bits) {
  if(bits == 0u) {
    return 32u;
  }
#if defined(__GNUC__) || defined(__clang__)
  return (uint32_t)__builtin_clz(bits);
#else
  uint32_t n = 0u;
  uint32_t v = bits;
  if((v & 0xFFFF0000u) == 0u) {
    n += 16u;
    v <<= 16u;
  }
  if((v & 0xFF000000u) == 0u) {
    n += 8u;
    v <<= 8u;
  }
  if((v & 0xF0000000u) == 0u) {
    n += 4u;
    v <<= 4u;
  }
  if((v & 0xC0000000u) == 0u) {
    n += 2u;
    v <<= 2u;
  }
  if((v & 0x80000000u) == 0u) {
    n += 1u;
  }
  return n;
#endif
}

static double jello_math_random(void) {
#if defined(_WIN32)
  static int seeded = 0;
  if(!seeded) {
    srand((unsigned)time(NULL));
    seeded = 1;
  }
  return (double)rand() / ((double)RAND_MAX + 1.0);
#else
  return (double)random() / ((double)(INT32_MAX) + 1.0);
#endif
}

static double jello_math_variadic_minmax(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg,
                                         double empty, int is_max) {
  call_frame* fr = ctx->fr;
  if(ins->c == 0u) {
    return empty;
  }
  double acc = jello_load_f64(fr, first_arg_reg);
  if(isnan(acc)) {
    return acc;
  }
  for(uint32_t i = 1; i < ins->c; i++) {
    double x = jello_load_f64(fr, first_arg_reg + i);
    if(isnan(x)) {
      return x;
    }
    if(is_max) {
      if(x > acc) acc = x;
    } else if(x < acc) {
      acc = x;
    }
  }
  return acc;
}

static double jello_math_hypot(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  call_frame* fr = ctx->fr;
  if(ins->c == 0u) {
    return 0.0;
  }
  if(ins->c == 1u) {
    double x = jello_load_f64(fr, first_arg_reg);
    return isnan(x) ? x : fabs(x);
  }
  if(ins->c == 2u) {
    double x = jello_load_f64(fr, first_arg_reg);
    double y = jello_load_f64(fr, first_arg_reg + 1u);
    return hypot(x, y);
  }

  double max = 0.0;
  for(uint32_t i = 0; i < ins->c; i++) {
    double x = jello_load_f64(fr, first_arg_reg + i);
    if(isnan(x)) {
      return x;
    }
    double ax = fabs(x);
    if(ax > max) {
      max = ax;
    }
  }
  if(max == 0.0) {
    return 0.0;
  }
  double sum = 0.0;
  for(uint32_t i = 0; i < ins->c; i++) {
    double scaled = jello_load_f64(fr, first_arg_reg + i) / max;
    sum += scaled * scaled;
  }
  return max * sqrt(sum);
}

static double jello_math_sign(double x) {
  if(isnan(x)) {
    return NAN;
  }
  if(x == 0.0 || x == -0.0) {
    return x;
  }
  return x > 0.0 ? 1.0 : -1.0;
}

void jello_invoke_math_native(exec_ctx* ctx, const jello_insn* ins, uint32_t func_index,
                              uint32_t first_arg_reg) {
  call_frame* fr = ctx->fr;
  double x = jello_load_f64(fr, first_arg_reg);
  double y = 0.0;

  switch(func_index) {
  case JELLO_NATIVE_BUILTIN_MATH_ABS:
    y = fabs(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_CEIL:
    y = ceil(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_FLOOR:
    y = floor(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ROUND:
    y = round(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_TRUNC:
    y = trunc(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_CBRT:
    y = cbrt(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_EXP:
    y = exp(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_EXPM1:
    y = expm1(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_LOG:
    y = log(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_LOG1P:
    y = log1p(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_LOG10:
    y = log10(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_LOG2:
    y = log2(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_POW:
    y = pow(x, jello_load_f64(fr, first_arg_reg + 1u));
    break;
  case JELLO_NATIVE_BUILTIN_MATH_SIN:
    y = sin(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_COS:
    y = cos(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_TAN:
    y = tan(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ASIN:
    y = asin(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ACOS:
    y = acos(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ATAN:
    y = atan(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ATAN2:
    y = atan2(jello_load_f64(fr, first_arg_reg), jello_load_f64(fr, first_arg_reg + 1u));
    break;
  case JELLO_NATIVE_BUILTIN_MATH_SINH:
    y = sinh(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ASINH:
    y = asinh(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_COSH:
    y = cosh(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ACOSH:
    y = acosh(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_TANH:
    y = tanh(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_ATANH:
    y = atanh(x);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_MAX:
    y = jello_math_variadic_minmax(ctx, ins, first_arg_reg, -INFINITY, 1);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_MIN:
    y = jello_math_variadic_minmax(ctx, ins, first_arg_reg, INFINITY, 0);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_RANDOM:
    y = jello_math_random();
    break;
  case JELLO_NATIVE_BUILTIN_MATH_CLZ32: {
    uint32_t bits = jello_to_uint32(x);
    jello_store_i32(fr, ins->a, (int32_t)jello_clz32(bits));
    return;
  }
  case JELLO_NATIVE_BUILTIN_MATH_FROUND:
    y = (double)(float)x;
    break;
  case JELLO_NATIVE_BUILTIN_MATH_HYPOT:
    y = jello_math_hypot(ctx, ins, first_arg_reg);
    break;
  case JELLO_NATIVE_BUILTIN_MATH_IMUL: {
    int32_t a = (int32_t)vm_load_u32(&fr->rf, first_arg_reg);
    int32_t b = (int32_t)vm_load_u32(&fr->rf, first_arg_reg + 1u);
    jello_store_i32(fr, ins->a, (int32_t)(a * b));
    return;
  }
  case JELLO_NATIVE_BUILTIN_MATH_SIGN:
    y = jello_math_sign(x);
    break;
  default:
    jello_vm_panic();
  }

  jello_store_f64(fr, ins->a, y);
}
