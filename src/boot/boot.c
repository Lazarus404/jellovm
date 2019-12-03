// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/boot_internal.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

#if defined(_WIN32)
#  include <direct.h>
#  ifndef PATH_MAX
#    define PATH_MAX 4096
#  endif
#else
#  include <limits.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#define JELLO_BOOT_IO_CHUNK 65536u

static int read_file(const char* path, uint8_t** out_data, size_t* out_size) {
  if(out_data) *out_data = NULL;
  if(out_size) *out_size = 0;
  FILE* f = fopen(path, "rb");
  if(!f) return -1;
  if(fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long sz = ftell(f);
  if(sz < 0) {
    fclose(f);
    return -1;
  }
  if(fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if(!data) {
    fclose(f);
    return -1;
  }
  if(fread(data, 1, (size_t)sz, f) != (size_t)sz) {
    free(data);
    fclose(f);
    return -1;
  }
  fclose(f);
  if(out_data) *out_data = data;
  else free(data);
  if(out_size) *out_size = (size_t)sz;
  return 0;
}

static void wr_u64_le(uint8_t* p, uint64_t v) {
  for(unsigned i = 0; i < 8u; i++) {
    p[i] = (uint8_t)((v >> (8u * i)) & 0xffu);
  }
}

static uint64_t rd_u64_le(const uint8_t* p) {
  uint64_t v = 0;
  for(unsigned i = 0; i < 8u; i++) {
    v |= ((uint64_t)p[i]) << (8u * i);
  }
  return v;
}

static int write_footer(FILE* f, uint64_t jlo_offset, uint64_t jlo_size) {
  uint8_t footer[JELLO_BOOT_FOOTER_SIZE];
  wr_u64_le(footer + 0, jlo_size);
  wr_u64_le(footer + 8, jlo_offset);
  memcpy(footer + 16, JELLO_BOOT_MAGIC, JELLO_BOOT_MAGIC_LEN);
  return fwrite(footer, 1, sizeof(footer), f) == sizeof(footer) ? 0 : -1;
}

static int parse_footer(const uint8_t* footer, uint64_t file_size, uint64_t* jlo_offset, uint64_t* jlo_size) {
  if(memcmp(footer + 16, JELLO_BOOT_MAGIC, JELLO_BOOT_MAGIC_LEN) != 0) return 0;
  *jlo_size = rd_u64_le(footer + 0);
  *jlo_offset = rd_u64_le(footer + 8);
  if(*jlo_size == 0) return 0;
  if(*jlo_offset >= file_size) return 0;
  if(*jlo_offset + *jlo_size > file_size - JELLO_BOOT_FOOTER_SIZE) return 0;
  return 1;
}

static int file_size_of(FILE* f, uint64_t* out_size) {
  if(fseek(f, 0, SEEK_END) != 0) return -1;
  long sz = ftell(f);
  if(sz < 0) return -1;
  *out_size = (uint64_t)sz;
  return 0;
}

static int read_footer_at_eof(FILE* f, uint64_t file_size, uint8_t footer[JELLO_BOOT_FOOTER_SIZE]) {
  if(file_size < JELLO_BOOT_FOOTER_SIZE) return -1;
  if(fseek(f, (long)(file_size - JELLO_BOOT_FOOTER_SIZE), SEEK_SET) != 0) return -1;
  return fread(footer, 1, JELLO_BOOT_FOOTER_SIZE, f) == JELLO_BOOT_FOOTER_SIZE ? 0 : -1;
}

/* Bare VM size: strip an existing boot payload + footer when present. */
static int vm_base_size(const char* vm_exe, uint64_t* out_base) {
  if(!vm_exe || !out_base) return -1;

  FILE* f = fopen(vm_exe, "rb");
  if(!f) return -1;

  uint64_t file_size = 0;
  if(file_size_of(f, &file_size) != 0) {
    fclose(f);
    return -1;
  }

  *out_base = file_size;
  if(file_size >= JELLO_BOOT_FOOTER_SIZE) {
    uint8_t footer[JELLO_BOOT_FOOTER_SIZE];
    uint64_t jlo_offset = 0;
    uint64_t jlo_size = 0;
    if(read_footer_at_eof(f, file_size, footer) == 0 &&
       parse_footer(footer, file_size, &jlo_offset, &jlo_size)) {
      *out_base = jlo_offset;
    }
  }

  fclose(f);
  return 0;
}

static int copy_bytes(FILE* in, FILE* out, uint64_t n) {
  uint8_t buf[JELLO_BOOT_IO_CHUNK];
  while(n > 0) {
    size_t chunk = n > JELLO_BOOT_IO_CHUNK ? JELLO_BOOT_IO_CHUNK : (size_t)n;
    if(fread(buf, 1, chunk, in) != chunk) return -1;
    if(fwrite(buf, 1, chunk, out) != chunk) return -1;
    n -= (uint64_t)chunk;
  }
  return 0;
}

int jello_boot_self_exe_path(char* out, size_t out_len) {
  if(!out || out_len == 0) return -1;
#if defined(__APPLE__)
  uint32_t size = (uint32_t)out_len;
  if(_NSGetExecutablePath(out, &size) != 0) return -1;
  return 0;
#elif defined(__linux__)
  ssize_t n = readlink("/proc/self/exe", out, out_len - 1u);
  if(n <= 0) return -1;
  out[n] = 0;
  return 0;
#else
  return -1;
#endif
}

int jello_boot_output_path(const char* jlo_path, char* out, size_t out_len) {
  if(!jlo_path || !out || out_len == 0) return -1;
  const char* slash = strrchr(jlo_path, '/');
#if defined(_WIN32)
  const char* bslash = strrchr(jlo_path, '\\');
  if(bslash && (!slash || bslash > slash)) slash = bslash;
#endif
  const char* base = slash ? slash + 1 : jlo_path;
  size_t dir_len = slash ? (size_t)(slash - jlo_path + 1) : 0;
  if(dir_len + 1 >= out_len) return -1;
  memcpy(out, jlo_path, dir_len);
  out[dir_len] = 0;

  char name[PATH_MAX];
  if(strlen(base) >= sizeof(name)) return -1;
  strcpy(name, base);
  char* dot = strrchr(name, '.');
  if(dot && strcmp(dot, ".jlo") == 0) *dot = 0;

#if defined(_WIN32)
  if(strlen(out) + strlen(name) + 5 >= out_len) return -1;
  strcat(out, name);
  strcat(out, ".exe");
#else
  if(strlen(out) + strlen(name) + 1 >= out_len) return -1;
  strcat(out, name);
#endif
  return 0;
}

int jello_boot_create(const char* vm_exe, const char* jlo_path, const char* out_exe) {
  if(!vm_exe || !jlo_path || !out_exe) return -1;

  uint64_t vm_base = 0;
  if(vm_base_size(vm_exe, &vm_base) != 0) return -1;

  uint8_t* jlo_data = NULL;
  size_t jlo_size = 0;
  if(read_file(jlo_path, &jlo_data, &jlo_size) != 0) return -1;

  FILE* vm = fopen(vm_exe, "rb");
  if(!vm) {
    free(jlo_data);
    return -1;
  }
  FILE* out = fopen(out_exe, "wb");
  if(!out) {
    fclose(vm);
    free(jlo_data);
    return -1;
  }

  int ok = 0;
  if(fseek(vm, 0, SEEK_SET) != 0) goto done;
  if(copy_bytes(vm, out, vm_base) != 0) goto done;
  if(fwrite(jlo_data, 1, jlo_size, out) != jlo_size) goto done;
  if(write_footer(out, vm_base, (uint64_t)jlo_size) != 0) goto done;
  ok = 1;

done:
  fclose(vm);
  fclose(out);
  free(jlo_data);
  if(!ok) return -1;

#if !defined(_WIN32)
  if(chmod(out_exe, 0755) != 0) return -1;
#endif
  return 0;
}

int jello_boot_probe(const char* exe_path, jello_boot_image* out) {
  if(!exe_path || !out) return -1;
  memset(out, 0, sizeof(*out));

  FILE* f = fopen(exe_path, "rb");
  if(!f) return -1;

  uint64_t file_size = 0;
  if(file_size_of(f, &file_size) != 0) {
    fclose(f);
    return -1;
  }
  if(file_size < JELLO_BOOT_FOOTER_SIZE) {
    fclose(f);
    return 0;
  }

  uint8_t footer[JELLO_BOOT_FOOTER_SIZE];
  if(read_footer_at_eof(f, file_size, footer) != 0) {
    fclose(f);
    return -1;
  }

  uint64_t jlo_offset = 0;
  uint64_t jlo_size = 0;
  if(!parse_footer(footer, file_size, &jlo_offset, &jlo_size)) {
    fclose(f);
    return 0;
  }

  out->data = (uint8_t*)malloc((size_t)jlo_size);
  if(!out->data) {
    fclose(f);
    return -1;
  }
  if(fseek(f, (long)jlo_offset, SEEK_SET) != 0 ||
     fread(out->data, 1, (size_t)jlo_size, f) != (size_t)jlo_size) {
    free(out->data);
    out->data = NULL;
    fclose(f);
    return -1;
  }

  out->size = (size_t)jlo_size;
  fclose(f);
  return 1;
}

void jello_boot_image_free(jello_boot_image* img) {
  if(!img) return;
  free(img->data);
  img->data = NULL;
  img->size = 0;
}
