// SPDX-License-Identifier: BSD-3-Clause

#include <jello.h>
#include <jello/jdll.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

JDLL_DEFINE_KIND(file);

typedef struct jdll_file_handle {
  FILE* fp;
} jdll_file_handle;

static int path_exists(const char* path) {
  return access(path, F_OK) == 0;
}

static int bytes_to_path(const uint8_t* data, uint32_t len, char* out, size_t cap) {
  if(!data || !len || cap < 2) return 0;
  uint32_t n = len < cap - 1u ? len : (uint32_t)(cap - 1u);
  memcpy(out, data, n);
  out[n] = 0;
  return 1;
}

static void jdll_file_handle_fin(void* payload) {
  jdll_file_handle* h = (jdll_file_handle*)payload;
  if(!h) return;
  if(h->fp) {
    fclose(h->fp);
    h->fp = NULL;
  }
  free(h);
}

static jdll_file_handle* file_handle_from_arg(jdlo_ctx* c, int index) {
  jello_abstract* a = jdl_arg_abstract(c, index);
  if(!a || !a->payload) return NULL;
  return (jdll_file_handle*)a->payload;
}

static int read_fp_to_bytes(jdlo_ctx* c, FILE* f) {
  if(!f) {
    jdl_fail(c, "file read: null handle");
    return 0;
  }
  if(fseek(f, 0, SEEK_END) != 0) {
    jdl_fail(c, "file read: seek failed");
    return 0;
  }
  long sz = ftell(f);
  if(sz < 0) {
    jdl_fail(c, "file read: tell failed");
    return 0;
  }
  if(fseek(f, 0, SEEK_SET) != 0) {
    jdl_fail(c, "file read: seek failed");
    return 0;
  }
  if(sz == 0) {
    jdl_return_bytes_copy(c, NULL, 0);
    return 1;
  }
  if(sz > 64 * 1024 * 1024) {
    jdl_fail(c, "file read: file too large");
    return 0;
  }
  uint8_t* buf = (uint8_t*)malloc((size_t)sz);
  if(!buf) {
    jdl_fail(c, "file read: oom");
    return 0;
  }
  size_t got = fread(buf, 1, (size_t)sz, f);
  if(got != (size_t)sz) {
    free(buf);
    jdl_fail(c, "file read: read failed");
    return 0;
  }
  jdl_return_bytes_copy(c, buf, (uint32_t)sz);
  free(buf);
  return 1;
}

void jdll_std_file_exists(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  char path[4096];
  if(!bytes_to_path(data, len, path, sizeof path)) {
    jdl_return_bool(c, 0);
    return;
  }
  jdl_return_bool(c, path_exists(path));
}

void jdll_std_file_read(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  char path[4096];
  if(!bytes_to_path(data, len, path, sizeof path)) {
    jdl_return_bytes_copy(c, NULL, 0);
    return;
  }
  FILE* f = fopen(path, "rb");
  if(!f) {
    jdl_fail(c, "file_read: open failed");
    return;
  }
  (void)read_fp_to_bytes(c, f);
  fclose(f);
}

void jdll_std_file_open(jdlo_ctx* c) {
  const uint8_t* data = jdl_arg_bytes_data(c, 0);
  uint32_t len = jdl_arg_bytes_len(c, 0);
  char path[4096];
  if(!bytes_to_path(data, len, path, sizeof path)) {
    jdl_fail(c, "file_open: bad path");
    return;
  }
  FILE* fp = fopen(path, "rb");
  if(!fp) {
    jdl_fail(c, "file_open: open failed");
    return;
  }
  jdll_file_handle* h = (jdll_file_handle*)malloc(sizeof(*h));
  if(!h) {
    fclose(fp);
    jdl_fail(c, "file_open: oom");
    return;
  }
  h->fp = fp;
  jdl_return_abstract(c, h, jdll_file_handle_fin);
}

void jdll_std_file_close(jdlo_ctx* c) {
  jello_abstract* a = jdl_arg_abstract(c, 0);
  if(!a || !a->payload) {
    jdl_return_bool(c, 0);
    return;
  }
  jdl_close_abstract(c, 0);
  jdl_return_bool(c, 1);
}

void jdll_std_file_read_handle(jdlo_ctx* c) {
  jdll_file_handle* h = file_handle_from_arg(c, 0);
  if(!h || !h->fp) {
    jdl_fail(c, "file_read_handle: invalid handle");
    return;
  }
  (void)read_fp_to_bytes(c, h->fp);
}

void jdll_std_file_write(jdlo_ctx* c) {
  const uint8_t* path_data = jdl_arg_bytes_data(c, 0);
  uint32_t path_len = jdl_arg_bytes_len(c, 0);
  const uint8_t* body = jdl_arg_bytes_data(c, 1);
  uint32_t body_len = jdl_arg_bytes_len(c, 1);
  char path[4096];
  if(!bytes_to_path(path_data, path_len, path, sizeof path)) {
    jdl_return_bool(c, 0);
    return;
  }
  FILE* f = fopen(path, "wb");
  if(!f) {
    jdl_return_bool(c, 0);
    return;
  }
  if(body_len > 0) {
    if(fwrite(body, 1, body_len, f) != body_len) {
      fclose(f);
      jdl_return_bool(c, 0);
      return;
    }
  }
  if(fclose(f) != 0) {
    jdl_return_bool(c, 0);
    return;
  }
  jdl_return_bool(c, 1);
}
