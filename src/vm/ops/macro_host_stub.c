// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <stddef.h>
#include <stdint.h>

// Default no-op stubs for standalone jellovm (jellovm_cli). jelloc embed-vm provides
// strong Rust definitions when JELLOVM_MACRO_HOST_STUB=OFF.
//
// ELF/macOS: weak stubs are overridden by jelloc's Rust symbols when linked together.
// MinGW PE/COFF: weak symbols are not reliable — use strong stubs for jellovm.dll.

#if defined(__GNUC__) && !defined(__MINGW32__) && !defined(_WIN32)
#define JELLO_WEAK __attribute__((weak))
#elif defined(_MSC_VER)
#define JELLO_WEAK __declspec(selectany)
#else
#define JELLO_WEAK
#endif

static int32_t macro_host_unavailable(void) { return -1; }

int32_t jello_rust_macro_emit(int32_t frag_id);
int32_t jello_rust_macro_show(int32_t frag_id);
int32_t jello_rust_macro_parse(const char* src, size_t src_len, const char* kind,
                               size_t kind_len, int32_t* out_id);
int32_t jello_rust_macro_gensym(const char* prefix, size_t prefix_len, int32_t* out_id);
int32_t jello_rust_macro_quote(uint32_t template_idx, int32_t* out_id);
int32_t jello_rust_macro_param(uint32_t param_idx, int32_t* out_id);
int32_t jello_rust_macro_emit_all(int32_t frag_id);

JELLO_WEAK int32_t jello_rust_macro_emit(int32_t frag_id) {
  (void)frag_id;
  return macro_host_unavailable();
}

JELLO_WEAK int32_t jello_rust_macro_show(int32_t frag_id) {
  (void)frag_id;
  return macro_host_unavailable();
}

JELLO_WEAK int32_t jello_rust_macro_parse(const char* src, size_t src_len, const char* kind,
                                          size_t kind_len, int32_t* out_id) {
  (void)src;
  (void)src_len;
  (void)kind;
  (void)kind_len;
  (void)out_id;
  return macro_host_unavailable();
}

JELLO_WEAK int32_t jello_rust_macro_gensym(const char* prefix, size_t prefix_len, int32_t* out_id) {
  (void)prefix;
  (void)prefix_len;
  (void)out_id;
  return macro_host_unavailable();
}

JELLO_WEAK int32_t jello_rust_macro_quote(uint32_t template_idx, int32_t* out_id) {
  (void)template_idx;
  (void)out_id;
  return macro_host_unavailable();
}

JELLO_WEAK int32_t jello_rust_macro_param(uint32_t param_idx, int32_t* out_id) {
  (void)param_idx;
  (void)out_id;
  return macro_host_unavailable();
}

JELLO_WEAK int32_t jello_rust_macro_emit_all(int32_t frag_id) {
  (void)frag_id;
  return macro_host_unavailable();
}
