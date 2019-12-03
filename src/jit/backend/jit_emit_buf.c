// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>

#include <stdlib.h>
#include <string.h>

void jello_jit_emit_buf_free(jello_jit_emit_buf* buf) {
  if(!buf) return;
  free(buf->data);
  buf->data = NULL;
  buf->size = 0;
  buf->cap = 0;
}

int jello_jit_emit_buf_reserve(jello_jit_emit_buf* buf, size_t extra) {
  if(!buf) return -1;
  size_t need = buf->size + extra;
  if(need <= buf->cap) return 0;
  size_t ncap = buf->cap ? buf->cap * 2u : 256u;
  while(ncap < need) ncap *= 2u;
  uint8_t* nd = (uint8_t*)realloc(buf->data, ncap);
  if(!nd) return -1;
  buf->data = nd;
  buf->cap = ncap;
  return 0;
}

static int emit_u32(jello_jit_emit_buf* buf, uint32_t word) {
  if(jello_jit_emit_buf_reserve(buf, 4u) != 0) return -1;
  memcpy(buf->data + buf->size, &word, 4u);
  buf->size += 4u;
  return 0;
}

const jello_jit_backend* jello_jit_backend_select(void) {
#if defined(JELLOVM_JIT_ARM64)
  extern const jello_jit_backend jello_jit_backend_arm64;
  return &jello_jit_backend_arm64;
#elif defined(JELLOVM_JIT_X64)
  extern const jello_jit_backend jello_jit_backend_x64;
  return &jello_jit_backend_x64;
#else
  return NULL;
#endif
}

#if defined(JELLOVM_JIT_ARM64)
extern const jello_jit_backend jello_jit_backend_arm64;
#endif
#if defined(JELLOVM_JIT_X64)
extern const jello_jit_backend jello_jit_backend_x64;
#endif

int jello_jit_emit_u32(jello_jit_emit_buf* buf, uint32_t word) {
  return emit_u32(buf, word);
}

int jello_jit_emit_bytes(jello_jit_emit_buf* buf, const uint8_t* bytes, size_t len) {
  if(!buf || !bytes || len == 0u) return -1;
  if(jello_jit_emit_buf_reserve(buf, len) != 0) return -1;
  memcpy(buf->data + buf->size, bytes, len);
  buf->size += len;
  return 0;
}
