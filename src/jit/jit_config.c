// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#include <jello/internal/jit_impl.h>

#include <jello/internal/vm_internal.h>

#include <stdlib.h>
#include <string.h>

int jello_jit_config_enabled(const jello_vm* vm) {
#ifndef JELLOVM_ENABLE_JIT
  (void)vm;
  return 0;
#else
  if(!vm || !vm->jit_enabled) return 0;
  const char* e = getenv("JELLO_JIT");
  if(e && (e[0] == '0' || strcmp(e, "false") == 0)) return 0;
  return 1;
#endif
}

uint32_t jello_jit_config_hot_threshold(void) {
  const char* e = getenv("JELLO_JIT_HOT");
  if(!e || !*e) return 32u;
  char* end = NULL;
  unsigned long v = strtoul(e, &end, 10);
  if(end == e || v == 0) return 1u;
  if(v > 0xffffffffu) return 0xffffffffu;
  return (uint32_t)v;
}

int jello_jit_config_run_enabled(void) {
  const char* e = getenv("JELLO_JIT_RUN");
  if(e && (e[0] == '0' || strcmp(e, "false") == 0)) return 0;
  return 1;
}

int jello_jit_config_dump_enabled(void) {
  const char* e = getenv("JELLO_JIT_DUMP");
  return e && e[0] == '1';
}
