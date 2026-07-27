// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal.h>
#include <jello/internal/jdll_internal.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Must match jelloc/src/jlo/format.rs NATIVE_BUILTIN_* constants. */
#define JELLO_NATIVE_BUILTIN_MATH_SQRT 0u
#define JELLO_NATIVE_BUILTIN_SYSTEM_EXIT 1u
#define JELLO_NATIVE_BUILTIN_I32_TO_BYTES 2u
#define JELLO_NATIVE_BUILTIN_F64_TO_BYTES 3u
#define JELLO_NATIVE_BUILTIN_F64_IS_NAN 4u
#define JELLO_NATIVE_BUILTIN_F64_IS_INFINITE 5u
#define JELLO_NATIVE_BUILTIN_STD_STRING 6u
#define JELLO_NATIVE_BUILTIN_JDLL_INIT 7u
#define JELLO_NATIVE_BUILTIN_SYSTEM_ARGS 8u
#define JELLO_NATIVE_BUILTIN_SYSTEM_ASSERT_EQ 9u
#define JELLO_NATIVE_BUILTIN_DYN_EQ 10u
#define JELLO_NATIVE_BUILTIN_DEEP_EQUAL 11u
#define JELLO_NATIVE_BUILTIN_COUNT 47u

#include <jello/internal/math.h>

/* math_sqrt(x: F64) -> F64. arg_reg = first arg, dst_reg = result. */
static void native_math_sqrt(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  call_frame* fr = ctx->fr;
  double x = vm_load_f64(&fr->rf, arg_reg);
  double y = sqrt(x);
  vm_store_f64(&fr->rf, dst_reg, y);
}

/* System.exit([code: I32]) -> never returns. No args: exit(123) for REPL. */
static void native_system_exit(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  (void)ctx;
  int code = 123;
  if(ins->c >= 1u) {
    code = (int)(int32_t)vm_load_u32(&ctx->fr->rf, first_arg_reg);
  }
  exit(code);
}

/* System.args -> List<Bytes>. Program argv set via jello_vm_set_program_args. */
static void native_system_args(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  (void)arg_reg;
  jello_vm* vm = ctx->vm;
  const jello_bc_module* m = ctx->m;
  uint32_t list_type_id = ctx->fr->f->reg_types[dst_reg];
  if(list_type_id >= m->ntypes || m->types[list_type_id].kind != JELLO_T_LIST) {
    jello_vm_panic();
  }
  jello_type_id bytes_type_id = m->types[list_type_id].as.unary.elem;
  jello_list* tail = NULL;
  for(int i = (int)vm->program_argc - 1; i >= 0; i--) {
    const char* s = vm->program_argv && vm->program_argv[i] ? vm->program_argv[i] : "";
    size_t slen = strlen(s);
    jello_bytes* b = jello_bytes_new(vm, bytes_type_id, (uint32_t)slen);
    if(b && slen > 0) memcpy(b->data, s, slen);
    jello_value head = jello_from_ptr(b);
    tail = jello_list_cons(vm, list_type_id, head, tail);
  }
  vm_store_ptr(&ctx->fr->rf, dst_reg, tail);
}

/* I32.to_bytes(x: I32) -> Bytes. Converts integer to decimal string (UTF-8). */
static void native_i32_to_bytes(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  int32_t x = (int32_t)vm_load_u32(&ctx->fr->rf, arg_reg);
  char buf[16];
  int n = snprintf(buf, sizeof buf, "%" PRId32, x);
  if (n < 0 || (size_t)n >= sizeof buf) n = 0;
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->fr->f;
  uint32_t type_id = f->reg_types[dst_reg];
  jello_bytes* b = jello_bytes_new(vm, type_id, (uint32_t)n);
  if (b && n > 0) {
    for (int i = 0; i < n; i++) b->data[i] = (uint8_t)buf[i];
  }
  vm_store_ptr(&ctx->fr->rf, dst_reg, b);
}

/* F64.to_bytes(x: F64) -> Bytes. Converts double to string (UTF-8), %g format. */
static void native_f64_to_bytes(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  double x = vm_load_f64(&ctx->fr->rf, arg_reg);
  char buf[64];
  int n = snprintf(buf, sizeof buf, "%g", x);
  if (n < 0 || (size_t)n >= sizeof buf) n = 0;
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->fr->f;
  uint32_t type_id = f->reg_types[dst_reg];
  jello_bytes* b = jello_bytes_new(vm, type_id, (uint32_t)n);
  if (b && n > 0) {
    for (int i = 0; i < n; i++) b->data[i] = (uint8_t)buf[i];
  }
  vm_store_ptr(&ctx->fr->rf, dst_reg, b);
}

/* Float.is_nan(x: F64) -> Bool. */
static void native_f64_is_nan(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  double x = vm_load_f64(&ctx->fr->rf, arg_reg);
  vm_store_u32(&ctx->fr->rf, dst_reg, (uint32_t)(isnan(x) ? 1 : 0));
}

/* Float.is_infinite(x: F64) -> Bool. */
static void native_f64_is_infinite(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  double x = vm_load_f64(&ctx->fr->rf, arg_reg);
  vm_store_u32(&ctx->fr->rf, dst_reg, (uint32_t)(isinf(x) ? 1 : 0));
}

static jello_bytes* value_to_bytes(exec_ctx* ctx, uint32_t dst_reg, jello_value v) {
  jello_vm* vm = ctx->vm;
  const jello_bc_function* f = ctx->fr->f;
  uint32_t type_id = f->reg_types[dst_reg];
  char buf[128];
  int n = 0;

  if(jello_is_null(v)) {
    n = snprintf(buf, sizeof buf, "null");
  } else if(jello_is_bool(v)) {
    n = snprintf(buf, sizeof buf, "%s", jello_as_bool(v) ? "true" : "false");
  } else if(jello_is_i32(v)) {
    n = snprintf(buf, sizeof buf, "%" PRId32, jello_as_i32(v));
  } else if(jello_is_box_f64(v)) {
    n = snprintf(buf, sizeof buf, "%g", jello_as_box_f64(v));
  } else if(jello_is_box_f32(v)) {
    n = snprintf(buf, sizeof buf, "%g", (double)jello_as_box_f32(v));
  } else if(jello_is_box_f16(v)) {
    n = snprintf(buf, sizeof buf, "%g", (double)jello_as_box_f16(v));
  } else if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BYTES) {
    jello_bytes* src = (jello_bytes*)jello_as_ptr(v);
    jello_bytes* out = jello_bytes_new(vm, type_id, src ? src->length : 0);
    if(out && src && src->length) memcpy(out->data, src->data, src->length);
    return out;
  } else if(jello_is_atom(v)) {
    uint32_t aid = jello_as_atom(v);
    const jello_bc_module* m = ctx->m;
    if(m && aid < m->natoms && m->atoms[aid]) {
      const char* name = m->atoms[aid];
      size_t len = strlen(name);
      jello_bytes* out = jello_bytes_new(vm, type_id, (uint32_t)len);
      if(out && len > 0) memcpy(out->data, name, len);
      return out;
    }
    n = snprintf(buf, sizeof buf, "atom:%u", aid);
  } else if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_ENUM) {
    jello_enum* e = (jello_enum*)jello_as_ptr(v);
    n = snprintf(buf, sizeof buf, "enum:%u", e ? e->tag : 0u);
  } else if(jello_is_ptr(v)) {
    n = snprintf(buf, sizeof buf, "[object]");
  } else {
    n = snprintf(buf, sizeof buf, "?");
  }

  if(n < 0 || (size_t)n >= sizeof buf) n = 0;
  jello_bytes* b = jello_bytes_new(vm, type_id, (uint32_t)n);
  if(b && n > 0) {
    for(int i = 0; i < n; i++) b->data[i] = (uint8_t)buf[i];
  }
  return b;
}

/* Std.string(v: Dynamic) -> Bytes */
static void native_std_string(exec_ctx* ctx, uint32_t dst_reg, uint32_t arg_reg) {
  jello_value v = vm_box_from_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, arg_reg);
  jello_bytes* b = value_to_bytes(ctx, dst_reg, v);
  vm_store_ptr(&ctx->fr->rf, dst_reg, b);
}

static int bytes_content_equal(jello_bytes* a, jello_bytes* b) {
  if(a == b) return 1;
  if(!a || !b) return 0;
  if(a->length != b->length) return 0;
  if(a->length == 0) return 1;
  return memcmp(a->data, b->data, a->length) == 0;
}

static int value_is_numeric(jello_value v) {
  return jello_is_i32(v) || jello_is_box_i64(v) || jello_is_box_f64(v) ||
         jello_is_box_f32(v) || jello_is_box_f16(v);
}

static int value_as_f64(jello_value v, double* out) {
  if(jello_is_i32(v)) {
    *out = (double)jello_as_i32(v);
    return 1;
  }
  if(jello_is_box_i64(v)) {
    *out = (double)jello_as_box_i64(v);
    return 1;
  }
  if(jello_is_box_f64(v)) {
    *out = jello_as_box_f64(v);
    return 1;
  }
  if(jello_is_box_f32(v)) {
    *out = (double)jello_as_box_f32(v);
    return 1;
  }
  if(jello_is_box_f16(v)) {
    *out = (double)vm_f16_bits_to_f32(jello_as_box_f16(v));
    return 1;
  }
  return 0;
}

static int value_as_exact_i64(jello_value v, int64_t* out) {
  if(jello_is_i32(v)) {
    *out = (int64_t)jello_as_i32(v);
    return 1;
  }
  if(jello_is_box_i64(v)) {
    *out = jello_as_box_i64(v);
    return 1;
  }
  return 0;
}

static int f64_is_exact_i64(double f, int64_t* out) {
  if(!isfinite(f)) return 0;
  double t = trunc(f);
  if(f != t) return 0;
  if(t > (double)INT64_MAX || t < (double)INT64_MIN) return 0;
  *out = (int64_t)t;
  return 1;
}

/* Numbers compare by mathematical value (Lua-style): any integer width
 * compares equal when values match; integer equals float when the float is
 * an exact integral representation of the same value. */
static int numeric_values_equal(jello_value a, jello_value b) {
  int64_t ai = 0;
  int64_t bi = 0;
  int a_int = value_as_exact_i64(a, &ai);
  int b_int = value_as_exact_i64(b, &bi);
  if(a_int && b_int) return ai == bi;

  if(a_int && !b_int) {
    double bf = 0.0;
    if(!value_as_f64(b, &bf)) return 0;
    if(f64_is_exact_i64(bf, &bi)) return ai == bi;
    return (double)ai == bf;
  }
  if(!a_int && b_int) {
    double af = 0.0;
    if(!value_as_f64(a, &af)) return 0;
    if(f64_is_exact_i64(af, &ai)) return ai == bi;
    return af == (double)bi;
  }

  double af = 0.0;
  double bf = 0.0;
  if(!value_as_f64(a, &af) || !value_as_f64(b, &bf)) return 0;
  return af == bf;
}

static int values_equal(jello_value a, jello_value b) {
  if(a == b) return 1;
  if(jello_is_null(a) || jello_is_null(b)) return jello_is_null(a) && jello_is_null(b);
  if(jello_is_bool(a) || jello_is_bool(b)) {
    return jello_is_bool(a) && jello_is_bool(b) && jello_as_bool(a) == jello_as_bool(b);
  }
  if(value_is_numeric(a) || value_is_numeric(b)) {
    if(!value_is_numeric(a) || !value_is_numeric(b)) return 0;
    return numeric_values_equal(a, b);
  }
  if(jello_is_atom(a) || jello_is_atom(b)) {
    return jello_is_atom(a) && jello_is_atom(b) && jello_as_atom(a) == jello_as_atom(b);
  }
  if(jello_is_ptr(a) && jello_is_ptr(b) && jello_obj_kind_of(a) == (uint32_t)JELLO_OBJ_BYTES &&
     jello_obj_kind_of(b) == (uint32_t)JELLO_OBJ_BYTES) {
    return bytes_content_equal((jello_bytes*)jello_as_ptr(a), (jello_bytes*)jello_as_ptr(b));
  }
  if(jello_is_ptr(a) && jello_is_ptr(b) && jello_obj_kind_of(a) == (uint32_t)JELLO_OBJ_ENUM &&
     jello_obj_kind_of(b) == (uint32_t)JELLO_OBJ_ENUM) {
    jello_enum* ea = (jello_enum*)jello_as_ptr(a);
    jello_enum* eb = (jello_enum*)jello_as_ptr(b);
    if(!ea || !eb) return ea == eb;
    /* Match compile-time enum `==`: tag + field-wise compare (not type_id). */
    if(ea->tag != eb->tag) return 0;
    if(ea->nfields != eb->nfields) return 0;
    for(uint32_t i = 0; i < ea->nfields; i++) {
      if(!values_equal(ea->fields[i], eb->fields[i])) return 0;
    }
    return 1;
  }
  return 0;
}

static int value_snippet(char* buf, size_t bufsz, jello_value v) {
  if(bufsz == 0) return 0;
  if(jello_is_null(v)) return snprintf(buf, bufsz, "null");
  if(jello_is_bool(v)) return snprintf(buf, bufsz, "%s", jello_as_bool(v) ? "true" : "false");
  if(jello_is_i32(v)) return snprintf(buf, bufsz, "%" PRId32, jello_as_i32(v));
  if(jello_is_box_i64(v)) return snprintf(buf, bufsz, "%" PRId64, jello_as_box_i64(v));
  if(jello_is_box_f64(v)) return snprintf(buf, bufsz, "%g", jello_as_box_f64(v));
  if(jello_is_box_f32(v)) return snprintf(buf, bufsz, "%g", (double)jello_as_box_f32(v));
  if(jello_is_box_f16(v)) return snprintf(buf, bufsz, "%g", (double)jello_as_box_f16(v));
  if(jello_is_atom(v)) return snprintf(buf, bufsz, "atom:%u", jello_as_atom(v));
  if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_BYTES) {
    jello_bytes* b = (jello_bytes*)jello_as_ptr(v);
    if(!b) return snprintf(buf, bufsz, "\"\"");
    uint32_t n = b->length;
    if(n > 48u) n = 48u;
    int w = snprintf(buf, bufsz, "\"%.*s\"", (int)n, (const char*)b->data);
    if(b->length > 48u && w > 0 && (size_t)w < bufsz) {
      (void)snprintf(buf + (size_t)w, bufsz - (size_t)w, "...");
    }
    return w;
  }
  if(jello_is_ptr(v) && jello_obj_kind_of(v) == (uint32_t)JELLO_OBJ_ENUM) {
    jello_enum* e = (jello_enum*)jello_as_ptr(v);
    return snprintf(buf, bufsz, "enum:%u", e ? e->tag : 0u);
  }
  if(jello_is_ptr(v)) return snprintf(buf, bufsz, "[object]");
  return snprintf(buf, bufsz, "?");
}

/* Dynamic value equality (content-aware for bytes, numeric, atom, …). */
static void native_dyn_eq(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  jello_value a = vm_box_from_typed(vm, ctx->m, ctx->f, &fr->rf, first_arg_reg);
  jello_value b = vm_box_from_typed(vm, ctx->m, ctx->f, &fr->rf, first_arg_reg + 1u);
  vm_store_u32(&fr->rf, ins->a, (uint32_t)values_equal(a, b));
}

/* System.assertEq(actual, expected, msg?) -> Bool */
static int bytes_ends_with_colon_space(const jello_bytes* mb) {
  return mb && mb->length >= 2u &&
         mb->data[mb->length - 2u] == (uint8_t)':' &&
         mb->data[mb->length - 1u] == (uint8_t)' ';
}

static void native_system_assert_eq(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  jello_value actual = vm_box_from_typed(vm, ctx->m, ctx->f, &fr->rf, first_arg_reg);
  jello_value expected = vm_box_from_typed(vm, ctx->m, ctx->f, &fr->rf, first_arg_reg + 1u);
  if(values_equal(actual, expected)) {
    vm_store_u32(&fr->rf, ins->a, 1u);
    return;
  }

  char actual_buf[128];
  char expected_buf[128];
  value_snippet(actual_buf, sizeof actual_buf, actual);
  value_snippet(expected_buf, sizeof expected_buf, expected);

  if(ins->c >= 3u) {
    jello_value msgv = vm_box_from_typed(vm, ctx->m, ctx->f, &fr->rf, first_arg_reg + 2u);
    if(jello_is_ptr(msgv) && jello_obj_kind_of(msgv) == (uint32_t)JELLO_OBJ_BYTES) {
      jello_bytes* mb = (jello_bytes*)jello_as_ptr(msgv);
      if(mb && mb->length > 0) {
        uint32_t n = mb->length;
        if(n > 160u) n = 160u;
        if(bytes_ends_with_colon_space(mb)) {
          snprintf(vm->trap_msg_buf, sizeof vm->trap_msg_buf, "%.*sexpected %s but got %s",
                   (int)n, (const char*)mb->data, expected_buf, actual_buf);
        } else {
          snprintf(vm->trap_msg_buf, sizeof vm->trap_msg_buf, "%.*s: expected %s but got %s",
                   (int)n, (const char*)mb->data, expected_buf, actual_buf);
        }
        (void)jello_vm_trap(vm, JELLO_TRAP_THROWN, vm->trap_msg_buf);
        return;
      }
    }
  }

  snprintf(vm->trap_msg_buf, sizeof vm->trap_msg_buf, "expected %s but got %s",
           expected_buf, actual_buf);
  (void)jello_vm_trap(vm, JELLO_TRAP_THROWN, vm->trap_msg_buf);
}

static uint32_t module_bytes_type_id(const jello_bc_module* m) {
  for(uint32_t i = 0; i < m->ntypes; i++) {
    if(m->types[i].kind == JELLO_T_BYTES) return i;
  }
  return 0u;
}

static void native_deep_equal(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  jello_vm* vm = ctx->vm;
  call_frame* fr = ctx->fr;
  const jello_bc_module* m = ctx->m;
  jello_value a = vm_box_from_typed(vm, m, ctx->f, &fr->rf, first_arg_reg);
  jello_value b = vm_box_from_typed(vm, m, ctx->f, &fr->rf, first_arg_reg + 1u);
  int eq = values_equal(a, b);
  char diff_buf[256];
  diff_buf[0] = 0;
  if(!eq) {
    char ab[96];
    char bb[96];
    value_snippet(ab, sizeof ab, a);
    value_snippet(bb, sizeof bb, b);
    snprintf(diff_buf, sizeof diff_buf, "expected %s but got %s", bb, ab);
  }
  uint32_t obj_tid = fr->f->reg_types[ins->a];
  jello_object* o = jello_object_new(vm, obj_tid);
  uint32_t eq_atom = vm_module_atom_id_or_default(m, "equal", JELLO_ATOM_EQUAL);
  uint32_t diff_atom = vm_module_atom_id_or_default(m, "diff", JELLO_ATOM_DIFF);
  jello_object_set(o, eq_atom, jello_make_bool(eq ? 1 : 0));
  if(eq) {
    jello_object_set(o, diff_atom, jello_make_null());
  } else {
    uint32_t bytes_tid = module_bytes_type_id(m);
    jello_bytes* diff = jello_bytes_new(vm, bytes_tid, (uint32_t)strlen(diff_buf));
    if(diff && diff_buf[0]) memcpy(diff->data, diff_buf, strlen(diff_buf));
    jello_object_set(o, diff_atom, jello_from_ptr(diff));
  }
  vm_store_ptr(&fr->rf, ins->a, o);
}

/* jdll_init(exports: Object, key: Bytes) -> Dynamic */
static void native_jdll_init(exec_ctx* ctx, const jello_insn* ins, uint32_t first_arg_reg) {
  (void)ins;
  if(!jello_jdll_fill_exports(ctx, first_arg_reg, first_arg_reg + 1u)) return;
  jello_value v = vm_box_from_typed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, first_arg_reg);
  vm_store_from_boxed(ctx->vm, ctx->m, ctx->f, &ctx->fr->rf, ins->a, v);
}

int jello_is_jdll_prim(uint32_t func_index) {
  return func_index == JELLO_FUNC_INDEX_JDLL_PRIM;
}

int jello_is_native_builtin(uint32_t func_index) {
  return func_index < JELLO_NATIVE_BUILTIN_COUNT;
}

/* Invoke native builtin. For JOP_CALL: first_arg=ins->b. For JOP_CALLR: first_arg=ins->imm. */
void jello_invoke_native_builtin(exec_ctx* ctx, const jello_insn* ins, uint32_t func_index, uint32_t first_arg_reg) {
  if(func_index >= JELLO_NATIVE_BUILTIN_COUNT) jello_vm_panic();
  if(func_index == JELLO_NATIVE_BUILTIN_MATH_SQRT) {
    native_math_sqrt(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_SYSTEM_EXIT) {
    native_system_exit(ctx, ins, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_I32_TO_BYTES) {
    native_i32_to_bytes(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_F64_TO_BYTES) {
    native_f64_to_bytes(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_F64_IS_NAN) {
    native_f64_is_nan(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_F64_IS_INFINITE) {
    native_f64_is_infinite(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_STD_STRING) {
    native_std_string(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_JDLL_INIT) {
    native_jdll_init(ctx, ins, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_SYSTEM_ARGS) {
    native_system_args(ctx, ins->a, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_SYSTEM_ASSERT_EQ) {
    native_system_assert_eq(ctx, ins, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_DYN_EQ) {
    native_dyn_eq(ctx, ins, first_arg_reg);
    return;
  }
  if(func_index == JELLO_NATIVE_BUILTIN_DEEP_EQUAL) {
    native_deep_equal(ctx, ins, first_arg_reg);
    return;
  }
  if(func_index >= JELLO_NATIVE_BUILTIN_MATH_ABS && func_index <= JELLO_NATIVE_BUILTIN_MATH_SIGN) {
    jello_invoke_math_native(ctx, ins, func_index, first_arg_reg);
    return;
  }
  if(func_index == 46u) {
    jello_invoke_macro_host_native(ctx, ins, first_arg_reg);
    return;
  }
  jello_vm_panic();
}
