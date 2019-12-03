// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jdll_internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JDLL_DEPS_MAGIC "JLYJDLLDEPS1"

typedef struct {
  uint32_t module_index;
  char* library_id;
} jdll_dep_entry;

typedef struct {
  jdll_dep_entry* items;
  uint32_t len;
} jdll_dep_list;

static int rd_u32le(const uint8_t* b, size_t blen, size_t* i, uint32_t* out) {
  if(*i + 4 > blen) return 0;
  *out = (uint32_t)b[*i] | ((uint32_t)b[*i + 1] << 8) | ((uint32_t)b[*i + 2] << 16) | ((uint32_t)b[*i + 3] << 24);
  *i += 4;
  return 1;
}

static int rd_str(const uint8_t* b, size_t blen, size_t* i, char** out) {
  uint32_t len = 0;
  if(!rd_u32le(b, blen, i, &len) || *i + len > blen) return 0;
  char* s = (char*)malloc((size_t)len + 1u);
  if(!s) return 0;
  memcpy(s, b + *i, len);
  s[len] = 0;
  *i += len;
  *out = s;
  return 1;
}

static void dep_list_free(jdll_dep_list* deps) {
  if(!deps) return;
  for(uint32_t i = 0; i < deps->len; i++) free(deps->items[i].library_id);
  free(deps->items);
  deps->items = NULL;
  deps->len = 0;
}

static int parse_jdll_deps_blob(const uint8_t* data, size_t blen, jdll_dep_list* out) {
  memset(out, 0, sizeof(*out));
  size_t magic_len = strlen(JDLL_DEPS_MAGIC) + 1u;
  if(blen < magic_len || memcmp(data, JDLL_DEPS_MAGIC, magic_len) != 0) return 0;
  size_t i = magic_len;
  uint32_t version = 0;
  if(!rd_u32le(data, blen, &i, &version) || version != 1u) return 0;
  uint32_t n = 0;
  if(!rd_u32le(data, blen, &i, &n)) return 0;
  if(n == 0) return 1;
  jdll_dep_entry* items = (jdll_dep_entry*)calloc(n, sizeof(jdll_dep_entry));
  if(!items) return 0;
  for(uint32_t e = 0; e < n; e++) {
    if(!rd_u32le(data, blen, &i, &items[e].module_index) || !rd_str(data, blen, &i, &items[e].library_id)) {
      jdll_dep_list tmp = { .items = items, .len = e };
      dep_list_free(&tmp);
      return 0;
    }
  }
  out->items = items;
  out->len = n;
  return 1;
}

static int extract_jdll_deps_module(const jello_bc_module* m, jdll_dep_list* out) {
  memset(out, 0, sizeof(*out));
  if(!m) return 0;
  for(uint32_t idx = 0; idx < m->nconst_bytes; idx++) {
    uint32_t len = m->const_bytes_len[idx];
    uint32_t off = m->const_bytes_off[idx];
    const uint8_t* blob = m->const_bytes_data + off;
    if(parse_jdll_deps_blob(blob, len, out)) return 1;
  }
  return 0;
}

int jello_jdll_preflight_module(const jello_bc_module* m, const char* entry_path) {
  jdll_dep_list deps = {0};
  if(!extract_jdll_deps_module(m, &deps)) return 1;

  const char* entry = entry_path ? entry_path : ".";
  for(uint32_t i = 0; i < deps.len; i++) {
    char* path = jello_jdll_resolve_library(entry, deps.items[i].library_id);
    if(!path) {
      fprintf(stderr, "error: native library '%s' not found (expected %s.jdll)\n",
              deps.items[i].library_id, deps.items[i].library_id);
      dep_list_free(&deps);
      return 0;
    }
    free(path);
  }
  dep_list_free(&deps);
  return 1;
}
