// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#ifndef JELLO_JDLL_H
#define JELLO_JDLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque call context passed to every JDLL export. */
typedef struct jdlo_ctx jdlo_ctx;

typedef uintptr_t jdl_value;
typedef void (*jdl_abstract_finalizer)(void* payload);

int jdl_arg_count(jdlo_ctx* c);

struct jello_vm* jdl_ctx_vm(jdlo_ctx* c);
const struct jello_bc_module* jdl_ctx_module(jdlo_ctx* c);

/* --- argument access --- */
int jdl_arg_bool(jdlo_ctx* c, int index);                        // Get bool argument
int32_t jdl_arg_i32(jdlo_ctx* c, int index);                     // Get i32 argument
int8_t jdl_arg_i8(jdlo_ctx* c, int index);                       // Get i8 argument
int16_t jdl_arg_i16(jdlo_ctx* c, int index);                     // Get i16 argument
int64_t jdl_arg_i64(jdlo_ctx* c, int index);                     // Get i64 argument
float jdl_arg_f32(jdlo_ctx* c, int index);                       // Get f32 argument
float jdl_arg_f16(jdlo_ctx* c, int index);                       // Get f16 argument
double jdl_arg_f64(jdlo_ctx* c, int index);                      // Get f64 argument
uint32_t jdl_arg_atom(jdlo_ctx* c, int index);                   // Get atom argument
const uint8_t* jdl_arg_bytes_data(jdlo_ctx* c, int index);       // Get bytes data
uint32_t jdl_arg_bytes_len(jdlo_ctx* c, int index);              // Get bytes length
struct jello_object* jdl_arg_object(jdlo_ctx* c, int index);     // Get object argument
struct jello_array* jdl_arg_array(jdlo_ctx* c, int index);       // Get array argument
struct jello_list* jdl_arg_list(jdlo_ctx* c, int index);         // Get list argument
struct jello_abstract* jdl_arg_abstract(jdlo_ctx* c, int index); // Get abstract argument
void* jdl_arg_abstract_payload(jdlo_ctx* c, int index);          // Get abstract payload
jdl_value jdl_arg_value(jdlo_ctx* c, int index);                 // Get value argument

/* --- type tests (on boxed jdl_value) --- */
int jdl_is_null(jdl_value v);
int jdl_is_bool(jdl_value v);
int jdl_is_i32(jdl_value v);
int jdl_is_i64(jdl_value v);
int jdl_is_f16(jdl_value v);
int jdl_is_f32(jdl_value v);
int jdl_is_f64(jdl_value v);
int jdl_is_atom(jdl_value v);
int jdl_is_bytes(jdl_value v);
int jdl_is_object(jdl_value v);
int jdl_is_array(jdl_value v);
int jdl_is_list(jdl_value v);
int jdl_is_abstract(jdl_value v);

/* --- callbacks --- */
int jdl_is_fun(jdl_value v);
struct jello_function* jdl_as_fun(jdl_value v);
struct jello_function* jdl_arg_fun(jdlo_ctx* c, int index);       // Get function argument
/**
 * Call a function with the given arguments.
 * @param c The call context.
 * @param this_obj The this object.
 * @param fun The function to call.
 * @param args The arguments to pass to the function.
 * @param nargs The number of arguments.
 * @param out The output value.
 * @param exc_out The output exception value.
 * @return 1 if the function call succeeded, 0 otherwise.
 */
int jdl_call_ex(jdlo_ctx* c, jdl_value this_obj, jdl_value fun, jdl_value* args, int nargs,
                jdl_value* out, jdl_value* exc_out);

/* --- GC roots (v1: stack order; release in reverse push order) --- */
void jdl_gc_root(jdlo_ctx* c, jdl_value v);                  // Add a GC root
void jdl_gc_release(jdlo_ctx* c, uint32_t count);            // Release GC roots

/* --- returns --- */
void jdl_return_bool(jdlo_ctx* c, int v);                    // Return a bool value
void jdl_return_i32(jdlo_ctx* c, int32_t v);                 // Return an i32 value
void jdl_return_i8(jdlo_ctx* c, int8_t v);                   // Return an i8 value
void jdl_return_i16(jdlo_ctx* c, int16_t v);                 // Return an i16 value
void jdl_return_i64(jdlo_ctx* c, int64_t v);                 // Return an i64 value
void jdl_return_f32(jdlo_ctx* c, float v);                   // Return an f32 value
void jdl_return_f16(jdlo_ctx* c, float v);                   // Return an f16 value
void jdl_return_f64(jdlo_ctx* c, double v);                  // Return an f64 value
void jdl_return_atom(jdlo_ctx* c, uint32_t atom_id);         // Return an atom value
void jdl_return_bytes_copy(jdlo_ctx* c, const uint8_t* data, uint32_t len); // Return a bytes value
void jdl_return_object(jdlo_ctx* c, struct jello_object* o); // Return an object value
void jdl_return_array(jdlo_ctx* c, struct jello_array* a);   // Return an array value
void jdl_return_list(jdlo_ctx* c, struct jello_list* l);     // Return a list value
void jdl_return_abstract(jdlo_ctx* c, void* payload, jdl_abstract_finalizer fin); // Return an abstract value
void jdl_close_abstract(jdlo_ctx* c, int index);             // Close an abstract value
void jdl_return_null(jdlo_ctx* c);                           // Return a null value
void jdl_return_value(jdlo_ctx* c, jdl_value v);             // Return a value value

/* --- object helpers (Tuple uses atom keys "0", "1", …) --- */
int jdl_obj_has_atom(struct jello_object* o, uint32_t atom_id);       // Check if an object has an atom
jdl_value jdl_obj_get_atom(struct jello_object* o, uint32_t atom_id); // Get an atom from an object
void jdl_obj_set_atom(jdlo_ctx* c, struct jello_object* o, uint32_t atom_id, jdl_value v); // Set an atom on an object

/* --- array helpers --- */
uint32_t jdl_array_len(struct jello_array* a);                        // Get the length of an array
jdl_value jdl_array_get(struct jello_array* a, uint32_t index);       // Get an element from an array

/* --- errors --- */
void jdl_fail(jdlo_ctx* c, const char* msg);                          // Fail with a message
/** Write a combined JDLL + bytecode stack trace into `buf` (NUL-terminated). Returns bytes written. */
int jdl_stack_trace(jdlo_ctx* c, char* buf, int buflen);              // Write a stack trace
void jdl_check_bool(jdlo_ctx* c, int index);                          // Check a bool argument
void jdl_check_i32(jdlo_ctx* c, int index);                           // Check an i32 argument
void jdl_check_i64(jdlo_ctx* c, int index);                           // Check an i64 argument
void jdl_check_bytes(jdlo_ctx* c, int index);                         // Check a bytes argument
void jdl_check_fun(jdlo_ctx* c, int index);                           // Check a function argument
void jdl_check_object(jdlo_ctx* c, int index);                        // Check an object argument
void jdl_check_abstract(jdlo_ctx* c, int index);                      // Check an abstract argument

/* v1: abstract kinds use the call-site register type_id; no runtime kind table yet. */
#define JDLL_DEFINE_KIND(name) /* Define an abstract kind */

#define JDLL_DEFINE_PRIM(jello_name, c_fn, arity) /* registered via .jdll.abi sidecar */

#define JDLL_DEFINE_PRIM_MULT(jello_name) /* varargs; ABI arity 255 + one elem kind */

#ifdef __cplusplus
}
#endif

#endif /* JELLO_JDLL_H */
