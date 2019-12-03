// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#ifndef JELLO_INTERNAL_BOOT_INTERNAL_H
#define JELLO_INTERNAL_BOOT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define JELLO_BOOT_MAGIC "JLYBOOT1"
#define JELLO_BOOT_MAGIC_LEN 8u
#define JELLO_BOOT_FOOTER_SIZE 24u

typedef struct jello_boot_image {
  uint8_t* data;
  size_t size;
} jello_boot_image;

/* Resolve the running VM executable path into `out` (NUL-terminated). Returns 0 on success. */
int jello_boot_self_exe_path(char* out, size_t out_len);

/* Derive output executable path from `program.jlo` -> `program` (same directory). */
int jello_boot_output_path(const char* jlo_path, char* out, size_t out_len);

/* Copy `vm_exe`, append `jlo_path` payload + footer, write `out_exe` (+x on Unix). Returns 0 on success. */
int jello_boot_create(const char* vm_exe, const char* jlo_path, const char* out_exe);

/* If `exe_path` contains an embedded boot payload, load it into `out`. Returns 1 if booted, 0 if not, -1 on error. */
int jello_boot_probe(const char* exe_path, jello_boot_image* out);

void jello_boot_image_free(jello_boot_image* img);

#endif
